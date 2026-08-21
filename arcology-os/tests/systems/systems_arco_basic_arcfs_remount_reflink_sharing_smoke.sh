#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every entry point this follow-on touches compiles cleanly at X86_64 level,
# combined with block_device_policy.abas the same way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSPopulateObjectRow ArcFSLoadObjects ArcFS.MountImage ArcFS.PrepareCommit \
             ArcFS.FormatVolume ArcFS.Reflink ArcFSEnsurePrivateSlot ArcFS.HandleWrite \
             ArcFS.HandleRead ArcFS.ReclaimGeneration ArcFS.GetHealthState; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS remount-reflink-sharing entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 11.5's reflink sharing now survives a real remount, verified
# DIRECTLY against each OID's own live dataSlot field and the shared slot's own refcount -- not
# merely inferred from matching content. A and B share one dataSlot after a real commit+remount
# (not two independent private copies that merely happen to read the same bytes); a SECOND
# consecutive remount with no intervening change still shares; a write through B alone forces
# real divergence (ArcFSEnsurePrivateSlot needed zero changes to make this work correctly even
# though the sharing relationship was RESTORED from a remount, not created fresh this session),
# and a further remount correctly shows A and B on DIFFERENT live slots, not silently re-merged.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-remount-reflink-sharing.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-remount-reflink-sharing.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-remount-reflink-sharing.efi" "APS ARCFS REMOUNT SHARE OK" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS REMOUNT SHARE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 11.5's on-disk reflink sharing now survives a real remount (verified directly against live dataSlot identity and refcount, not just content), and correctly stops sharing after a real divergence, is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
