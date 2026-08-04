#!/bin/sh
set -eu

service=${1:?service binary required}
root=$(mktemp -d /dev/shm/lazarus-volatile-storage.XXXXXX)
trap 'rm -rf "$root"' EXIT INT TERM

mkdir -p "$root/images"
printf 'name=Volatile Storage Test\nimage_storage=%s/images\n' "$root" >"$root/bench.profile"

response=$(printf '%s\n' '{"command":"profile"}' |
    "$service" --stdio --config "$root/bench.profile" --security "$root/admin.auth")
printf '%s\n' "$response" | grep -q '"storage_online":false'
printf '%s\n' "$response" | grep -q 'live-system RAM storage is not accepted'

