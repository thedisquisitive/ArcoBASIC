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

# shellcheck source=./_qemu_stream_common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/_qemu_stream_common.sh"

# -m 512: explicit, generous RAM, matching every other harness script in this directory now (see
# run-uefi-hello.sh's own header note and .agents/reports/aps-qemu-ram-ceiling.md) -- QEMU's own
# default (no -m flag) is 128 MiB, confirmed empirically insufficient for RFC-0042 Phase Q's
# production-scale ArcFS capacities. Kept consistent here too even though this harness's own
# existing artifact doesn't need it yet, so every script in this directory shares one RAM budget.
qemu_run_and_check "$TIMEOUT_SECONDS" "$EXPECTED" "$QEMU_BIN" \
    -bios "$OVMF_FD" \
    -m 512 \
    -drive "file=$IMAGE_FILE,format=raw" \
    -net none \
    -vga none \
    -display none \
    -serial stdio \
    -monitor none \
    -no-reboot
exit $?
