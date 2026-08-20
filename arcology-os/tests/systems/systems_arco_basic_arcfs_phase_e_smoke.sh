#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every ArcFS Phase E entry point compiles cleanly at X86_64 level. Combined the
# same way every prior ArcFS smoke test already does, since these depend on RAMDisk.* (stdlib/
# block_device_policy.abas, RFC-0038) via ArcFS.CommitImage/MountImage.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.CreateSnapshot ArcFS.DeleteSnapshot ArcFS.IsSectorReachable \
             ArcFS.ReclaimableSectorCount ArcFS.SnapshotRetainedSectorCount \
             ArcFS.SnapshotResolve ArcFS.SnapshotReadFile ArcFS.CommitImage ArcFS.MountImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase E entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: ArcFS.CreateSnapshot() pins the active generation; two further commits leave
# that generation superseded-and-unpinned in between (garbage) and a new active generation on top
# (live); ArcFS.IsSectorReachable correctly distinguishes all three cases against the SAME
# function. ArcFS.SnapshotResolve/SnapshotReadFile read the snapshotted generation's own content
# directly off the BlockDevice, byte-for-byte, even though the live tree has since overwritten it
# twice (verified via a fresh remount, not leftover in-memory state). ArcFS.DeleteSnapshot removes
# the pin and ArcFS.ReclaimableSectorCount/SnapshotRetainedSectorCount both move correctly once
# nothing protects that generation anymore.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-e.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-e.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-e.efi" "APS ARCFS PHASE E OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE E OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: ArcFS.CreateSnapshot/DeleteSnapshot pin and unpin a generation correctly, ArcFS.IsSectorReachable/ReclaimableSectorCount correctly distinguish active/snapshotted/garbage generations, and ArcFS.SnapshotResolve/SnapshotReadFile read a pinned generation's own content directly off the BlockDevice even after the live tree has diverged twice -- under a real CPU (QEMU/OVMF)"
