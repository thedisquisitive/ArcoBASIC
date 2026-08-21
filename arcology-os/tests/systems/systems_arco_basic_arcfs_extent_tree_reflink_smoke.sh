#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Section 11.1/11.5 (Extent Tree, real on-disk reflink sharing)
# entry point compiles cleanly at X86_64 level, combined with block_device_policy.abas the same
# way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSGatherUniqueDataSlots ArcFSCountTouchedChunks ArcFSGatherSortedExtentEntries \
             ArcFSWriteOneExtentLeafNode ArcFSBuildExtentTree ArcFSLastBuiltExtentTreeCount \
             ArcFSExtentTreeCollectAllEntriesInto ArcFSExtentTreeLoadDataSlotIntoRow \
             ArcFSExtentTreeContainsSectorForSlot ArcFSExtentTreeChunkAt ArcFSDataSlotsOverlap \
             ArcFSWriteOneLeafNode ArcFSPopulateObjectRow ArcFSLoadObjects ArcFSTreeContainsSector \
             ArcFSGenerationContainsSector ArcFS.SnapshotReadFile ArcFSFindExtentOverlaps \
             ArcFS.RepairResolveExtentOverlaps ArcFSScanGeneration ArcFS.Reflink ArcFS.HandleWrite \
             ArcFS.HandleRead ArcFS.FormatVolume ArcFS.PrepareCommit ArcFS.MountImage \
             ArcFS.ReclaimGeneration ArcFS.GetHealthState; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Extent Tree entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 11.1's real, variable-count Extent Tree and Section 11.5's
# genuine on-disk reflink sharing, end to end under a real CPU. File A gets a real multi-chunk
# write (2 real chunks); reflinking B from A while undiverged and committing produces exactly 2
# real Extent Tree entries, not 4 -- the shared chunks were written to disk exactly once (the
# on-disk reflink sharing proof), with ArcFS.GetHealthState() confirming Pass 5's own overlap
# check does not false-positive on the legitimate sharing; a write through B alone forces
# ArcFSEnsurePrivateSlot to diverge it onto its own private dataSlot, and the entry count
# genuinely grows to 4 (the divergence-produces-real-separation proof); two more files (C, D) push
# the total past ArcFSExtentTreeNodeCapacity() (4), forcing a real multi-node, multi-level tree,
# with all four files' content still exactly correct after a real commit+remount; a real reclaim
# pass afterward leaves every file's content intact, proving Extent Tree node reachability holds.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-extent-tree-reflink.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-extent-tree-reflink.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-extent-tree-reflink.efi" "APS ARCFS EXTENT TREE OK" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS EXTENT TREE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 11.1's real variable-count Extent Tree and Section 11.5's genuine on-disk reflink sharing (verified via real Extent Tree entry counts, not just content correctness) is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
