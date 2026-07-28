#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"

required="
README.md
packages/world
packages/arcology-lazarus/APKBUILD.in
packages/lazarus-config/APKBUILD
packages/lazarus-os/APKBUILD
packages/lazarus-service/APKBUILD
packages/lazarus-session/APKBUILD
openrc/lazarus-service
openrc/lazarus-session
openrc/lazarus-network
openrc/lazarus-cups-browsed
rootfs/etc/cups/cups-browsed.conf
rootfs/usr/local/bin/lazarus-kiosk
rootfs/usr/local/sbin/lazarus-install-os
rootfs/usr/local/sbin/lazarus-network-up
	rootfs/etc/arcology-lazarus/network.conf
	rootfs/etc/inittab
	rootfs/etc/udhcpc/post-bound/10-lazarus-network-status
rootfs/etc/X11/xinit/xinitrc
profiles/bench.profile
udev/90-arcology-lazarus.rules
boot/README.md
boot/live-init
scripts/build-rootfs.sh
scripts/build-rootfs-nspawn.sh
scripts/stage-current-lazarus.sh
qemu/README.md
qemu/build-live-iso.sh
qemu/create-test-disks.sh
qemu/install-system-disk.sh
qemu/profiles/installed.env
qemu/profiles/live.env
qemu/run-profile.sh
qemu/run-installed.sh
qemu/run-live.sh
qemu/smoke-live-usb.sh
qemu/smoke-installed.sh
qemu/smoke-service.sh
"

for path in $required; do
	if [ ! -e "$base/$path" ]; then
		echo "missing: $path" >&2
		exit 1
	fi
done

for driver_package in \
	xf86-video-amdgpu \
	xf86-video-ati \
	xf86-video-intel \
	xf86-video-nouveau \
	xf86-video-fbdev \
	xf86-video-vesa; do
	grep -qx "$driver_package" "$base/packages/world" || {
		echo "Missing physical display package: $driver_package" >&2
		exit 1
	}
done

for service in lazarus-service lazarus-session lazarus-network; do
	grep -q 'use lazarus-storage' "$base/openrc/$service" || {
		echo "$service must treat image storage as optional during boot." >&2
		exit 1
	}
done

grep -q 'use lazarus-storage lazarus-service' "$base/openrc/lazarus-session" || {
	echo "The kiosk must still start when the privileged service is unavailable." >&2
	exit 1
}

if grep -q '^tty1::respawn:' "$base/rootfs/etc/inittab"; then
	echo "tty1 must be reserved for kiosk startup diagnostics." >&2
	exit 1
fi

for display_path in auto modesetting fbdev vesa; do
	grep -q "auto modesetting fbdev vesa" "$base/rootfs/usr/local/bin/lazarus-kiosk" || {
		echo "Kiosk display fallback sequence is incomplete: $display_path" >&2
		exit 1
	}
done

grep -q 'all Xorg display paths failed; retrying' "$base/rootfs/usr/local/bin/lazarus-kiosk" || {
	echo "Kiosk must retry after display startup failure." >&2
	exit 1
}

for path in \
	openrc/lazarus-service \
	openrc/lazarus-storage \
	openrc/lazarus-session \
	openrc/lazarus-network \
	openrc/lazarus-cups-browsed \
	rootfs/usr/local/bin/lazarus-kiosk \
	rootfs/usr/local/sbin/lazarus-install-os \
	rootfs/usr/local/sbin/lazarus-network-up \
	rootfs/etc/udhcpc/post-bound/10-lazarus-network-status \
	rootfs/etc/X11/xinit/xinitrc \
	scripts/build-rootfs.sh \
	scripts/build-rootfs-nspawn.sh \
	scripts/stage-current-lazarus.sh \
	qemu/build-live-iso.sh \
	qemu/create-test-disks.sh \
	qemu/install-system-disk.sh \
	qemu/run-profile.sh \
	qemu/run-installed.sh \
	qemu/run-live.sh \
	qemu/smoke-live-usb.sh \
	qemu/smoke-installed.sh \
	qemu/smoke-service.sh
do
	if [ ! -x "$base/$path" ]; then
		echo "not executable: $path" >&2
		exit 1
	fi
done

for service in udev-trigger hwdrivers udev-settle; do
	grep -q "rc-update add $service sysinit" "$base/scripts/build-rootfs.sh" || {
		echo "Physical hardware coldplug service is not enabled: $service" >&2
		exit 1
	}
done

if ! grep -q '^CreateIPPPrinterQueues Driverless$' "$base/rootfs/etc/cups/cups-browsed.conf"; then
	echo "cups-browsed must create driverless IPP queues" >&2
	exit 1
fi

if ! grep -q '^CreateRemoteCUPSPrinterQueues No$' "$base/rootfs/etc/cups/cups-browsed.conf"; then
	echo "cups-browsed must not import arbitrary remote CUPS queues" >&2
	exit 1
fi

for package in cups cups-client cups-filters avahi avahi-tools; do
	if ! grep -q "^$package$" "$base/packages/world"; then
		echo "missing printer package: $package" >&2
		exit 1
	fi
done

if ! grep -q 'addgroup root lpadmin' "$base/scripts/build-rootfs.sh"; then
	echo "the Lazarus service account must be authorized for CUPS administration" >&2
	exit 1
fi

echo "Arcology Lazarus OS scaffold is complete."
