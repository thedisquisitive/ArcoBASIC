#!/bin/sh
set -eu

service_root=${1:?staged Lazarus rootfs required}
windows_image=${2:?Windows-layout qcow2 fixture required}

if [ "$(id -u)" -ne 0 ]; then
    echo "service_drive_analysis_nbd.sh must run as root" >&2
    exit 77
fi

service="$service_root/usr/sbin/lazarus-service"
work=$(mktemp -d "${LAZARUS_TEST_TMPDIR:-/var/tmp}/lazarus-drive-analysis.XXXXXX")
linux_image="$work/linux.raw"
unknown_image="$work/unknown.raw"
bitlocker_image="$work/bitlocker.raw"
mount_root="$work/mount"
profile="$work/bench.profile"
device=
device_link="/dev/disk/by-path/lazarus-drive-analysis-$$"

cleanup() {
    status=$?
    umount "$mount_root" 2>/dev/null || true
    rm -f "$device_link"
    [ -z "$device" ] || qemu-nbd --disconnect "$device" 2>/dev/null || true
    rm -rf "$work"
    return "$status"
}
trap cleanup EXIT INT TERM

for tool in qemu-img qemu-nbd systemd-nspawn mount umount udevadm; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool is required for the disposable drive-analysis test." >&2
        exit 1
    }
done
[ -x "$service" ] || { echo "Staged Lazarus service not found: $service" >&2; exit 1; }
[ -x "$service_root/sbin/mkfs.ext4" ] || { echo "Staged mkfs.ext4 not found." >&2; exit 1; }

modprobe nbd max_part=16
for candidate in /dev/nbd10 /dev/nbd11 /dev/nbd12 /dev/nbd13 /dev/nbd14 /dev/nbd15; do
    name=${candidate##*/}
    [ -b "$candidate" ] || continue
    [ ! -s "/sys/block/$name/pid" ] || continue
    device=$candidate
    break
done
[ -n "$device" ] || { echo "A free NBD device is required." >&2; exit 1; }

mkdir -p /dev/disk/by-path "$mount_root"
ln -s "../../${device#/dev/}" "$device_link"
printf 'name=Drive Analysis Integration\nsource=%s\nimage_storage=%s/images\n' "$device_link" "$work" >"$profile"
mkdir -p "$work/images"

disconnect() {
    qemu-nbd --disconnect "$device"
    udevadm settle --timeout=10 || true
}

analyze() {
    request=$(printf '{"command":"drive_analysis","selector":"%s"}' "$device_link")
    printf '%s\n' "$request" | systemd-nspawn -q --pipe -D "$service_root" \
        --capability=CAP_SYS_ADMIN \
        --bind="$device" \
        --bind-ro=/dev/disk \
        --bind="$work":/analysis-test \
        /usr/sbin/lazarus-service --stdio --config /analysis-test/bench.profile | tail -n 1
}

qemu-img create -f raw "$linux_image" 256M >/dev/null
qemu-nbd --connect "$device" --format raw "$linux_image"
udevadm settle --timeout=10 || true
systemd-nspawn -q -D "$service_root" --bind="$device" /sbin/mkfs.ext4 -F -L LINUX_TEST "$device" >/dev/null
mount "$device" "$mount_root"
mkdir -p "$mount_root/etc"
printf 'NAME="Arcology Test Linux"\nPRETTY_NAME="Arcology Test Linux 24.04"\nVERSION_ID="24.04"\n' >"$mount_root/etc/os-release"
sync
umount "$mount_root"
linux_before=$(sha256sum "$linux_image" | awk '{print $1}')
linux_response=$(analyze)
linux_after=$(sha256sum "$linux_image" | awk '{print $1}')
[ "$linux_before" = "$linux_after" ]
printf '%s\n' "$linux_response" | grep -q '"ok":true'
printf '%s\n' "$linux_response" | grep -q '"detected_os_family":"linux"'
printf '%s\n' "$linux_response" | grep -q '"detected_os_name":"Arcology Test Linux 24.04"'
printf '%s\n' "$linux_response" | grep -q '"os_detection_confidence":"confirmed"'
printf '%s\n' "$linux_response" | grep -q '"raw_capture_available":true'
disconnect

qemu-nbd --connect "$device" --format qcow2 --read-only "$windows_image"
udevadm settle --timeout=10 || true
windows_response=$(analyze)
printf '%s\n' "$windows_response" | grep -q '"ok":true'
printf '%s\n' "$windows_response" | grep -q '"detected_os_family":"windows"'
printf '%s\n' "$windows_response" | grep -q '"os_detection_confidence":"layout-only"'
printf '%s\n' "$windows_response" | grep -q '"boot_mode":"Legacy BIOS/MBR"'
disconnect

# The checked-in Windows fixture has one NTFS partition beginning at LBA 2048. Change only its
# disposable copy's filesystem signature to BitLocker so the source inspector and analysis response
# exercise encrypted-volume classification without requiring a recovery key.
qemu-img convert -O raw "$windows_image" "$bitlocker_image"
printf '%s' '-FVE-FS-' | dd of="$bitlocker_image" bs=1 seek=$((2048 * 512 + 3)) conv=notrunc status=none
qemu-nbd --connect "$device" --format raw --read-only "$bitlocker_image"
udevadm settle --timeout=10 || true
bitlocker_response=$(analyze)
printf '%s\n' "$bitlocker_response" | grep -q '"ok":true'
printf '%s\n' "$bitlocker_response" | grep -q '"encryption_detected":true'
printf '%s\n' "$bitlocker_response" | grep -q '"encryption_type":"BitLocker"'
printf '%s\n' "$bitlocker_response" | grep -q '"detected_os_family":"windows"'
disconnect

qemu-img create -f raw "$unknown_image" 64M >/dev/null
qemu-nbd --connect "$device" --format raw --read-only "$unknown_image"
udevadm settle --timeout=10 || true
unknown_response=$(analyze)
printf '%s\n' "$unknown_response" | grep -q '"ok":true'
printf '%s\n' "$unknown_response" | grep -q '"detected_os_family":"unknown"'
printf '%s\n' "$unknown_response" | grep -q '"layout_valid":false'
printf '%s\n' "$unknown_response" | grep -q '"raw_capture_available":true'

echo "read-only Linux confirmation, Windows-layout and BitLocker classification, and damaged/unknown raw-imaging availability passed"
