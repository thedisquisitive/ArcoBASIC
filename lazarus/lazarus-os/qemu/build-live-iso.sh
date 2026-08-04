#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${1:-$base/build/arcology-lazarus-live.iso}"
out_tmp="${out}.tmp.$$"
rootfs="${ROOTFS:-$base/build/rootfs}"
iso_root="$base/build/live-iso-root"
initramfs_work="$base/build/live-initramfs"
live_initramfs="$base/build/initramfs-lazarus-live"
state_image="$base/build/lazarus-live-state.img"

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

for tool in mksquashfs unsquashfs xorriso grub-mkrescue gzip cpio truncate; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "$tool was not found." >&2
		exit 1
	fi
done

if [ ! -d "$rootfs" ]; then
	echo "Rootfs not found: $rootfs" >&2
	echo "Build it first on an Alpine/apk-capable host:" >&2
	echo "  lazarus/lazarus-os/scripts/build-rootfs.sh $rootfs" >&2
	exit 1
fi

state_tools=host
if ! command -v mkfs.ext4 >/dev/null 2>&1 || ! command -v blkid >/dev/null 2>&1; then
	state_tools=container
	command -v systemd-nspawn >/dev/null 2>&1 || {
		echo "mkfs.ext4/blkid are unavailable on the host and systemd-nspawn was not found." >&2
		exit 1
	}
	[ -x "$rootfs/sbin/mkfs.ext4" ] && [ -x "$rootfs/sbin/blkid" ] || {
		echo "The staged rootfs does not contain mkfs.ext4 and blkid." >&2
		exit 1
	}
fi

format_state_image() {
	image=$1
	if [ "$state_tools" = host ]; then
		mkfs.ext4 -F -L LAZARUS_STATE "$image" >/dev/null
	else
		run_root systemd-nspawn -q --pipe -D "$rootfs" --bind="$image":/tmp/lazarus-state.img \
			/sbin/mkfs.ext4 -F -L LAZARUS_STATE /tmp/lazarus-state.img >/dev/null
	fi
}

state_image_label() {
	image=$1
	if [ "$state_tools" = host ]; then
		blkid -p -s LABEL -o value "$image"
	else
		run_root systemd-nspawn -q --pipe -D "$rootfs" --bind-ro="$image":/tmp/lazarus-state-header.img \
			/sbin/blkid -p -s LABEL -o value /tmp/lazarus-state-header.img
	fi
}

run_root rm -rf "$iso_root"
mkdir -p "$iso_root/boot/grub"

run_root mksquashfs "$rootfs" "$iso_root/boot/rootfs.squashfs" -noappend
run_root chmod 0644 "$iso_root/boot/rootfs.squashfs"
printf '%s\n' 'Arcology Lazarus OS live media' > "$iso_root/boot/arcology-lazarus-live.marker"

run_root rm -rf "$initramfs_work" "$live_initramfs"
mkdir -p "$initramfs_work"
run_root sh -c "cd '$initramfs_work' && gzip -dc '$rootfs/boot/initramfs-lts' | cpio -id --quiet"
kernel_release="$(basename "$(find "$rootfs/lib/modules" -mindepth 1 -maxdepth 1 -type d | head -n 1)")"
for module in \
	"kernel/fs/squashfs/squashfs.ko.gz" \
	"kernel/fs/overlayfs/overlay.ko.gz"
do
	if [ ! -f "$rootfs/lib/modules/$kernel_release/$module" ]; then
		echo "Missing live module: $module" >&2
		exit 1
	fi
	run_root install -Dm644 \
		"$rootfs/lib/modules/$kernel_release/$module" \
		"$initramfs_work/usr/lib/modules/$kernel_release/$module"
done
run_root install -Dm755 "$base/boot/live-init" "$initramfs_work/init"
run_root sh -c "cd '$initramfs_work' && find . -print0 | cpio --null -o -H newc --quiet | gzip -9 > '$live_initramfs'"
run_root chmod 0644 "$live_initramfs"

cat > "$iso_root/boot/grub/grub.cfg" <<'EOF'
set timeout=0
set default=0

menuentry "Arcology Lazarus OS live" {
    linux /boot/vmlinuz-lts console=ttyS0,115200 console=tty0
    initrd /boot/initramfs-lazarus-live
}
EOF

if [ -f "$rootfs/boot/vmlinuz-lts" ]; then
	run_root cp "$rootfs/boot/vmlinuz-lts" "$iso_root/boot/vmlinuz-lts"
	run_root chmod 0644 "$iso_root/boot/vmlinuz-lts"
else
	echo "Missing kernel in rootfs: /boot/vmlinuz-lts" >&2
	exit 1
fi

if [ -f "$live_initramfs" ]; then
	run_root cp "$live_initramfs" "$iso_root/boot/initramfs-lazarus-live"
	run_root chmod 0644 "$iso_root/boot/initramfs-lazarus-live"
else
	echo "Missing live initramfs: $live_initramfs" >&2
	exit 1
fi

trap 'rm -f "$out_tmp"' EXIT HUP INT TERM
rm -f "$out_tmp"
rm -f "$state_image"
truncate -s "${LIVE_STATE_SIZE:-64M}" "$state_image"
format_state_image "$state_image"
# Partition 4 follows the ISO/HFS boot payload in grub-mkrescue's hybrid GPT. On a USB it is a
# normal writable ext4 filesystem; on optical media the live system simply remains stateless.
grub-mkrescue -o "$out_tmp" "$iso_root" -- -append_partition 4 0x83 "$state_image"

# Do not replace a bootable ISO until its embedded rootfs can be read back.
# QEMU otherwise can open the output while xorriso is still writing it.
verify_dir="$base/build/iso-verify-$$"
rm -rf "$verify_dir"
mkdir -p "$verify_dir"
xorriso -osirrox on -indev "$out_tmp" -extract /boot/rootfs.squashfs "$verify_dir/rootfs.squashfs" >/dev/null
xorriso -osirrox on -indev "$out_tmp" -extract /boot/arcology-lazarus-live.marker "$verify_dir/arcology-lazarus-live.marker" >/dev/null
unsquashfs -s "$verify_dir/rootfs.squashfs" >/dev/null
[ "$(cat "$verify_dir/arcology-lazarus-live.marker")" = "Arcology Lazarus OS live media" ]
state_start="$(xorriso -indev "$out_tmp" -report_system_area plain 2>/dev/null | awk '
    /GPT partname local :   4  Appended4/ { found=1; next }
    found && /GPT start and size :   4/ { print $7; exit }
')"
[ -n "$state_start" ] || { echo "Live state partition is missing from the hybrid image." >&2; exit 1; }
dd if="$out_tmp" of="$verify_dir/state-header.img" bs=512 skip="$state_start" count=16 status=none
[ "$(state_image_label "$verify_dir/state-header.img")" = "LAZARUS_STATE" ] || {
	echo "Live state partition label could not be verified." >&2
	exit 1
}
rm -rf "$verify_dir"
mv -f "$out_tmp" "$out"
trap - EXIT HUP INT TERM
echo "Live ISO written: $out"
