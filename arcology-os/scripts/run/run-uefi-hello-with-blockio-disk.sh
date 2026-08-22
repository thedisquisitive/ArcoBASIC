#!/usr/bin/env bash
# QEMU/OVMF harness for proving real UEFI Block I/O Protocol discovery and I/O against a
# genuinely separate, real emulated disk (RFC-0038 Section 18's "Real block-device drivers ...
# virtio-blk (QEMU-native, useful for faster iteration than AHCI emulation)"). A near-identical
# twin of run-uefi-hello.sh (see that script for the base rationale on every flag this one also
# uses); the only addition is a second `-drive`/`-device virtio-blk-pci` pair attaching
# DISK_IMAGE as a real block device distinct from the boot media.
#
# The boot media itself (the `fat:` directory OVMF boots BOOTX64.EFI from) is ALSO a real
# EFI_BLOCK_IO_PROTOCOL handle once OVMF enumerates it -- UEFI.BLOCKIO.DISCOVER's single
# LocateProtocol call (matching UEFI.GOP.DISCOVER's own documented single-handle scope) returns
# whichever handle firmware hands back first, and this harness does not control or guarantee
# which one that is. The fixture this harness runs is written to check for its own disk's known
# content and report honestly either way -- see blockio-disk-discovery.abas's own header comment.
#
# Usage: run-uefi-hello-with-blockio-disk.sh EFI_FILE DISK_IMAGE EXPECTED_OUTPUT [TIMEOUT_SECONDS]
#
# Exit status: identical contract to run-uefi-hello.sh (0 pass, 1 output mismatch, 2 environment
# problem).
set -euo pipefail

EFI_FILE="${1:?usage: run-uefi-hello-with-blockio-disk.sh EFI_FILE DISK_IMAGE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
DISK_IMAGE="${2:?usage: run-uefi-hello-with-blockio-disk.sh EFI_FILE DISK_IMAGE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
EXPECTED="${3:?usage: run-uefi-hello-with-blockio-disk.sh EFI_FILE DISK_IMAGE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
TIMEOUT_SECONDS="${4:-20}"

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

if [ ! -f "$EFI_FILE" ]; then
    echo "ERROR: EFI application not found: $EFI_FILE" >&2
    exit 2
fi
if [ ! -f "$DISK_IMAGE" ]; then
    echo "ERROR: disk image not found: $DISK_IMAGE" >&2
    exit 2
fi

BOOT_DIR="$(mktemp -d)"
trap 'rm -rf "$BOOT_DIR"' EXIT
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$EFI_FILE" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

# -vga none forces OVMF's console onto the serial port. Both drives are attached via
# virtio-blk-pci -- real, distinct PCI block devices OVMF's own built-in virtio-blk driver
# enumerates as genuine EFI_BLOCK_IO_PROTOCOL handles, not a QEMU/ArcoBASIC-specific mechanism.
# Explicit PCI addr= on both (empirically confirmed against real OVMF, not assumed): OVMF's PCI
# bus walk binds drivers/installs BlockIo handles in ascending PCI address order, and
# UEFI.BLOCKIO.DISCOVER's own single LocateProtocol call returns the FIRST handle in that
# resulting order -- giving the disk under test (blockio0) a lower address than the boot media's
# own drive makes discovery land on it deterministically, without adding any multi-handle
# enumeration surface to the binding itself.
# -nodefaults (a standard QEMU flag, no ArcoBASIC-specific mechanism) is required here: without
# it, QEMU's default "pc" machine type auto-adds an empty IDE CD-ROM drive that also exposes a
# real EFI_BLOCK_IO_PROTOCOL handle (BlockSize=2048, MediaPresent=FALSE) -- and, empirically
# confirmed against real OVMF, that handle is what LocateProtocol returns FIRST, ahead of either
# explicit drive above, defeating the addr= ordering entirely. -vga none/-net none/-serial stdio
# below re-add every default this harness still needs.
# -m 512: explicit, generous RAM -- QEMU's own default (no -m flag) is 128 MiB, confirmed
# empirically insufficient for RFC-0042 Phase Q's production-scale ArcFS capacities (-nodefaults
# above suppresses default DEVICES, not the default RAM size, so this is still needed here); see
# run-uefi-hello.sh's own header note and .agents/reports/aps-qemu-ram-ceiling.md for the full story.
OUTPUT="$(timeout "$TIMEOUT_SECONDS" "$QEMU_BIN" \
    -nodefaults \
    -bios "$OVMF_FD" \
    -m 512 \
    -drive file="$DISK_IMAGE",format=raw,if=none,id=blockio0 \
    -device virtio-blk-pci,drive=blockio0,addr=0x3 \
    -drive file="fat:rw:$BOOT_DIR",format=raw,if=none,id=bootdisk \
    -device virtio-blk-pci,drive=bootdisk,addr=0x4 \
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
