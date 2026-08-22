#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every entry point this increment touches compiles cleanly at X86_64 level,
# combined with block_device_policy.abas the same way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSVerifyCheckpointChecksum ArcFSRingSlotGeneration ArcFSSelectRingCheckpoint \
             ArcFSReadCheckpoint ArcFSPeekCheckpoint ArcFSWriteCheckpointRecord ArcFS.MountImage \
             ArcFS.CommitImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS checkpoint-self-description entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0042 Sections 7-9): the checkpoint record is now self-describing. A real
# committed checkpoint (recordLength=112) has its own on-disk SHAPE downgraded in place to a
# byte-for-byte replica of the pre-RFC-0042 layout (104-byte payload, no recordLength, checksum at
# the old fixed offset) and re-mounts correctly via the LEGACY interpretation path, with real
# content and attributes intact. A fresh commit afterward writes a real self-describing record
# again and mounts via the SELF-DESCRIBING path -- proving a volume can move between both shapes in
# place, mid-lifetime, with no reformat. A deliberately implausible recordLength written directly
# onto a real checkpoint sector makes the mount fail closed, not silently misread.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-checkpoint-self-description.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-checkpoint-self-description.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-checkpoint-self-description.efi" "APS ARCFS CHECKPOINT COMPAT OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS CHECKPOINT COMPAT OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0042's self-describing checkpoint record is real -- a genuinely older on-disk shape (byte-for-byte, not simulated) still mounts via the legacy interpretation path, a fresh commit moves the same volume to the self-describing shape with no reformat, and a corrupted recordLength fails closed, QEMU-proven under a real CPU"
