#!/usr/bin/env bash
set -euo pipefail

# Installs the Arcology Fission Compiler Substrate as the native ArcoBASIC
# compiler:
#
#   $PREFIX/bin/fission                           launcher (sets FISSION_HOME and
#                                                 ARCOBASIC_STDLIB relative to itself)
#   $PREFIX/lib/arcobasic/fission-native          the native compiler (x86-64 ELF)
#   $PREFIX/share/arcobasic/fission/              compiler + native runtime sources
#   $PREFIX/share/arcobasic/stdlib/               the ArcoBASIC standard library
#   $PREFIX/share/arcobasic/.rivet/fission-runtime/   prebuilt native runtime cache
#
# Usage:  fission build FILE.abas -o OUT [--target linux-x86_64|bytecode|sir|amir]
#
# The compiler binary comes from, in order:
#   FISSION_SEED=/path/to/fission-native   an explicit compiler binary
#   BOOTSTRAP=1                            a full bootstrap from the legacy compiler
#                                          (ArcoFission, built from this repository)
#   otherwise the verified seed committed at packaging/fission/seed/ (seconds)
# REBUILD=1 then recompiles the compiler from this checkout's sources with that
# binary and installs the result (stage 2 must reproduce stage 1 byte for byte
# unless SKIP_FIXPOINT=1). The seed is refreshed with
# scripts/build/refresh-fission-seed.sh.
#
# Defaults to /usr/local. Override with PREFIX=..., BUILD_DIR=..., DESTDIR=...,
# SUDO=doas. PREFIX=$HOME/.local needs no privileges.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"
STAGE_DIR="$(mktemp -d)"
trap 'rm -rf "$STAGE_DIR"' EXIT

if [[ -z "${SUDO+x}" ]]; then
    if [[ -n "$DESTDIR" || -w "$(dirname "$DESTDIR$PREFIX")" || "${EUID:-$(id -u)}" -eq 0 ]]; then
        SUDO=()
    else
        SUDO=(sudo)
    fi
else
    # shellcheck disable=SC2206
    SUDO=($SUDO)
fi

fail() {
    echo "install-fission: $*" >&2
    exit 1
}

[[ "$(uname -s)" == "Linux" && "$(uname -m)" == "x86_64" ]] || fail "native compilation targets Linux x86-64 only"

# ---- 1. the compiler binary ------------------------------------------------
SEED_DIR="$REPO_ROOT/packaging/fission/seed"
SEED_BINARY="$SEED_DIR/fission-native-linux-x86_64"

full_bootstrap() {
    G0="$BUILD_DIR/ArcoFission"
    if [[ ! -x "$G0" ]]; then
        echo "install-fission: building the bootstrap compiler (ArcoFission)"
        cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
        cmake --build "$BUILD_DIR" --target ArcoFission -j"$(nproc 2>/dev/null || echo 4)"
    fi
    echo "install-fission: stage 0 - legacy compiler builds the Fission driver (bytecode VM)"
    "$G0" build fission/cli/fission_build.abas -o "$STAGE_DIR/stage0"
    echo "install-fission: stage 1 - native compiler built by stage 0 (several minutes)"
    "$STAGE_DIR/stage0" build fission/cli/fission_build.abas -o "$STAGE_DIR/stage1"
    NATIVE="$STAGE_DIR/stage1"
}

verified_seed() {
    [[ -f "$SEED_BINARY" && -f "$SEED_BINARY.sha256" ]] || fail "no committed seed at $SEED_BINARY (use BOOTSTRAP=1)"
    local want got
    want="$(cut -d' ' -f1 "$SEED_BINARY.sha256")"
    got="$(sha256sum "$SEED_BINARY" | cut -d' ' -f1)"
    [[ "$want" == "$got" ]] || fail "seed checksum mismatch (expected $want, got $got)"
    cp "$SEED_BINARY" "$STAGE_DIR/seed"
    chmod 0755 "$STAGE_DIR/seed"
    NATIVE="$STAGE_DIR/seed"
}

cd "$REPO_ROOT"
if [[ -n "${FISSION_SEED:-}" ]]; then
    [[ -x "$FISSION_SEED" ]] || fail "FISSION_SEED is not executable: $FISSION_SEED"
    NATIVE="$FISSION_SEED"
