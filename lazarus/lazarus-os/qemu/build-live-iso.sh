#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
out="${1:-$base/build/arcology-lazarus-live.iso}"
out_tmp="${out}.tmp.$$"
rootfs="${ROOTFS:-$base/build/rootfs}"
iso_root="$base/build/live-iso-root"
initramfs_work="$base/build/live-initramfs"
live_initramfs="$base/build/initramfs-lazarus-live"

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

for tool in mksquashfs unsquashfs xorriso grub-mkrescue gzip cpio; do
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
    linux /boot/vmlinuz-lts console=tty0 console=ttyS0,115200
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
grub-mkrescue -o "$out_tmp" "$iso_root"

# Do not replace a bootable ISO until its embedded rootfs can be read back.
# QEMU otherwise can open the output while xorriso is still writing it.
verify_dir="$base/build/iso-verify-$$"
rm -rf "$verify_dir"
mkdir -p "$verify_dir"
xorriso -osirrox on -indev "$out_tmp" -extract /boot/rootfs.squashfs "$verify_dir/rootfs.squashfs" >/dev/null
xorriso -osirrox on -indev "$out_tmp" -extract /boot/arcology-lazarus-live.marker "$verify_dir/arcology-lazarus-live.marker" >/dev/null
unsquashfs -s "$verify_dir/rootfs.squashfs" >/dev/null
[ "$(cat "$verify_dir/arcology-lazarus-live.marker")" = "Arcology Lazarus OS live media" ]
rm -rf "$verify_dir"
mv -f "$out_tmp" "$out"
trap - EXIT HUP INT TERM
echo "Live ISO written: $out"
