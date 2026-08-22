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

for entry in ArcFSObjectRowAddress ArcFSNamespaceRowAddress ArcFSAttributeRowAddress \
             ArcFS.Initialize ArcFS.CreateFile ArcFS.Rename ArcFS.Reflink ArcFS.SetAttribute; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS reserved-rows entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0042 Section 10): the Object (40->56 bytes), Namespace (64->80 bytes), and
# Attribute (56->64 bytes) row layouts each gained real reserved fields for future per-row growth,
# with the on-disk tree leaf formats left completely unchanged. Two objects with real content, real
# namespace entries, and real attributes -- every new reserved field reads exactly zero immediately
# after creation, still exactly zero after a real commit+remount, and still exactly zero after a
# real rename and a real reflink followed by another commit+remount -- with every existing
# structural check (lookup, attribute get, health state) unaffected.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-reserved-rows.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-reserved-rows.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-reserved-rows.efi" "APS ARCFS RESERVED ROWS OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS RESERVED ROWS OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0042's reserved row growth capacity is real -- the Object/Namespace/Attribute rows each carry genuinely unpopulated reserved fields for future use, staying exactly zero through real creation, commit, remount, rename, and reflink, with the on-disk tree formats unchanged, QEMU-proven under a real CPU"
