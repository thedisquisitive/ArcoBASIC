#!/bin/sh
set -eu

# Exercises the multiple-image-storage-location feature end to end against a disposable NBD
# disk: add an additional local storage location (with format), confirm it mounts at its own
# /mnt/lazarus-storage-extra/<id> path independent of the primary, confirm the default flag and
# mount/unmount/remove commands work, and confirm none of this touches primary-location state.

PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export PATH

repo="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
os_base="$repo/lazarus-os"
container_root="${LAZARUS_ROOTFS:-$os_base/build/rootfs}"
service="$container_root/usr/sbin/lazarus-service"
extra_helper_source="$os_base/rootfs/usr/local/sbin/lazarus-mount-extra-storage"
work="$(mktemp -d "$os_base/build/extra-storage-test.XXXXXX")"
disk="$work/extra-storage.raw"
profile="$work/bench.profile"
runner="$work/run-service-test.sh"
nbd=""
by_id="/dev/disk/by-id/lazarus-extra-storage-test"

run_root() {
    if [ "$(id -u)" -eq 0 ]; then
        "$@"
    elif [ -n "${SUDO_PASSWORD:-}" ]; then
        printf '%s\n' "$SUDO_PASSWORD" | sudo -S "$@"
    else
        sudo "$@"
    fi
}

cleanup() {
    set +e
    [ -z "$nbd" ] || run_root qemu-nbd --disconnect "$nbd" >/dev/null 2>&1
    run_root rm -f "$by_id" >/dev/null 2>&1
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

for tool in qemu-img qemu-nbd parted partprobe mkfs.ext4 systemd-nspawn blkid; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "$tool is required for the destructive virtual-storage test." >&2
        exit 1
    }
done
[ -x "$service" ] || { echo "Alpine Lazarus service not found: $service" >&2; exit 1; }
[ -d "$container_root" ] || { echo "Lazarus OS rootfs not found: $container_root" >&2; exit 1; }
[ -f "$extra_helper_source" ] || { echo "lazarus-mount-extra-storage not found: $extra_helper_source" >&2; exit 1; }

for candidate in /dev/nbd0 /dev/nbd1 /dev/nbd2 /dev/nbd3 /dev/nbd4 /dev/nbd5 /dev/nbd6 /dev/nbd7; do
    name="${candidate##*/}"
    [ -b "$candidate" ] || continue
    [ ! -s "/sys/block/$name/pid" ] || continue
    nbd="$candidate"
    break
done
[ -n "$nbd" ] || { echo "No free NBD device is available." >&2; exit 1; }
partition="${nbd}p1"

qemu-img create -f raw "$disk" 1G >/dev/null
run_root qemu-nbd --connect="$nbd" --format=raw "$disk"
run_root udevadm settle --timeout=10 || true
sleep 1
run_root mkdir -p /dev/disk/by-id
run_root ln -s "../../${nbd#/dev/}" "$by_id"
# Partition and format outside the container: an nspawn container only sees the specific device
# nodes it was started with (via --bind), it does not get live udev passthrough for partitions
# created dynamically inside it, so add_local_storage_location's own "format" (mode) codepath
# cannot be exercised purely inside nspawn here. Instead pre-create the filesystem the same way
# the primary-storage destructive test does, and exercise the "existing filesystem" mode -- that
# still exercises every line specific to add_local_storage_location (id generation, extra mount
# target, mount/unmount/remove/set-default), which is this test's actual purpose.
run_root parted -s "$nbd" mklabel gpt
run_root parted -s "$nbd" mkpart primary ext4 1MiB 100%
run_root partprobe "$nbd" || true
sleep 1
[ -b "$partition" ] || { echo "Disposable NBD partition did not appear: $partition" >&2; exit 1; }
run_root mkfs.ext4 -F -L LAZARUS_EXTRA_TEST "$partition" >/dev/null

