#!/usr/bin/env bash
set -euo pipefail

IMAGE_FILE="${1:?usage: run-arcology-hardware-image.sh IMAGE_FILE [EXPECTED_OUTPUT] [TIMEOUT_SECONDS]}"
EXPECTED="${2:-ARCOLOGY HARDWARE TEST}"
TIMEOUT_SECONDS="${3:-20}"

QEMU_BIN=$(command -v qemu-system-x86_64 || true)
if [ -z "$QEMU_BIN" ]; then
    echo "ERROR: qemu-system-x86_64 was not found on PATH." >&2
    exit 2
fi
OVMF_FD=$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1 || true)
if [ -z "$OVMF_FD" ]; then
    echo "ERROR: no non-Secure-Boot OVMF firmware image was found." >&2
    exit 2
fi
if [ ! -f "$IMAGE_FILE" ]; then
    echo "ERROR: boot image not found: $IMAGE_FILE" >&2
    exit 2
fi

OUTPUT=$(timeout "$TIMEOUT_SECONDS" "$QEMU_BIN" \
    -bios "$OVMF_FD" \
    -drive "file=$IMAGE_FILE,format=raw" \
    -net none \
    -vga none \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot 2>/dev/null || true)

if printf '%s' "$OUTPUT" | grep -aqF "$EXPECTED"; then
    echo "PASS: $EXPECTED"
    exit 0
fi

echo "FAIL: expected output not found: $EXPECTED" >&2
echo "--- captured console output (last 4000 bytes) ---" >&2
printf '%s' "$OUTPUT" | tail -c 4000 >&2
exit 1
