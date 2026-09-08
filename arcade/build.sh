#!/usr/bin/env bash
# Builds ARCADE's own capsule into arcade/build/ through the project buildchain:
#
#   Fissure probe -> Rivet build graph -> ArcoFission native capsule
#
# Run from the repository root or anywhere else; the script locates itself. Native always builds
# through that chain. The optional web capsule still builds directly with ArcoFission when
# ARCOFISSION_WEB_TOOLCHAIN_DIR is set because Rivet's current fission adapter models only the
# native capsule action.
#
# Defaults to a CMAKE_BUILD_TYPE=Release ArcoFission (-O3), not a plain no-build-type-set `build/`
# one -- an unoptimized capsule embeds an unoptimized bytecode VM (compiled-in runtime code, not
# something ArcoFission's own build type can paper over): a real benchmark measured 2m15s
# unoptimized vs. 5.1s at -O3 on one machine, a ~26x difference (see arco3d/README.md, where this
# was first measured -- this script's own convention was carried forward from the deleted
# arcoflow/build.sh prototype, which established it first). Set ARCOFISSION=/path/to/it to use a
# different one (e.g. an unoptimized build kept around for debugging the compiler itself).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ARCOFISSION="${ARCOFISSION:-$REPO_ROOT/build-release/ArcoFission}"
RIVET="${RIVET:-$REPO_ROOT/build/rivet/rivet}"
FISSURE="${FISSURE:-$REPO_ROOT/build/fissure/fissure}"
ARCOBASIC_STDLIB="${ARCOBASIC_STDLIB:-$REPO_ROOT/stdlib}"

if [ ! -x "$ARCOFISSION" ]; then
    echo "arcade/build.sh: $ARCOFISSION not found -- build the ArcoFission target first" >&2
    echo "  (cmake -S $REPO_ROOT -B $REPO_ROOT/build-release -DCMAKE_BUILD_TYPE=Release &&" >&2
    echo "   cmake --build $REPO_ROOT/build-release --target ArcoFission), or set ARCOFISSION=/path/to/it" >&2
    exit 1
fi

if [ ! -x "$RIVET" ]; then
    echo "arcade/build.sh: $RIVET not found -- build the rivet target first" >&2
    echo "  (cmake --build $REPO_ROOT/build --target rivet), or set RIVET=/path/to/it" >&2
    exit 1
fi

if [ ! -x "$FISSURE" ]; then
    echo "arcade/build.sh: $FISSURE not found -- build the fissure target first" >&2
    echo "  (cmake --build $REPO_ROOT/build --target fissure), or set FISSURE=/path/to/it" >&2
    exit 1
fi

echo "==> native: arcade/build/arcade (Fissure -> Rivet -> ArcoFission)"
(
    cd "$SCRIPT_DIR"
    ARCOBASIC_STDLIB="$ARCOBASIC_STDLIB" ARCOFISSION_PATH="$ARCOFISSION" RIVET_PATH="$RIVET" "$FISSURE" run --full
)

if [ -n "${ARCOFISSION_WEB_TOOLCHAIN_DIR:-}" ]; then
    mkdir -p "$SCRIPT_DIR/build/web"
    echo "==> web: arcade/build/web/arcade.html"
    ARCOBASIC_STDLIB="$ARCOBASIC_STDLIB" "$ARCOFISSION" native "$SCRIPT_DIR/arcade.abas" -o "$SCRIPT_DIR/build/web/arcade.html" --target web
else
    echo "==> skipping web capsule (ARCOFISSION_WEB_TOOLCHAIN_DIR not set; see docs/arcofission.md)"
fi

echo "==> run it:"
echo "      ARCOFISSION_PATH=\"$ARCOFISSION\" \\"
echo "      WAYLAND_DISPLAY= XDG_SESSION_TYPE=x11 DISPLAY=:1 \\"
echo "        \"$SCRIPT_DIR/build/arcade\" [path/to/file.abas | path/to/project.arcoproj]"
