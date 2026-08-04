#!/bin/sh
set -eu

PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export PATH

repo="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
os_base="$repo/lazarus-os"
container_root="${LAZARUS_ROOTFS:-$os_base/build/rootfs}"
service="$container_root/usr/sbin/lazarus-service"
work="$(mktemp -d "$os_base/build/format-storage-test.XXXXXX")"
disk="$work/used-storage.raw"
profile="$work/bench.profile"
helper="$work/mount-storage-test"
runner="$work/run-service-test.sh"
storage_inside="/work/${work#"$repo"/}/images"
nbd=""
by_id="/dev/disk/by-id/lazarus-format-storage-test"

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

for candidate in /dev/nbd0 /dev/nbd1 /dev/nbd2 /dev/nbd3 /dev/nbd4 /dev/nbd5 /dev/nbd6 /dev/nbd7; do
    name="${candidate##*/}"
    [ -b "$candidate" ] || continue
    [ ! -s "/sys/block/$name/pid" ] || continue
    nbd="$candidate"
    break
done
[ -n "$nbd" ] || { echo "No free NBD device is available." >&2; exit 1; }
partition="${nbd}p1"

qemu-img create -f raw "$disk" 2G >/dev/null
run_root qemu-nbd --connect="$nbd" --format=raw "$disk"
run_root udevadm settle --timeout=10 || true
sleep 1
run_root parted -s "$nbd" mklabel msdos
run_root parted -s "$nbd" mkpart primary ext4 1MiB 100%
run_root partprobe "$nbd" || true
sleep 1
[ -b "$partition" ] || { echo "Disposable NBD partition did not appear: $partition" >&2; exit 1; }
run_root mkfs.ext4 -F -L USED_DATA "$partition" >/dev/null
run_root mkdir -p /dev/disk/by-id
run_root ln -s "../../${nbd#/dev/}" "$by_id"

printf 'name=Format Storage Test\nimage_storage=%s\nsource=/dev/disk/by-path/format-test-source\n' "$storage_inside" >"$profile"
mkdir -p "$work/images"
cat >"$helper" <<EOF
#!/bin/sh
set -eu
mkdir -p /mnt/lazarus-storage
mount '$partition' /mnt/lazarus-storage
mkdir -p /mnt/lazarus-storage/images
EOF
chmod +x "$helper"

