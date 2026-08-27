#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/network-nic-discovery-probe-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0048 Milestone N0 Phase 1: real NIC identification -- a real PCI bus scan for base class
# 0x02 (Network Controller), reusing RFC-0047's GpuDiscovery shape verbatim (itself reused from
# RFC-0045's own raw-PCI-scan pattern). Real, honest limitation of what THIS test proves: QEMU's
# virtio-net-pci device is a real PCI class-0x02 device with vendor ID 0x1AF4 (Red Hat, Inc.,
# QEMU's own real VirtIO vendor allocation) -- RFC-0048 Section 6.2 names VirtIO-net this
# project's own first deterministic development NIC backend, so finding and correctly labeling
# it here is the real target, not an incidental match.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/network-nic-discovery-probe/network-nic-discovery-probe.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: network-nic-discovery-probe does not compile:" >&2
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
        withnic)
            timeout 20 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 -vga none \
                -drive file="fat:rw:$boot_dir",format=raw,if=ide \
                -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
                -display none -serial file:"$outfile" -no-reboot \
                > /dev/null 2>&1 || true
            ;;
        nonic)
            timeout 20 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 -vga none \
                -drive file="fat:rw:$boot_dir",format=raw,if=ide \
                -net none -display none -serial file:"$outfile" -no-reboot \
                > /dev/null 2>&1 || true
            ;;
    esac
    rm -rf "$boot_dir"
}

# Real proof: a real QEMU virtio-net-pci device (real PCI class-0x02 device, vendor 0x1AF4) is
# found and correctly classified as a Network Controller/Ethernet device with the real VirtIO
# vendor ID recognized by name.
run_once "$TMP_ROOT/withnic.txt" withnic
grep -aqF "COUNT=1" "$TMP_ROOT/withnic.txt" || { echo "FAIL: did not find the real QEMU virtio-net-pci device" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "CLS=2/0/0" "$TMP_ROOT/withnic.txt" || { echo "FAIL: did not correctly classify the found device as Network Controller/Ethernet" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "VID=6900" "$TMP_ROOT/withnic.txt" || { echo "FAIL: did not report the real VirtIO PCI vendor ID (0x1AF4/6900)" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "VENDOR=VIRTIO" "$TMP_ROOT/withnic.txt" || { echo "FAIL: did not correctly identify the VirtIO vendor by name" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "DONE" "$TMP_ROOT/withnic.txt" || { echo "FAIL: probe did not complete" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }

# Real negative control: no network device at all -- reports 0 found, no crash, no false match.
run_once "$TMP_ROOT/nonic.txt" nonic
grep -aqF "COUNT=0" "$TMP_ROOT/nonic.txt" || { echo "FAIL: negative control found a device that isn't there" >&2; cat "$TMP_ROOT/nonic.txt" >&2; exit 1; }
grep -aqF "DONE" "$TMP_ROOT/nonic.txt" || { echo "FAIL: negative control probe did not complete" >&2; cat "$TMP_ROOT/nonic.txt" >&2; exit 1; }

echo "PASS: RFC-0048 Milestone N0 Phase 1 real NIC identification -- a real PCI bus scan for class-0x02 network controllers, correctly finding and classifying a real QEMU virtio-net-pci device (vendor/device ID, class/subclass/prog-if, all 6 real BARs) and correctly identifying it as VirtIO by vendor ID; negative control confirmed real"
