#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
qemu_dir="${QEMU_DIR:-$base/build/qemu}"
system_disk="${SYSTEM_DISK:-$qemu_dir/lazarus-installed-smoke.qcow2}"
serial_log="$qemu_dir/installed-smoke-serial.log"
ovmf_vars="$qemu_dir/OVMF_VARS-installed-smoke.fd"

rm -f "$system_disk" "$serial_log" "$ovmf_vars"
SUDO_PASSWORD="${SUDO_PASSWORD:-}" \
	"$base/qemu/install-system-disk.sh" "$system_disk" ERASE

QEMU_DISPLAY=none \
OVMF_VARS="$ovmf_vars" \
QEMU_EXTRA="-serial file:$serial_log -monitor none" \
	"$base/qemu/run-installed.sh" "$system_disk" &
qemu_pid=$!

cleanup() {
	kill "$qemu_pid" 2>/dev/null || true
	wait "$qemu_pid" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

for _ in $(seq 1 180); do
	if grep -q 'lazarus-xinitrc: exec Lazarus client' "$serial_log" 2>/dev/null; then
		sleep 5
		if grep -q 'Lazarus GTK application exited' "$serial_log"; then
			echo "Installed kiosk launched and then exited." >&2
			tail -n 160 "$serial_log" >&2
			exit 1
		fi
		grep -q 'lazarus-hardware: coldplug-complete' "$serial_log"
		grep -q 'lazarus-kiosk: trying Xorg display path: auto' "$serial_log"
		echo "Fresh installed-appliance boot reached the Lazarus GTK kiosk."
		exit 0
	fi
	sleep 0.5
done

echo "Installed-appliance kiosk boot timed out." >&2
tail -n 200 "$serial_log" >&2
exit 1
