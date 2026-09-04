#!/usr/bin/env bash
# Builds real, standalone native capsules for every arco3d test into arco3d/build/ (via
# `ArcoFission native`), plus a plain arcosh symlink for ad-hoc interactive use. Run from
# anywhere; it locates itself. Safe to rerun any time -- rebuilds ArcoFission/arcosh first if
# either is missing, then recompiles every capsule fresh.
#
# Each resulting arco3d/build/<name> is a genuine ELF64 executable with the ArcoBASIC bytecode and
# a bytecode VM embedded directly in it -- no arcosh, no ArcoFission, no ArcoBASIC toolchain of any
# kind needs to be installed to run it afterward. This *is* possible today: getting here required
# fixing four real, distinct compiler bugs (all in this same session) that made every nontrivial
# CLASS-based ArcoBASIC program fail at bytecode-VM runtime despite compiling without error --
# see arco3d/README.md for what each one was.
#
# REPO_BUILD_DIR defaults to a real CMAKE_BUILD_TYPE=Release configuration (-O3), not the
# no-build-type-set `build/` most of the repo's own docs mention -- an unoptimized capsule embeds
# an unoptimized bytecode VM (this is compiled-in runtime code, not something ArcoFission's own
# build type could paper over), and the difference is not subtle: the exact same benchmark script
# measured 2m15s unoptimized vs. 5.1s at -O3, a ~26x difference, on this machine. Override with
# REPO_BUILD_DIR=/path/to/build if you specifically need an unoptimized/debuggable capsule for
# troubleshooting the compiler itself.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
REPO_BUILD_DIR="${REPO_BUILD_DIR:-$REPO_ROOT/build-release}"

NPROC="$(nproc 2>/dev/null || echo 4)"
if [ ! -x "$REPO_BUILD_DIR/arcosh" ] || [ ! -x "$REPO_BUILD_DIR/ArcoFission" ]; then
    echo "arco3d/build.sh: building arcosh + ArcoFission into $REPO_BUILD_DIR (first run only) ..."
    cmake -S "$REPO_ROOT" -B "$REPO_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$REPO_BUILD_DIR" --target arcosh ArcoFission -j"$NPROC"
fi
# The lean/no-GUI/no-libcurl runtime ArcoFission prefers linking native capsules against
# (see fission.cpp's `prefer_lean_runtime`/`native_core_link_dependencies`) is its own
# EXCLUDE_FROM_ALL CMake target -- ordinary `cmake --build` never touches it, so a native capsule
# would otherwise silently link against a stale copy after any change here. Confirmed the hard
# way: this is exactly the gap that made arco3d's own ExitTheProgram fix appear not to work at
# first, purely because this rebuild step didn't exist yet.
if grep -q "ArcoFissionCapsuleCoreProbe" "$REPO_BUILD_DIR/CMakeCache.txt" 2>/dev/null; then
    cmake --build "$REPO_BUILD_DIR" --target ArcoFissionCapsuleCoreProbe -j"$NPROC"
fi

mkdir -p "$SCRIPT_DIR/build"
ln -sf "$REPO_BUILD_DIR/arcosh" "$SCRIPT_DIR/build/arcosh"

cd "$SCRIPT_DIR"
for test_file in tests/*.abas; do
    name="$(basename "$test_file" .abas)"
    echo "==> arco3d/build/$name"
    "$REPO_BUILD_DIR/ArcoFission" native "$test_file" -o "build/$name"
done

# The interactive asset-creation tool itself, as a real standalone capsule too -- not just
# runnable through arcosh. Same `ArcoFission native` path as the tests above; the only difference
# is this one links the full GUI/GTK/GLFW dependency set (see `ldd`) instead of the lean headless
# runtime the tests prefer, since it actually opens a window.
echo "==> arco3d/build/arco3d"
"$REPO_BUILD_DIR/ArcoFission" native arco3d.abas -o "build/arco3d"
