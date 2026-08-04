#!/bin/sh
set -eu

service=${1:?service binary required}
cli=${2:?cli binary required}
source_image=${3:?source qcow2 required}
removable_image=${4:?removable raw image required}

if [ "$(id -u)" -ne 0 ]; then
    echo "service_browse_export.sh must run as root" >&2
    exit 77
fi

root=$(mktemp -d "${LAZARUS_TEST_TMPDIR:-/var/tmp}/lazarus-browse-export.XXXXXX")
source_device=/dev/nbd8
destination_device=/dev/nbd9
socket="$root/service.sock"
profile="$root/bench.profile"
service_pid=
destination_mount="$root/destination"

cleanup() {
    status=$?
    if [ "$status" -ne 0 ] && [ -f "$root/service.log" ]; then
        echo "Lazarus service log:" >&2
        tail -n 80 "$root/service.log" >&2 || true
    fi
    [ -z "$service_pid" ] || kill "$service_pid" 2>/dev/null || true
    umount "$destination_mount" 2>/dev/null || true
    qemu-nbd --disconnect "$source_device" 2>/dev/null || true
    qemu-nbd --disconnect "$destination_device" 2>/dev/null || true
    rm -rf "$root"
    return "$status"
}
trap cleanup EXIT INT TERM

modprobe nbd max_part=16
qemu-nbd --connect "$source_device" --format qcow2 --read-only "$source_image"
qemu-nbd --connect "$destination_device" --format raw "$removable_image"
mkdir -p "$root/images" "$destination_mount"
cat >"$profile" <<EOF
name=Browse Export Integration
image_storage=$root/images
source=/sys/block/nbd8
removable_media=/dev/nbd9
EOF

image_directory="$root/images/T-BROWSE/Test/fixture.laz"
mkdir -p "$(dirname "$image_directory")"
if ! LAZARUS_COMPRESSION=zstd "$cli" image-source "$profile" "$source_device" "$image_directory" T-BROWSE Test Technician \
        "Browse export integration" >"$root/imaging.log" 2>&1; then
    tail -n 30 "$root/imaging.log" >&2
    exit 1
fi

"$service" --config "$profile" --socket "$socket" --security "$root/admin.auth" >"$root/service.log" 2>&1 &
service_pid=$!
for _ in $(seq 1 50); do
    [ -S "$socket" ] && break
    sleep 0.1
done
[ -S "$socket" ]

call() {
    printf '%s\n' "$1" | socat - "UNIX-CONNECT:$socket"
}

open_response=$(call "{\"command\":\"browse_open\",\"image_directory\":\"$image_directory\"}" | tail -n 1)
printf '%s\n' "$open_response" | grep -q '"ok":true'
session=$(printf '%s\n' "$open_response" | sed -n 's/.*"session_id":"\([^"]*\)".*/\1/p')
volume=$(printf '%s\n' "$open_response" | sed -n 's/.*"volumes_rows":"\([^\\]*\)\\t.*/\1/p')
[ -n "$session" ]
[ -n "$volume" ]

list_response=$(call "{\"command\":\"browse_list\",\"session_id\":\"$session\",\"volume_token\":\"$volume\",\"relative_path\":\"\"}" | tail -n 1)
printf '%s\n' "$list_response" | grep -q '"ok":true'
filename=LAZARUS-TEST.txt
filename_hex=$(printf '%s' "$filename" | od -An -tx1 | tr -d ' \n')
printf '%s\n' "$list_response" | grep -qi "$filename_hex"

export_response=$(call "{\"command\":\"browse_export\",\"session_id\":\"$session\",\"volume_token\":\"$volume\",\"destination_selector\":\"$destination_device\",\"destination_folder\":\"Recovered\",\"source_paths_hex\":\"$filename_hex\"}" | tail -n 1)
printf '%s\n' "$export_response" | grep -q '"ok":true'
printf '%s\n' "$export_response" | grep -q '"safe_to_disconnect":true'
call "{\"command\":\"browse_close\",\"session_id\":\"$session\"}" | tail -n 1 | grep -q '"ok":true'

mount -o ro "$destination_device" "$destination_mount"
recovered=$(find "$destination_mount/Recovered" -type f -name "$filename" -print -quit)
[ -n "$recovered" ]
grep -q 'Arcology Lazarus QEMU source fixture' "$recovered"
umount "$destination_mount"

echo "read-only image browse and removable-media export passed"
