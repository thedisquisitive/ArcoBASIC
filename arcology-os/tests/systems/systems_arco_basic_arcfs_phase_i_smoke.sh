#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Section 7 (real allocation and reclamation) entry point
# compiles cleanly at X86_64 level, combined with block_device_policy.abas the same way every
# prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSBitmapGet ArcFSBitmapSet ArcFSLoadBitmap ArcFSWriteBitmapRecord \
             ArcFSAllocateDataExtent ArcFSGenerationContainsSector ArcFS.IsSectorReachable \
             ArcFS.ReclaimGeneration ArcFS.ReclaimableSectorCount ArcFS.FormatVolume \
             ArcFS.PrepareCommit ArcFS.PublishCommit ArcFS.MountImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase I entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 7's real free-space allocation and reclamation, end to end,
# under a real CPU. Scoped to file DATA EXTENTS (metadata stays contiguous-append this increment,
# pending Phase J's B+tree rewrite -- see .agents/reports/aps-arcfs-phase-i.md). A generation's
# own superseded data extent becomes genuinely unreachable once a later generation supersedes it
# (ordinary copy-on-write, unchanged since Phase C); ArcFS.ReclaimGeneration() + PrepareCommit
# ONLY (no publish) leaves it still allocated after a remount (RFC-0040 Section 7.4's crash
# safety); redoing it with a real publish actually frees it; and the next file created afterward
# gets an extent sector genuinely below the pre-reclaim high-water mark, proving real reuse
# (RFC-0040 Section 7.2/7.5), not just growth, with its content still correct after being
# relocated across three generations.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-i.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-i.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-i.efi" "APS ARCFS PHASE I OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE I OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 7's real free-space allocation and reclamation (real reachability, real crash-safe reclamation, real reuse) is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
