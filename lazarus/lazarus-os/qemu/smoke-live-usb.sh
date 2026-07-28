#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
iso="${1:-$base/build/arcology-lazarus-live.iso}"
work="${QEMU_DIR:-$base/build/qemu}"
serial_log="$work/live-usb-smoke-serial.log"
ovmf_code="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
ovmf_vars_template="${OVMF_VARS_TEMPLATE:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
ovmf_vars="$work/OVMF_VARS-live-usb-smoke.fd"

for path in "$iso" "$ovmf_code" "$ovmf_vars_template"; do
	if [ ! -f "$path" ]; then
		echo "Required file not found: $path" >&2
		exit 1
	fi
done

mkdir -p "$work"
"$base/qemu/create-test-disks.sh" >/dev/null
cp "$ovmf_vars_template" "$ovmf_vars"
rm -f "$serial_log"

accel=""
cpu="max"
if [ -e /dev/kvm ] && [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
	accel="-enable-kvm"
	cpu="host"
fi

qemu-system-x86_64 \
	$accel \
	-machine q35 \
	-cpu "$cpu" \
	-m 2048 \
	-smp 2 \
	-drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
	-drive if=pflash,format=raw,file="$ovmf_vars" \
	-device qemu-xhci \
	-drive file="$iso",format=raw,if=none,id=lazarus_live_usb,readonly=on \
	-device usb-storage,drive=lazarus_live_usb,bootindex=1,serial=LAZARUS_LIVE_USB \
	-drive file="$work/destination.qcow2",if=none,id=physical_nvme,format=qcow2 \
	-device nvme,drive=physical_nvme,serial=LAZARUS_DEST_TEST \
	-drive file="$work/lazarus-install-test.vhd",if=none,id=physical_sata,format=vpc \
	-device ich9-ahci,id=lazarus_ahci \
	-device ide-hd,bus=lazarus_ahci.0,drive=physical_sata,model=LAZARUS_INSTALL_TEST,serial=LAZARUS_INSTALL_TEST \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0 \
	-display none \
	-serial file:"$serial_log" \
	-monitor none &
qemu_pid=$!

cleanup() {
	kill "$qemu_pid" 2>/dev/null || true
	wait "$qemu_pid" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

for _ in $(seq 1 120); do
	if grep -q 'lazarus-xinitrc: exec Lazarus client' "$serial_log" 2>/dev/null &&
	   grep -q 'lazarus-network: Arcology Lazarus DHCP lease:' "$serial_log" 2>/dev/null &&
	   grep -q 'Starting Arcology Lazarus printer discovery .*\[ ok \]' "$serial_log" 2>/dev/null &&
	   grep -q 'lazarus-hardware: coldplug-complete' "$serial_log" 2>/dev/null &&
	   grep -q 'LAZARUS_DEST_TEST' "$serial_log" 2>/dev/null &&
	   grep -q 'LAZARUS_INSTALL_TEST' "$serial_log" 2>/dev/null; then
		core_inventory="$(sed -n '/lazarus-core-inventory: begin/,/lazarus-core-inventory: end/p' "$serial_log")"
		printf '%s\n' "$core_inventory" | grep -q 'QEMU NVMe Ctrl'
		printf '%s\n' "$core_inventory" | grep -q 'LAZARUS_INSTALL_TEST'
		grep -q 'lazarus-live: Boot media mounted from /dev/sd' "$serial_log"
		grep -q 'lazarus-live: Switching to Arcology Lazarus OS.' "$serial_log"
		# Keep the guest alive through the first periodic device and network
		# refreshes. A launch marker alone does not prove the GTK process stayed up.
		sleep 7
		if grep -Eq 'general protection fault.*lazarus-gui|Lazarus GTK application exited' "$serial_log"; then
			echo "USB live-boot smoke test failed: Lazarus GUI exited after launch." >&2
			tail -n 100 "$serial_log" >&2
			exit 1
		fi
		echo "USB live-boot, NVMe/AHCI discovery, DHCP, and printer-discovery smoke test passed."
		exit 0
	fi
	if grep -q 'Could not find Arcology Lazarus boot media' "$serial_log" 2>/dev/null; then
		echo "USB live-boot smoke test failed: boot media was not found." >&2
		tail -n 80 "$serial_log" >&2
		exit 1
	fi
	sleep 0.5
done

echo "USB live-boot smoke test timed out." >&2
tail -n 80 "$serial_log" >&2
exit 1
