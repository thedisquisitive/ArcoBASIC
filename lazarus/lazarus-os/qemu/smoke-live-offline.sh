#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
iso="${1:-$base/build/arcology-lazarus-live.iso}"
work="${QEMU_DIR:-$base/build/qemu}"
serial_log="$work/live-offline-smoke-serial.log"
ovmf_code="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
ovmf_vars_template="${OVMF_VARS_TEMPLATE:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
ovmf_vars="$work/OVMF_VARS-live-offline-smoke.fd"

for path in "$iso" "$ovmf_code" "$ovmf_vars_template"; do
	[ -f "$path" ] || { echo "Required file not found: $path" >&2; exit 1; }
done

mkdir -p "$work"
cp "$ovmf_vars_template" "$ovmf_vars"
rm -f "$serial_log"

accel=""
cpu="max"
if [ -e /dev/kvm ] && [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
	accel="-enable-kvm"
	cpu="host"
fi

qemu-system-x86_64 \
	$accel -machine q35 -cpu "$cpu" -m 2048 -smp 2 \
	-drive if=pflash,format=raw,readonly=on,file="$ovmf_code" \
	-drive if=pflash,format=raw,file="$ovmf_vars" \
	-device qemu-xhci \
	-drive file="$iso",format=raw,if=none,id=lazarus_live_usb,readonly=on \
	-device usb-storage,drive=lazarus_live_usb,bootindex=1,serial=LAZARUS_LIVE_USB \
	-nic none -display none -serial file:"$serial_log" -monitor none &
qemu_pid=$!

cleanup() {
	kill "$qemu_pid" 2>/dev/null || true
	wait "$qemu_pid" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

for _ in $(seq 1 120); do
	if grep -q 'lazarus-xinitrc: exec Lazarus client' "$serial_log" 2>/dev/null; then
		sleep 7
		if grep -Eq 'general protection fault.*lazarus-gui|Lazarus GTK application exited' "$serial_log"; then
			echo "Offline live-boot smoke test failed: Lazarus GUI exited after launch." >&2
			tail -n 100 "$serial_log" >&2
			exit 1
		fi
		grep -q "continuing offline" "$serial_log"
		echo "Offline live-boot smoke test passed; networking did not delay the kiosk."
		exit 0
	fi
	sleep 0.5
done

echo "Offline live-boot smoke test timed out." >&2
tail -n 100 "$serial_log" >&2
exit 1
