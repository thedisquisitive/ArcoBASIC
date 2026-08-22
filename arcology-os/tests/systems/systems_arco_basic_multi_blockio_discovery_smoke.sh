#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: the fixture (a frozen inline copy of uefi_block_device_policy.abas, including
# RFC-0044's new DiscoverAll/SelectDiscovered surface) compiles cleanly at X86_64 level.
FIXTURE="$ROOT/tests/fixtures/multi-blockio-discovery/multi-blockio-discovery.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/multi-blockio.efi" --target uefi-x86_64 --entry Main > /dev/null

# The real proof (RFC-0044 Phase 1, Section 7.3's own acceptance criterion): real multi-handle
# EFI_BLOCK_IO_PROTOCOL enumeration via LocateHandle+HandleProtocol (not LocateProtocol's
# single-handle contract) against a genuinely two-device QEMU topology (the boot media itself,
# which OVMF's own PartitionDxe additionally splits into a whole-disk AND a partition BlockIo
# handle -- confirmed empirically, real handle count is >= 2 even with only one attached disk --
# plus a dedicated known-content virtio-blk disk), and a real proof that SelectDiscovered
# genuinely ROUTES subsequent ReadSectors/WriteSectors calls to the specifically selected device:
# the fixture checks the magic pattern is ABSENT on a deliberately non-selected device and PRESENT
# on the selected one, not just that discovery finds more than one handle.
python3 "$ROOT/scripts/build/build-blockio-test-disk.py" "$TMP_ROOT/blockio-disk.img" > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/multi-blockio.efi" "$TMP_ROOT/blockio-disk.img" "MULTI BLOCKIO DONE" 30)"
[ "$OUTPUT" = "PASS: MULTI BLOCKIO DONE" ] || { echo "FAIL: unexpected harness output: $OUTPUT" >&2; exit 1; }

# Determinism: 3x repeat, matching this project's own standing discipline for any new real
# hardware-binding proof.
for i in 1 2 3; do
    REPEAT_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/multi-blockio.efi" "$TMP_ROOT/blockio-disk.img" "MULTI BLOCKIO DONE" 30)"
    [ "$REPEAT_OUTPUT" = "PASS: MULTI BLOCKIO DONE" ] || { echo "FAIL: non-deterministic on repeat $i: $REPEAT_OUTPUT" >&2; exit 1; }
done

# Negative control #1: without the dedicated test disk attached, the fixture must honestly report
# that its known 128-sector device was not found among whatever real handles WERE discovered, not
# silently report success against the wrong device.
NEGATIVE_OUTPUT_1="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/multi-blockio.efi" "MULTI BLOCKIO NO TEST DISK FOUND" 25)"
[ "$NEGATIVE_OUTPUT_1" = "PASS: MULTI BLOCKIO NO TEST DISK FOUND" ] || { echo "FAIL: negative control #1 (no test disk attached) did not report honestly: $NEGATIVE_OUTPUT_1" >&2; exit 1; }

# Negative control #2: a corrupted magic sector on the real, selected test disk must be reported
# as genuinely missing, not a vacuous always-MATCH.
python3 - "$TMP_ROOT/blockio-disk.img" "$TMP_ROOT/blockio-disk-corrupt.img" <<'PYEOF'
import shutil
import sys
shutil.copyfile(sys.argv[1], sys.argv[2])
with open(sys.argv[2], "r+b") as f:
    f.seek(5 * 512)
    f.write(b"XXXX")
PYEOF

NEGATIVE_OUTPUT_2="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/multi-blockio.efi" "$TMP_ROOT/blockio-disk-corrupt.img" "MULTI BLOCKIO TEST MISSING MAGIC" 30)"
[ "$NEGATIVE_OUTPUT_2" = "PASS: MULTI BLOCKIO TEST MISSING MAGIC" ] || { echo "FAIL: negative control #2 (corrupted magic sector) did not report honestly: $NEGATIVE_OUTPUT_2" >&2; exit 1; }

echo "PASS: real multi-handle EFI_BLOCK_IO_PROTOCOL enumeration (LocateHandle+HandleProtocol) discovers genuinely distinct real devices and SelectDiscovered genuinely routes I/O to the selected one, 3x determinism confirmed, both negative controls confirmed real"
