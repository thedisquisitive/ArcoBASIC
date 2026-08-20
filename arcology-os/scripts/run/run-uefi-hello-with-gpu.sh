#!/usr/bin/env bash
# Deterministic QEMU/OVMF harness for ArcoBASIC UEFI applications that need a real GOP
# (Graphics Output Protocol) framebuffer -- e.g. anything calling UEFI.GOP.Discover. Identical to
# run-uefi-hello.sh except it attaches a real `-vga std` display device instead of `-vga none`,
# which is what makes OVMF's GOP driver bind to something at all.
#
# The application under test MUST report its result over the SERIAL port (SerialByte, matching
# every ArcFS fixture's own established pattern), NOT via systemTable.ConsoleOut.Write -- with a
# real display device attached, OVMF's ConOut defaults to the graphics console instead of serial
# (the exact inverse of run-uefi-hello.sh's own "-vga none forces ConOut onto serial" comment),
# which this headless harness cannot capture. `-display none` still keeps the run headless (no
# window), but a `-vga std` device's own framebuffer memory exists and is writable/readable either
# way -- headless does not mean "no framebuffer," only "nothing rendered to an actual window."
#
# Usage: run-uefi-hello-with-gpu.sh EFI_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]
#
# Exit status: identical contract to run-uefi-hello.sh.
set -euo pipefail

EFI_FILE="${1:?usage: run-uefi-hello-with-gpu.sh EFI_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
EXPECTED="${2:?usage: run-uefi-hello-with-gpu.sh EFI_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
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

BOOT_DIR="$(mktemp -d)"
trap 'rm -rf "$BOOT_DIR"' EXIT
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$EFI_FILE" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

OUTPUT="$(timeout "$TIMEOUT_SECONDS" "$QEMU_BIN" \
    -bios "$OVMF_FD" \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none \
    -vga std \
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
