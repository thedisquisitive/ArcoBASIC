#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: ArcFS.ActivateSystemVolume (this addendum's one new entry point) compiles
# cleanly at X86_64 level, combined with block_device_policy.abas the same way every prior ArcFS
# smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.ActivateSystemVolume ArcFS.MountImageSafe ArcFS.RepairReattachOrphans \
             ArcFS.RepairCommit; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS system-volume entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0039 Section 78 Phase G's "system volume use," narrowed honestly to what
# ArcFS itself can deliver -- Section 37's activation checklist and Section 38's ReadOnlySafety,
# turned into a real, self-healing BOOT POLICY rather than tools a human runs by hand. Three real
# outcomes, all in one continuous boot session: a healthy volume activates read-write directly; a
# volume with a real, automatically-repairable structural defect (the same orphan scenario RFC-0039
# Phase F's own fixture builds) self-heals during activation and STILL comes up read-write, with
# the reattached object proven to be the SAME object (same OID, same content) rather than a fresh
# one silently fabricated in its place; a catastrophically corrupt volume (destroyed superblock)
# correctly reports Unavailable rather than guessing. Real UEFI Block IO discovery of physical
# storage remains explicitly out of scope -- that is RFC-0038's own named future work (its Section
# 17.5 stop condition warns against improvising new UEFI protocol bindings as a side effect of
# unrelated work), not something this addendum attempts.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-system-volume.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-system-volume.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-system-volume.efi" "APS ARCFS SYSVOL OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS SYSVOL OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: ArcFS.ActivateSystemVolume implements RFC-0039 Section 37/38's activation + ReadOnlySafety as a real self-healing boot policy -- healthy activates read-write, an automatically-repairable defect self-heals and still comes up read-write, and a catastrophically corrupt volume reports Unavailable rather than guessing -- under a real CPU (QEMU/OVMF)"
