#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/aex-preloaded-boot-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

PRELOAD="$TMP_ROOT/preloaded-test.aex"
python3 "$SOURCE_DIR/arcology-os/scripts/build/generate-aex-userspace-smoke.py" "$PRELOAD"

COMPOSED="$TMP_ROOT/aex-preloaded-boot.abas"
{
    sed -n '1,1578p' "$SOURCE_DIR/arcology-os/stdlib/arcfs_policy.abas" | sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d'
    printf '\n'
    sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d' "$SOURCE_DIR/arcology-os/stdlib/aex_format_policy.abas"
    printf '\n'
    sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d;/^#CALLCONV /d;/^#EXPORT /d' "$SOURCE_DIR/arcology-os/tests/fixtures/aex-preloaded-boot/aex-preloaded-boot.abas"
} > "$COMPOSED"

"$ARCOFISSION" build "$COMPOSED" -o "$TMP_ROOT/BOOTX64.EFI" --target uefi-x86_64 --entry Main >/dev/null
python3 "$SOURCE_DIR/arcology-os/scripts/build/build-arcology-hardware-image.py" \
    "$TMP_ROOT/BOOTX64.EFI" "$TMP_ROOT/arcology-aex-preload.img"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1 || \
   ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: boot image built; qemu-system-x86_64/OVMF unavailable."
    exit 0
fi

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"
timeout 30 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
    -drive "file=$TMP_ROOT/arcology-aex-preload.img,format=raw" \
    -device loader,file="$PRELOAD",addr=0x7000000,force-raw=on \
    -net none -vga none -display none -serial file:"$TMP_ROOT/out.txt" -monitor none -no-reboot \
    > "$TMP_ROOT/qemu.log" 2>&1 || true

OUT="$TMP_ROOT/out.txt"
[ -s "$OUT" ] || { echo "FAIL: no serial output captured" >&2; cat "$TMP_ROOT/qemu.log" >&2; exit 1; }
grep -aqF "ARCO " "$OUT" || { echo "FAIL: Arcology boot marker missing" >&2; cat "$OUT" >&2; exit 1; }
grep -aqF "AEX PASS" "$OUT" || { echo "FAIL: preloaded AEX did not activate" >&2; cat "$OUT" >&2; exit 1; }

echo "PASS: Arcology boot image activated a preloaded AEX program under QEMU"
