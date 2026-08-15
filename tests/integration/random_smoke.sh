#!/usr/bin/env bash
set -euo pipefail

ARCO_CLI="$1"
ARCOFISSION="$2"
SOURCE_DIR="$3"
FIXTURE="$SOURCE_DIR/tests/fixtures/random/deterministic.abas"
EXPECTED="$SOURCE_DIR/tests/fixtures/random/expected-output.txt"
TMP_ROOT="${TMPDIR:-/tmp}/arcobasic-random-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCO_CLI" "$FIXTURE" > "$TMP_ROOT/interpreter.txt"
diff -u "$EXPECTED" "$TMP_ROOT/interpreter.txt"

"$ARCOFISSION" compile-run "$FIXTURE" > "$TMP_ROOT/compile-run.txt"
diff -u "$EXPECTED" "$TMP_ROOT/compile-run.txt"

"$ARCOFISSION" bytecode "$FIXTURE" -o "$TMP_ROOT/random.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/random.arcof" > "$TMP_ROOT/bytecode.txt"
diff -u "$EXPECTED" "$TMP_ROOT/bytecode.txt"

"$ARCOFISSION" native "$FIXTURE" -o "$TMP_ROOT/random-native" > /dev/null
"$TMP_ROOT/random-native" > "$TMP_ROOT/native.txt"
diff -u "$EXPECTED" "$TMP_ROOT/native.txt"

echo "PASS: deterministic random interpreter and hosted capsules"
