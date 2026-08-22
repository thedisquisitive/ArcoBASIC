#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Phase H entry point compiles cleanly at X86_64 level, combined
# with block_device_policy.abas the same way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSCrc32C ArcFSReadSuperblockRing ArcFSReadCheckpoint ArcFSWriteCheckpointRecord \
             ArcFSWriteSuperblockRecord ArcFS.FormatVolume ArcFS.PrepareCommit ArcFS.PublishCommit \
             ArcFS.MountImage ArcFS.ReclaimableSectorCount; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase H entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 16's Phase H (FMV2 superblock/checkpoint shape, Sections 8 and
# 15; CRC-32C checksums, Section 8.4), end to end, under a real CPU. A freshly formatted FMV2
# volume mounts through the new 4-copy superblock ring and resolves its root; a real commit
# publishes generation 2 and its checkpoint's PreviousGeneration field correctly links back to
# generation 1's checkpoint sector (RFC-0040 Section 8.3's backward-linked history, genuinely
# walkable); flipping one byte of a committed object record makes CRC-32C catch it and
# ArcFS.MountImage() fail closed; and the ring tolerates two of its four copies being reverted to
# stale generation-1 content, still correctly selecting generation 2 via the two copies that
# actually have it. 128-bit OIDs (RFC-0040 Section 10) are deliberately not part of this delivery
# -- see .agents/reports/aps-arcfs-phase-h.md for why.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-h.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-h.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-h.efi" "APS ARCFS PHASE H OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE H OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Phase H's FMV2 format foundation (superblock ring, generation history, CRC-32C) is real and QEMU-proven -- a fresh format, a real commit, a genuinely detected corruption, and a degraded ring all resolve correctly under a real CPU (QEMU/OVMF)"
