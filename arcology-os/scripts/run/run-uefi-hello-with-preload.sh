#!/usr/bin/env bash
# QEMU/OVMF harness for ArcoBASIC UEFI applications that need a second, raw data blob already
# present in guest physical memory at a fixed address before the application starts running --
# RFC-0038's RAM Disk Block Device Provider proof needs its backing FAT32 test image to already
# be at RAMDiskBaseAddress() (0x4000000, see stdlib/block_device_policy.abas) by the time
# RAMDisk.ReadSectors is first called, and this project's frontend has no UEFI Block IO Protocol
# binding today to copy it there itself (RFC-0038 Stop Condition #17.5's own anticipated gap --
# this harness sidesteps it rather than hitting it, per that RFC's implementation report).
#
# A near-identical twin of run-uefi-hello.sh (see that script for the base rationale on every
# flag this one also uses); the only addition is a `-device loader,file=...,addr=...,force-raw=on`
# QEMU device, a standard, real QEMU mechanism (not an ArcoBASIC/Arcology-specific hack) for
# staging a raw file's bytes into guest physical memory before boot.
#
# Usage: run-uefi-hello-with-preload.sh EFI_FILE PRELOAD_FILE PRELOAD_ADDR EXPECTED_OUTPUT [TIMEOUT_SECONDS]
#   PRELOAD_ADDR is a guest-physical address in QEMU's own numeric syntax (e.g. 0x4000000).
#
# Exit status: identical contract to run-uefi-hello.sh (0 pass, 1 output mismatch, 2 environment
# problem).
set -euo pipefail

EFI_FILE="${1:?usage: run-uefi-hello-with-preload.sh EFI_FILE PRELOAD_FILE PRELOAD_ADDR EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
PRELOAD_FILE="${2:?usage: run-uefi-hello-with-preload.sh EFI_FILE PRELOAD_FILE PRELOAD_ADDR EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
PRELOAD_ADDR="${3:?usage: run-uefi-hello-with-preload.sh EFI_FILE PRELOAD_FILE PRELOAD_ADDR EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
EXPECTED="${4:?usage: run-uefi-hello-with-preload.sh EFI_FILE PRELOAD_FILE PRELOAD_ADDR EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
TIMEOUT_SECONDS="${5:-20}"

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
if [ ! -f "$PRELOAD_FILE" ]; then
    echo "ERROR: preload data file not found: $PRELOAD_FILE" >&2
    exit 2
fi

BOOT_DIR="$(mktemp -d)"
trap 'rm -rf "$BOOT_DIR"' EXIT
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$EFI_FILE" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

# -vga none forces OVMF's console onto the serial port. The `loader` device places
# PRELOAD_FILE's raw bytes at PRELOAD_ADDR in guest physical memory before the guest's first
# instruction executes -- force-raw=on means "just the bytes," no ELF/other format interpretation.
# -m 512: explicit, generous RAM -- QEMU's own default (no -m flag) is 128 MiB, confirmed
# empirically insufficient for RFC-0042 Phase Q's production-scale ArcFS capacities; see
# run-uefi-hello.sh's own header note and .agents/reports/aps-qemu-ram-ceiling.md for the full story.
OUTPUT="$(timeout "$TIMEOUT_SECONDS" "$QEMU_BIN" \
    -bios "$OVMF_FD" \
    -m 512 \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -device loader,file="$PRELOAD_FILE",addr="$PRELOAD_ADDR",force-raw=on \
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
