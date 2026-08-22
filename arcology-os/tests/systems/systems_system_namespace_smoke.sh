#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: every RFC-0041 System Namespace entry point compiles cleanly at X86_64 level,
# combined with block_device_policy.abas + arcfs_policy.abas since ArcFS.Resolve is what
# Namespace.Resolve delegates into.
{
    echo "#PROFILE UEFI"
    echo "#TARGET X86_64"
    echo "#RUNTIME NONE"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/uefi_block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/block_device_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/arcfs_policy.abas"
    grep -v '^#PROFILE\|^#TARGET\|^#RUNTIME' "$ROOT/stdlib/system_namespace_policy.abas"
} > "$TMP_ROOT/combined.abas"

for entry in Namespace.Initialize Namespace.CreateContainer Namespace.CreateObject \
             Namespace.AttachFilesystem Namespace.DetachFilesystem Namespace.Resolve \
             Namespace.GetObjectHandle Namespace.GetObjectKind; do
    "$ARCOFISSION" reveal "$TMP_ROOT/combined.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/entry.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/entry.txt" || {
        echo "FAIL: System Namespace entry point $entry does not compile:" >&2
        cat "$TMP_ROOT/entry.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0041): a plain container tree; an Object Reference entry whose opaque
# handle/kind round-trip exactly; a REAL ArcFS volume attached at a namespace location
# (RFC-0039 Section 10's ATTACH step, made concrete for the first time); resolving a path that
# crosses the attachment boundary all the way to the attached volume's own real file, read back
# byte-for-byte through that cross-boundary resolution (not merely an ID match); a second
# attachment attempt correctly rejected; not-found correctly propagated through delegation;
# resolving the attachment point itself with no remainder correctly delegates to the attached
# volume's own root; and detaching removes reachability entirely.
FIXTURE="$ROOT/tests/fixtures/system-namespace/system-namespace.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/system-namespace.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/system-namespace.efi" "APS NAMESPACE OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS NAMESPACE OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: the Arcology System Namespace (RFC-0041) attaches a real ArcFS volume and delegates cross-boundary path resolution to it correctly, including not-found propagation and detach-removes-reachability -- under a real CPU (QEMU/OVMF)"
