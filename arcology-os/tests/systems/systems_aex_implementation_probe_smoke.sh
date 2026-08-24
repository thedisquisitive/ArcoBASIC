#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/aex-implementation-probe-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0046 AEX Phase 5: real native x86-64 implementation record and loader selection (Section
# 61's fifth implementation phase, Section 15/15.1/16). Builds directly on Phase 4's manifest and
# component graph parsing -- AEX.SelectImplementation walks a component's own ImplementationIDs
# list, validates each IMPLEMENTATION chunk in turn (owning component matches, real type, real
# bounds), and among the implementations actually compatible with this loader (NATIVE, X86_64, an
# ABI it can satisfy) picks the one with the highest declared priority, or fails with the specific
# implementation-resolution error Section 15.1 itself requires when none qualify. Still in-memory
# only -- ArcFS-backed code/data mapping is Phase 6, a real, separate, later increment.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/aex-implementation-probe/aex-implementation-probe.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: aex-implementation-probe does not compile:" >&2
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

for CASE in "VSEL : PASS" "PRIO : PASS" "ARCH : PASS" "ABIV : PASS" "IMIS : PASS" "ITYP : PASS" "IOOB : PASS" "IOWN : PASS"; do
    grep -aqF "$CASE" "$OUT" || { echo "FAIL: expected '$CASE' not found in output" >&2; cat "$OUT" >&2; exit 1; }
done
grep -aqF "DONE ALLPASS" "$OUT" || { echo "FAIL: probe did not report a real overall ALLPASS" >&2; cat "$OUT" >&2; exit 1; }

echo "PASS: RFC-0046 AEX Phase 5 -- real native x86-64 implementation record parsing and Section 15.1 loader selection, confirmed against 8 real byte-exact fixtures (a single compatible implementation, priority-based selection among two compatible implementations, an architecture mismatch, an ABI-minor-too-new mismatch, an unresolvable implementation chunk ID, a wrong-typed implementation chunk, an implementation exceeding its own declared logical length, and an owning-component mismatch), all correctly accepted, selected, or rejected"
