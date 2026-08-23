#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/xhci-controller-init-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0045 Phase 2: real xHCI host controller discovery (raw PCI config space scan), reset (real
# USBSTS.CNR confirmed cleared), Command/Event ring + DCBAA setup, controller start (real
# USBSTS.HCH confirmed cleared), and real port enumeration/reset -- proven independently of any
# USB device/keyboard enumeration (RFC-0045 Phase 3's own scope), per RFC-0045 Section 8's own
# mandate to isolate controller-level bugs from device-level ones.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/xhci-controller-init/xhci-controller-init.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: xhci-controller-init does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/xhci.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"

run_once() {
    local outfile="$1" with_kbd="$2"
    local boot_dir
    boot_dir="$(mktemp -d)"
    mkdir -p "$boot_dir/EFI/BOOT"
    cp "$TMP_ROOT/xhci.efi" "$boot_dir/EFI/BOOT/BOOTX64.EFI"
    if [ "$with_kbd" = "yes" ]; then
        timeout 25 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 \
            -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
            -drive file="fat:rw:$boot_dir",format=raw,if=ide \
            -net none -vga none -display none -serial stdio -monitor none -no-reboot \
            > "$outfile" 2>&1 || true
    else
        timeout 25 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 \
            -device qemu-xhci,id=xhci \
            -drive file="fat:rw:$boot_dir",format=raw,if=ide \
            -net none -vga none -display none -serial stdio -monitor none -no-reboot \
            > "$outfile" 2>&1 || true
    fi
    rm -rf "$boot_dir"
}

# The real proof: a genuinely attached USB keyboard is detected on some real port (not a fixed
# assumed index -- discovery order is firmware/topology-determined), and a real port reset
# succeeds on it.
run_once "$TMP_ROOT/positive.txt" "yes"
grep -aqF "PCIOK" "$TMP_ROOT/positive.txt" || { echo "FAIL: PCI discovery did not succeed" >&2; cat "$TMP_ROOT/positive.txt" >&2; exit 1; }
grep -aqF "RSTOK" "$TMP_ROOT/positive.txt" || { echo "FAIL: controller reset did not succeed" >&2; cat "$TMP_ROOT/positive.txt" >&2; exit 1; }
grep -aqF "RNGOK" "$TMP_ROOT/positive.txt" || { echo "FAIL: ring/DCBAA setup did not succeed" >&2; cat "$TMP_ROOT/positive.txt" >&2; exit 1; }
grep -aqF "STROK" "$TMP_ROOT/positive.txt" || { echo "FAIL: controller start did not succeed" >&2; cat "$TMP_ROOT/positive.txt" >&2; exit 1; }
grep -aqF "PRST" "$TMP_ROOT/positive.txt" || { echo "FAIL: port reset did not succeed on the real attached USB keyboard" >&2; cat "$TMP_ROOT/positive.txt" >&2; exit 1; }
grep -aqF "DONE" "$TMP_ROOT/positive.txt" || { echo "FAIL: fixture did not report DONE" >&2; cat "$TMP_ROOT/positive.txt" >&2; exit 1; }

# Negative control: with NO USB device attached, port detection must honestly report none found,
# not a false positive on some unconnected port.
run_once "$TMP_ROOT/negative.txt" "no"
grep -aqF "NDEV" "$TMP_ROOT/negative.txt" || { echo "FAIL: negative control did not honestly report no device attached" >&2; cat "$TMP_ROOT/negative.txt" >&2; exit 1; }

# Determinism: 2 more repeats of the positive path.
for i in 1 2; do
    run_once "$TMP_ROOT/repeat$i.txt" "yes"
    grep -aqF "PRST" "$TMP_ROOT/repeat$i.txt" || { echo "FAIL: non-deterministic on repeat $i" >&2; cat "$TMP_ROOT/repeat$i.txt" >&2; exit 1; }
done

echo "PASS: real xHCI PCI discovery + MMIO capability/operational register access + real controller reset (USBSTS.CNR cleared) + real Command/Event ring and DCBAA setup + real controller start (USBSTS.HCH cleared) + real port enumeration correctly distinguishing a connected USB device from unconnected ports + real port reset, negative control and 3x determinism confirmed real"
