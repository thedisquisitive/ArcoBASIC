#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-region-policy-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/physical_region_database.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry ValidateRegion > "$TMP_ROOT/ast.txt"
grep -qF 'Function ValidateRegion AS BOOL' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry RegionsOverlap > "$TMP_ROOT/amir.txt"
grep -qF 'INT.MUL' "$TMP_ROOT/amir.txt"
grep -qF 'INT.CMP' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/region-policy.efi" --target uefi-x86_64 --entry ValidateRegion >/dev/null
test -s "$TMP_ROOT/region-policy.efi"
echo 'PASS: ArcoBASIC physical-region validation policy compiles freestanding'
