#!/bin/sh
set -eu

service=${1:?service binary required}
source_image=${2:?source qcow2 required}

if [ "$(id -u)" -ne 0 ]; then
    echo "service_backup_restore_nbd.sh must run as root" >&2
    exit 77
fi

root=$(mktemp -d "${LAZARUS_TEST_TMPDIR:-/var/tmp}/lazarus-backup-restore.XXXXXX")
destination_image="$root/destination.raw"
socket="$root/service.sock"
profile="$root/bench.profile"
source_device=
destination_device=
source_link="/dev/disk/by-path/lazarus-e2e-source-$$"
destination_link="/dev/disk/by-path/lazarus-e2e-destination-$$"
service_pid=

cleanup() {
    status=$?
    if [ "$status" -ne 0 ] && [ -f "$root/service.log" ]; then
        echo "Lazarus service log:" >&2
        tail -n 80 "$root/service.log" >&2 || true
    fi
    [ -z "$service_pid" ] || kill "$service_pid" 2>/dev/null || true
    rm -f "$source_link" "$destination_link"
    [ -z "$source_device" ] || qemu-nbd --disconnect "$source_device" 2>/dev/null || true
    [ -z "$destination_device" ] || qemu-nbd --disconnect "$destination_device" 2>/dev/null || true
    rm -rf "$root"
    return "$status"
}
trap cleanup EXIT INT TERM

modprobe nbd max_part=16
for candidate in /dev/nbd10 /dev/nbd11 /dev/nbd12 /dev/nbd13 /dev/nbd14 /dev/nbd15; do
    name=${candidate##*/}
    [ -b "$candidate" ] || continue
    [ ! -s "/sys/block/$name/pid" ] || continue
    if [ -z "$source_device" ]; then
        source_device=$candidate
    elif [ -z "$destination_device" ]; then
        destination_device=$candidate
        break
    fi
done
[ -n "$source_device" ] && [ -n "$destination_device" ] || {
    echo "Two free NBD devices are required." >&2
    exit 1
}

qemu-img create -f raw "$destination_image" 3G >/dev/null
qemu-nbd --connect "$source_device" --format qcow2 --read-only "$source_image"
qemu-nbd --connect "$destination_device" --format raw "$destination_image"
udevadm settle --timeout=10 || true
mkdir -p /dev/disk/by-path "$root/images"
ln -s "../../${source_device#/dev/}" "$source_link"
ln -s "../../${destination_device#/dev/}" "$destination_link"

printf 'name=Backup Restore Integration\nimage_storage=%s/images\n' "$root" >"$profile"
"$service" --config "$profile" --socket "$socket" --security "$root/admin.auth" \
    --technicians "$root/technicians.auth" --activity-log "$root/activity.log" >"$root/service.log" 2>&1 &
service_pid=$!
for _ in $(seq 1 50); do
    [ -S "$socket" ] && break
    sleep 0.1
done
[ -S "$socket" ]

call() {
    # Keep the receive side open after stdin reaches EOF. Restore can be quiet while the
    # destination flushes and reopens for readback; socat's short default close interval
    # otherwise drops the final service response even though the operation continues.
    printf '%s\n' "$1" | socat -t 300 - "UNIX-CONNECT:$socket"
}

image_directory="$root/images/end-to-end.laz"
backup_response=$(call "{\"command\":\"image_source\",\"selector\":\"$source_link\",\"output_directory\":\"$image_directory\",\"ticket_number\":\"E2E-001\",\"customer_name\":\"Integration Test\",\"technician\":\"Test Tech\",\"purpose\":\"Backup Restore Integration\",\"preset\":\"backup-before-repair\"}" | tail -n 1)
printf '%s\n' "$backup_response" | grep -q '"ok":true'
printf '%s\n' "$backup_response" | grep -q '"verified":true'
printf '%s\n' "$backup_response" | grep -Eq '"zero_bytes_elided":[1-9][0-9]*'
printf '%s\n' "$backup_response" | grep -Eq '"zero_chunks_elided":[1-9][0-9]*'

restore_response=$(call "{\"command\":\"restore_image\",\"image_directory\":\"$image_directory\",\"selector\":\"$destination_link\",\"confirmation\":\"ERASE\",\"technician\":\"Test Tech\"}" | tail -n 1)
if ! printf '%s\n' "$restore_response" | grep -q '"type":"final"'; then
    echo "restore connection ended without a final response: $restore_response" >&2
    if ! kill -0 "$service_pid" 2>/dev/null; then
        wait "$service_pid" || echo "service exited with status $?" >&2
        service_pid=
    fi
    exit 1
fi
printf '%s\n' "$restore_response" | grep -q '"ok":true'
printf '%s\n' "$restore_response" | grep -q '"readback_verified":true'
printf '%s\n' "$restore_response" | grep -q '"destination_layout_validated":true'

source_size=$(blockdev --getsize64 "$source_device")
cmp -n "$source_size" "$source_device" "$destination_device"

echo "unassigned serial-less by-path backup, verification, restore, readback, and layout validation passed"
