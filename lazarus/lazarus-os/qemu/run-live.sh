#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
qemu_dir="${QEMU_DIR:-$base/build/qemu}"
iso="${1:?usage: run-live.sh PATH_TO_ISO}"
display="${QEMU_DISPLAY:-gtk}"
display_args="-display $display,zoom-to-fit=on"
[ "$display" = "none" ] && display_args="-display none"
# Keep the appliance test mode conservative.  The Alpine simple framebuffer
# cannot always accept an arbitrary virtio-vga mode during early boot.
width="${QEMU_WIDTH:-1024}"
height="${QEMU_HEIGHT:-768}"
firmware_args=""
serial_args=""
boot_media_args=""

if [ "${QEMU_SERIAL_LOG:-auto}" != "off" ]; then
	serial_log="${QEMU_SERIAL_LOG:-$qemu_dir/live-current-serial.log}"
	mkdir -p "$(dirname "$serial_log")"
	rm -f "$serial_log"
	serial_args="-serial file:$serial_log"
	echo "Serial log: $serial_log"
fi

if [ ! -f "$iso" ]; then
	echo "ISO not found: $iso" >&2
	exit 1
fi

case "${QEMU_BOOT_MEDIA:-usb}" in
	usb)
		boot_media_args="-boot menu=on -drive file=$iso,if=none,id=lazarus_live_usb,format=raw,readonly=on -device usb-storage,drive=lazarus_live_usb,serial=LAZARUS_LIVE_USB,bootindex=1"
		;;
	cdrom)
		boot_media_args="-boot d -cdrom $iso"
		;;
	*)
		echo "Invalid QEMU_BOOT_MEDIA: ${QEMU_BOOT_MEDIA}" >&2
		echo "Expected usb or cdrom." >&2
		exit 1
		;;
esac

"$base/qemu/create-test-disks.sh"

if [ "${QEMU_FIRMWARE:-uefi}" = "uefi" ]; then
	ovmf_code="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
	ovmf_vars_template="${OVMF_VARS_TEMPLATE:-/usr/share/OVMF/OVMF_VARS_4M.fd}"
	ovmf_vars="${OVMF_VARS:-$qemu_dir/OVMF_VARS-live.fd}"
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
	-device qemu-xhci \
	$boot_media_args \
	-drive file="$qemu_dir/customer-source.qcow2",if=none,id=customer_source,format=qcow2 \
	-device virtio-blk-pci,drive=customer_source,serial=LAZARUS_SOURCE_TEST \
	-drive file="$qemu_dir/destination.qcow2",if=none,id=destination,format=qcow2 \
	-device nvme,drive=destination,serial=LAZARUS_DEST_TEST \
	-drive file="$qemu_dir/image-storage.raw",if=none,id=image_storage,format=raw \
	-device virtio-blk-pci,drive=image_storage,serial=LAZARUS_STORAGE_TEST \
	-drive file="$qemu_dir/lazarus-install-test.vhd",if=none,id=install_test,format=vpc \
	-device ich9-ahci,id=lazarus_ahci \
	-device ide-hd,bus=lazarus_ahci.0,drive=install_test,model=LAZARUS_INSTALL_TEST,serial=LAZARUS_INSTALL_TEST \
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
	$serial_args \
	${QEMU_EXTRA:-}
