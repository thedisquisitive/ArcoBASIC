#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
rootfs="${1:-$base/build/rootfs}"
lazarus_build_dir="${LAZARUS_BUILD_DIR:-$base/build/lazarus-current}"

if ! command -v apk >/dev/null 2>&1; then
	echo "apk was not found. Run this on Alpine or install apk-tools on the build host." >&2
	exit 1
fi

mkdir -p "$rootfs"
mkdir -p "$rootfs/etc/apk"
if [ -d /etc/apk/keys ]; then
	mkdir -p "$rootfs/etc/apk/keys"
	cp /etc/apk/keys/* "$rootfs/etc/apk/keys/" 2>/dev/null || true
fi
if [ -f /etc/apk/repositories ]; then
	cp /etc/apk/repositories "$rootfs/etc/apk/repositories"
elif [ -n "${ALPINE_MIRROR:-}" ]; then
	printf '%s\n' "$ALPINE_MIRROR/latest-stable/main" "$ALPINE_MIRROR/latest-stable/community" > "$rootfs/etc/apk/repositories"
else
	printf '%s\n' \
		"https://dl-cdn.alpinelinux.org/alpine/latest-stable/main" \
		"https://dl-cdn.alpinelinux.org/alpine/latest-stable/community" \
		> "$rootfs/etc/apk/repositories"
fi

echo "Preparing Alpine rootfs at: $rootfs"
echo "This installs packages into the rootfs path only; it does not format or write a boot device."

world_normal="$rootfs/tmp/arcology-lazarus-world.normal"
world_grub="$rootfs/tmp/arcology-lazarus-world.grub"
mkdir -p "$rootfs/tmp"
sed '/^#/d; /^$/d' "$base/packages/world" | grep -Ev '^(grub|grub-efi)$' > "$world_normal"
sed '/^#/d; /^$/d' "$base/packages/world" | grep -E '^(grub|grub-efi)$' > "$world_grub" || true

apk add \
	--root "$rootfs" \
	--initdb \
	--update-cache \
	$(cat "$world_normal")

if [ -s "$world_grub" ]; then
	apk add \
		--root "$rootfs" \
		--no-scripts \
		$(cat "$world_grub")
fi

cp -R "$base/rootfs/." "$rootfs/"
install -Dm755 "$base/openrc/lazarus-service" "$rootfs/etc/init.d/lazarus-service"
install -Dm755 "$base/openrc/lazarus-session" "$rootfs/etc/init.d/lazarus-session"
install -Dm755 "$base/openrc/lazarus-storage" "$rootfs/etc/init.d/lazarus-storage"
install -Dm755 "$base/openrc/lazarus-printers" "$rootfs/etc/init.d/lazarus-printers"
install -Dm755 "$base/openrc/lazarus-cups-browsed" "$rootfs/etc/init.d/lazarus-cups-browsed"
install -Dm755 "$base/openrc/lazarus-network" "$rootfs/etc/init.d/lazarus-network"
install -Dm755 "$base/rootfs/usr/local/sbin/lazarus-mount-storage" "$rootfs/usr/local/sbin/lazarus-mount-storage"
install -Dm755 "$base/rootfs/usr/local/sbin/lazarus-restore-printers" "$rootfs/usr/local/sbin/lazarus-restore-printers"
install -Dm644 "$base/udev/90-arcology-lazarus.rules" "$rootfs/etc/udev/rules.d/90-arcology-lazarus.rules"
install -Dm644 "$base/profiles/bench.profile" "$rootfs/etc/arcology-lazarus/bench.profile"

"$base/scripts/stage-current-lazarus.sh" "$rootfs" "$lazarus_build_dir"

chroot "$rootfs" addgroup -S lazarus 2>/dev/null || true
chroot "$rootfs" adduser -S -D -H -h /var/lib/arcology-lazarus -G lazarus lazarus 2>/dev/null || true
# CUPS authorizes administrative operations through its @SYSTEM group. The
# privileged Lazarus service runs as root and must be a member for lpadmin to
# add, remove, and select report printers through the local CUPS socket.
chroot "$rootfs" addgroup root lpadmin 2>/dev/null || true
chroot "$rootfs" rc-update add udev sysinit
# udev alone only starts the daemon. Physical disks behind controllers that
# were not needed to boot the live USB do not appear until coldplug loads their
# modalias drivers and replays the existing device events.
chroot "$rootfs" rc-update add udev-trigger sysinit
chroot "$rootfs" rc-update add hwdrivers sysinit
chroot "$rootfs" rc-update add udev-settle sysinit
chroot "$rootfs" rc-update add lazarus-storage default
chroot "$rootfs" rc-update add lazarus-printers default
chroot "$rootfs" rc-update add lazarus-network default
chroot "$rootfs" rc-update add dbus default
chroot "$rootfs" rc-update add seatd default
chroot "$rootfs" rc-update add avahi-daemon default
chroot "$rootfs" rc-update add cupsd default
chroot "$rootfs" rc-update add lazarus-cups-browsed default
chroot "$rootfs" rc-update add lazarus-service default
chroot "$rootfs" rc-update add lazarus-session default

mkdir -p "$rootfs/var/lib/arcology-lazarus/images" "$rootfs/var/log/arcology-lazarus" "$rootfs/run/arcology-lazarus" "$rootfs/tmp" "$rootfs/var/tmp"
chmod 1777 "$rootfs/tmp" "$rootfs/var/tmp"

echo "Rootfs scaffold complete with current Lazarus binaries staged."
echo "Next: bootloader config and image assembly."
