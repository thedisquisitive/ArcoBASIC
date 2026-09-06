#!/usr/bin/env bash
set -euo pipefail

HELPER=$1
ARCFS_LINUX=$2
UDEV_RULE=${3:-}
FIXTURE_BUILDER=${4:-}
MKFS_HELPER=${5:-}
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/arcfs-helper-smoke.XXXXXX")
trap 'rm -rf "$TMP_ROOT"' EXIT

output=$(ARCFS_MOUNT_DRY_RUN=1 ARCFS_LINUX="$ARCFS_LINUX" sh "$HELPER" /dev/sdb /mnt/arcfs -o ro,partition=2,allow_other)
expected="$ARCFS_LINUX mount /dev/sdb /mnt/arcfs --partition 2 -- -o allow_other"
if [[ "$output" != "$expected" ]]; then
    echo "Unexpected partition helper command:" >&2
    echo "  got:      $output" >&2
    echo "  expected: $expected" >&2
    exit 1
fi

output=$(ARCFS_MOUNT_DRY_RUN=1 ARCFS_LINUX="$ARCFS_LINUX" sh "$HELPER" disk.img /mnt/arcfs -o offset=1048576)
expected="$ARCFS_LINUX mount disk.img /mnt/arcfs --offset 1048576 -- -o allow_other"
if [[ "$output" != "$expected" ]]; then
    echo "Unexpected offset helper command:" >&2
    echo "  got:      $output" >&2
    echo "  expected: $expected" >&2
    exit 1
fi

output=$(ARCFS_MOUNT_DRY_RUN=1 ARCFS_LINUX="$ARCFS_LINUX" sh "$HELPER" /dev/sdb /mnt/arcfs -o rw)
expected="$ARCFS_LINUX mount /dev/sdb /mnt/arcfs -- -o allow_other"
if [[ "$output" != "$expected" ]]; then
    echo "Unexpected rw helper command:" >&2
    echo "  got:      $output" >&2
    echo "  expected: $expected" >&2
    exit 1
fi

output=$(ARCFS_MOUNT_DRY_RUN=1 ARCFS_LINUX="$ARCFS_LINUX" sh "$HELPER" /dev/sdb /media/daedalus/ArcFS -o rw,nosuid,nodev,uhelper=udisks2)
expected="$ARCFS_LINUX mount /dev/sdb /media/daedalus/ArcFS -- -o nosuid,nodev,allow_other"
if [[ "$output" != "$expected" ]]; then
    echo "Unexpected udisks helper command:" >&2
    echo "  got:      $output" >&2
    echo "  expected: $expected" >&2
    exit 1
fi

if [[ -n "$UDEV_RULE" ]]; then
    grep -q 'IMPORT{program}=".*/bin/sh .*arcfs-linux probe' "$UDEV_RULE"
    grep -q 'ENV{ID_FS_TYPE}=="arcfs"' "$UDEV_RULE"
fi

if [[ -n "$FIXTURE_BUILDER" ]]; then
    image=$TMP_ROOT/arcfs.img
    "$FIXTURE_BUILDER" --write-image "$image"
    probe=$("$ARCFS_LINUX" probe "$image")
    grep -q '^ID_FS_TYPE=arcfs$' <<<"$probe"
    grep -q '^ID_FS_USAGE=filesystem$' <<<"$probe"
    grep -q '^ID_FS_VERSION=FMV2$' <<<"$probe"
    grep -q '^ID_FS_UUID=000000000000002a$' <<<"$probe"
fi

image=$TMP_ROOT/mkfs.img
truncate -s 4194304 "$image"
"$ARCFS_LINUX" mkfs "$image" --force --volume-id 2a >/dev/null
probe=$("$ARCFS_LINUX" probe "$image")
grep -q '^ID_FS_TYPE=arcfs$' <<<"$probe"
grep -q '^ID_FS_UUID=000000000000002a$' <<<"$probe"
"$ARCFS_LINUX" ls "$image" / >/dev/null

if [[ -n "$MKFS_HELPER" ]]; then
    helper_image=$TMP_ROOT/mkfs-helper.img
    truncate -s 4194304 "$helper_image"
    ARCFS_LINUX="$ARCFS_LINUX" sh "$MKFS_HELPER" "$helper_image" --force --volume-id 2a >/dev/null
    "$ARCFS_LINUX" probe "$helper_image" | grep -q '^ID_FS_TYPE=arcfs$'
fi

large_image=$TMP_ROOT/mkfs-large.img
truncate -s 128M "$large_image"
"$ARCFS_LINUX" mkfs "$large_image" --force --volume-id 2a >/dev/null
"$ARCFS_LINUX" inspect "$large_image" | grep -q '^objects: 1 (0 files, 1 directories)$'

# Snapshot/rollback through the real CLI binary's own argv parsing and dispatch -- a different
# failure surface than tests/unit/arcfs_host_tests.cpp, which calls Volume's C++ API directly and
# never exercises parse_args()/main()'s "snapshot"/"rollback" command handling at all. File-content
# preservation across a snapshot is already covered thoroughly there; this just proves the CLI
# plumbing (create/list/ls/delete/rollback, --label, positional ID) actually works end to end.
snapshot_image=$TMP_ROOT/snapshot-cli.img
truncate -s 4194304 "$snapshot_image"
"$ARCFS_LINUX" mkfs "$snapshot_image" --force --volume-id 2a >/dev/null
"$ARCFS_LINUX" snapshot list "$snapshot_image" | grep -q '^no snapshots$'
"$ARCFS_LINUX" snapshot create "$snapshot_image" --label "cli-smoke" | grep -q '^created snapshot 1 ("cli-smoke")'
"$ARCFS_LINUX" inspect "$snapshot_image" | grep -q '^snapshots: 1$'
snapshot_line=$("$ARCFS_LINUX" snapshot list "$snapshot_image" | tail -n1)
snapshot_id=$(printf '%s' "$snapshot_line" | cut -f1)
[[ "$snapshot_id" == "1" ]]
printf '%s' "$snapshot_line" | grep -q 'cli-smoke$'
"$ARCFS_LINUX" snapshot ls "$snapshot_image" "$snapshot_id" / >/dev/null
"$ARCFS_LINUX" rollback "$snapshot_image" "$snapshot_id" | grep -q '^rolled back to snapshot 1'
"$ARCFS_LINUX" snapshot delete "$snapshot_image" "$snapshot_id" | grep -q '^deleted snapshot 1$'
"$ARCFS_LINUX" snapshot list "$snapshot_image" | grep -q '^no snapshots$'
