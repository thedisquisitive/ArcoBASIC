#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: ArcFS.HandleWrite/HandleRead's new offset-based signature (RFC-0040 Section
# 11.4, closing Phase K's own last major deferred item) compiles cleanly at X86_64 level, combined
# with block_device_policy.abas the same way every prior ArcFS smoke test already does.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.HandleWrite ArcFS.HandleRead ArcFS.Resize ArcFSEnsurePrivateSlot \
             ArcFS.FormatVolume ArcFS.PrepareCommit ArcFS.MountImage ArcFS.ReclaimGeneration \
             ArcFS.GetHealthState; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS offset-I/O entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: RFC-0040 Section 11.4's offset-based partial reads/writes, end to end under a
# real CPU. A mid-file write (offset 40, within a 100-byte file) patches its own range WITHOUT
# shrinking the file or discarding what's outside that range -- the exact gotcha Phase K's own
# fixture had to work around with low-level verification, retroactively fixed by this change.
# Offset-based HandleRead returns a middle slice directly. A write whose offset (9000) lands far
# past the current size (100) creates a REAL sparse gap spanning chunk 0's own tail, ALL of chunk
# 1, and the start of chunk 2 -- a multi-chunk gap created by a WRITE's own offset+growth, not an
# explicit Resize call -- reading as zero and leaving chunk 1 genuinely untouched, both in-session
# and after a real commit/remount. Health scrub and reclaim afterward leave everything intact.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-offset-io.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-offset-io.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-offset-io.efi" "APS ARCFS OFFSET IO OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS OFFSET IO OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: RFC-0040 Section 11.4's offset-based partial reads/writes (mid-file patch preserving surrounding content, offset-based reads, a real multi-chunk sparse gap created by write growth) is genuine and QEMU-proven under a real CPU (QEMU/OVMF)"
