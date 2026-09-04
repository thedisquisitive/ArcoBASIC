#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build/package-arcfs}"
OUT_DIR="${OUT_DIR:-$SOURCE_DIR/dist}"
PACKAGE="${PACKAGE:-arcobasic-arcfs}"
VERSION="${VERSION:-}"
ARCH="${ARCH:-$(dpkg --print-architecture 2>/dev/null || uname -m)}"
MAINTAINER="${MAINTAINER:-Daedalus <daedalus@localhost>}"

if [[ -z "$VERSION" ]]; then
    VERSION="$(sed -n 's/^project(ArcoBASIC VERSION \([^ ]*\).*/\1/p' "$SOURCE_DIR/CMakeLists.txt" | head -n 1)"
fi
if [[ -z "$VERSION" ]]; then
    VERSION="0.1.0"
fi

command -v cmake >/dev/null 2>&1 || {
    echo "build-arcfs-deb: cmake is required" >&2
    exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
    echo "build-arcfs-deb: dpkg-deb is required" >&2
    exit 1
}

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DARCO_ENABLE_GUI=ON \
    -DARCO_ENABLE_NETWORK=OFF
cmake --build "$BUILD_DIR"

STAGE_ROOT="$(mktemp -d)"
trap 'rm -rf "$STAGE_ROOT"' EXIT

FULL_ROOT="$STAGE_ROOT/full"
PKG_ROOT="$STAGE_ROOT/${PACKAGE}_${VERSION}_${ARCH}"
mkdir -p "$FULL_ROOT" "$PKG_ROOT/DEBIAN" "$OUT_DIR"

cmake --install "$BUILD_DIR" --prefix "$FULL_ROOT/usr"

install -Dm0755 "$FULL_ROOT/usr/bin/arcfs-linux" "$PKG_ROOT/usr/bin/arcfs-linux"
install -Dm0755 "$FULL_ROOT/usr/bin/arcfsctl" "$PKG_ROOT/usr/bin/arcfsctl"
install -Dm0755 "$FULL_ROOT/usr/bin/arconaut" "$PKG_ROOT/usr/bin/arconaut"
install -Dm0755 "$FULL_ROOT/usr/sbin/mount.arcfs" "$PKG_ROOT/usr/sbin/mount.arcfs"
install -Dm0755 "$FULL_ROOT/usr/sbin/mkfs.arcfs" "$PKG_ROOT/usr/sbin/mkfs.arcfs"
install -Dm0644 "$FULL_ROOT/usr/lib/udev/rules.d/61-arcfs.rules" "$PKG_ROOT/usr/lib/udev/rules.d/61-arcfs.rules"
install -Dm0644 "$FULL_ROOT/usr/share/applications/arconaut.desktop" "$PKG_ROOT/usr/share/applications/arconaut.desktop"
install -Dm0644 "$FULL_ROOT/usr/share/icons/hicolor/512x512/apps/arconaut.png" "$PKG_ROOT/usr/share/icons/hicolor/512x512/apps/arconaut.png"
install -Dm0644 "$FULL_ROOT/usr/share/pixmaps/arconaut.png" "$PKG_ROOT/usr/share/pixmaps/arconaut.png"
install -Dm0644 "$SOURCE_DIR/docs/arcfs-linux.md" "$PKG_ROOT/usr/share/doc/$PACKAGE/arcfs-linux.md"
install -Dm0644 "$SOURCE_DIR/docs/arcfsctl.md" "$PKG_ROOT/usr/share/doc/$PACKAGE/arcfsctl.md"
install -Dm0644 "$SOURCE_DIR/docs/arconaut.md" "$PKG_ROOT/usr/share/doc/$PACKAGE/arconaut.md"
install -Dm0644 "$SOURCE_DIR/tools/arcfsctl.abas" "$PKG_ROOT/usr/share/arcobasic/tools/arcfsctl.abas"
install -Dm0644 "$SOURCE_DIR/examples/arconaut.abas" "$PKG_ROOT/usr/share/arcobasic/examples/arconaut.abas"

INSTALLED_SIZE="$(du -sk "$PKG_ROOT/usr" | awk '{print $1}')"
cat > "$PKG_ROOT/DEBIAN/control" <<CONTROL
Package: $PACKAGE
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Maintainer: $MAINTAINER
Depends: libc6, libstdc++6, libfuse3-4 | libfuse3-3, fuse3, udev, udisks2, libglfw3, libcairo2, libpango-1.0-0, libpangocairo-1.0-0, libgtk-3-0 | libgtk-3-0t64
Installed-Size: $INSTALLED_SIZE
Description: ArcFS host support and Arconaut administrator
 ArcFS host support installs the Linux FUSE runtime, mount and mkfs helpers,
 udev discovery rules for desktop storage stacks, and the ArcoFission-built
 arcfsctl and Arconaut ArcoBASIC capsules for managing ArcFS support,
 devices, partitions, images, maintenance tasks, and plugins.
CONTROL

cat > "$PKG_ROOT/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e

if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload || true
    udevadm trigger --subsystem-match=block || true
fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl try-reload-or-restart udisks2.service >/dev/null 2>&1 || true
fi

exit 0
POSTINST

cat > "$PKG_ROOT/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e

if command -v udevadm >/dev/null 2>&1; then
    udevadm control --reload || true
    udevadm trigger --subsystem-match=block || true
fi
if command -v systemctl >/dev/null 2>&1; then
    systemctl try-reload-or-restart udisks2.service >/dev/null 2>&1 || true
fi

exit 0
POSTRM

chmod 0755 "$PKG_ROOT/DEBIAN" "$PKG_ROOT/DEBIAN/postinst" "$PKG_ROOT/DEBIAN/postrm"
find "$PKG_ROOT" -type d -exec chmod 0755 {} +

DEB="$OUT_DIR/${PACKAGE}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$PKG_ROOT" "$DEB"

echo "$DEB"