# No primary location is configured at all -- this test exists to prove extras stand on their
# own and never require (or disturb) a primary assignment.
printf 'name=Extra Storage Test\n' >"$profile"

cat >"$runner" <<EOF
#!/bin/sh
set -eu
requests=/tmp/lazarus-extra-requests
responses=/tmp/lazarus-extra-responses
security=/tmp/lazarus-extra-admin.auth
rm -f "\$requests" "\$responses" "\$security"
mkfifo "\$requests" "\$responses"
/usr/sbin/lazarus-service --config '/work/${profile#"$repo"/}' --security "\$security" --stdio <"\$requests" >"\$responses" &
pid=\$!
trap 'kill "\$pid" 2>/dev/null || true' EXIT
exec 3>"\$requests"
exec 4<"\$responses"
call() { printf '%s\\n' "\$1" >&3; IFS= read -r response <&4; printf '%s\\n' "\$response"; }

setup=\$(call '{"command":"admin_setup","new_password":"test"}')
token=\$(printf '%s\\n' "\$setup" | sed -n 's/.*"token":"\\([^"]*\\)".*/\\1/p')
[ -n "\$token" ]

added=\$(call "\$(printf '{"command":"add_local_storage_location","admin_token":"%s","selector":"$nbd","mode":"existing","directory":"images","name":"Test Extra Drive","set_default":"1"}' "\$token")")
printf '%s\\n' "\$added" | grep -q '"ok":true'
id=\$(printf '%s\\n' "\$added" | grep -o 'extra-[0-9a-f]\\{16\\}' | head -n1)
[ -n "\$id" ]
mount_target="/mnt/lazarus-storage-extra/\$id"
mountpoint -q "\$mount_target"
[ -d "\$mount_target/images" ]

listed=\$(call '{"command":"profile"}')
printf '%s\\n' "\$listed" | grep -q "\$id"
printf '%s\\n' "\$listed" | grep -q 'Test Extra Drive'

unmounted=\$(call "\$(printf '{"command":"unmount_storage_location","admin_token":"%s","id":"%s"}' "\$token" "\$id")")
printf '%s\\n' "\$unmounted" | grep -q '"ok":true'
! mountpoint -q "\$mount_target"

remounted=\$(call "\$(printf '{"command":"mount_storage_location","admin_token":"%s","id":"%s"}' "\$token" "\$id")")
printf '%s\\n' "\$remounted" | grep -q '"ok":true'
mountpoint -q "\$mount_target"

cleared=\$(call "\$(printf '{"command":"set_default_storage_location","admin_token":"%s","id":""}' "\$token")")
printf '%s\\n' "\$cleared" | grep -q '"ok":true'

removed=\$(call "\$(printf '{"command":"remove_storage_location","admin_token":"%s","id":"%s"}' "\$token" "\$id")")
printf '%s\\n' "\$removed" | grep -q '"ok":true'
! mountpoint -q "\$mount_target"

final=\$(call '{"command":"profile"}')
! printf '%s\\n' "\$final" | grep -q "\$id"
printf '%s\\n' "\$final" | grep -q '"storage_locations_rows":""'

exec 3>&-
exec 4<&-
kill "\$pid"
wait "\$pid" 2>/dev/null || true
trap - EXIT
EOF
chmod +x "$runner"

run_root systemd-nspawn \
    -D "$container_root" \
    --capability=CAP_SYS_ADMIN \
    --bind "$repo":/work \
    --bind "$nbd" \
    --bind "$partition" \
    --bind-ro /dev/disk \
    --bind-ro "$extra_helper_source":/usr/local/sbin/lazarus-mount-extra-storage \
    "/work/${runner#"$repo"/}"

grep -q '^storage.count=0$' "$profile"
! grep -q 'image_storage_device=' "$profile"
! grep -q 'nas_storage_protocol=' "$profile"

echo "An additional local storage location was added, mounted at its own path independent of the primary, unmounted, remounted, had its default flag cleared, and was removed -- all without ever configuring or disturbing a primary location."
