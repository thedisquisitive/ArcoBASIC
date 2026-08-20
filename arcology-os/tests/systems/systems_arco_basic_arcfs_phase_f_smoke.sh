#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every ArcFS Phase F entry point compiles cleanly at X86_64 level. Combined
# the same way every prior ArcFS smoke test already does, since these depend on RAMDisk.*
# (stdlib/block_device_policy.abas, RFC-0038).
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.ScrubVolume ArcFS.GetHealthState ArcFS.MountImageSafe \
             ArcFS.RepairReattachOrphans ArcFS.RepairCommit ArcFS.PrepareCommit \
             ArcFS.CommitImage ArcFS.MountImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase F entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: a healthy baseline scrubs clean (ArcFS.ScrubVolume = 0, ArcFS.GetHealthState =
# Healthy). Deleting a file's namespace entry but leaving its object row alive (Phase A's own
# ArcFS.Remove) and committing that state creates a real orphan on disk; ScrubVolume/GetHealthState
# correctly report it (RFC-0039 Section 39's namespace reachability analysis / orphan detection).
# ArcFS.MountImageSafe activates ReadOnlySafety (Section 38), and ArcFS.PrepareCommit genuinely
# REFUSES an ordinary mutation while it is armed -- not merely reports the state, actually blocks
# the write. ArcFS.RepairReattachOrphans + ArcFS.RepairCommit (Section 39's Apply -> Verify) fix
# the one orphan, commit through the sole sanctioned exception to the guard, re-verify a clean
# scan, and restore normal read-write commits -- and the repaired object is still readable,
# byte-for-byte, at its new :lost+found location, proving the SAME object (same OID, same data)
# was reattached, not a fresh copy fabricated in its place.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-f.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-f.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-f.efi" "APS ARCFS PHASE F OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE F OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: ArcFS.ScrubVolume/GetHealthState correctly detect a real orphaned object on disk, ArcFS.MountImageSafe/PrepareCommit make ReadOnlySafety genuinely block mutation, and ArcFS.RepairReattachOrphans/RepairCommit fix it, re-verify, and restore read-write access -- under a real CPU (QEMU/OVMF)"
