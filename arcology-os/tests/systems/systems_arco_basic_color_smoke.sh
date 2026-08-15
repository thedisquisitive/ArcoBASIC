#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-color-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/graphics_color.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry ColorRGB > "$TMP_ROOT/ast.txt"
grep -qF 'Function ColorRGB AS U32' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry ColorRGB > "$TMP_ROOT/amir.txt"
grep -qF 'INT.MUL' "$TMP_ROOT/amir.txt"
grep -qF 'INT.ADD' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/graphics-color.efi" --target uefi-x86_64 --entry ColorRGB >/dev/null
test -s "$TMP_ROOT/graphics-color.efi"
echo 'PASS: ArcoBASIC canonical RGB/BGR color packing compiles freestanding'