cat >"$runner" <<EOF
#!/bin/sh
set -eu
requests=/tmp/lazarus-format-requests
responses=/tmp/lazarus-format-responses
security=/tmp/lazarus-format-admin.auth
rm -f "\$requests" "\$responses" "\$security"
mkdir -p /mnt/lazarus-drives/preconfigured
mount '$partition' /mnt/lazarus-drives/preconfigured
mkdir -p '/mnt/lazarus-drives/preconfigured/Existing/Lazarus Images'
mkfifo "\$requests" "\$responses"
/usr/sbin/lazarus-service --config '/work/${profile#"$repo"/}' --security "\$security" --stdio <"\$requests" >"\$responses" &
pid=\$!
trap 'kill "\$pid" 2>/dev/null || true' EXIT
exec 3>"\$requests"
exec 4<"\$responses"
call() { printf '%s\\n' "\$1" >&3; IFS= read -r response <&4; printf '%s\\n' "\$response"; }
setup=\$(call '{"command":"admin_setup","new_password":"test"}')
token=\$(printf '%s\\n' "\$setup" | sed -n 's/.*\"token\":\"\\([^\"]*\\)\".*/\\1/p')
[ -n "\$token" ]
root_folders=\$(call "\$(printf '{"command":"storage_folders","admin_token":"%s","selector":"$nbd","relative_path":""}' "\$token")")
printf '%s\\n' "\$root_folders" | grep -q '"ok":true'
printf '%s\\n' "\$root_folders" | grep -q '4578697374696e67'
nested_folders=\$(call "\$(printf '{"command":"storage_folders","admin_token":"%s","selector":"$nbd","relative_path":"Existing"}' "\$token")")
printf '%s\\n' "\$nested_folders" | grep -q '"ok":true'
printf '%s\\n' "\$nested_folders" | grep -q '4c617a6172757320496d61676573'
created_folder=\$(call "\$(printf '{"command":"storage_create_folder","admin_token":"%s","selector":"$nbd","relative_path":"Existing","name":"Created From Browser"}' "\$token")")
printf '%s\\n' "\$created_folder" | grep -q '"ok":true'
printf '%s\\n' "\$created_folder" | grep -q '"relative_path":"Existing/Created From Browser"'
[ -d '/mnt/lazarus-drives/preconfigured/Existing/Created From Browser' ]
invalid_folder=\$(call "\$(printf '{"command":"storage_create_folder","admin_token":"%s","selector":"$nbd","relative_path":"Existing","name":"../Escape"}' "\$token")")
printf '%s\\n' "\$invalid_folder" | grep -q '"ok":false'
escape_attempt=\$(call "\$(printf '{"command":"storage_folders","admin_token":"%s","selector":"$nbd","relative_path":"../"}' "\$token")")
printf '%s\\n' "\$escape_attempt" | grep -q '"ok":false'
existing=\$(call "\$(printf '{"command":"configure_image_storage","admin_token":"%s","selector":"$nbd","mode":"existing","directory":"Existing/Lazarus Images"}' "\$token")")
printf '%s\\n' "\$existing" | grep -q '\"ok\":true'
printf '%s\\n' "\$existing" | grep -q '\"mount_path\":\"/mnt/lazarus-storage/Existing/Lazarus Images\"'
mountpoint -q /mnt/lazarus-storage
[ -d '/mnt/lazarus-storage/Existing/Lazarus Images' ]
! mountpoint -q /mnt/lazarus-drives/preconfigured
unmounted=\$(call "\$(printf '{"command":"unmount_image_storage","admin_token":"%s"}' "\$token")")
printf '%s\\n' "\$unmounted" | grep -q '\"ok\":true'
result=\$(call "\$(printf '{"command":"configure_image_storage","admin_token":"%s","selector":"$nbd","mode":"format","directory":"Fresh/Lazarus Images","confirmation":"ERASE"}' "\$token")")
printf '%s\\n' "\$result" | grep -q '\"ok\":true'
printf '%s\\n' "\$result" | grep -q '\"filesystem\":\"ext4\"'
printf '%s\\n' "\$result" | grep -q '\"mount_path\":\"/mnt/lazarus-storage/Fresh/Lazarus Images\"'
[ -d '/mnt/lazarus-storage/Fresh/Lazarus Images' ]
unmounted=\$(call "\$(printf '{"command":"unmount_image_storage","admin_token":"%s"}' "\$token")")
printf '%s\\n' "\$unmounted" | grep -q '\"ok\":true'
! mountpoint -q /mnt/lazarus-storage
mounted=\$(call '{"command":"profile"}')
printf '%s\\n' "\$mounted" | grep -q '\"storage_online\":true'
mountpoint -q /mnt/lazarus-storage
exec 3>&-
exec 4<&-
kill "\$pid"
wait "\$pid" 2>/dev/null || true
trap - EXIT
umount /mnt/lazarus-storage
EOF
chmod +x "$runner"

run_root systemd-nspawn \
    -D "$container_root" \
    --capability=CAP_SYS_ADMIN \
    --bind "$repo":/work \
    --bind "$nbd" \
    --bind "$partition" \
    --bind-ro /dev/disk \
    --bind-ro "$helper":/usr/local/sbin/lazarus-mount-storage \
    "/work/${runner#"$repo"/}"

[ "$(run_root blkid -s TYPE -o value "$partition")" = ext4 ]
run_root parted -sm "$nbd" print | grep -q '^BYT;$'
run_root parted -sm "$nbd" print | grep -q ':gpt:'
grep -q '^image_storage_device=/dev/disk/by-id/lazarus-format-storage-test$' "$profile"
grep -q '^image_storage_volume=' "$profile"
grep -q '^image_storage=/mnt/lazarus-storage/Fresh/Lazarus Images$' "$profile"

echo "The confined folder browser listed a Home-mounted disk, storage adopted its selected folder, and the disposable disk was reformatted with a custom persistent image folder."
