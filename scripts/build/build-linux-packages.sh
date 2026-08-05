#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR_FROM_ENV="${BUILD_DIR:-}"
BUILD_DIR="${BUILD_DIR:-}"
OUT_DIR="${OUT_DIR:-$SOURCE_DIR/dist}"
PACKAGE="${PACKAGE:-arcobasic}"
VERSION="${VERSION:-}"
MAINTAINER="${MAINTAINER:-Daedalus <daedalus@localhost>}"
SUMMARY="${SUMMARY:-ArcoBASIC language tools and ArcoSH shell}"
LICENSE="${LICENSE:-Proprietary}"
XBPS_BINDIR="${XBPS_BINDIR:-}"
PACKAGE_PROFILE="${PACKAGE_PROFILE:-full}"

usage() {
    cat <<USAGE
ArcoBASIC Linux package builder

Usage:
  scripts/build/build-linux-packages.sh [--all] [--deb] [--rpm] [--tar] [--xbps-src] [--headless] [--full] [--out DIR]

Outputs:
  --deb    Debian package for Debian, Ubuntu, Mint, Pop!_OS, etc.
  --rpm    RPM package for Fedora, RHEL, openSUSE, Mageia, etc. Requires rpmbuild.
  --tar    Portable tar.gz install tree for any Linux distro.
  --xbps-src
           Void Linux xbps-src package definition archive.
           For release .xbps packages, use scripts/build/build-void-native-package.sh.
  --headless
           Build core tools without GUI/libcurl dynamic dependencies.
  --full   Build with optional GUI/network backends when dependencies are found. Default.
  --all    Build every supported format available on this host. Default.

Environment:
  PACKAGE      Package name, default: arcobasic
  VERSION      Package version, default: CMake project version
  BUILD_DIR    CMake build directory, default: ./build
  OUT_DIR      Output directory, default: ./dist
  MAINTAINER   Package maintainer text
  LICENSE      RPM license field, default: Proprietary
  PACKAGE_PROFILE
               full or headless, default: full
USAGE
}

want_deb=0
want_rpm=0
want_xbps=0
want_tar=0
want_xbps_src=0
explicit=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)
            want_deb=1
            want_rpm=1
            want_xbps=1
            want_tar=1
            want_xbps_src=1
            explicit=1
            ;;
        --deb)
            want_deb=1
            explicit=1
            ;;
        --rpm)
            want_rpm=1
            explicit=1
            ;;
        --xbps)
            echo "build-linux-packages: --xbps was removed as a release path." >&2
            echo "Use scripts/build/build-void-native-package.sh so Void packages are built natively through xbps-src." >&2
            exit 2
            ;;
        --tar|--tar.gz|--portable)
            want_tar=1
            explicit=1
            ;;
        --xbps-src|--void)
            want_xbps_src=1
            explicit=1
            ;;
        --headless|--core)
            PACKAGE_PROFILE="headless"
            ;;
        --full)
            PACKAGE_PROFILE="full"
            ;;
        --out)
            shift
            if [[ $# -eq 0 ]]; then
                echo "build-linux-packages: --out needs a directory" >&2
                exit 2
            fi
            OUT_DIR="$1"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "build-linux-packages: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ "$explicit" -eq 0 ]]; then
    want_deb=1
    want_rpm=1
    want_tar=1
    want_xbps_src=1
fi

if [[ -z "$BUILD_DIR" ]]; then
    BUILD_DIR="$SOURCE_DIR/build/package-$PACKAGE_PROFILE"
elif [[ -z "$BUILD_DIR_FROM_ENV" && "$BUILD_DIR" == "$SOURCE_DIR/build" ]]; then
    BUILD_DIR="$SOURCE_DIR/build/package-$PACKAGE_PROFILE"
fi

if [[ -z "$VERSION" ]]; then
    VERSION="$(sed -n 's/^project(ArcoBASIC VERSION \([^ ]*\).*/\1/p' "$SOURCE_DIR/CMakeLists.txt" | head -n 1)"
fi
if [[ -z "$VERSION" ]]; then
    VERSION="0.1.0"
fi

host_arch="$(uname -m)"
deb_arch="$host_arch"
if command -v dpkg >/dev/null 2>&1; then
    deb_arch="$(dpkg --print-architecture)"
else
    case "$host_arch" in
        x86_64) deb_arch="amd64" ;;
        aarch64|arm64) deb_arch="arm64" ;;
    esac
fi

rpm_arch="$host_arch"
case "$host_arch" in
    amd64) rpm_arch="x86_64" ;;
    arm64) rpm_arch="aarch64" ;;
esac

xbps_arch="$host_arch"
case "$host_arch" in
    amd64) xbps_arch="x86_64" ;;
    arm64) xbps_arch="aarch64" ;;
esac

need_command() {
    local command_name="$1"
    local feature="$2"
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "build-linux-packages: skipping $feature; missing command: $command_name" >&2
        return 1
    fi
}

