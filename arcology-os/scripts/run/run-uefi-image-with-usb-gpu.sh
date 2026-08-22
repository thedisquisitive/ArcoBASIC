#!/usr/bin/env bash
# Deterministic QEMU/OVMF harness for a real, wrapped removable-media IMAGE (the same raw .img
# build-arcology-hardware-image.py produces, dd-writable to a real USB stick) booted through a
# REAL USB Mass Storage Class device -- not a plain `-drive`/virtio-blk/IDE block device the way
# run-arcology-hardware-image.sh and run-uefi-hello-with-blockio-disk.sh attach their own drives.
# Real UEFI firmware routes USB boot media through its own USB host controller + Mass Storage
# Class driver stack, genuinely distinct from how it discovers a SATA/NVMe/IDE disk -- proving a
# fixture boots via a plain `-drive` does NOT prove it boots via USB specifically. This harness
# exists so any fixture that will actually ship on a USB stick gets proven through the SAME driver
# path real hardware will use, not merely a block-device path that happens to also succeed.
#
# Also attaches a real `-vga std` GOP device (see run-uefi-hello-with-gpu.sh's own header note for
# why -vga none would prevent OVMF's GOP driver from binding to anything) -- combined here because
# the fixtures this harness exists for (real USB-bootable images with a real on-screen render) need
# both real capabilities at once, not one or the other.
#
# The application under test MUST report its result over the SERIAL port (matching
# run-uefi-hello-with-gpu.sh's own identical requirement) -- with a real display device attached,
# OVMF's ConOut defaults to the graphics console instead of serial, which this headless harness
# cannot capture otherwise.
#
# Usage: run-uefi-image-with-usb-gpu.sh IMAGE_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]
#
# Exit status: identical contract to run-uefi-hello.sh (0 pass, 1 output mismatch, 2 environment
# problem).
set -euo pipefail

IMAGE_FILE="${1:?usage: run-uefi-image-with-usb-gpu.sh IMAGE_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
EXPECTED="${2:?usage: run-uefi-image-with-usb-gpu.sh IMAGE_FILE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
TIMEOUT_SECONDS="${3:-25}"

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
    pacman -S edk2-ovmf              (Arch Linux)

This harness never downloads firmware automatically.
EOF
    exit 2
fi

if [ ! -f "$IMAGE_FILE" ]; then
    echo "ERROR: boot image not found: $IMAGE_FILE" >&2
    exit 2
fi

# shellcheck source=./_qemu_stream_common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_qemu_stream_common.sh"

# -nodefaults: without it, QEMU's default "pc" machine type auto-adds an empty IDE CD-ROM drive
# that also presents a real EFI_BLOCK_IO_PROTOCOL handle, the same reason
# run-uefi-hello-with-blockio-disk.sh already needs it -- not load-bearing for THIS harness's own
# single-drive design, but kept for consistency with every other multi-device harness in this
# directory. qemu-xhci: a real USB 3 host controller; usb-storage attached to it is what makes
# OVMF's own USB Mass Storage Class driver bind to this image at all, instead of it being invisible
# to firmware's own boot-device enumeration.
qemu_run_and_check "$TIMEOUT_SECONDS" "$EXPECTED" "$QEMU_BIN" \
    -nodefaults \
    -bios "$OVMF_FD" \
    -m 512 \
    -device qemu-xhci,id=xhci \
    -drive if=none,id=usbstick,format=raw,file="$IMAGE_FILE" \
    -device usb-storage,bus=xhci.0,drive=usbstick \
    -net none \
    -vga std \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot
exit $?
