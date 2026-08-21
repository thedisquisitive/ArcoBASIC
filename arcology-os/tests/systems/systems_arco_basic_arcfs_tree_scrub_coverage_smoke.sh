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
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFSScanGeneration ArcFSAttributeTreeCollectAllEntriesTolerant \
             ArcFSExtentTreeCollectAllEntriesTolerant ArcFSPeekCheckpoint ArcFS.GetHealthState \
             ArcFS.ScrubVolume ArcFS.MountImageSafe ArcFS.SetAttribute ArcFS.GetAttribute; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS tree-scrub-coverage entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: the Attribute Tree and Extent Tree both now have real defect-tolerant scrub
# coverage, wired into ArcFSScanGeneration as Pass 6/7 -- closing two previously-named gaps
# (ArcFSAttributeTreeCollectAllEntriesTolerant existed since Phase L but was never actually wired
# into a scan pass; the Extent Tree had no tolerant variant at all). A file with real multi-chunk
# content (real Extent Tree entries) AND a real attribute (real Attribute Tree entry) scrubs
# Healthy after a real commit+remount, confirming neither new pass false-positives on legitimate
# data. Directly corrupting the Attribute Tree's own root node on the BlockDevice (breaking its
# CRC-32C checksum) makes ArcFS.GetHealthState() genuinely report Corrupt (matching this
# implementation's own established convention for any tree-node-level structural defect); restoring
# the bytes returns Healthy. The identical corrupt/restore cycle against the Extent Tree's own root
# node proves Pass 7 the same way, with file content confirmed byte-for-byte correct afterward.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-tree-scrub-coverage.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-tree-scrub-coverage.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-tree-scrub-coverage.efi" "APS ARCFS SCRUB COVERAGE OK" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS SCRUB COVERAGE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: the Attribute Tree and Extent Tree both have real defect-tolerant scrub coverage (Pass 6/7), verified to detect genuine on-disk corruption and NOT false-positive on legitimate data, QEMU-proven under a real CPU"
