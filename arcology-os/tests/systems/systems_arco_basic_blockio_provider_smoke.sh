#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: the UefiBlockDevice.* provider itself, and the fixture that inlines a frozen
# copy of it, both compile cleanly at X86_64 level.
for entry in UefiBlockDevice.Discover UefiBlockDevice.IsDiscovered UefiBlockDevice.SectorSize \
             UefiBlockDevice.SectorCount UefiBlockDevice.IsWritable UefiBlockDevice.ReadSectors \
             UefiBlockDevice.WriteSectors; do
    "$ARCOFISSION" reveal "$ROOT/stdlib/uefi_block_device_policy.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: UefiBlockDevice entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

FIXTURE="$ROOT/tests/fixtures/blockio-provider/blockio-provider.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: the RFC-0038 Section 6.1 BlockDevice contract shape (SectorSize/SectorCount/
# IsWritable/ReadSectors/WriteSectors), genuinely backed by real discovered EFI_BLOCK_IO_PROTOCOL
# hardware -- discovery, geometry (SectorSize=512, SectorCount=128, matching the real test disk
# build-blockio-test-disk.py builds), Requirement 6.1's out-of-bounds rejection, a real read
# recovering the known magic pattern at sector 5, and a real write+read-back round trip at
# sector 10, ALL going through UefiBlockDevice.* itself, not the raw UEFI.BLOCKIO.* intrinsics
# directly (systems_arco_basic_blockio_disk_smoke already proves those on their own).
python3 "$ROOT/scripts/build/build-blockio-test-disk.py" "$TMP_ROOT/blockio-disk.img" > /dev/null

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/blockio-provider.efi" --target uefi-x86_64 --entry Main > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/blockio-provider.efi" "$TMP_ROOT/blockio-disk.img" "BLOCKIO PROVIDER DONE" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: BLOCKIO PROVIDER DONE" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# Negative control: confirm the magic-sector check is real, not vacuous.
python3 - "$TMP_ROOT/blockio-disk.img" "$TMP_ROOT/blockio-disk-corrupt.img" <<'PYEOF'
import shutil
import sys
shutil.copyfile(sys.argv[1], sys.argv[2])
with open(sys.argv[2], "r+b") as f:
    f.seek(5 * 512)
    f.write(b"XXXX")
PYEOF

NEGATIVE_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/blockio-provider.efi" "$TMP_ROOT/blockio-disk-corrupt.img" "BLOCKIO PROVIDER SECTOR5 MAGIC NOMATCH" 25)"
[ "$NEGATIVE_OUTPUT" = "PASS: BLOCKIO PROVIDER SECTOR5 MAGIC NOMATCH" ] || { echo "FAIL: negative control did not report NOMATCH for corrupted disk content" >&2; exit 1; }

echo "PASS: UefiBlockDevice.* provider (RFC-0038 Section 6.1 BlockDevice contract shape) genuinely backed by real UEFI Block I/O hardware, negative control confirmed real"
