#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Section 13/14 (Full Health Model, Expanded Repair Coverage)
# entry point compiles cleanly at X86_64 level, combined with block_device_policy.abas the same
# way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSHealthRecordSector ArcFSWriteHealthRecord ArcFSReadHealthRecord \
             ArcFS.GetLastScrubGeneration ArcFS.GetLastScrubResult ArcFS.GetLastRepairGeneration \
             ArcFS.GetLastRepairSummary ArcFS.GetCumulativeChecksumErrors \
             ArcFS.GetCumulativeIoErrors ArcFSRingSlotGeneration ArcFSRingIsConsistent \
             ArcFSSelectRingCheckpoint ArcFS.ScrubVolume ArcFS.GetHealthState \
             ArcFS.GetHealthStateCached ArcFS.MountImageSafe ArcFSFindExtentOverlaps \
             ArcFS.RepairResolveExtentOverlaps ArcFS.RepairReconcileAllocationBitmap \
             ArcFS.RepairReattachOrphans ArcFS.RepairCommit ArcFS.FormatVolume \
             ArcFS.PrepareCommit ArcFS.MountImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Health Model entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 13 (Full Health Model) and Section 14 (Expanded Repair
# Coverage), the last phase named in RFC-0040's own implementation plan, end to end under a real
# CPU. All six states ArcFS.GetHealthState can itself report (Healthy, Degraded, ReadOnlySafety,
# NeedsOfflineCheck via a real ring-copy inconsistency, Corrupt via real tree-node corruption,
# Unavailable via a fully zeroed ring), plus NeedsScrub via ArcFS.GetHealthStateCached on a
# never-scrubbed volume. A REAL extent-overlap defect -- constructed by directly corrupting one
# file's on-disk extent list to claim another's real chunk sector, closing a gap Phase F's own
# report named explicitly (this exact defect class had never been exercised under real QEMU
# execution anywhere in this project before this fixture) -- detected and repaired via the new
# ArcFS.RepairResolveExtentOverlaps. Allocation-bitmap reconciliation correcting a wrongly-freed
# but still-reachable sector. ReadOnlySafety genuinely blocking a commit (not just reporting a
# status value) and recovering after repair. The persisted Health Record surviving real commits
# and remounts with accurate scrub/repair history and cumulative error counters throughout.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-health-model.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-health-model.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-health-model.efi" "APS ARCFS HEALTH MODEL OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS HEALTH MODEL OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 13/14's full seven-state health model, real extent-overlap repair, allocation-bitmap reconciliation, and the persisted Health Record are genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
