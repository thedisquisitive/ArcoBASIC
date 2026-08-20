#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: ArcFS.RollbackToSnapshot (this phase's one new entry point) compiles cleanly
# at X86_64 level, combined with block_device_policy.abas the same way every prior ArcFS smoke
# test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.RollbackToSnapshot ArcFS.CreateSnapshot ArcFS.CommitImage ArcFS.MountImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase G entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0039 Section 51's "update snapshots" workflow, end to end. Snapshots a
# known-good generation ("pre-update"), commits a bad update on top of it (confirmed to have
# really taken effect), then ArcFS.RollbackToSnapshot republishes the snapshotted generation as
# active -- a real, atomic undo (the exact same generation 2 data, never copied or
# reconstructed) -- confirmed via a fresh remount. Finally confirms the volume is fully
# READ-WRITE usable after rollback, not merely readable: a new file commits and round-trips on
# top of the rolled-back tree, and the rolled-back file survives that further commit unchanged.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-g.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-g.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-g.efi" "APS ARCFS PHASE G OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE G OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: ArcFS.RollbackToSnapshot implements RFC-0039 Section 51's update-snapshot rollback for real -- a bad update is genuinely undone, and the volume is fully read-write usable afterward -- under a real CPU (QEMU/OVMF)"
