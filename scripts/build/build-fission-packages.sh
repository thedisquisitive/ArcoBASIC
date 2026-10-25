#!/usr/bin/env bash
set -euo pipefail

# Builds distributable packages of the native ArcoBASIC compiler (Fission):
#
#   arcobasic-fission_<version>_amd64.deb               installs under /usr
#   arcobasic-fission-<version>-linux-x86_64.tar.gz     relocatable: unpack anywhere,
#                                                       put <dir>/bin on PATH
#
# Both contain `fission` (launcher), the native compiler, the compiler/runtime
# sources it compiles every program against, the ArcoBASIC standard library and a
# prebuilt runtime cache. Programs it builds are static executables, so they run
# anywhere without the package. The compiler comes from the committed seed
# (packaging/fission/seed); FISSION_SEED=/path overrides it.
#
# Usage: scripts/build/build-fission-packages.sh
#   VERSION=0.1.0 REVISION=1 OUT_DIR=dist FISSION_SEED=...

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
VERSION="${VERSION:-0.1.0}"
REVISION="${REVISION:-1}"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/dist}"
MAINTAINER="${MAINTAINER:-$(git -C "$REPO_ROOT" config user.name 2>/dev/null || echo ArcoBASIC) <$(git -C "$REPO_ROOT" config user.email 2>/dev/null || echo noreply@localhost)>}"
PACKAGE="arcobasic-fission"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail() { echo "build-fission-packages: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }
need tar; need gzip; need sha256sum
[[ "$(uname -m)" == "x86_64" ]] || fail "builds the x86-64 package only"

# Source date for reproducible archives: the last commit, else now.
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$REPO_ROOT" log -1 --format=%ct 2>/dev/null || date +%s)}"
export SOURCE_DATE_EPOCH

mkdir -p "$OUT_DIR"

# ---- stage the install tree once (verified seed, smoke test, prebuilt cache) ----
stage() {  # $1 = prefix inside the tree ("/usr" or "")
    local root="$1" prefix="$2"
    PREFIX="$prefix" DESTDIR="$root" SUDO= "$REPO_ROOT/scripts/install/install-fission.sh" >/dev/null
}

DEB_ROOT="$WORK/deb"
mkdir -p "$DEB_ROOT"
stage "$DEB_ROOT" /usr

# The launcher finds everything relative to itself, so the same tree works as a
# relocatable archive too: <dir>/bin, <dir>/lib, <dir>/share.
TGZ_NAME="${PACKAGE}-${VERSION}-linux-x86_64"
TGZ_ROOT="$WORK/$TGZ_NAME"
mkdir -p "$TGZ_ROOT"
cp -a "$DEB_ROOT/usr/." "$TGZ_ROOT/"

# Predictable modes regardless of the builder's umask.
find "$DEB_ROOT" "$TGZ_ROOT" -type d -exec chmod 0755 {} +
find "$DEB_ROOT" "$TGZ_ROOT" -type f -perm -u+x -exec chmod 0755 {} +
find "$DEB_ROOT" "$TGZ_ROOT" -type f ! -perm -u+x -exec chmod 0644 {} +

# ---- documentation common to both ----
write_readme() {
cat > "$1" <<EOF
ArcoBASIC native compiler (Fission) $VERSION

Compile an ArcoBASIC program to a standalone native Linux x86-64 executable:

    fission build hello.abas -o hello
    ./hello

    fission --help                 usage
    fission build X.abas --target bytecode|sir|amir     other outputs
    fission build X.abas --pipeline                     show the resolved route

Notes
  * #IMPORT "name" finds files next to the importing file, then ./stdlib, the
    installed ArcoBASIC standard library and \$ARCOBASIC_STDLIB.
  * #IMPORT "x" AS Alias is not supported yet.
  * Some library calls are not available natively; an unsupported call is
    reported when you build, naming the function.
  * Programs you build are static executables: they need neither this package
    nor any other runtime.
  * The first build may take a few seconds longer if the prebuilt runtime cache
    cannot be used; it is then rebuilt into \$FISSION_CACHE or
    ~/.cache/arcobasic/fission-runtime.
  * Linux x86-64 only.
EOF
}

# ---- tar.gz ----
write_readme "$TGZ_ROOT/README.txt"
cat > "$TGZ_ROOT/INSTALL.txt" <<'EOF'
Unpack anywhere and add its bin folder to PATH:

    tar xzf arcobasic-fission-*.tar.gz
    export PATH="$PWD/arcobasic-fission-*/bin:$PATH"     # or add it to your shell profile
    fission --version

To install system-wide instead, copy the contents of this folder into /usr/local
(sudo cp -a bin lib share /usr/local/); or use the .deb package.
EOF
TGZ="$OUT_DIR/$TGZ_NAME.tar.gz"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
    -C "$WORK" -cf - "$TGZ_NAME" | gzip -9n > "$TGZ"

# ---- .deb ----
if command -v dpkg-deb >/dev/null 2>&1; then
    DOC="$DEB_ROOT/usr/share/doc/$PACKAGE"
    mkdir -p "$DOC" "$DEB_ROOT/DEBIAN"
    write_readme "$DOC/README"
    cat > "$DOC/copyright" <<EOF
Copyright (c) The Disquisitive. All rights reserved.

License: Proprietary. This package is distributed for testing. Programs you
compile with it belong to you.
EOF
    chmod 0644 "$DOC/README" "$DOC/copyright"
    chmod 0755 "$DEB_ROOT/usr/share/doc" "$DOC" "$DEB_ROOT/DEBIAN"
    SIZE_KB="$(du -sk --exclude=DEBIAN "$DEB_ROOT" | cut -f1)"
    cat > "$DEB_ROOT/DEBIAN/control" <<EOF
Package: $PACKAGE
Version: $VERSION-$REVISION
Section: devel
Priority: optional
Architecture: amd64
Installed-Size: $SIZE_KB
Maintainer: $MAINTAINER
Depends: coreutils
Description: native ArcoBASIC compiler (Fission)
 Compiles ArcoBASIC programs to standalone native Linux x86-64 executables.
 Self-hosted: the compiler is itself written in ArcoBASIC. Includes the
 ArcoBASIC standard library and a prebuilt native runtime cache.
EOF
    (cd "$DEB_ROOT" && find usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums)
    DEB="$OUT_DIR/${PACKAGE}_${VERSION}-${REVISION}_amd64.deb"
    dpkg-deb --root-owner-group -Zgzip --build "$DEB_ROOT" "$DEB" >/dev/null
else
    echo "build-fission-packages: dpkg-deb not found; skipping the .deb" >&2
    DEB=""
fi

for f in "$TGZ" ${DEB:+"$DEB"}; do
    (cd "$OUT_DIR" && sha256sum "$(basename "$f")" >> SHA256SUMS.new)
done
sort -u -k2 "$OUT_DIR/SHA256SUMS.new" > "$OUT_DIR/SHA256SUMS" && rm -f "$OUT_DIR/SHA256SUMS.new"
echo "build-fission-packages: wrote"
ls -l "$TGZ" ${DEB:+"$DEB"} | awk '{print "  " $5 "  " $9}'
