#!/bin/sh
set -eu

os_base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
lazarus_dir="$(CDPATH= cd -- "$os_base/.." && pwd)"
rootfs="${1:?usage: stage-current-lazarus.sh ROOTFS [BUILD_DIR]}"
build_dir="${2:-$os_base/build/lazarus-current}"

if ! command -v cmake >/dev/null 2>&1; then
	echo "cmake was not found; cannot stage current Lazarus build." >&2
	exit 1
fi

mkdir -p "$rootfs" "$build_dir"

echo "Configuring current Lazarus build for rootfs staging..."
cmake -S "$lazarus_dir" -B "$build_dir" \
	-DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
	-DCMAKE_INSTALL_PREFIX=/usr

echo "Building current Lazarus binaries..."
cmake --build "$build_dir"

echo "Installing current Lazarus binaries into rootfs: $rootfs"
DESTDIR="$rootfs" cmake --install "$build_dir"

echo "Refreshing Lazarus OS overlay in rootfs"
profile_backup=""
if [ -f "$rootfs/etc/arcology-lazarus/bench.profile" ]; then
	profile_backup="$(mktemp)"
	cp "$rootfs/etc/arcology-lazarus/bench.profile" "$profile_backup"
fi
cp -a "$os_base/rootfs/." "$rootfs/"
if [ -n "$profile_backup" ]; then
	cp "$profile_backup" "$rootfs/etc/arcology-lazarus/bench.profile"
	rm -f "$profile_backup"
fi
chown -R root:root \
	"$rootfs/etc/arcology-lazarus" \
	"$rootfs/etc/X11/xinit" \
	"$rootfs/usr/local/bin" \
	"$rootfs/usr/local/sbin"
chmod 0755 \
	"$rootfs/usr/local/bin/lazarus-kiosk" \
	"$rootfs/usr/local/sbin/lazarus-install-os" \
	"$rootfs/usr/local/sbin/lazarus-mount-storage" \
	"$rootfs/usr/local/sbin/lazarus-restore-printers"

if [ ! -x "$rootfs/usr/sbin/lazarus-service" ]; then
	echo "Expected service missing after install: /usr/sbin/lazarus-service" >&2
	exit 1
fi

if [ ! -x "$rootfs/usr/bin/lazarus-gui" ]; then
	echo "Expected GUI missing after install: /usr/bin/lazarus-gui" >&2
	exit 1
fi

echo "Current Lazarus build staged into rootfs."
