#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
qemu_dir="${QEMU_DIR:-$base/build/qemu}"
system_disk="${1:-$qemu_dir/lazarus-system.qcow2}"
display="${QEMU_DISPLAY:-gtk}"
display_args="-display $display,zoom-to-fit=on"
[ "$display" = "none" ] && display_args="-display none"
firmware_args=""

if [ ! -f "$system_disk" ]; then
	echo "System disk not found: $system_disk" >&2
	echo "Run lazarus/lazarus-os/qemu/create-test-disks.sh first, then install Lazarus OS to the system disk." >&2
	exit 1
fi

system_format="qcow2"
if command -v qemu-img >/dev/null 2>&1; then
	system_format="$(qemu-img info "$system_disk" 2>/dev/null | awk -F': ' '$1 == "file format" {print $2; exit}')"
fi
case "$system_format" in
	qcow2|vpc|raw) ;;
	*)
		echo "Unsupported system disk image format: ${system_format:-unknown}" >&2
		exit 1
		;;
esac

"$base/qemu/create-test-disks.sh"

if [ "${QEMU_FIRMWARE:-uefi}" = "uefi" ]; then
	ovmf_code="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
	ovmf_vars_template="${OVMF_VARS_TEMPLATE:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
	ovmf_vars="${OVMF_VARS:-$qemu_dir/OVMF_VARS-installed.fd}"
	if [ ! -f "$ovmf_code" ]; then
		echo "OVMF code firmware not found: $ovmf_code" >&2
		exit 1
	fi
	if [ ! -f "$ovmf_vars" ]; then
		if [ ! -f "$ovmf_vars_template" ]; then
			echo "OVMF vars template not found: $ovmf_vars_template" >&2
			exit 1
		fi
		cp "$ovmf_vars_template" "$ovmf_vars"
	fi
	firmware_args="-drive if=pflash,format=raw,readonly=on,file=$ovmf_code -drive if=pflash,format=raw,file=$ovmf_vars"
fi

accel=""
cpu="max"
if [ -e /dev/kvm ] && [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
	accel="-enable-kvm"
	cpu="host"
fi

exec qemu-system-x86_64 \
	$accel \
	$firmware_args \
	-m "${QEMU_MEMORY:-4096}" \
	-smp "${QEMU_CPUS:-2}" \
	-machine q35 \
	-cpu "$cpu" \
	-drive file="$system_disk",if=none,id=lazarus_system,format="$system_format" \
	-device virtio-blk-pci,drive=lazarus_system,serial=LAZARUS_OS_TEST \
	-drive file="$qemu_dir/customer-source.qcow2",if=none,id=customer_source,format=qcow2 \
	-device virtio-blk-pci,drive=customer_source,serial=LAZARUS_SOURCE_TEST \
	-drive file="$qemu_dir/destination.qcow2",if=none,id=destination,format=qcow2 \
	-device virtio-blk-pci,drive=destination,serial=LAZARUS_DEST_TEST \
	-drive file="$qemu_dir/image-storage.raw",if=none,id=image_storage,format=raw \
	-device virtio-blk-pci,drive=image_storage,serial=LAZARUS_STORAGE_TEST \
	-device qemu-xhci \
	-drive file="$qemu_dir/recovery-media.raw",if=none,id=recovery_media,format=raw \
	-device usb-storage,drive=recovery_media,serial=LAZ_RECOVERY_TEST \
	-device usb-mouse \
	-device usb-tablet \
	-device virtio-mouse-pci \
	-device virtio-tablet-pci \
	-device virtio-keyboard-pci \
	-vga none \
	-device virtio-vga \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0 \
	$display_args \
	${QEMU_EXTRA:-}
