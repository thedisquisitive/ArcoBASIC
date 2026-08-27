#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/aex-interface-metadata-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0046 AEX Phase 8: interface metadata validation. The composed file includes ArcFS's active
# in-memory object/handle slice because the active AEX policy still contains AEX.LoadFromArcFS and
# the current systems backend emits declared helpers eagerly.

COMPOSED="$TMP_ROOT/aex-interface-metadata.abas"
{
    printf '#PROFILE UEFI\n#TARGET X86_64\n#RUNTIME NONE\n\n'
    sed -n '1,1578p' "$SOURCE_DIR/arcology-os/stdlib/arcfs_policy.abas" | sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d'
    printf '\n'
    sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d' "$SOURCE_DIR/arcology-os/stdlib/aex_format_policy.abas"
    printf '\n'
    sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d' "$SOURCE_DIR/arcology-os/tests/fixtures/aex-interface-metadata/aex-interface-metadata-body.abas"
} > "$COMPOSED"

"$ARCOFISSION" reveal "$COMPOSED" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: aex-interface-metadata does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$COMPOSED" -o "$TMP_ROOT/probe.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"

BOOT_DIR="$(mktemp -d)"
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$TMP_ROOT/probe.efi" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

timeout 30 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none -vga none -display none -serial file:"$TMP_ROOT/out.txt" -no-reboot \
    > "$TMP_ROOT/qemu.log" 2>&1 || true
rm -rf "$BOOT_DIR"

OUT="$TMP_ROOT/out.txt"
[ -s "$OUT" ] || { echo "FAIL: no serial output captured" >&2; cat "$TMP_ROOT/qemu.log" >&2; exit 1; }

for CASE in "INTF : PASS" "RSLV : PASS" "NPRO : PASS" "LOOK : PASS" "MISS : PASS" "TYPE : PASS" "OWNR : PASS" "BNDS : PASS" "DIRN : PASS" "VERS : PASS"; do
    grep -aqF "$CASE" "$OUT" || { echo "FAIL: expected '$CASE' not found in output" >&2; cat "$OUT" >&2; exit 1; }
done
grep -aqF "DONE ALLPASS" "$OUT" || { echo "FAIL: probe did not report a real overall ALLPASS" >&2; cat "$OUT" >&2; exit 1; }

echo "PASS: RFC-0046 AEX Phase 8 -- provided/required interface metadata validation confirmed under QEMU"
