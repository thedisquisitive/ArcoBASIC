#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/virtio-net-transport-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0048 Milestone N0 remaining work (Networking Development Steering Addendum, Section 11):
# real VirtIO-net legacy PCI transport -- MMIO/BAR mapping, device reset, feature negotiation, and
# interface identity (MAC, link state, capabilities), consumed through the generic Network
# Interface Contract rather than called directly. `disable-modern=on` forces QEMU's virtio-net-pci
# into pure legacy transport (a plain I/O-space BAR0, no modern virtio-1.0 PCI capabilities);
# `vectors=0` disables MSI-X, which keeps the legacy device-specific config offset fixed at +0x14
# rather than +0x18 -- this driver's own documented, empirically-confirmed assumption. A fixed
# `mac=` value keeps the expected output fully deterministic.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/virtio-net-transport/virtio-net-transport.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: virtio-net-transport does not compile:" >&2
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
                -netdev user,id=n0 \
                -device virtio-net-pci,netdev=n0,mac=52:54:00:12:34:56,disable-modern=on,vectors=0 \
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

# Real proof: a real QEMU legacy virtio-net-pci device is discovered, its I/O transport mapped,
# reset, feature-negotiated, and its real fixed MAC/link-state read back correctly.
run_once "$TMP_ROOT/withnic.txt" withnic
grep -aqF "START=1" "$TMP_ROOT/withnic.txt" || { echo "FAIL: NetworkInterface.Start did not succeed against a real virtio-net-pci device" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "MACFEAT=32" "$TMP_ROOT/withnic.txt" || { echo "FAIL: VIRTIO_NET_F_MAC was not negotiated" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "STATFEAT=65536" "$TMP_ROOT/withnic.txt" || { echo "FAIL: VIRTIO_NET_F_STATUS was not negotiated" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "MAC=52:54:00:12:34:56" "$TMP_ROOT/withnic.txt" || { echo "FAIL: did not read back the real fixed MAC address" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "LINK=1" "$TMP_ROOT/withnic.txt" || { echo "FAIL: did not report the real link as up" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }
grep -aqF "DONE" "$TMP_ROOT/withnic.txt" || { echo "FAIL: probe did not complete" >&2; cat "$TMP_ROOT/withnic.txt" >&2; exit 1; }

# Real negative control: no VirtIO-net device at all -- Start honestly reports failure, no crash.
run_once "$TMP_ROOT/nonic.txt" nonic
grep -aqF "START=0" "$TMP_ROOT/nonic.txt" || { echo "FAIL: negative control reported success when no device exists" >&2; cat "$TMP_ROOT/nonic.txt" >&2; exit 1; }
grep -aqF "DONE" "$TMP_ROOT/nonic.txt" || { echo "FAIL: negative control probe did not complete" >&2; cat "$TMP_ROOT/nonic.txt" >&2; exit 1; }

echo "PASS: RFC-0048 Milestone N0 real VirtIO-net legacy PCI transport -- discovery, I/O-BAR mapping, reset, feature negotiation, and MAC/link-state readback all correct through the generic Network Interface Contract against a real QEMU virtio-net-pci device; negative control confirmed real"
