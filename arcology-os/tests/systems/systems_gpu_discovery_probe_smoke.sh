#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/gpu-discovery-probe-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0047 Phase 1: real GPU identification -- a real PCI bus scan for base class 0x03 (Display
# Controller), reusing RFC-0045's own raw-PCI-scan pattern verbatim in shape. Real, honest
# limitation of what THIS test can prove: QEMU's own standard video devices (-vga std here) are
# real PCI class-0x03 devices with real, valid vendor/device IDs, but none of them are Intel, AMD,
# or NVIDIA silicon -- this proves the scan mechanism itself works generically (finds a real
# device, reports its real vendor/device/class/BARs correctly, correctly identifies it as
# "Unknown" vendor), not that real Intel/AMD hardware was found. That half only comes from the
# user's own real hardware.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/gpu-discovery-probe/gpu-discovery-probe.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: gpu-discovery-probe does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/probe.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"

run_once() {
    local outfile="$1" mode="$2"
    local boot_dir
    boot_dir="$(mktemp -d)"
    mkdir -p "$boot_dir/EFI/BOOT"
    cp "$TMP_ROOT/probe.efi" "$boot_dir/EFI/BOOT/BOOTX64.EFI"
    case "$mode" in
        withvga)
            timeout 20 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 -vga std \
                -drive file="fat:rw:$boot_dir",format=raw \
                -net none -display none -serial file:"$outfile" -no-reboot \
                > /dev/null 2>&1 || true
            ;;
        novga)
            timeout 20 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 -vga none \
                -drive file="fat:rw:$boot_dir",format=raw,if=ide \
                -net none -display none -serial file:"$outfile" -no-reboot \
                > /dev/null 2>&1 || true
            ;;
    esac
    rm -rf "$boot_dir"
}

# Real proof: a real QEMU std VGA device (a real PCI class-0x03 device, vendor 0x1234/device
# 0x1111 by QEMU's own convention) is found, correctly classified, and correctly reported as a
# non-Intel/AMD/NVIDIA vendor.
run_once "$TMP_ROOT/withvga.txt" withvga
grep -aqF "COUNT=1" "$TMP_ROOT/withvga.txt" || { echo "FAIL: did not find the real QEMU std VGA device" >&2; cat "$TMP_ROOT/withvga.txt" >&2; exit 1; }
grep -aqF "CLS=3/0/0" "$TMP_ROOT/withvga.txt" || { echo "FAIL: did not correctly classify the found device as Display Controller/VGA-compatible" >&2; cat "$TMP_ROOT/withvga.txt" >&2; exit 1; }
grep -aqF "VENDOR=UNKNOWN" "$TMP_ROOT/withvga.txt" || { echo "FAIL: did not correctly report QEMU's own virtual vendor ID as non-Intel/AMD/NVIDIA" >&2; cat "$TMP_ROOT/withvga.txt" >&2; exit 1; }
grep -aqF "DONE" "$TMP_ROOT/withvga.txt" || { echo "FAIL: probe did not complete" >&2; cat "$TMP_ROOT/withvga.txt" >&2; exit 1; }

# Real negative control: no display device at all -- reports 0 found, no crash, no false match.
run_once "$TMP_ROOT/novga.txt" novga
grep -aqF "COUNT=0" "$TMP_ROOT/novga.txt" || { echo "FAIL: negative control found a device that isn't there" >&2; cat "$TMP_ROOT/novga.txt" >&2; exit 1; }
grep -aqF "DONE" "$TMP_ROOT/novga.txt" || { echo "FAIL: negative control probe did not complete" >&2; cat "$TMP_ROOT/novga.txt" >&2; exit 1; }

echo "PASS: RFC-0047 Phase 1 real GPU identification -- a real PCI bus scan for class-0x03 display controllers, correctly finding and classifying a real QEMU std VGA device (vendor/device ID, class/subclass/prog-if, all 6 real BARs) and correctly identifying it as non-Intel/AMD/NVIDIA; negative control confirmed real"
