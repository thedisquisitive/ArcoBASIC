#!/usr/bin/env bash
set -euo pipefail

# Builds ONE tester package containing the Geode IDE and the native ArcoBASIC
# compiler (Fission), as a .tar.gz and a .deb:
#
#   geode-ide-bundle-<ver>-linux-x86_64.tar.gz   unpack anywhere, run ./geode-ide
#   geode-ide-bundle_<ver>-<rev>_amd64.deb       /opt/geode-ide + /usr/bin/{geode-ide,fission}
#
# The IDE finds the bundled compiler by itself (its launcher puts arcobasic/bin on
# PATH), so the ArcoBASIC language pack works with nothing else installed.
#
# Inputs:
#   IDE_TARBALL   the Geode IDE tester archive
#                 (default: ~/projects/geode-ide/build/linux-tester/package/*.tar.gz,
#                  produced by `cpack --preset linux-tester` in the IDE repository)
# Usage: scripts/build/build-tester-bundle.sh
#   VERSION=0.1.0 REVISION=1 OUT_DIR=dist IDE_TARBALL=... FISSION_SEED=...

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
VERSION="${VERSION:-0.1.0}"
REVISION="${REVISION:-1}"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/dist}"
IDE_DIR="${IDE_DIR:-$HOME/projects/geode-ide}"
MAINTAINER="${MAINTAINER:-$(git -C "$REPO_ROOT" config user.name 2>/dev/null || echo ArcoBASIC) <$(git -C "$REPO_ROOT" config user.email 2>/dev/null || echo noreply@localhost)>}"
PACKAGE="geode-ide-bundle"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fail() { echo "build-tester-bundle: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || fail "required command not found: $1"; }
need tar; need gzip; need sha256sum
[[ "$(uname -m)" == "x86_64" ]] || fail "builds the x86-64 package only"

if [[ -z "${IDE_TARBALL:-}" ]]; then
    shopt -s nullglob
    candidates=("$IDE_DIR"/build/linux-tester/package/geode-ide-*-tester-linux-x86_64.tar.gz)
    shopt -u nullglob
    [[ ${#candidates[@]} -eq 1 ]] || fail "set IDE_TARBALL=<Geode IDE tester archive> (found ${#candidates[@]} candidates in $IDE_DIR/build/linux-tester/package)"
    IDE_TARBALL="${candidates[0]}"
fi
[[ -f "$IDE_TARBALL" ]] || fail "no such file: $IDE_TARBALL"

SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git -C "$REPO_ROOT" log -1 --format=%ct 2>/dev/null || date +%s)}"
export SOURCE_DATE_EPOCH
mkdir -p "$OUT_DIR"

# ---- the compiler (verified seed, prebuilt cache) ----
FISSION_OUT="$WORK/fission-out"
OUT_DIR="$FISSION_OUT" VERSION="$VERSION" REVISION="$REVISION" "$SCRIPT_DIR/build-fission-packages.sh" >/dev/null
FISSION_TGZ="$(ls "$FISSION_OUT"/arcobasic-fission-*-linux-x86_64.tar.gz)"
mkdir "$WORK/fission"
tar xzf "$FISSION_TGZ" -C "$WORK/fission"
FISSION_TREE="$(echo "$WORK"/fission/arcobasic-fission-*)"
[[ -x "$FISSION_TREE/bin/fission" ]] || fail "the compiler package has no bin/fission"

# ---- the IDE ----
mkdir "$WORK/ide"
tar xzf "$IDE_TARBALL" -C "$WORK/ide"
IDE_NAME="$(ls "$WORK/ide")"
IDE_TREE="$WORK/ide/$IDE_NAME"
[[ -x "$IDE_TREE/geode-ide" && -x "$IDE_TREE/geode-desktop" ]] || fail "unexpected IDE archive layout: $IDE_NAME"
[[ -d "$IDE_TREE/language-packs/arcobasic" ]] || fail "the IDE archive does not contain the ArcoBASIC language pack"
grep -q 'arcobasic/bin' "$IDE_TREE/geode-ide" || fail "the IDE launcher does not use a bundled arcobasic/bin; repackage the IDE from the current source"

# ---- assemble: IDE tree with the compiler in arcobasic/ ----
ROOT_NAME="geode-ide-bundle-$VERSION-linux-x86_64"
BUNDLE="$WORK/$ROOT_NAME"
cp -a "$IDE_TREE" "$BUNDLE"
mkdir "$BUNDLE/arcobasic"
cp -a "$FISSION_TREE/." "$BUNDLE/arcobasic/"
rm -f "$BUNDLE/arcobasic/INSTALL.txt"

cat > "$BUNDLE/README-BUNDLE.md" <<EOF
# Geode IDE + ArcoBASIC native compiler (tester bundle $VERSION)

Everything in one folder. Read README-TESTERS.md (the IDE) and TESTER-TERMS.md first.

    tar -xzf $ROOT_NAME.tar.gz
    cd $ROOT_NAME
    ./geode-ide

The launcher puts \`arcobasic/bin\` (the Fission compiler) on PATH for the IDE, so
ArcoBASIC works out of the box:

1. **Languages** -> install ArcoBASIC.
2. **New** -> *Hello, ArcoBASIC* -> **Create project** -> **Run** (F5).
3. **Export app** produces a native program that runs without the IDE or the compiler.

To use the compiler from a terminal too:

    export PATH="\$PWD/arcobasic/bin:\$PATH"
    fission build hello.abas -o hello && ./hello

See arcobasic/README.txt for the compiler. Linux x86-64 only. The tester terms are a
placeholder in this build.
EOF

# Predictable modes regardless of the builder's umask.
find "$BUNDLE" -type d -exec chmod 0755 {} +
find "$BUNDLE" -type f -perm -u+x -exec chmod 0755 {} +
find "$BUNDLE" -type f ! -perm -u+x -exec chmod u+rw,go+r,go-w {} +

# ---- tar.gz ----
TGZ="$OUT_DIR/$ROOT_NAME.tar.gz"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
    -C "$WORK" -cf - "$ROOT_NAME" | gzip -9n > "$TGZ"

# ---- .deb ----
DEB=""
if command -v dpkg-deb >/dev/null 2>&1; then
    DEB_ROOT="$WORK/deb"
    mkdir -p "$DEB_ROOT/opt" "$DEB_ROOT/usr/bin" "$DEB_ROOT/usr/share/applications" \
             "$DEB_ROOT/usr/share/icons/hicolor/scalable/apps" "$DEB_ROOT/usr/share/doc/$PACKAGE" "$DEB_ROOT/DEBIAN"
    cp -a "$BUNDLE" "$DEB_ROOT/opt/geode-ide"
    ln -s /opt/geode-ide/geode-ide "$DEB_ROOT/usr/bin/geode-ide"
    ln -s /opt/geode-ide/arcobasic/bin/fission "$DEB_ROOT/usr/bin/fission"
    cp "$BUNDLE/geode-ide.svg" "$DEB_ROOT/usr/share/icons/hicolor/scalable/apps/geode-ide.svg"
    printf '%s\n' '[Desktop Entry]' 'Type=Application' 'Name=Geode IDE (tester build)' \
        'Comment=Visual-first hybrid IDE: build apps from connected Geodes' 'Exec=/usr/bin/geode-ide %f' \
        'Icon=geode-ide' 'Terminal=false' 'Categories=Development;IDE;' \
        > "$DEB_ROOT/usr/share/applications/geode-ide.desktop"
    cp "$BUNDLE/README-BUNDLE.md" "$DEB_ROOT/usr/share/doc/$PACKAGE/README"
    cp "$BUNDLE/TESTER-TERMS.md" "$DEB_ROOT/usr/share/doc/$PACKAGE/TESTER-TERMS.md"
    printf '%s\n' "Copyright $(date -u -d "@$SOURCE_DATE_EPOCH" +%Y) The Disquisitive. All rights reserved." '' \
        'License: Proprietary. Distributed for testing under the terms in TESTER-TERMS.md.' \
        'Third-party notices are in /opt/geode-ide/LICENSES.' > "$DEB_ROOT/usr/share/doc/$PACKAGE/copyright"
    printf '%s\n' "$PACKAGE ($VERSION-$REVISION) unstable; urgency=low" '' \
        '  * Geode IDE tester build bundled with the native ArcoBASIC compiler (Fission).' '' \
        " -- $MAINTAINER  $(date -u -R -d "@$SOURCE_DATE_EPOCH")" | gzip -9n > "$DEB_ROOT/usr/share/doc/$PACKAGE/changelog.Debian.gz"
    find "$DEB_ROOT" -type d -exec chmod 0755 {} +
    find "$DEB_ROOT/usr/share" -type f -exec chmod 0644 {} +
    SIZE_KB="$(du -sk --exclude=DEBIAN "$DEB_ROOT" | cut -f1)"
    printf '%s\n' "Package: $PACKAGE" "Version: $VERSION-$REVISION" 'Section: devel' 'Priority: optional' \
        'Architecture: amd64' "Installed-Size: $SIZE_KB" "Maintainer: $MAINTAINER" \
        'Depends: libc6 (>= 2.38)' \
        'Recommends: libvulkan1, libx11-6, g++' \
        'Conflicts: arcobasic-fission' 'Replaces: arcobasic-fission' 'Provides: arcobasic-fission' \
        'Description: Geode IDE tester build with the native ArcoBASIC compiler' \
        ' Visual-first hybrid IDE (Generation 0 tester build) together with the' \
        ' self-hosted native ArcoBASIC compiler (Fission), so ArcoBASIC Geodes build' \
        ' and export as standalone Linux executables out of the box.' \
        ' Needs a Vulkan-capable GPU and an X11 or Xwayland session.' > "$DEB_ROOT/DEBIAN/control"
    (cd "$DEB_ROOT" && find opt usr -type f -print0 | sort -z | xargs -0 md5sum > DEBIAN/md5sums)
    DEB="$OUT_DIR/${PACKAGE}_${VERSION}-${REVISION}_amd64.deb"
    dpkg-deb --root-owner-group -Zxz -z6 --build "$DEB_ROOT" "$DEB" >/dev/null
else
    echo "build-tester-bundle: dpkg-deb not found; skipping the .deb" >&2
fi

: > "$OUT_DIR/SHA256SUMS.bundle"
for f in "$TGZ" ${DEB:+"$DEB"}; do (cd "$OUT_DIR" && sha256sum "$(basename "$f")" >> SHA256SUMS.bundle); done
touch "$OUT_DIR/SHA256SUMS"
cat "$OUT_DIR/SHA256SUMS" "$OUT_DIR/SHA256SUMS.bundle" | sort -u -k2 > "$OUT_DIR/SHA256SUMS.new"
mv "$OUT_DIR/SHA256SUMS.new" "$OUT_DIR/SHA256SUMS"
rm -f "$OUT_DIR/SHA256SUMS.bundle"
echo "build-tester-bundle: wrote"
ls -l "$TGZ" ${DEB:+"$DEB"} | awk '{print "  " $5 "  " $9}'
