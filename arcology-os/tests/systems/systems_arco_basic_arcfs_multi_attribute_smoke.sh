#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every entry point this increment touches compiles cleanly at X86_64 level,
# combined with block_device_policy.abas the same way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSFindAttributeRow ArcFSFindFreeAttributeRow ArcFS.SetAttribute ArcFS.HasAttribute \
             ArcFS.GetAttributeType ArcFS.GetAttribute ArcFS.GetAttributeHigh ArcFS.RemoveAttribute \
             ArcFSGatherSortedActiveAttributes ArcFSBuildAttributeTree ArcFSLoadAttributes \
             ArcFS.MountImage ArcFS.PrepareCommit; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS multi-attribute entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 12.1's multi-attribute-per-object model. Two objects (A, B) each
# get their own attribute; A gets a SECOND, distinct attribute at once -- the old one-slot-per-
# object model made that impossible (a second ArcFS.SetAttribute on the same object silently
# clobbered the first). Rows key on the FULL (oid, attributeId) pair, proven by giving both A and B
# an attributeId=1 that must not collide. ArcFS.RemoveAttribute clears exactly one attribute without
# disturbing the object's others. A commit+remount round trip confirms the removed attribute stays
# gone and the survivors round-trip byte-for-byte through a real on-disk Attribute Tree walk; a
# second commit+remount re-adds a fresh attribute at the vacated (oid, attributeId), proving the
# freed row is genuinely reusable across generations, not just within one session.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-multi-attribute.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-multi-attribute.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-multi-attribute.efi" "APS ARCFS MULTI ATTRIBUTE OK" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS MULTI ATTRIBUTE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 12.1's multi-attribute-per-object model is real -- multiple distinct attributes per object, (oid, attributeId)-keyed rows, ArcFS.RemoveAttribute, and durable on-disk persistence across two separate commit+remount cycles, QEMU-proven under a real CPU"
