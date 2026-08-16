#!/usr/bin/env bash
set -euo pipefail

# Exercises ArcoFission's Windows capsule target end to end: cross-compiles arco_compiler/
# arco_runtime with mingw-w64, builds a Windows capsule from a small ArcoBASIC script, confirms
# the result is a real PE32+ x86-64 executable, and -- when Wine is available -- actually runs it
# and checks the output. Skips cleanly (not a failure) when the mingw-w64 cross-compiler isn't
# installed, matching the convention used by systems_qemu_ovmf_harness_smoke.sh for other
# optional-toolchain coverage.

ARCOFISSION="$1"
SOURCE_DIR="$2"
CMAKE="${3:-cmake}"

if ! command -v x86_64-w64-mingw32-g++ > /dev/null 2>&1; then
    echo "SKIP: x86_64-w64-mingw32-g++ is not installed; install g++-mingw-w64-x86-64 to exercise the Windows capsule target."
    exit 0
fi

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-windows-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

WIN_BUILD="$TMP_ROOT/build-windows"
"$CMAKE" -S "$SOURCE_DIR" -B "$WIN_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$SOURCE_DIR/cmake/toolchains/mingw-w64-x86_64.cmake" \
    -DCMAKE_BUILD_TYPE=Release > "$TMP_ROOT/configure.log" 2>&1
"$CMAKE" --build "$WIN_BUILD" --target arco_compiler -j"$(nproc)" > "$TMP_ROOT/build.log" 2>&1

cat > "$TMP_ROOT/hello.abas" <<'SCRIPT'
LET count = 2 + 3
PRINT "hello windows capsule"
PRINT count * 10
SCRIPT

ARCOFISSION_WINDOWS_TOOLCHAIN_DIR="$WIN_BUILD" \
    "$ARCOFISSION" native "$TMP_ROOT/hello.abas" -o "$TMP_ROOT/hello.exe" --target windows-x86_64 \
    > "$TMP_ROOT/native.log" 2>&1
# build_native_windows_bytecode (fission.cpp) only prints this after its own is_pe64_file check
# (MZ header, PE signature, IMAGE_FILE_MACHINE_AMD64) passes, so this is already a real structural
# check, not just a process-exit-code one.
grep -q "PE32+ WRITTEN" "$TMP_ROOT/native.log"

if command -v file > /dev/null 2>&1; then
    file "$TMP_ROOT/hello.exe" | grep -q "PE32+ executable.*x86-64"
fi

if command -v wine > /dev/null 2>&1; then
    # msvcrt's text-mode stdio writes CRLF line endings, same as any native Windows console
    # program; normalize before comparing rather than treating that as a bug.
    ACTUAL="$(timeout 30 wine "$TMP_ROOT/hello.exe" 2>/dev/null | tr -d '\r' || true)"
    EXPECTED="hello windows capsule
50"
    if [ "$ACTUAL" != "$EXPECTED" ]; then
        echo "Windows capsule produced unexpected output under Wine:"
        echo "--- expected ---"
        echo "$EXPECTED"
        echo "--- actual ---"
        echo "$ACTUAL"
        exit 1
    fi
else
    echo "NOTE: wine is not installed; verified the capsule builds as a valid PE32+ executable but did not execute it."
fi
