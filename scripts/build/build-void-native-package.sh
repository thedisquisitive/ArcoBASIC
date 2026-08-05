#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
VOID_PACKAGES="${VOID_PACKAGES:-$HOME/xbps/void-packages}"
VERSION="${VERSION:-}"
PACKAGE="${PACKAGE:-arcobasic}"
ARCH="${ARCH:-x86_64}"
OUT_DIR="${OUT_DIR:-$SOURCE_DIR/dist/void}"
ALLOW_RESTRICTED="${ALLOW_RESTRICTED:-yes}"

if [[ -z "$VERSION" ]]; then
    VERSION="$(sed -n 's/^project(ArcoBASIC VERSION \([^ ]*\).*/\1/p' "$SOURCE_DIR/CMakeLists.txt" | head -n 1)"
fi
if [[ -z "$VERSION" ]]; then
    VERSION="0.1.0"
fi

usage() {
    cat <<USAGE
Build a native Void Linux XBPS package through xbps-src.

Usage:
  scripts/build/build-void-native-package.sh [--void-packages DIR] [--out DIR] [--arch ARCH]

Environment:
  VOID_PACKAGES      void-packages checkout, default: ~/xbps/void-packages
  OUT_DIR            artifact output directory, default: ./dist/void
  VERSION            package version, default: CMake project version
  ARCH               xbps-src host arch, default: x86_64
  ALLOW_RESTRICTED   set XBPS_ALLOW_RESTRICTED, default: yes
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --void-packages)
            shift
            VOID_PACKAGES="${1:?--void-packages needs a directory}"
            ;;
        --out)
            shift
            OUT_DIR="${1:?--out needs a directory}"
            ;;
        --arch)
            shift
            ARCH="${1:?--arch needs an architecture}"
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "build-void-native-package: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

if [[ ! -x "$VOID_PACKAGES/xbps-src" ]]; then
    echo "build-void-native-package: xbps-src not found in $VOID_PACKAGES" >&2
    exit 1
fi

MASTERDIR="$VOID_PACKAGES/masterdir-$ARCH"
if [[ ! -d "$MASTERDIR" ]]; then
    echo "build-void-native-package: expected masterdir not found: $MASTERDIR" >&2
    echo "Run: cd $(printf '%q' "$VOID_PACKAGES") && ./xbps-src -A $ARCH binary-bootstrap" >&2
    exit 1
fi

DISTFILE="$PACKAGE-$VERSION.tar.gz"
LOCAL_SOURCE_DIR="$VOID_PACKAGES/hostdir/sources/$PACKAGE-$VERSION"
SRC_PKG_DIR="$VOID_PACKAGES/srcpkgs/$PACKAGE"
TEMPLATE_SOURCE="$SOURCE_DIR/packaging/void/srcpkgs/$PACKAGE/template"
WRAP_DIR="${TMPDIR:-/tmp}/arco-xbps-wrappers-$ARCH"

if [[ ! -f "$TEMPLATE_SOURCE" ]]; then
    echo "build-void-native-package: template not found: $TEMPLATE_SOURCE" >&2
    exit 1
fi

mkdir -p "$SOURCE_DIR/dist/source" "$LOCAL_SOURCE_DIR" "$SRC_PKG_DIR" "$OUT_DIR"
rm -f "$SOURCE_DIR/dist/source/$DISTFILE"

tar \
    --exclude='./.git' \
    --exclude='./.agents' \
    --exclude='./.claude' \
    --exclude='./.codex' \
    --exclude='./build' \
    --exclude='./build-*' \
    --exclude='./dist' \
    --exclude='./var' \
    --exclude='./lazarus' \
    --exclude='./lazarus-os' \
    --exclude='./arcoalpha' \
    --exclude='./arcoalpha.zip' \
    --exclude='./packaging/void' \
    --exclude='./arcocompy-save.acpy' \
    --exclude='./arcocompydb-customer.acdb' \
    --exclude='./arco-file-log-example.txt' \
    --exclude='./*.arcodb' \
    --transform="s,^./,$PACKAGE-$VERSION/," \
    -czf "$SOURCE_DIR/dist/source/$DISTFILE" \
    -C "$SOURCE_DIR" .

CHECKSUM="$(sha256sum "$SOURCE_DIR/dist/source/$DISTFILE" | awk '{print $1}')"
cp "$SOURCE_DIR/dist/source/$DISTFILE" "$LOCAL_SOURCE_DIR/$DISTFILE"

cp "$TEMPLATE_SOURCE" "$SRC_PKG_DIR/template"
sed -i \
    -e "s/^version=.*/version=$VERSION/" \
    -e "s/^checksum=.*/checksum=$CHECKSUM/" \
    "$SRC_PKG_DIR/template"

rm -rf "$WRAP_DIR"
mkdir -p "$WRAP_DIR"
for tool in "$MASTERDIR"/usr/bin/xbps-*; do
    name="$(basename "$tool")"
    {
        printf '#!/usr/bin/env bash\n'
        printf 'export LD_LIBRARY_PATH=%q${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}\n' "$MASTERDIR/usr/lib"
        printf 'exec %q "$@"\n' "$tool"
    } > "$WRAP_DIR/$name"
    chmod +x "$WRAP_DIR/$name"
done

(
    cd "$VOID_PACKAGES"
    PATH="$WRAP_DIR:$MASTERDIR/usr/bin:$PATH" \
    XBPS_ALLOW_RESTRICTED="$ALLOW_RESTRICTED" \
    ./xbps-src -A "$ARCH" pkg "$PACKAGE"
)

PKG="$VOID_PACKAGES/hostdir/binpkgs/${PACKAGE}-${VERSION}_1.${ARCH}.xbps"
REPO="$VOID_PACKAGES/hostdir/binpkgs/${ARCH}-repodata"
if [[ ! -f "$PKG" ]]; then
    echo "build-void-native-package: expected package not found: $PKG" >&2
    exit 1
fi

cp "$PKG" "$OUT_DIR/"
if [[ -f "$REPO" ]]; then
    cp "$REPO" "$OUT_DIR/"
fi

echo "$OUT_DIR/$(basename "$PKG")"
if [[ -f "$OUT_DIR/$(basename "$REPO")" ]]; then
    echo "$OUT_DIR/$(basename "$REPO")"
fi
