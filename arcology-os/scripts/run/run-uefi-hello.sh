#!/usr/bin/env bash
# Deterministic QEMU/OVMF harness for ArcoBASIC UEFI applications (Packet WP-010,
# arcology-os/docs/systems/qemu-ovmf-harness.md). Boots a built .efi application under real UEFI firmware and
# checks its console output for an expected string.
#
# Usage: run-uefi-hello.sh EFI_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]
#
# Exit status:
#   0  expected output found -- prints "PASS: EXPECTED_OUTPUT"
#   1  QEMU ran but the expected output was not found -- prints "FAIL: ..." with captured output
#   2  environment problem (qemu/OVMF not installed, or EFI_FILE missing) -- never a silent
#      download; installation guidance is printed to stderr instead
set -euo pipefail

EFI_FILE="${1:?usage: run-uefi-hello.sh EFI_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
EXPECTED="${2:?usage: run-uefi-hello.sh EFI_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
TIMEOUT_SECONDS="${3:-20}"

QEMU_BIN="$(command -v qemu-system-x86_64 || true)"
if [ -z "$QEMU_BIN" ]; then
    cat >&2 <<'EOF'
ERROR: qemu-system-x86_64 was not found on PATH.

Install QEMU's x86 system emulator to run this harness, for example:
    apt install qemu-system-x86     (Debian/Ubuntu)
    dnf install qemu-system-x86     (Fedora)
    pacman -S qemu-system-x86       (Arch Linux)

This harness never downloads or installs anything automatically.
EOF
    exit 2
fi

OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1 || true)"
if [ -z "$OVMF_FD" ]; then
    cat >&2 <<'EOF'
ERROR: no OVMF UEFI firmware image was found under /usr/share/ovmf or /usr/share/OVMF.

Install OVMF to run this harness, for example:
    apt install ovmf                (Debian/Ubuntu)
    dnf install edk2-ovmf           (Fedora)
    pacman -S edk2-ovmf             (Arch Linux)

This harness never downloads firmware automatically.
EOF
    exit 2
fi

if [ ! -f "$EFI_FILE" ]; then
    echo "ERROR: EFI application not found: $EFI_FILE" >&2
    exit 2
fi

# A directory served as a virtual FAT filesystem via QEMU's built-in `fat:` driver is an
# "equivalent test image" to a real FAT-formatted partition (Packet WP-010 explicitly allows
# either) and needs no extra tools (mtools/mkfs.vfat) beyond QEMU itself.
BOOT_DIR="$(mktemp -d)"
trap 'rm -rf "$BOOT_DIR"' EXIT
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$EFI_FILE" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

# -vga none forces OVMF's console onto the serial port (with a display device present, ConOut
# defaults to the graphics console instead, which a headless harness cannot capture).
OUTPUT="$(timeout "$TIMEOUT_SECONDS" "$QEMU_BIN" \
    -bios "$OVMF_FD" \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none \
    -vga none \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot 2>/dev/null || true)"

if printf '%s' "$OUTPUT" | grep -aqF "$EXPECTED"; then
    echo "PASS: $EXPECTED"
    exit 0
fi

echo "FAIL: expected output not found: $EXPECTED" >&2
echo "--- captured console output (last 4000 bytes) ---" >&2
printf '%s' "$OUTPUT" | tail -c 4000 >&2
exit 1
