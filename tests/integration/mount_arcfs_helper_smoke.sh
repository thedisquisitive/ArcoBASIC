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
