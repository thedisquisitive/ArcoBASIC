#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every ArcFS Phase B entry point compiles cleanly at X86_64 level.
# ArcFS.MountImage and friends depend on RAMDisk.* (stdlib/block_device_policy.abas, RFC-0038) --
# combined into one probe file the same way the fixture itself combines them, since ArcFS Phase B
# is the first real consumer of RFC-0039 Section 80's own "BlockStorage interface" dependency note.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.MountImage ArcFSReadSuperblockRing ArcFSReadCheckpoint ArcFSLoadObjects \
             ArcFSLoadNamespace ArcFSMagicMatches ArcFSCrc32C; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase B entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! command -v python3 > /dev/null 2>&1 || \
   ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF/python3 not installed; this fixture only proves anything when executed."
    exit 0
fi

# Build the real, checksummed ArcFS test image (arcology-os/scripts/build/
# build-arcfs-test-image.py -- deterministic, independently verified byte-for-byte against a
# from-scratch Python re-parse when this test was authored; see .agents/reports/
# aps-arcfs-phase-b.md). Its four objects/three namespace entries and 97-byte file (byte-sum
# 6142) match this script's own expectations below exactly.
python3 "$ROOT/scripts/build/build-arcfs-test-image.py" "$TMP_ROOT/arcfs-good.img" > /dev/null

# The real proof: a real BlockDevice (RFC-0038's RAMDisk) preloaded with that image ->
# ArcFS.MountImage() validates the superblock, checkpoint, and every object/namespace record's
# checksum, then loads them into Phase A's already-proven in-memory tables -> Phase A's own,
# completely unchanged Resolve/OpenHandle/HandleRead functions correctly answer queries against
# data that came from a real on-disk image (multi-component path resolution to the exact OIDs the
# image generator assigned, root, a deliberately-missing path, and a byte-for-byte + checksum
# verified file read) -- all under a real CPU.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-b.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-b.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-preload.sh" "$TMP_ROOT/aps-arcfs-phase-b.efi" "$TMP_ROOT/arcfs-good.img" 0x4000000 "APS ARCFS PHASE B OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE B OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# The deliberate negative test (RFC-0039 Sections 39/74's own untrusted-media threat model,
# matching RFC-0038's own FAT32.Mount negative-test precedent): mounting against a zeroed
# BlockDevice MUST return FALSE and fail closed. This fixture's own success marker IS the
# rejection.
dd if=/dev/zero of="$TMP_ROOT/arcfs-zeroed.img" bs=1K count=32 > /dev/null 2>&1
NEGATIVE_FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-b-negative.abas"
"$ARCOFISSION" build "$NEGATIVE_FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-b-negative.efi" --target uefi-x86_64 > /dev/null

NEGATIVE_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-preload.sh" "$TMP_ROOT/aps-arcfs-phase-b-negative.efi" "$TMP_ROOT/arcfs-zeroed.img" 0x4000000 "APS ARCFS MOUNT REJECTED" 20)"
echo "$NEGATIVE_OUTPUT"
[ "$NEGATIVE_OUTPUT" = "PASS: APS ARCFS MOUNT REJECTED" ] || { echo "FAIL: unexpected harness output for the negative test" >&2; exit 1; }

echo "PASS: ArcFS.MountImage validates a real checksummed on-disk image and loads it into Phase A's proven object/namespace/handle contract, and fails closed against an invalid image, under a real CPU (QEMU/OVMF)"
