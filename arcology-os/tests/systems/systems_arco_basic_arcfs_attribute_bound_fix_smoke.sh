#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every entry point this fix touches compiles cleanly at X86_64 level, combined
# with block_device_policy.abas the same way every other ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSReadCheckpoint ArcFS.MountImage ArcFS.SetAttribute ArcFS.CommitImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS attribute-bound-fix entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: ArcFSReadCheckpoint's own attributeTreeCount sanity bound checked against the OLD
# ArcFSMaxObjects() (64) instead of ArcFSMaxAttributes() (128) -- a leftover from the multi-attribute
# increment (RFC-0040 Section 12.1) never exercised by any prior fixture, since none populated more
# than a handful of attributes at once. A legitimate checkpoint claiming between 65 and 128 active
# attributes would have been wrongly refused as invalid. 10 objects get 12 attributes each (120
# total, past the old bound, within the real one); a real commit+remount confirms the volume mounts
# and every checked attribute round-trips correctly.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-attribute-bound-fix.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-attribute-bound-fix.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-attribute-bound-fix.efi" "APS ARCFS ATTR BOUND FIX OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS ATTR BOUND FIX OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: ArcFSReadCheckpoint's attributeTreeCount bound now correctly checks against ArcFSMaxAttributes(), not the old ArcFSMaxObjects() -- a real, previously-unexercised bug, fixed and QEMU-proven under a real CPU"
