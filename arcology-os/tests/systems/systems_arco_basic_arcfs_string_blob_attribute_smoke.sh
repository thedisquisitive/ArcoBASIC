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
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.Initialize ArcFS.CreateFile ArcFS.CommitImage ArcFS.MountImage ArcFS.SetAttribute \
             ArcFS.SetStringAttribute ArcFS.SetBlobAttribute ArcFS.GetStringAttribute ArcFS.GetBlobAttribute; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS string/blob attribute entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0042 Section 12): a real UTF-8 string (genuine multi-byte content, not just
# ASCII) and a real binary blob (spanning the full 0..255 byte range, including a real 0x00 mid-
# buffer and a real 0xFF) both survive a real commit + remount, coexisting on the SAME object with
# a pre-existing integer-typed attribute that stays completely undisturbed. Also proves the
# ArcFSFileCapacityBytes() size cap is refused (not silently truncated, and leaves no attribute row
# behind), and that GetStringAttribute fails closed when the caller's own buffer is too small.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-string-blob-attribute.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-string-blob-attribute.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-string-blob-attribute.efi" "APS ARCFS STRING BLOB ATTRIBUTE OK" 40)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS STRING BLOB ATTRIBUTE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0042 Phase R is real -- a genuine multi-byte UTF-8 string and a genuine binary blob (full 0..255 byte range, real 0x00/0xFF) both committed and read back byte-for-byte after a real remount, coexisting with an undisturbed pre-existing integer attribute, QEMU-proven under a real CPU"
