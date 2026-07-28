#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
repo="$(CDPATH= cd -- "$base/../.." && pwd)"
arch="${ALPINE_ARCH:-x86_64}"
mirror="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"
builder="${ALPINE_BUILDER:-$base/build/alpine-builder}"
rootfs="${ROOTFS:-$base/build/rootfs}"
rootfs_inside="$rootfs"
case "$rootfs" in
	"$repo"/*)
		rootfs_inside="/work/${rootfs#"$repo"/}"
		;;
esac

require_command() {
	if ! command -v "$1" >/dev/null 2>&1; then
		echo "$1 was not found." >&2
		exit 1
	fi
}

run_root() {
	if [ "$(id -u)" -eq 0 ]; then
		"$@"
	elif [ -n "${SUDO_PASSWORD:-}" ]; then
		printf '%s\n' "$SUDO_PASSWORD" | sudo -S "$@"
	elif command -v sudo >/dev/null 2>&1; then
		sudo "$@"
	else
		echo "This step requires root. Re-run as root or install sudo." >&2
		exit 1
	fi
}

download() {
	url="$1"
	out="$2"
	if command -v curl >/dev/null 2>&1; then
		curl -fL "$url" -o "$out"
	else
		wget -O "$out" "$url"
	fi
}

require_command systemd-nspawn
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
	echo "curl or wget is required to fetch Alpine minirootfs." >&2
	exit 1
fi

mkdir -p "$base/build/downloads"

if [ ! -e "$builder/.arcology-lazarus-builder" ]; then
	echo "Preparing Alpine nspawn builder: $builder"
	release_yaml="$base/build/downloads/latest-releases-$arch.yaml"
	download "$mirror/latest-stable/releases/$arch/latest-releases.yaml" "$release_yaml"
	minirootfs="$(awk '/file: alpine-minirootfs-.*\.tar\.gz/ {print $2; exit}' "$release_yaml")"
	if [ -z "$minirootfs" ]; then
		echo "Could not locate alpine-minirootfs entry in latest-releases.yaml." >&2
		exit 1
	fi
	archive="$base/build/downloads/$minirootfs"
	if [ ! -f "$archive" ]; then
		download "$mirror/latest-stable/releases/$arch/$minirootfs" "$archive"
	fi
	rm -rf "$builder"
	mkdir -p "$builder"
	run_root tar -xzf "$archive" -C "$builder"
	run_root sh -c "printf '%s\n' '$mirror/latest-stable/main' '$mirror/latest-stable/community' > '$builder/etc/apk/repositories'"
	run_root touch "$builder/.arcology-lazarus-builder"
fi

echo "Building Arcology Lazarus OS rootfs inside Alpine nspawn."
run_root systemd-nspawn \
	-D "$builder" \
	--bind "$repo":/work \
	--chdir /work \
	/bin/sh -lc "
		set -eu
		apk update
		apk add alpine-base build-base cmake pkgconf openssl-dev zstd-dev gtk4.0-dev gtkmm4-dev hivex-dev
		LAZARUS_BUILD_DIR=/work/lazarus/lazarus-os/build/lazarus-current-alpine \
			/work/lazarus/lazarus-os/scripts/build-rootfs.sh '$rootfs_inside'
	"

echo "Rootfs built with Alpine nspawn: $rootfs"
