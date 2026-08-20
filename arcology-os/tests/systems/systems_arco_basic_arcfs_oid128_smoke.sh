#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Section 10 (128-bit OID) entry point compiles cleanly at
# X86_64 level, combined with block_device_policy.abas the same way every prior ArcFS smoke test
# already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSAllocateOid ArcFSLastAllocatedOidHigh ArcFSCreateObject ArcFS.Reflink \
             ArcFSWriteObjectRecord ArcFSLoadOneObject ArcFS.MountImage ArcFS.FormatVolume \
             ArcFS.PrepareCommit ArcFSSnapshotFindObjectRecord ArcFSScanGeneration; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS 128-bit OID entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 10's 128-bit OID identity and allocation, end to end, under a
# real CPU. An ordinary allocation still stores oidHigh=0 (nothing observable changes for the
# common case); forcing the low-half counter to its maximum value and reflinking a file proves
# the carry arithmetic is real (the pre-wrap low value is returned, the pre-wrap high value is
# durably stored as that object's own identity, and the counter afterward shows the high half
# incremented AND the low half skipped past this implementation's own reserved 0 "not found"
# sentinel); a real commit and remount prove the widened on-disk object record round-trips a
# nonzero oidHigh, and that ArcFS.MountImage's high-water-mark restore is genuine 128-bit
# arithmetic, not a low-only approximation. 128-bit comparison in internal lookup functions and
# propagation through the public ArcFS.* calling surface remain explicitly out of scope -- see
# .agents/reports/aps-arcfs-oid-128bit.md for why.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-oid128.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-oid128.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-oid128.efi" "APS ARCFS OID128 OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS OID128 OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 10's 128-bit OID identity and allocation (real carry arithmetic, real on-disk round-trip, real mount-time high-water-mark restore) is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
