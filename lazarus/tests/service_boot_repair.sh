#!/bin/sh
set -eu

service=${1:?service binary required}
root=$(mktemp -d "${TMPDIR:-/tmp}/lazarus-boot-repair.XXXXXX")
trap 'rm -rf "$root"' EXIT INT TERM

mkdir -p "$root/storage"
cat >"$root/bench.profile" <<EOF
name=Boot Repair Test
image_storage=$root/storage
source=/test/source
destination=/test/destination
EOF

# boot_repair_apply requires the exact confirmation phrase before it looks at the disk at all,
# matching driver_apply_offline's precedent -- this is checked without needing a real block device.
unconfirmed=$(printf '%s\n' \
    '{"command":"boot_repair_apply","destination_selector":"/test/destination","confirmation":"NO"}' \
    | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$unconfirmed" | grep -q '"ok":false'
printf '%s\n' "$unconfirmed" | grep -q 'Type APPLY BOOT REPAIR'

missing_confirmation=$(printf '%s\n' \
    '{"command":"boot_repair_apply","destination_selector":"/test/destination"}' \
    | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$missing_confirmation" | grep -q '"ok":false'
printf '%s\n' "$missing_confirmation" | grep -q 'Type APPLY BOOT REPAIR'

# With the confirmation satisfied, a selector that does not match any connected disk is rejected
# before any mount is attempted.
not_connected_apply=$(printf '%s\n' \
    '{"command":"boot_repair_apply","destination_selector":"/test/destination","confirmation":"APPLY BOOT REPAIR"}' \
    | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$not_connected_apply" | grep -q '"ok":false'
printf '%s\n' "$not_connected_apply" | grep -q 'Boot repair requires a connected non-system disk'

# boot_repair_plan is read-only and needs no confirmation, but still requires a connected disk.
not_connected_plan=$(printf '%s\n' \
    '{"command":"boot_repair_plan","destination_selector":"/test/destination"}' \
    | "$service" --stdio --config "$root/bench.profile" | tail -n 1)
printf '%s\n' "$not_connected_plan" | grep -q '"ok":false'
printf '%s\n' "$not_connected_plan" | grep -q 'Boot repair requires a connected non-system disk'

echo "boot repair guard rails passed"
