#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
repo="$(CDPATH= cd -- "$base/../.." && pwd)"
builder="${ALPINE_BUILDER:-$base/build/alpine-builder}"
rootfs="${1:-$base/build/rootfs}"

case "$rootfs" in
	/*) ;;
	*) rootfs="$(pwd)/$rootfs" ;;
esac

if [ "$(id -u)" -ne 0 ]; then
	echo "stage-current-lazarus-nspawn.sh must run as root (systemd-nspawn)." >&2
	exit 1
fi
if ! command -v systemd-nspawn >/dev/null 2>&1; then
	echo "systemd-nspawn was not found." >&2
	exit 1
fi
if [ ! -e "$builder/.arcology-lazarus-builder" ]; then
	echo "Alpine nspawn builder not found: $builder" >&2
	echo "Run scripts/build-rootfs-nspawn.sh first to bootstrap it." >&2
	exit 1
fi

rootfs_inside="$rootfs"
case "$rootfs" in
	"$repo"/*)
		rootfs_inside="/work/${rootfs#"$repo"/}"
		;;
esac

echo "Cross-building and staging current Lazarus binaries via the Alpine nspawn builder (musl)."
echo "This does not run apk add and does not change installed packages."
systemd-nspawn \
	-D "$builder" \
	--bind "$repo":/work \
	--chdir /work \
	/bin/sh -lc "
		set -eu
		/work/lazarus/lazarus-os/scripts/stage-current-lazarus.sh '$rootfs_inside' /work/lazarus/lazarus-os/build/lazarus-current-alpine
	"
echo "Staged current Lazarus build (musl) into: $rootfs"
