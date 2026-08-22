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
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSCheckFeatureFlags ArcFSRecognizedIncompatMask ArcFSRecognizedRoCompatMask \
             ArcFS.MountImage ArcFS.MountImageSafe ArcFS.PrepareCommit; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS feature-negotiation entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0042 Section 7): the superblock's own feature-flags field, unused since
# Phase H ("none defined yet"), now has a real mount-time contract. An unrecognized INCOMPAT bit,
# written directly onto a real superblock ring slot with a correctly recomputed checksum, genuinely
# refuses the mount; restoring flags to 0 mounts the SAME volume normally again. An unrecognized
# RO-COMPAT bit mounts successfully but read-only -- a real commit attempt is genuinely refused by
# the same ReadOnlySafety gate RFC-0039 Phase F's own repair mechanism already established, not
# merely a status flag nobody enforces; restoring flags to 0 resumes real read-write commits. A bit
# set only in the reserved-for-future range is genuinely ignored, mounting normally.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-feature-negotiation.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-feature-negotiation.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-feature-negotiation.efi" "APS ARCFS FEATURE FLAGS OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS FEATURE FLAGS OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0042's feature negotiation is real -- an unrecognized incompat bit refuses to mount, an unrecognized ro-compat bit mounts read-only with commits genuinely blocked, reserved-for-future bits are genuinely ignored, and every transition is reversible with no reformat, QEMU-proven under a real CPU"