elif [[ "${BOOTSTRAP:-0}" == "1" ]]; then
    full_bootstrap
    REBUILD=1   # the fixpoint check below needs a second stage
else
    verified_seed
    echo "install-fission: using the verified seed compiler"
fi

if [[ "${REBUILD:-0}" == "1" ]]; then
    echo "install-fission: recompiling the compiler from this checkout (several minutes)"
    "$NATIVE" build fission/cli/fission_build.abas -o "$STAGE_DIR/rebuilt1"
    if [[ "${SKIP_FIXPOINT:-0}" != "1" ]]; then
        "$STAGE_DIR/rebuilt1" build fission/cli/fission_build.abas -o "$STAGE_DIR/rebuilt2"
        cmp -s "$STAGE_DIR/rebuilt1" "$STAGE_DIR/rebuilt2" || fail "bootstrap fixpoint failed: consecutive generations differ"
        echo "install-fission: fixpoint verified (consecutive generations are byte-identical)"
    fi
    NATIVE="$STAGE_DIR/rebuilt1"
fi

# ---- 2. stage the install tree -----------------------------------------------
TREE="$STAGE_DIR/tree"
mkdir -p "$TREE/bin" "$TREE/lib/arcobasic" "$TREE/share/arcobasic/fission" "$TREE/share/arcobasic/stdlib"
install -m 0755 "$NATIVE" "$TREE/lib/arcobasic/fission-native"

# Compiler and runtime sources; the tests, examples and Rivet build files are
# development material and are not needed to compile a program.
(cd "$REPO_ROOT/fission" && tar cf - --exclude=./tests --exclude=./build --exclude=./examples .) |
    (cd "$TREE/share/arcobasic/fission" && tar xf -)
cp "$REPO_ROOT"/stdlib/*.abas "$TREE/share/arcobasic/stdlib/"

cat > "$TREE/bin/fission" <<'LAUNCHER'
#!/bin/sh
# Launcher for the Fission native ArcoBASIC compiler: finds its sources and the
# standard library relative to its own location, so the install can be moved.
self=$(readlink -f "$0")
prefix=$(dirname "$(dirname "$self")")
: "${FISSION_HOME:=$prefix/share/arcobasic}"
: "${ARCOBASIC_STDLIB:=$prefix/share/arcobasic/stdlib}"
export FISSION_HOME ARCOBASIC_STDLIB
case "${1:-}" in
    --version|version) echo "fission (Arcology Fission Compiler Substrate) - native ArcoBASIC compiler"; exit 0 ;;
    --help|-h|help)
        echo "usage: fission build FILE [-o OUTPUT] [--target linux-x86_64|bytecode|sir|amir] [--language NAME] [--pipeline]"
        echo "  compiles an ArcoBASIC program to a standalone native executable (default target: linux-x86_64)"
        exit 0 ;;
esac
exec "$prefix/lib/arcobasic/fission-native" "$@"
LAUNCHER
chmod 0755 "$TREE/bin/fission"

# ---- 3. prebuilt runtime cache + smoke test, before anything is installed ----
SMOKE="$STAGE_DIR/smoke.abas"
cat > "$SMOKE" <<'EOF'
PRINT "fission-ok " + String(6 * 7)
EOF
( cd "$STAGE_DIR" && FISSION_HOME="$TREE/share/arcobasic" ARCOBASIC_STDLIB="$TREE/share/arcobasic/stdlib" \
    "$TREE/lib/arcobasic/fission-native" build "$SMOKE" -o "$STAGE_DIR/smoke" >/dev/null )
[[ "$("$STAGE_DIR/smoke")" == "fission-ok 42" ]] || fail "smoke test failed: the staged compiler produced a wrong program"
[[ -f "$TREE/share/arcobasic/.rivet/fission-runtime/runtime.frag" ]] || fail "the runtime cache was not prebuilt"

# ---- 4. install ---------------------------------------------------------------
"${SUDO[@]}" mkdir -p "$DESTDIR$PREFIX"
"${SUDO[@]}" cp -R "$TREE/." "$DESTDIR$PREFIX/"
echo "install-fission: installed to $DESTDIR$PREFIX"
echo "install-fission: try:  $PREFIX/bin/fission build hello.abas -o hello && ./hello"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "install-fission: note: $PREFIX/bin is not on PATH" ;; esac
