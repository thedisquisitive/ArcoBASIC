#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every ArcFS Phase A entry point compiles cleanly at X86_64 level (RFC-0039
# Phase A's own "validate the object and API contract" -- a compile-time check that the contract
# is at least well-formed, before the real QEMU proof exercises it for real).
#
# stdlib/arcfs_policy.abas is no longer self-contained on its own since Phase B's ArcFS.MountImage
# (appended to the same file) depends on RAMDisk.* (stdlib/block_device_policy.abas, RFC-0038) --
# this compiler's `reveal` typechecks the whole module regardless of which --entry is requested, so
# even an unrelated Phase A entry point now fails to reveal from that file alone. Combine the two
# the same way the fixtures themselves already do.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in ArcFS.Initialize ArcFS.CreateFile ArcFS.CreateDirectory ArcFS.Lookup ArcFS.Resolve \
             ArcFS.Rename ArcFS.Remove ArcFS.OpenHandle ArcFS.CloseHandle ArcFS.HandleWrite \
             ArcFS.HandleRead; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: ArcFS Phase A entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: creates nested directories and a file, resolves multi-component colon-delimited
# paths (including a deliberately-missing one), writes and reads file content through a handle
# with an exact length + checksum match, renames within the same directory (OID-preserving),
# moves to a different directory (OID-preserving, old path confirmed gone, new path confirmed
# resolving to the SAME OID), confirms a handle opened AFTER the move still reads the original
# content, and removes it -- all under a real CPU. This is RFC-0039 Section 5.1's central design
# principle ("a path is a human and programmatic lookup expression; a persistent Object ID is
# identity") proven end to end, not merely asserted.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-arcfs-phase-a.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-arcfs-phase-a.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-arcfs-phase-a.efi" "APS ARCFS PHASE A OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS ARCFS PHASE A OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: ArcFS Phase A's in-memory object/namespace/handle contract -- create, resolve, read/write, rename, move, remove, with OID identity surviving rename/move -- holds under a real CPU (QEMU/OVMF)"
