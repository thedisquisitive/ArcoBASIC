#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check (RFC-0043 Phase S): every block-device dispatch entry point compiles cleanly at
# X86_64 level, combined with the real UefiBlockDevice provider (not just RAMDisk's).
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.SelectBlockDevice ArcFS.ActiveBlockDevice ArcFSBlockReadSectors \
             ArcFSBlockWriteSectors ArcFSBlockSectorCount ArcFS.FormatVolume ArcFS.MountImage \
             ArcFS.CommitImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS block-device dispatch entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0043 Phase T): a genuine two-process cross-boot persistence proof. Process A
# (the WRITER, aps-arcfs-persist-writer.abas) selects the real UefiBlockDevice provider, formats a
# fresh ArcFS volume directly onto a real attached disk, creates a real file with real content and
# a real attribute, commits, confirms RAMDisk's own memory was never touched, and fully exits.
# Process B (the READER, aps-arcfs-persist-reader.abas) is a genuinely SEPARATE fixture, compiled
# separately and launched in a SEPARATE qemu-system-x86_64 process against the SAME disk image
# file -- it mounts what the writer committed and reads it back byte-for-byte against literal
# constants baked into its own source, with zero shared process state between the two.
"$ARCOFISSION" build "$ROOT/tests/fixtures/arcfs-persist/aps-arcfs-persist-writer.abas" \
    -o "$TMP_ROOT/writer.efi" --target uefi-x86_64 --entry Main > /dev/null
"$ARCOFISSION" build "$ROOT/tests/fixtures/arcfs-persist/aps-arcfs-persist-reader.abas" \
    -o "$TMP_ROOT/reader.efi" --target uefi-x86_64 --entry Main > /dev/null

dd if=/dev/zero of="$TMP_ROOT/disk.img" bs=262144 count=1 status=none

WRITER_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/writer.efi" "$TMP_ROOT/disk.img" "PERSIST WRITER RAMDISK CLEAN" 25)"
echo "$WRITER_OUTPUT"
[ "$WRITER_OUTPUT" = "PASS: PERSIST WRITER RAMDISK CLEAN" ] || { echo "FAIL: writer did not confirm RAMDisk stayed untouched" >&2; exit 1; }

# Independent, host-side confirmation: the real disk image FILE on the host filesystem now starts
# with ArcFS's own real superblock magic ("ARCFSB02") -- not inferred from QEMU's own console
# output, read directly off the artifact the writer process left behind after fully exiting.
MAGIC="$(python3 -c "print(open('$TMP_ROOT/disk.img','rb').read(8).decode('ascii', errors='replace'))")"
[ "$MAGIC" = "ARCFSB02" ] || { echo "FAIL: real disk image does not start with the ArcFS superblock magic (got: $MAGIC)" >&2; exit 1; }
echo "PASS: real disk image file genuinely contains the ArcFS superblock magic after the writer process fully exited"

READER_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/reader.efi" "$TMP_ROOT/disk.img" "PERSIST READER DONE" 25)"
echo "$READER_OUTPUT"
[ "$READER_OUTPUT" = "PASS: PERSIST READER DONE" ] || { echo "FAIL: unexpected reader output" >&2; exit 1; }

# Negative control: confirm the attribute-value check is real, not vacuous.
sed 's/IF ArcFS.GetAttribute(oid, 1) <> 424242 THEN/IF ArcFS.GetAttribute(oid, 1) <> 424243 THEN/' \
    "$ROOT/tests/fixtures/arcfs-persist/aps-arcfs-persist-reader.abas" > "$TMP_ROOT/reader-negative.abas"
"$ARCOFISSION" build "$TMP_ROOT/reader-negative.abas" -o "$TMP_ROOT/reader-negative.efi" --target uefi-x86_64 --entry Main > /dev/null
NEGATIVE_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-blockio-disk.sh" "$TMP_ROOT/reader-negative.efi" "$TMP_ROOT/disk.img" "PERSIST READER ATTRIBUTE VALUE MISMATCH" 25)"
[ "$NEGATIVE_OUTPUT" = "PASS: PERSIST READER ATTRIBUTE VALUE MISMATCH" ] || { echo "FAIL: negative control did not report a real mismatch" >&2; exit 1; }

echo "PASS: RFC-0043 Phase T is real -- ArcFS data genuinely survives past the process that wrote it, proven by two separate QEMU processes sharing nothing but a real disk image file, negative control confirmed real"
