#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Section 9 (growable Object Tree) entry point compiles cleanly
# at X86_64 level, combined with block_device_policy.abas the same way every prior ArcFS smoke
# test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSAllocateTreeNode ArcFSGatherSortedActiveObjects ArcFSWriteOneLeafNode \
             ArcFSWriteOneInternalNode ArcFSBuildObjectTree ArcFSTreeCollectAllEntriesInto \
             ArcFSTreeCollectAllEntriesTolerant ArcFSTreeContainsSector ArcFSPopulateObjectRow \
             ArcFSLoadObjects ArcFSGenerationMetadataRangeContains ArcFSGenerationContainsSector \
             ArcFSSnapshotFindObjectRecord ArcFSScanGeneration ArcFS.FormatVolume \
             ArcFS.PrepareCommit ArcFS.MountImage ArcFS.ReclaimGeneration; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase J entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 9's growable Object Tree, scoped to the Object Tree only
# (metadata bulk-rebuilt fresh every commit as a real, checksummed, multi-node B+tree, replacing
# the flat objectRoot+index array -- see .agents/reports/aps-arcfs-phase-j.md for the full
# reasoning), end to end under a real CPU. Six files plus root, with a deliberately small node
# capacity, forces a real multi-leaf, multi-level tree; every object still resolves with correct
# content after a real commit and remount; the new tree-walking scrub reports Healthy; reachability
# correctly follows an entire superseded tree (root, leaves, and internal node), not just a
# single sector; and real reclamation frees superseded tree structure, not just file data, with
# every file intact afterward.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-j.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-j.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-j.efi" "APS ARCFS PHASE J OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE J OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 9's growable Object Tree (real multi-node B+tree, real reachability through tree structure, real reclamation of superseded tree nodes) is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
