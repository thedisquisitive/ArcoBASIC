#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: the fixture compiles cleanly at X86_64 level.
FIXTURE="$ROOT/tests/fixtures/blockio-disk-discovery/blockio-disk-discovery.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: genuine UEFI Block I/O Protocol discovery against a REAL attached disk (no
# RAM-preload trick, unlike RFC-0038's own RAM Disk provider) -- real MediaId/BlockSize/LastBlock
# reads matching the disk build-blockio-test-disk.py actually built (BLOCKSIZE=512, LASTBLOCK=127
# for a 128-sector image), a real ReadBlocks call recovering a known magic pattern from sector 5,
# and a real WriteBlocks + fresh ReadBlocks round trip against sector 10 (a sector the test image
# never writes at build time), proving both directions of real hardware I/O, not just discovery.
python3 "$ROOT/scripts/build/build-blockio-test-disk.py" "$TMP_ROOT/blockio-disk.img" > /dev/null

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/blockio-disk.efi" --target uefi-x86_64 --entry Main > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/blockio-disk.efi" "$TMP_ROOT/blockio-disk.img" "BLOCKIO DISK DONE" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: BLOCKIO DISK DONE" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# Negative control: confirm the magic-sector check genuinely distinguishes correct from incorrect
# disk content, not a vacuous always-MATCH -- corrupt the magic bytes and confirm NOMATCH is
# reported honestly instead of the harness/fixture silently reporting success either way.
python3 - "$TMP_ROOT/blockio-disk.img" "$TMP_ROOT/blockio-disk-corrupt.img" <<'PYEOF'
import shutil
import sys
shutil.copyfile(sys.argv[1], sys.argv[2])
with open(sys.argv[2], "r+b") as f:
    f.seek(5 * 512)
    f.write(b"XXXX")
PYEOF

NEGATIVE_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/blockio-disk.efi" "$TMP_ROOT/blockio-disk-corrupt.img" "BLOCKIO DISK SECTOR5 MAGIC NOMATCH" 25)"
[ "$NEGATIVE_OUTPUT" = "PASS: BLOCKIO DISK SECTOR5 MAGIC NOMATCH" ] || { echo "FAIL: negative control did not report NOMATCH for corrupted disk content" >&2; exit 1; }

echo "PASS: real UEFI Block I/O Protocol discovery, read, and write against a genuinely attached QEMU disk (no RAM-preload trick), negative control confirmed real"
