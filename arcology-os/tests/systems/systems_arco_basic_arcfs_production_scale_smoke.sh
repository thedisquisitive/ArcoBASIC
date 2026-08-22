#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every entry point this increment touches compiles cleanly at X86_64 level,
# combined with block_device_policy.abas the same way every other ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.Initialize ArcFS.CreateFile ArcFS.CommitImage ArcFS.MountImage ArcFS.HandleWrite \
             ArcFS.HandleRead ArcFS.SetAttribute; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS production-scale entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0042 Section 11): production-scale ArcFS capacities are genuinely usable, not
# just larger numbers. Scenario 1: 80 real objects (25% more than this project's own old 64-object
# ceiling) on one volume, forcing a genuinely deep multi-level Object Tree, real commit+remount,
# real path resolution for the first/middle/last object. Scenario 2, a separate freshly-formatted
# volume: the new 32KB max file size (ArcFSMaxFileChunks() 4->8), full content verified byte-for-
# byte after a real commit+remount, alongside a real attribute. The two scenarios are kept on
# separate volumes deliberately -- a real, important finding from this phase's own design work: the
# Allocation Bitmap's volume-size ceiling (unchanged this phase, ~254KB) is tightly coupled to
# object count, since every commit bulk-rebuilds the whole Object+Namespace trees, so persisting
# anywhere near the new 2048-object LIVE ceiling needs a future volume-size increment first.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-production-scale.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-production-scale.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-production-scale.efi" "APS ARCFS PRODUCTION SCALE OK" 40)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PRODUCTION SCALE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0042's production-scale ArcFS capacities are real -- 80 real objects (beyond the old 64-object ceiling) committed and individually resolved after a real remount, and the new 32KB max file size verified byte-for-byte, QEMU-proven under a real CPU"
