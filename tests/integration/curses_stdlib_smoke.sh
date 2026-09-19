#!/usr/bin/env bash
set -euo pipefail

# Regression coverage for stdlib/curses.abas's own pure logic (tests/stdlib/curses_selftest.abas) --
# escape-sequence decoding, menu-item normalization/filtering, and the exact scrolling-window shape
# that once triggered a real native-backend miscompilation (Entry 35,
# .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md; see also that file's own dedicated regression case in
# tests/integration/linux_native_backend_smoke.sh). Run on both the bytecode VM and the native
# backend and diffed against each other, the same "never trust native alone" discipline every other
# entry in this test suite follows.

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/curses-stdlib-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

SELFTEST="$SOURCE_DIR/tests/stdlib/curses_selftest.abas"

"$ARCOFISSION" compile-run "$SELFTEST" > "$TMP_ROOT/bytecode-run.txt"
grep -q "^all curses stdlib tests passed$" "$TMP_ROOT/bytecode-run.txt"

"$ARCOFISSION" build "$SELFTEST" -o "$TMP_ROOT/curses_selftest_native" --target linux-x86_64 > /dev/null
"$TMP_ROOT/curses_selftest_native" > "$TMP_ROOT/native-run.txt"
grep -q "^all curses stdlib tests passed$" "$TMP_ROOT/native-run.txt"

diff -u "$TMP_ROOT/bytecode-run.txt" "$TMP_ROOT/native-run.txt"