find_command() {
    local command_name="$1"
    local candidate
    if command -v "$command_name" >/dev/null 2>&1; then
        command -v "$command_name"
        return 0
    fi
    if [[ -n "$XBPS_BINDIR" && -x "$XBPS_BINDIR/$command_name" ]]; then
        printf '%s\n' "$XBPS_BINDIR/$command_name"
        return 0
    fi
    for candidate in \
        "$HOME/xbps/void-packages/masterdir-$(uname -m)/usr/bin/$command_name" \
        "$HOME/xbps/void-packages/masterdir-x86_64/usr/bin/$command_name" \
        "$HOME/void-packages/masterdir-$(uname -m)/usr/bin/$command_name" \
        "$HOME/void-packages/masterdir-x86_64/usr/bin/$command_name"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

stage_install() {
    local prefix="$1"
    cmake --install "$BUILD_DIR" --prefix "$prefix/usr"
    find "$prefix/usr/bin" -type f -exec chmod 0755 {} +
    find "$prefix/usr/share" -type f -exec chmod 0644 {} +
    find "$prefix/usr/share/arcobasic/scripts" -type f -name "*.sh" -exec chmod 0755 {} + 2>/dev/null || true
}

configure_build() {
    local cmake_args=()
    case "$PACKAGE_PROFILE" in
        full)
            cmake_args+=("-DARCO_ENABLE_GUI=ON" "-DARCO_ENABLE_NETWORK=ON")
            ;;
        headless)
            cmake_args+=("-DARCO_ENABLE_GUI=OFF" "-DARCO_ENABLE_NETWORK=OFF")
            ;;
        *)
            echo "build-linux-packages: PACKAGE_PROFILE must be full or headless" >&2
            exit 2
            ;;
    esac
    cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" "${cmake_args[@]}"
}

build_deb() {
    need_command dpkg-deb ".deb" || return 0

    local stage_root pkg_root installed_size deb
    stage_root="$(mktemp -d)"
    pkg_root="$stage_root/${PACKAGE}_${VERSION}_${deb_arch}"
    mkdir -p "$pkg_root/DEBIAN"
    stage_install "$pkg_root"

    installed_size="$(du -sk "$pkg_root/usr" | awk '{print $1}')"
    cat > "$pkg_root/DEBIAN/control" <<CONTROL
Package: $PACKAGE
Version: $VERSION
Section: shells
Priority: optional
Architecture: $deb_arch
Maintainer: $MAINTAINER
Depends: libc6, libstdc++6
Installed-Size: $installed_size
Description: $SUMMARY
 ArcoBASIC is a readable BASIC-family scripting language.
 ArcoSH is an ArcoBASIC-powered shell intended for sysadmin workflows,
 profile scripting, tutorials, and interactive automation.
CONTROL

    chmod 0755 "$pkg_root/DEBIAN"
    deb="$OUT_DIR/${PACKAGE}_${VERSION}_${deb_arch}.deb"
    dpkg-deb --build --root-owner-group "$pkg_root" "$deb"
    rm -rf "$stage_root"
    echo "$deb"
}

