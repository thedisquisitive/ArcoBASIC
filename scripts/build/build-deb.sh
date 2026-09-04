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

INSTALLED_SIZE="$(du -sk "$PKG_ROOT/usr" | awk '{print $1}')"
cat > "$PKG_ROOT/DEBIAN/control" <<CONTROL
Package: $PACKAGE
Version: $VERSION
Section: shells
Priority: optional
Architecture: $ARCH
Maintainer: $MAINTAINER
Depends: libc6, libstdc++6, libfuse3-4 | libfuse3-3, fuse3, udev, udisks2, libglfw3, libcairo2, libpango-1.0-0, libpangocairo-1.0-0, libgtk-3-0 | libgtk-3-0t64, libcurl4
Installed-Size: $INSTALLED_SIZE
Description: ArcoBASIC language tools and ArcoSH shell
 ArcoBASIC is a readable BASIC-family scripting language.
 ArcoSH is an ArcoBASIC-powered shell intended for sysadmin workflows,
 profile scripting, tutorials, and interactive automation.
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
