#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
qemu_dir="${QEMU_DIR:-$base/build/qemu}"
repo="$(CDPATH= cd -- "$base/../.." && pwd)"
reset=0

if [ "${1:-}" = "--reset" ]; then
	reset=1
elif [ "$#" -gt 0 ]; then
	echo "usage: $0 [--reset]" >&2
	exit 2
fi

mkdir -p "$qemu_dir"

run_root() {
	if [ "$(id -u)" -eq 0 ]; then
		"$@"
	elif [ -n "${SUDO_PASSWORD:-}" ]; then
		printf '%s\n' "$SUDO_PASSWORD" | sudo -S "$@"
	else
		sudo "$@"
	fi
}

if [ "$reset" -eq 1 ]; then
	for fixture in \
		"$qemu_dir/customer-source.qcow2" \
		"$qemu_dir/destination.qcow2" \
		"$qemu_dir/image-storage.raw" \
		"$qemu_dir/recovery-media.raw" \
		"$qemu_dir/lazarus-install-test.vhd"
	do
		if [ -e "$fixture" ]; then
			echo "Resetting test fixture: $fixture"
			rm -f -- "$fixture"
		fi
	done
fi

create_disk() {
	path="$1"
	size="$2"
	if [ -e "$path" ]; then
		echo "Keeping existing disk: $path"
		return
	fi
	echo "Creating $path ($size)"
	qemu-img create -f qcow2 "$path" "$size"
}

if ! command -v qemu-img >/dev/null 2>&1; then
	echo "qemu-img was not found." >&2
	exit 1
fi

create_disk "$qemu_dir/lazarus-system.qcow2" 24G
	if [ ! -e "$qemu_dir/customer-source.qcow2" ]; then
	raw_source="$qemu_dir/customer-source.raw"
	partition_image="$qemu_dir/customer-source.ntfs"
	fixture_text="$qemu_dir/customer-source-fixture.txt"
	echo "Creating MBR/NTFS customer source fixture ($raw_source)"
	truncate -s 2G "$raw_source"
	if command -v sfdisk >/dev/null 2>&1; then
		printf 'label: dos\nstart=2048,type=7,bootable\n' | sfdisk "$raw_source" >/dev/null
	elif command -v busybox >/dev/null 2>&1; then
		# BusyBox reports ioctl failure for a regular file after writing the table.
		busybox fdisk -u "$raw_source" >/dev/null 2>&1 <<'EOF' || true
o
n
p
1
2048

t
7
a
1
w
EOF
		busybox fdisk -l "$raw_source" 2>/dev/null | grep -q "2048"
	else
		echo "Neither sfdisk nor BusyBox fdisk is available to create the source partition table." >&2
		rm -f "$raw_source"
		exit 1
	fi
	truncate -s 2047M "$partition_image"
	printf '%s\n' \
		'Arcology Lazarus QEMU source fixture' \
		'This file must survive backup, verification, and restore.' \
		'Ticket: QEMU-TEST-0001' > "$fixture_text"
	if command -v mkntfs >/dev/null 2>&1 && command -v ntfscp >/dev/null 2>&1; then
		mkntfs -F -L LAZARUS_SOURCE "$partition_image" >/dev/null
		ntfscp -f "$partition_image" "$fixture_text" /LAZARUS-TEST.txt
	elif [ -d "$base/build/alpine-builder" ] && command -v systemd-nspawn >/dev/null 2>&1; then
		inside_partition="/work/${partition_image#"$repo"/}"
		inside_fixture="/work/${fixture_text#"$repo"/}"
		run_root systemd-nspawn -q -D "$base/build/alpine-builder" --bind "$repo":/work \
			/bin/sh -lc "apk add ntfs-3g-progs >/dev/null && mkntfs -F -L LAZARUS_SOURCE '$inside_partition' >/dev/null && ntfscp -f '$inside_partition' '$inside_fixture' /LAZARUS-TEST.txt"
	else
		echo "mkntfs and ntfscp are required to create the NTFS source fixture." >&2
		echo "Install ntfs-3g-progs or prepare the Alpine builder first." >&2
		rm -f "$raw_source" "$partition_image" "$fixture_text"
		exit 1
	fi
	dd if="$partition_image" of="$raw_source" bs=1M seek=1 conv=notrunc,sparse status=none
	qemu-img convert -f raw -O qcow2 "$raw_source" "$qemu_dir/customer-source.qcow2"
	rm -f "$raw_source" "$partition_image" "$fixture_text"
else
	echo "Keeping existing disk: $qemu_dir/customer-source.qcow2"
fi
create_disk "$qemu_dir/destination.qcow2" 4G
if [ ! -e "$qemu_dir/image-storage.raw" ]; then
	echo "Creating formatted persistent image storage: $qemu_dir/image-storage.raw"
	qemu-img create -f raw "$qemu_dir/image-storage.raw" 32G >/dev/null
	/sbin/mkfs.ext4 -F -L LAZARUS_IMAGES "$qemu_dir/image-storage.raw" >/dev/null
else
	echo "Keeping existing disk: $qemu_dir/image-storage.raw"
fi
if [ ! -e "$qemu_dir/recovery-media.raw" ]; then
	echo "Creating formatted removable recovery media: $qemu_dir/recovery-media.raw"
	qemu-img create -f raw "$qemu_dir/recovery-media.raw" 4G >/dev/null
	/sbin/mkfs.vfat -F 32 -n LAZ_RECOVER "$qemu_dir/recovery-media.raw" >/dev/null
else
	echo "Keeping existing disk: $qemu_dir/recovery-media.raw"
fi
if [ ! -e "$qemu_dir/lazarus-install-test.vhd" ]; then
	echo "Creating $qemu_dir/lazarus-install-test.vhd (24G VHD test target)"
	qemu-img create -f vpc "$qemu_dir/lazarus-install-test.vhd" 24G
else
	echo "Keeping existing disk: $qemu_dir/lazarus-install-test.vhd"
fi

cat <<EOF

QEMU disks are ready:
  $qemu_dir/lazarus-system.qcow2
  $qemu_dir/customer-source.qcow2
  $qemu_dir/destination.qcow2
  $qemu_dir/image-storage.raw
  $qemu_dir/recovery-media.raw
  $qemu_dir/lazarus-install-test.vhd
EOF
