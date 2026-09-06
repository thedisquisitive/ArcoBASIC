#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$SOURCE_DIR/build}"
OUT_DIR="${OUT_DIR:-$SOURCE_DIR/dist}"
PACKAGE="${PACKAGE:-arcobasic}"
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
    echo "build-deb: cmake is required" >&2
    exit 1
}
command -v dpkg-deb >/dev/null 2>&1 || {
    echo "build-deb: dpkg-deb is required" >&2
    exit 1
}

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR"

STAGE_ROOT="$(mktemp -d)"
trap 'rm -rf "$STAGE_ROOT"' EXIT

PKG_ROOT="$STAGE_ROOT/${PACKAGE}_${VERSION}_${ARCH}"
mkdir -p "$PKG_ROOT/DEBIAN" "$OUT_DIR"

cmake --install "$BUILD_DIR" --prefix "$PKG_ROOT/usr"

# ArcFS host support (arcfs-linux, arcfsctl, Arconaut, mount.arcfs/mkfs.arcfs, the udev rule, and
# Arconaut's own desktop entry/icon) ships exclusively in the arcobasic-arcfs package built by
# build-arcfs-deb.sh. cmake --install above pulls all of it in here too since those targets are
# unconditional on Linux; strip them so the two .debs don't both claim the same paths and collide
# under dpkg -i when installed together.
rm -f \
    "$PKG_ROOT/usr/bin/arcfs-linux" \
    "$PKG_ROOT/usr/bin/arcfsctl" \
    "$PKG_ROOT/usr/bin/arconaut" \
    "$PKG_ROOT/usr/sbin/mount.arcfs" \
    "$PKG_ROOT/usr/sbin/mkfs.arcfs" \
    "$PKG_ROOT/usr/lib/udev/rules.d/61-arcfs.rules" \
    "$PKG_ROOT/usr/share/applications/arconaut.desktop" \
    "$PKG_ROOT/usr/share/icons/hicolor/512x512/apps/arconaut.png" \
    "$PKG_ROOT/usr/share/pixmaps/arconaut.png" \
    "$PKG_ROOT/usr/share/arcobasic/tools/arcfsctl.abas" \
    "$PKG_ROOT/usr/share/arcobasic/examples/arconaut.abas"
find "$PKG_ROOT/usr" -type d -empty -delete

INSTALLED_SIZE="$(du -sk "$PKG_ROOT/usr" | awk '{print $1}')"
cat > "$PKG_ROOT/DEBIAN/control" <<CONTROL
Package: $PACKAGE
Version: $VERSION
Section: shells
Priority: optional
Architecture: $ARCH
Maintainer: $MAINTAINER
Depends: libc6, libstdc++6, libglfw3, libcairo2, libpango-1.0-0, libpangocairo-1.0-0, libgtk-3-0 | libgtk-3-0t64, libcurl4
Recommends: arcobasic-arcfs
Installed-Size: $INSTALLED_SIZE
Description: ArcoBASIC language tools
 ArcoBASIC is a readable BASIC-family scripting language.
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

chmod 0755 "$PKG_ROOT/DEBIAN"
chmod 0755 "$PKG_ROOT/DEBIAN/postinst" "$PKG_ROOT/DEBIAN/postrm"
find "$PKG_ROOT/usr/bin" -type f -exec chmod 0755 {} +
find "$PKG_ROOT/usr/share" -type f -exec chmod 0644 {} +
find "$PKG_ROOT/usr/share/arcobasic/scripts" -type f -name "*.sh" -exec chmod 0755 {} + 2>/dev/null || true

DEB="$OUT_DIR/${PACKAGE}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$PKG_ROOT" "$DEB"

echo "$DEB"
