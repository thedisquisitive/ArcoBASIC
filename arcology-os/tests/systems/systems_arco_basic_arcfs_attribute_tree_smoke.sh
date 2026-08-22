#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0040 Section 12 (Persistent Typed Attributes) entry point compiles
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

for entry in ArcFS.SetAttribute ArcFS.HasAttribute ArcFS.GetAttributeType ArcFS.GetAttribute \
             ArcFS.GetAttributeHigh ArcFSAttributeTreeNodeCapacity \
             ArcFSGatherSortedActiveAttributes ArcFSWriteOneAttributeLeafNode \
             ArcFSBuildAttributeTree ArcFSAttributeTreeCollectAllEntriesInto \
             ArcFSAttributeTreeCollectAllEntriesTolerant ArcFSAttributeTreeContainsSector \
             ArcFSPopulateAttributeRow ArcFSLoadAttributes ArcFSGenerationContainsSector \
             ArcFSReadCheckpoint ArcFSWriteCheckpointRecord ArcFSPeekCheckpoint \
             ArcFS.FormatVolume ArcFS.PrepareCommit ArcFS.MountImage ArcFS.ReclaimGeneration \
             ArcFS.GetHealthState; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Attribute Tree entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 12's Attribute Tree, end to end under a real CPU. Closes
# RFC-0039 Phase D's own explicitly documented gap ("a value set here does NOT survive
# ArcFS.MountImage()"). 5 objects get distinctly typed attributes (unsigned, signed, boolean,
# UUID/OID with BOTH halves, and one object deliberately gets NO attribute at all) forcing a real
# multi-node Attribute Tree; every one survives a real commit and remount, including the
# genuinely-absent case. Health scrub reports Healthy through the entirely new tree-sourced
# path. Committing again with no further change supersedes the WHOLE Attribute Tree, and
# reachability correctly reports the old root unreachable. Real reclamation leaves everything
# intact. Updating an existing attribute (changing both its type and value) persists correctly,
# leaving every other object's attribute untouched.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-attribute-tree.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-attribute-tree.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-attribute-tree.efi" "APS ARCFS ATTRIBUTE TREE OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS ATTRIBUTE TREE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 12's Attribute Tree (real multi-node B+tree, typed values including a real 128-bit UUID/OID, reachability through tree structure, reclamation of superseded tree nodes, and durable attribute mutation surviving a real remount) is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
