#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-region-allocator-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/physical_region_allocator.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry CanCoalesce > "$TMP_ROOT/ast.txt"
grep -qF 'Function CanReserveRegion AS BOOL' "$TMP_ROOT/ast.txt"
grep -qF 'Function CanSplitRegion AS BOOL' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry CanCoalesce > "$TMP_ROOT/amir.txt"
grep -qF 'INT.CMP' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/region-allocator.efi" --target uefi-x86_64 --entry CanCoalesce >/dev/null
test -s "$TMP_ROOT/region-allocator.efi"
echo 'PASS: ArcoBASIC region reservation/split/coalesce policy compiles freestanding'
