#!/bin/sh
set -eu

PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export PATH

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
qemu_dir="${QEMU_DIR:-$base/build/qemu}"
rootfs="${ROOTFS:-$base/build/rootfs}"
system_disk="${1:-$qemu_dir/lazarus-system.qcow2}"
confirmation="${2:-}"
nbd_device="${NBD_DEVICE:-}"
mount_root="${MOUNT_ROOT:-$base/build/install-mount}"
work_dir="$base/build/installer-work"

run_root() {
	if [ "$(id -u)" -eq 0 ]; then
		"$@"
	elif [ -n "${SUDO_PASSWORD:-}" ]; then
		printf '%s\n' "$SUDO_PASSWORD" | sudo -S "$@"
	elif command -v sudo >/dev/null 2>&1; then
		sudo "$@"
	else
		echo "This step requires root. Re-run as root or install sudo." >&2
		exit 1
	fi
}

need_tool() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "$1 was not found." >&2
		exit 1
	fi
}

find_nbd() {
	for candidate in /dev/nbd0 /dev/nbd1 /dev/nbd2 /dev/nbd3 /dev/nbd4 /dev/nbd5 /dev/nbd6 /dev/nbd7; do
		if [ ! -e "$candidate" ]; then
			continue
		fi
		name="$(basename "$candidate")"
		if [ ! -e "/sys/block/$name/pid" ] || [ "$(cat "/sys/block/$name/pid" 2>/dev/null || true)" = "" ]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done
	return 1
}

cleanup() {
	set +e
	if mountpoint -q "$mount_root/boot/efi"; then
		run_root umount "$mount_root/boot/efi"
	fi
	if mountpoint -q "$mount_root/var/lib/arcology-lazarus"; then
		run_root umount "$mount_root/var/lib/arcology-lazarus"
	fi
	if mountpoint -q "$mount_root"; then
		run_root umount "$mount_root"
	fi
	if [ -n "${attached_nbd:-}" ]; then
		run_root qemu-nbd --disconnect "$attached_nbd" >/dev/null 2>&1
	fi
}
trap cleanup EXIT INT TERM

for tool in qemu-nbd qemu-img parted partprobe mkfs.vfat mkfs.ext4 rsync blkid grub-install; do
	need_tool "$tool"
done

if [ "$confirmation" != "ERASE" ]; then
	cat >&2 <<EOF
Refusing to install without explicit confirmation.

This will erase and reinstall the QEMU Lazarus OS system disk image:
  $system_disk

Usage:
  $0 $system_disk ERASE
EOF
	exit 2
fi

if [ ! -d "$rootfs" ]; then
	echo "Rootfs not found: $rootfs" >&2
	echo "Build it first: SUDO_PASSWORD='...' lazarus/lazarus-os/scripts/build-rootfs-nspawn.sh" >&2
	exit 1
fi

if [ ! -f "$rootfs/boot/vmlinuz-lts" ] || [ ! -f "$rootfs/boot/initramfs-lts" ]; then
	echo "Rootfs is missing /boot/vmlinuz-lts or /boot/initramfs-lts." >&2
	exit 1
fi

mkdir -p "$qemu_dir"
if [ ! -f "$system_disk" ]; then
	echo "Creating system disk: $system_disk"
	qemu-img create -f qcow2 "$system_disk" "${SYSTEM_DISK_SIZE:-24G}"
fi

echo "Installing Arcology Lazarus OS to QEMU system disk image:"
echo "  $system_disk"
echo "Rootfs:"
echo "  $rootfs"

run_root modprobe nbd max_part=16
if [ -z "$nbd_device" ]; then
	nbd_device="$(find_nbd || true)"
fi
if [ -z "$nbd_device" ]; then
	echo "No free /dev/nbd device was found." >&2
	exit 1
fi
attached_nbd="$nbd_device"

run_root qemu-nbd --disconnect "$nbd_device" >/dev/null 2>&1 || true
run_root qemu-nbd --connect="$nbd_device" "$system_disk"
sleep 1

run_root parted -s "$nbd_device" mklabel gpt
run_root parted -s "$nbd_device" mkpart ESP fat32 1MiB 513MiB
run_root parted -s "$nbd_device" set 1 esp on
run_root parted -s "$nbd_device" mkpart LAZARUS_OS ext4 513MiB 8705MiB
run_root parted -s "$nbd_device" mkpart LAZARUS_STATE ext4 8705MiB 100%
run_root partprobe "$nbd_device" || true
sleep 2

efi_part="${nbd_device}p1"
root_part="${nbd_device}p2"
state_part="${nbd_device}p3"
if [ ! -b "$efi_part" ] || [ ! -b "$root_part" ] || [ ! -b "$state_part" ]; then
	echo "Partition devices did not appear: $efi_part $root_part $state_part" >&2
	exit 1
fi

run_root mkfs.vfat -F 32 -n LAZARUS_EFI "$efi_part"
run_root mkfs.ext4 -F -L LAZARUS_OS "$root_part"
run_root mkfs.ext4 -F -L LAZARUS_STATE "$state_part"

run_root rm -rf "$mount_root"
run_root mkdir -p "$mount_root"
run_root mount "$root_part" "$mount_root"
run_root mkdir -p "$mount_root/boot/efi"
run_root mount "$efi_part" "$mount_root/boot/efi"
run_root mkdir -p "$mount_root/var/lib/arcology-lazarus"
run_root mount "$state_part" "$mount_root/var/lib/arcology-lazarus"

run_root rsync -aHAX --numeric-ids --exclude '/var/lib/arcology-lazarus/*' "$rootfs"/ "$mount_root"/
run_root mkdir -p "$mount_root/boot/efi" "$mount_root/var/lib/arcology-lazarus/images" "$mount_root/var/log/arcology-lazarus"

root_uuid="$(run_root blkid -s UUID -o value "$root_part")"
efi_uuid="$(run_root blkid -s UUID -o value "$efi_part")"
state_uuid="$(run_root blkid -s UUID -o value "$state_part")"

mkdir -p "$work_dir"
cat > "$work_dir/fstab" <<EOF
UUID=$root_uuid	/	ext4	rw,relatime	0 1
UUID=$efi_uuid	/boot/efi	vfat	umask=0077	0 2
UUID=$state_uuid	/var/lib/arcology-lazarus	ext4	rw,relatime	0 2
EOF
run_root install -Dm644 "$work_dir/fstab" "$mount_root/etc/fstab"

run_root grub-install \
	--target=x86_64-efi \
	--efi-directory="$mount_root/boot/efi" \
	--boot-directory="$mount_root/boot" \
	--removable \
	--no-nvram \
	--recheck

run_root mkdir -p "$mount_root/boot/grub"
cat > "$work_dir/grub.cfg" <<EOF
set timeout=3
set default=0

menuentry "Arcology Lazarus OS" {
    linux /boot/vmlinuz-lts root=UUID=$root_uuid rw modules=ext4,nvme,ahci,sd_mod,usb_storage,virtio_blk console=ttyS0,115200 console=tty0
    initrd /boot/initramfs-lts
}
EOF
run_root install -Dm644 "$work_dir/grub.cfg" "$mount_root/boot/grub/grub.cfg"

run_root sync
echo "Installed Arcology Lazarus OS system disk:"
echo "  $system_disk"
