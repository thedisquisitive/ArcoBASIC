#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every entry point this increment touches compiles cleanly at X86_64 level.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/system_namespace_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.ActivateSystemVolume ArcFS.SelectBlockDevice Namespace.Initialize \
             Namespace.AttachFilesystem Namespace.Resolve; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS system-volume/namespace entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0043 Phase V): closes the investigation-identified gap -- ArcFS.
# ActivateSystemVolume (RFC-0039 Phase G's real boot policy) and the RFC-0041 Arcology System
# Namespace's own filesystem attachment/delegation have NEVER before been proven against a real
# persistent block device (both their own existing fixtures use RAMDisk exclusively). Two
# genuinely separate fixtures, compiled separately, run as two separate qemu-system-x86_64
# processes against the same real disk image file: the WRITER formats/creates/commits real content
# to a real attached disk and fully exits; the READER (a separate process, separate compile)
# discovers the SAME disk, calls ArcFS.ActivateSystemVolume() (not a plain MountImage), attaches
# the activated volume into the System Namespace, and resolves a path that delegates across the
# attachment boundary into the real content -- confirmed to reach the SAME object a direct
# ArcFS.Lookup would.
"$ARCOFISSION" build "$ROOT/tests/fixtures/arcfs-sysvol/aps-arcfs-sysvol-writer.abas" \
    -o "$TMP_ROOT/writer.efi" --target uefi-x86_64 --entry Main > /dev/null
"$ARCOFISSION" build "$ROOT/tests/fixtures/arcfs-sysvol/aps-arcfs-sysvol-reader.abas" \
    -o "$TMP_ROOT/reader.efi" --target uefi-x86_64 --entry Main > /dev/null

# 4 MiB: RFC-0043 Phase U's own real, honestly-named cost applies here too -- every commit now
# allocates a genuinely fresh ~395-sector Allocation Bitmap span, so a disk sized for the OLD
# single-sector bitmap (RFC-0043 Phase T's own 256 KiB frozen fixtures, unaffected since they
# inline their own pre-Phase-U stdlib copy) is no longer big enough for a fixture built against the
# CURRENT live stdlib.
dd if=/dev/zero of="$TMP_ROOT/disk.img" bs=1048576 count=4 status=none

WRITER_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/writer.efi" "$TMP_ROOT/disk.img" "SYSVOL WRITER DONE" 25)"
echo "$WRITER_OUTPUT"
[ "$WRITER_OUTPUT" = "PASS: SYSVOL WRITER DONE" ] || { echo "FAIL: unexpected writer output" >&2; exit 1; }

READER_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/reader.efi" "$TMP_ROOT/disk.img" "SYSVOL READER DONE" 25)"
echo "$READER_OUTPUT"
[ "$READER_OUTPUT" = "PASS: SYSVOL READER DONE" ] || { echo "FAIL: unexpected reader output" >&2; exit 1; }

# Negative control: confirm the delegated-resolution-reaches-the-same-object check is real.
sed 's/IF directOid <> arcfsOid THEN/IF directOid = arcfsOid THEN/' \
    "$ROOT/tests/fixtures/arcfs-sysvol/aps-arcfs-sysvol-reader.abas" > "$TMP_ROOT/reader-negative.abas"
"$ARCOFISSION" build "$TMP_ROOT/reader-negative.abas" -o "$TMP_ROOT/reader-negative.efi" --target uefi-x86_64 --entry Main > /dev/null
NEGATIVE_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/reader-negative.efi" "$TMP_ROOT/disk.img" "SYSVOL READER OID MISMATCH" 25)"
[ "$NEGATIVE_OUTPUT" = "PASS: SYSVOL READER OID MISMATCH" ] || { echo "FAIL: negative control did not report a real mismatch" >&2; exit 1; }

echo "PASS: RFC-0043 Phase V is real -- ArcFS.ActivateSystemVolume and RFC-0041's System Namespace attachment/delegation both genuinely work against a real persistent block device across a real process boundary, for the first time, negative control confirmed real"
