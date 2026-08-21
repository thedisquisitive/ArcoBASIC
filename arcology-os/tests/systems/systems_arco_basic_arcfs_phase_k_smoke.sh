#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Section 11 (extent-based variable-length and sparse files)
# entry point compiles cleanly at X86_64 level, combined with block_device_policy.abas the same
# way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSMaxFileChunks ArcFSSlotSizeBytes ArcFSChunkTouchedGet ArcFSChunkTouchedSet \
             ArcFSAllocateOneSector ArcFSWriteExtentListRecord ArcFSBuildAndWriteExtentList \
             ArcFSLoadExtentListIntoPool ArcFSExtentListContainsSector ArcFSExtentListChunkAt \
             ArcFSExtentListsOverlap ArcFSEnsurePrivateSlot ArcFS.HandleWrite ArcFS.HandleRead \
             ArcFS.Resize ArcFSWriteOneLeafNode ArcFSPopulateObjectRow ArcFSLoadObjects \
             ArcFSTreeContainsSector ArcFSScanGeneration ArcFS.SnapshotReadFile ArcFS.Reflink \
             ArcFS.FormatVolume ArcFS.PrepareCommit ArcFS.MountImage ArcFS.ReclaimGeneration; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase K entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 11's extent-based variable-length and sparse files, end to end
# under a real CPU. A partial (sub-chunk) write; a Resize that grows a file's visible size across
# multiple chunk boundaries WITHOUT writing real data, proven to read as zero both in-session and
# after a real commit/remount (a genuine sparse hole, not a zero-filled extent); a real write
# spanning all 4 chunks, each getting its own real, checksummed, persisted extent; a real reclaim
# afterward with content still exactly intact; a reflink sharing one data-pool slot between two
# OIDs; a small write through the CLONE forcing ArcFSEnsurePrivateSlot to give it a private slot,
# with both the copied bytes AND the copied touched-chunk bitmap verified directly against the
# post-mount data pool (the public HandleRead API cannot observe chunks beyond a shrunk `size`,
# so this fixture checks them at the data-pool level instead); and a final scrub/reclaim leaving
# both files' content -- including the size-invisible-but-real chunks -- exactly intact.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-k.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-k.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-k.efi" "APS ARCFS PHASE K OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE K OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 11's extent-based variable-length and sparse files (real per-chunk extents, genuine sparse holes that persist correctly, reflink+COW divergence with correct touched-bitmap propagation, and reclaim/scrub of the widened extent-list shape) is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