build_tarball() {
    need_command tar "portable tar.gz" || return 0

    local stage_root install_root tarball
    stage_root="$(mktemp -d)"
    install_root="$stage_root/${PACKAGE}-${VERSION}-linux-${host_arch}"
    mkdir -p "$install_root"
    stage_install "$install_root"
    cat > "$install_root/README.install" <<README
ArcoBASIC portable Linux package

Run tools directly from:
  usr/bin/arcosh
  usr/bin/arco_cli
  usr/bin/ArcoFission

Optional local install:
  sudo cp -a usr/* /usr/

For desktop icon/MIME integration, prefer the .deb or .rpm package when one is
available for your distro.
README
    tarball="$OUT_DIR/${PACKAGE}-${VERSION}-linux-${host_arch}.tar.gz"
    tar -C "$stage_root" -czf "$tarball" "$(basename "$install_root")"
    rm -rf "$stage_root"
    echo "$tarball"
}

build_rpm() {
    need_command rpmbuild ".rpm" || return 0

    local rpmtop spec install_root rpm
    rpmtop="$(mktemp -d /tmp/arco-rpmbuild.XXXXXX)"
    mkdir -p "$rpmtop/BUILD" "$rpmtop/BUILDROOT" "$rpmtop/RPMS" "$rpmtop/SOURCES" "$rpmtop/SPECS" "$rpmtop/SRPMS" "$rpmtop/TMP" "$rpmtop/rpmdb"
    install_root="$rpmtop/install-root"
    mkdir -p "$install_root"
    stage_install "$install_root"

    spec="$rpmtop/SPECS/${PACKAGE}.spec"
    cat > "$spec" <<SPEC
Name:           $PACKAGE
Version:        $VERSION
Release:        1%{?dist}
Summary:        $SUMMARY
License:        $LICENSE
BuildArch:      $rpm_arch

%description
ArcoBASIC is a readable BASIC-family scripting language.
ArcoSH is an ArcoBASIC-powered shell intended for sysadmin workflows,
profile scripting, tutorials, and interactive automation.

%install
mkdir -p %{buildroot}
cp -a "$install_root"/* %{buildroot}/

%files
/usr/bin/arcosh
/usr/bin/arco_cli
/usr/bin/ArcoFission
/usr/share/arcobasic
/usr/share/arcosh
/usr/share/applications/arconaut.desktop
/usr/share/mime/packages/application-x-arcobasic.xml
/usr/share/icons/hicolor/512x512/apps/arconaut.png
/usr/share/icons/hicolor/512x512/mimetypes/application-x-arcobasic.png
/usr/share/pixmaps/arconaut.png
/usr/share/doc/arcobasic
SPEC

    rpmbuild \
        --define "_topdir $rpmtop" \
        --define "_tmppath $rpmtop/TMP" \
        --define "_dbpath $rpmtop/rpmdb" \
        --define "_rpmlock_path $rpmtop/rpmdb/.rpm.lock" \
        -bb "$spec"
    rpm="$(find "$rpmtop/RPMS" -type f -name '*.rpm' | head -n 1)"
    if [[ -n "$rpm" ]]; then
        cp "$rpm" "$OUT_DIR/"
        echo "$OUT_DIR/$(basename "$rpm")"
    fi
    rm -rf "$rpmtop"
}

build_xbps_src() {
    local srcpkg_root template archive
    srcpkg_root="$OUT_DIR/xbps-src/srcpkgs/${PACKAGE}"
    rm -rf "$srcpkg_root"
    mkdir -p "$srcpkg_root"

    template="$srcpkg_root/template"
    cat > "$template" <<TEMPLATE
# Template file for '$PACKAGE'
pkgname=$PACKAGE
version=$VERSION
revision=1
hostmakedepends="cmake pkg-config bsdtar"
makedepends="libstdc++-devel libcurl-devel glfw-devel gtk+3-devel pango-devel cairo-devel MesaLib-devel glu-devel"
short_desc="$SUMMARY"
maintainer="$MAINTAINER"
license="$LICENSE"
homepage="https://example.invalid/arcobasic"

# Local alpha packaging:
# Use scripts/build/build-void-native-package.sh to create a release .xbps package
# from a local source tarball through xbps-src.
#
# When ArcoBASIC has a public source tarball, add:
# distfiles="https://.../arcobasic-\${version}.tar.gz"
# checksum="..."

do_build() {
	cmake -S . -B build \\
		-DCMAKE_BUILD_TYPE=Release \\
		-DCMAKE_INSTALL_PREFIX=/usr \\
		-DARCO_ENABLE_GUI=ON \\
		-DARCO_ENABLE_NETWORK=ON
	cmake --build build
}

do_check() {
	ctest --test-dir build --output-on-failure -R 'arcosh_alpha_smoke|arcofission_alpha_smoke'
}

do_install() {
	DESTDIR="\$DESTDIR" cmake --install build
}
TEMPLATE

    cat > "$srcpkg_root/README.md" <<'README'
# ArcoBASIC xbps-src package definition

Copy this package directory into a Void Linux `void-packages` checkout:

\`\`\`sh
cp -a srcpkgs/arcobasic /path/to/void-packages/srcpkgs/
cd /path/to/void-packages
./xbps-src pkg arcobasic
\`\`\`

For a real release `.xbps`, use:

\`\`\`sh
scripts/build/build-void-native-package.sh --void-packages /path/to/void-packages --out dist/void
\`\`\`

That path builds inside Void's xbps-src environment so dependencies such as
GLFW, GTK, libcurl, and libstdc++ are detected and recorded by XBPS.
README

    archive="$OUT_DIR/${PACKAGE}-${VERSION}-xbps-src.tar.gz"
    tar -C "$OUT_DIR/xbps-src" -czf "$archive" srcpkgs
    echo "$archive"
}

command -v cmake >/dev/null 2>&1 || {
    echo "build-linux-packages: cmake is required" >&2
    exit 1
}

mkdir -p "$OUT_DIR"
configure_build
cmake --build "$BUILD_DIR"

outputs=()
append_outputs() {
    local output
    output="$("$@")"
    if [[ -n "$output" ]]; then
        while IFS= read -r line; do
            outputs+=("$line")
        done <<< "$output"
    fi
}

if [[ "$want_deb" -eq 1 ]]; then
    append_outputs build_deb
fi
if [[ "$want_tar" -eq 1 ]]; then
    append_outputs build_tarball
fi
if [[ "$want_rpm" -eq 1 ]]; then
    append_outputs build_rpm
fi
if [[ "$want_xbps_src" -eq 1 ]]; then
    append_outputs build_xbps_src
fi

if [[ "${#outputs[@]}" -eq 0 ]]; then
    echo "build-linux-packages: no packages were built" >&2
    exit 1
fi

printf '%s\n' "${outputs[@]}"
