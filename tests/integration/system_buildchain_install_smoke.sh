#!/usr/bin/env bash
set -euo pipefail

# Stages the system buildchain installer into a temporary prefix, then proves the installed tools
# can find their installed resources. Does not touch /usr/local.

SOURCE_DIR="$1"
BUILD_DIR="$2"

TMP_PREFIX="$(mktemp -d)"
TMP_WORK="$(mktemp -d)"
trap 'rm -rf "$TMP_PREFIX" "$TMP_WORK"' EXIT

SUDO= PREFIX="$TMP_PREFIX" BUILD_DIR="$BUILD_DIR" \
    "$SOURCE_DIR/scripts/install/install-system-buildchain.sh" >/tmp/arco-system-buildchain-install.log

"$TMP_PREFIX/bin/ArcoFission" --version | grep -q "ArcoFission alpha"
"$TMP_PREFIX/bin/fissure" status >/tmp/arco-installed-fissure-status.log

cp -r "$SOURCE_DIR/rivet/examples/tiny-project/." "$TMP_WORK/"
(
    cd "$TMP_WORK"
    "$TMP_PREFIX/bin/rivet" build --jobs 1 >/tmp/arco-installed-rivet-build.log
    test -x build/tinyapp
)

printf 'PRINT "installed-native-ok"\n' > "$TMP_WORK/hello.abas"
"$TMP_PREFIX/bin/ArcoFission" native "$TMP_WORK/hello.abas" -o "$TMP_WORK/hello" \
    >/tmp/arco-installed-arcofission-native.log
"$TMP_WORK/hello" | grep -q "installed-native-ok"

echo "system_buildchain_install_smoke: all checks passed"
