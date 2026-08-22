#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every ArcFS Phase D entry point compiles cleanly at X86_64 level. Combined the
# same way every prior ArcFS smoke test already does, since ArcFS.FormatVolume/CommitImage (used
# by the fixture below) depend on RAMDisk.* (stdlib/block_device_policy.abas, RFC-0038).
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.Resize ArcFS.Reflink ArcFS.SetAttribute ArcFS.HasAttribute ArcFS.GetAttribute \
             ArcFS.Rename ArcFS.Remove ArcFS.CommitImage ArcFS.MountImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase D entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: Reflink shares a data-pool slot between two OIDs with nothing copied at reflink
# time (the clone reads the source's exact bytes before either side writes); a write through the
# clone's handle triggers copy-on-write, proven by reading the SOURCE immediately afterward and
# finding it completely unaffected (RFC-0039 Section 22). Resize grows (zero-filling) and shrinks
# within the existing fixed capacity, also going through the same copy-on-write gate. Rename, move,
# delete, and a typed attribute are all exercised, then everything is committed as a real
# generation (Phase C's own protocol, unchanged) and the volume is remounted from scratch --
# proving the mutations are durable, and positively proving the one documented exception (the
# attribute does NOT survive remount, since attribute persistence is deferred) rather than merely
# asserting it in a comment.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-d.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-d.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-d.efi" "APS ARCFS PHASE D OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE D OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: ArcFS.Reflink's real copy-on-write, ArcFS.Resize, and Phase A's rename/move/delete all hold -- including surviving a real commit + remount cycle, with the one documented exception (attribute persistence) positively proven absent rather than merely asserted -- under a real CPU (QEMU/OVMF)"
