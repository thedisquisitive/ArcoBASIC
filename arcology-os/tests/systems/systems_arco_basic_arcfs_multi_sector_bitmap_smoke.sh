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
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.FormatVolume ArcFS.MountImage ArcFS.CommitImage ArcFSLoadBitmap \
             ArcFSWriteBitmapRecord ArcFSBitmapSectorCount ArcFSCheckpointBaseSector \
             ArcFSHealthRecordSector ArcFSCheckFeatureFlags; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS multi-sector bitmap entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0043 Phase U): the Allocation Bitmap's own volume-size ceiling is real and
# raised. Step One: a real volume, formatted and committed by the current code, genuinely
# allocates sectors well past the OLD 508-sector (~254KB) ceiling -- directly observed (the real
# checkpoint sector number read back after a real commit), not inferred -- with real file content
# and a real attribute surviving a real commit+remount byte-for-byte. Step Two: the real on-disk
# superblock genuinely carries the new incompat bit (RFC-0042 Section 7's own feature-negotiation
# mechanism, its first real consumer), and a reader that does not recognize that specific bit
# (a frozen legacy replica of ArcFSCheckFeatureFlags, the same same-process technique RFC-0042
# Phase O already established) genuinely refuses to mount it.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-multi-sector-bitmap.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-multi-sector-bitmap.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-multi-sector-bitmap.efi" "APS ARCFS MULTI SECTOR BITMAP OK" 40)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS MULTI SECTOR BITMAP OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0043 Phase U is real -- the Allocation Bitmap's volume-size ceiling is genuinely raised past the old 508-sector limit, real content survives a real commit+remount past it, and the new incompat feature bit genuinely makes an unrecognizing reader refuse, QEMU-proven under a real CPU"
