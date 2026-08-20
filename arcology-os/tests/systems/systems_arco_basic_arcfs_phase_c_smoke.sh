#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every ArcFS Phase C entry point compiles cleanly at X86_64 level.
# ArcFS.FormatVolume/PrepareCommit/PublishCommit/CommitImage depend on RAMDisk.* (stdlib/
# block_device_policy.abas, RFC-0038) -- combined into one probe file the same way both fixtures
# themselves already do, and the same way Phase A's and Phase B's own smoke tests already do.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.FormatVolume ArcFS.PrepareCommit ArcFS.PublishCommit ArcFS.CommitImage \
             ArcFS.MountImage; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase C entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; these fixtures only prove anything when executed."
    exit 0
fi

# The commit-protocol proof: ArcFS.FormatVolume() writes a brand-new generation-1 volume to a real
# BlockDevice (RFC-0038's RAMDisk); ordinary Phase A CreateDirectory/CreateFile/HandleWrite calls
# mutate the in-memory tree; ArcFS.CommitImage() durably publishes it as generation 2; a fresh
# ArcFS.MountImage() (Phase B's own reader, unchanged) proves generation 2 round-trips exactly. A
# second mutate+commit proves generation 3 round-trips too, AND that generation 2's own checkpoint
# sector was never modified in place -- copy-on-write (RFC-0039 Section 5.2), not overwrite.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-c.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-c.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-c.efi" "APS ARCFS PHASE C OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE C OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# The crash-injection proof (RFC-0039 Section 78's own "crash-injection tests"; Section 75.5's
# requirement that recovery yields either the complete old generation or the complete new
# generation, never a hybrid): ArcFS.PrepareCommit() writes every sector a commit needs WITHOUT
# touching the superblock -- simply never calling ArcFS.PublishCommit() IS the simulated crash, no
# real power cut needed. This fixture proves both directions: prepare-without-publish leaves the
# previous generation as the only one any subsequent mount can see; redoing the same mutation and
# completing the commit makes the new generation visible.
CRASH_FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-c-crash.abas"
"$ARCOFISSION" build "$CRASH_FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-c-crash.efi" --target uefi-x86_64 > /dev/null

CRASH_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-c-crash.efi" "APS ARCFS PHASE C CRASH OK" 20)"
echo "$CRASH_OUTPUT"
[ "$CRASH_OUTPUT" = "PASS: APS ARCFS PHASE C CRASH OK" ] || { echo "FAIL: unexpected harness output for the crash-injection test" >&2; exit 1; }

echo "PASS: ArcFS.FormatVolume + PrepareCommit/PublishCommit/CommitImage implement RFC-0039's copy-on-write commit protocol -- durable, atomic checkpoint publication proven across two real generations, and an unpublished prepared commit proven invisible until published -- under a real CPU (QEMU/OVMF)"
