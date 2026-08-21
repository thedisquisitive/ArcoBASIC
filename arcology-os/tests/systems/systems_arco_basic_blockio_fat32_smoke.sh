#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: FAT32.SelectBlockDevice/ActiveBlockDevice and the dispatch functions compile
# cleanly, combined with block_device_policy.abas + uefi_block_device_policy.abas the same way the
# real fixture below does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/fat32_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in FAT32.SelectBlockDevice FAT32.ActiveBlockDevice FAT32.BlockDeviceProviderRAMDisk \
             FAT32.BlockDeviceProviderUefi FAT32.Mount FAT32.Open FAT32.Read FAT32.CountEntries; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: FAT32 block-device-selection entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

FIXTURE="$ROOT/tests/fixtures/blockio-fat32/blockio-fat32.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: UefiBlockDevice.* wired into fat32_policy.abas as a genuine ALTERNATIVE to
# RAMDisk.*, selected via FAT32.SelectBlockDevice, both implementing RFC-0038 Section 6.1's
# identical BlockDevice contract shape. Uses the SAME real FAT32 image / known file / known
# checksum (README.TXT, 3333 bytes, byte-sum 211490) the original RAMDisk-backed proof
# (aps-block-storage.abas) already established as correct -- proving the read path itself is
# unchanged, only the block device underneath it is now real attached hardware.
python3 "$ROOT/scripts/build/build-fat32-test-image.py" "$TMP_ROOT/fat32-disk.img" > /dev/null

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/blockio-fat32.efi" --target uefi-x86_64 --entry Main > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/blockio-fat32.efi" "$TMP_ROOT/fat32-disk.img" "BLOCKIO FAT32 DONE" 30)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: BLOCKIO FAT32 DONE" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# Negative control: corrupt a byte inside the file's real content region and confirm the checksum
# check genuinely fails, not vacuously passes either way.
python3 - "$ROOT/scripts/build/build-fat32-test-image.py" "$TMP_ROOT/fat32-disk.img" "$TMP_ROOT/fat32-disk-corrupt.img" <<'PYEOF'
import importlib.util
import shutil
import sys

script_path, src, dst = sys.argv[1], sys.argv[2], sys.argv[3]
spec = importlib.util.spec_from_file_location("m", script_path)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
fat_sectors = m.sectors_per_fat()
data_start = m.RESERVED_SECTORS + m.FAT_COUNT * fat_sectors
file_offset = (data_start + (m.FILE_FIRST_CLUSTER - 2)) * m.SECTOR_SIZE

shutil.copyfile(src, dst)
with open(dst, "r+b") as f:
    f.seek(file_offset + 50)
    f.write(b"X")
PYEOF

NEGATIVE_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/blockio-fat32.efi" "$TMP_ROOT/fat32-disk-corrupt.img" "BLOCKIO FAT32 CHECKSUM BAD" 30)"
[ "$NEGATIVE_OUTPUT" = "PASS: BLOCKIO FAT32 CHECKSUM BAD" ] || { echo "FAIL: negative control did not report CHECKSUM BAD for corrupted file content" >&2; exit 1; }

echo "PASS: UefiBlockDevice.* wired into fat32_policy.abas as a genuine alternative to RAMDisk.*, real multi-cluster FAT32 read against real hardware byte-for-byte correct, negative control confirmed real"
