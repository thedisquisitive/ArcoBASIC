#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/aex-manifest-probe-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0046 AEX Phase 4: real manifest and component graph parsing (Section 61's fourth
# implementation phase). Builds directly on Phase 1-3's header/directory validation --
# AEX.ValidateManifest resolves the header's own RootManifestID through the chunk directory,
# bounds-checks the manifest's own variable regions, confirms RootComponentID is genuinely
# among DeclaredComponentIDs (a real graph-consistency check, not just a bounds check), then
# validates every declared component chunk in turn, including that each component's own
# recorded OwningAssemblyID actually matches the manifest's AssemblyID. Still in-memory only --
# ArcFS-backed loading is Phase 6, a real, separate, later increment.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/aex-manifest-probe/aex-manifest-probe.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: aex-manifest-probe does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/probe.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"

BOOT_DIR="$(mktemp -d)"
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$TMP_ROOT/probe.efi" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

timeout 20 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none -vga none -display none -serial file:"$TMP_ROOT/out.txt" -no-reboot \
    > "$TMP_ROOT/qemu.log" 2>&1 || true
rm -rf "$BOOT_DIR"

OUT="$TMP_ROOT/out.txt"
[ -s "$OUT" ] || { echo "FAIL: no serial output captured" >&2; cat "$TMP_ROOT/qemu.log" >&2; exit 1; }

for CASE in "VALID: PASS" "MISMF: PASS" "WRTYP: PASS" "MFOOB: PASS" "RTNDC: PASS" "MISCP: PASS" "CPASM: PASS" "CPOOB: PASS"; do
    grep -aqF "$CASE" "$OUT" || { echo "FAIL: expected '$CASE' not found in output" >&2; cat "$OUT" >&2; exit 1; }
done
grep -aqF "DONE ALLPASS" "$OUT" || { echo "FAIL: probe did not report a real overall ALLPASS" >&2; cat "$OUT" >&2; exit 1; }

echo "PASS: RFC-0046 AEX Phase 4 -- real manifest and component graph parsing, confirmed against 8 real byte-exact fixtures (valid manifest, unresolvable RootManifestID, wrong-typed root chunk, manifest content out of its own declared logical length, an undeclared RootComponentID, a declared-but-missing required component, a component whose own OwningAssemblyID doesn't match the manifest's AssemblyID, and a component out of its own declared logical length), all correctly accepted or rejected"
