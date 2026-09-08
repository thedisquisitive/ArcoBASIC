#!/usr/bin/env bash
set -euo pipefail

# End-to-end smoke for ARCADE's buildchain conversion. The native capsule must be produced by:
#
#   arcade/build.sh -> fissure run --full -> RegisterCommandProbe -> rivet build -> ArcoFission native
#
# The copied project keeps this test isolated from a developer's local arcade/.fissure/.rivet/build
# state while still using the real built tools from this checkout.

FISSURE="$(readlink -f "$1")"
RIVET="$(readlink -f "$2")"
ARCOFISSION="$(readlink -f "$3")"
SOURCE_DIR="$(readlink -f "$4")"

TMP_ROOT="${TMPDIR:-/tmp}/arcade-buildchain-smoke-$$"
PROJECT_DIR="$TMP_ROOT/project"
mkdir -p "$PROJECT_DIR"
trap 'rm -rf "$TMP_ROOT"' EXIT

cp -r "$SOURCE_DIR/arcade" "$PROJECT_DIR/"
cd "$PROJECT_DIR/arcade"
rm -rf build .fissure .rivet

echo "=== arcade build.sh (Fissure -> Rivet -> ArcoFission) ==="
FISSURE="$FISSURE" RIVET="$RIVET" ARCOFISSION="$ARCOFISSION" ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" ./build.sh | tee buildchain.log
grep -q "\[FISSURE\] impact:" buildchain.log
grep -q "arcade.rivet-build -- RUN" buildchain.log
test -x build/arcade

echo "=== direct rivet build 2 (cache hit) ==="
ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" ARCOFISSION_PATH="$ARCOFISSION" "$RIVET" build --jobs 1 | tee rivet-cache.log
grep -q "\[CACHE\]   arcade.abas" rivet-cache.log
if grep -q "\[COMPILE\]" rivet-cache.log; then
    echo "FAIL: second Rivet build recompiled ARCADE despite no changes" >&2
    exit 1
fi

echo "arcade_buildchain_smoke: all checks passed"
