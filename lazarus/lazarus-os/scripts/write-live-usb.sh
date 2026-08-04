#!/bin/sh
set -eu

iso=${1:?usage: write-live-usb.sh ISO_PATH DEVICE ERASE}
device=${2:?usage: write-live-usb.sh ISO_PATH DEVICE ERASE}
confirmation=${3:-}

if [ "$confirmation" != "ERASE" ]; then
	echo "Type ERASE as the third argument to confirm writing over $device." >&2
	exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
	echo "write-live-usb.sh must run as root." >&2
	exit 1
fi
if [ ! -f "$iso" ]; then
	echo "ISO not found: $iso" >&2
	exit 1
fi
if [ ! -b "$device" ]; then
	echo "Not a block device: $device" >&2
	exit 1
fi
case "$device" in
	*[0-9]|*p[0-9]*)
		echo "$device looks like a partition, not a whole disk. Point this at the whole device (e.g. /dev/sda)." >&2
		exit 1
		;;
esac
if grep -q "^$device[ 	]" /proc/mounts; then
	echo "$device is currently mounted. Unmount it before writing." >&2
	exit 1
fi
for part in "$device"[0-9]*; do
	[ -e "$part" ] || continue
	if grep -q "^$part[ 	]" /proc/mounts; then
		echo "$part is currently mounted. Unmount it before writing." >&2
		exit 1
	fi
done

echo "Writing $iso to $device. Do not remove the drive until this completes."
dd if="$iso" of="$device" bs=4M status=progress conv=fsync
sync
blockdev --rereadpt "$device" 2>/dev/null || true
iso_bytes=$(wc -c <"$iso")
iso_hash=$(sha256sum "$iso" | awk '{print $1}')
device_hash=$(head -c "$iso_bytes" "$device" | sha256sum | awk '{print $1}')
if [ "$device_hash" != "$iso_hash" ]; then
	echo "Verification failed: the bytes read back from $device do not match the ISO." >&2
	exit 1
fi
case "$device" in
	/dev/nvme*|/dev/mmcblk*|/dev/loop*|/dev/nbd*) state_partition="${device}p4" ;;
	*) state_partition="${device}4" ;;
esac
for _ in $(seq 1 30); do
	[ -b "$state_partition" ] && break
	sleep 0.1
done
if [ ! -b "$state_partition" ] ||
   [ "$(blkid -p -s LABEL -o value "$state_partition" 2>/dev/null || true)" != "LAZARUS_STATE" ]; then
	echo "Verification failed: the writable Lazarus state partition was not found at $state_partition." >&2
	exit 1
fi
echo "Write complete and verified ($iso_hash), including writable appliance state. $device is safe to remove; it should now boot $(basename "$iso")."
