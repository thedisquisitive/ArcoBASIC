#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-allocator-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/uefi_memory_manager.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry AllocatePagesFromRegion > "$TMP_ROOT/ast.txt"
grep -qF 'Function AllocatePagesFromRegion AS U64' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry AllocatePagesFromRegion > "$TMP_ROOT/amir.txt"
grep -qF 'INT.MUL' "$TMP_ROOT/amir.txt"
grep -qF 'INT.SUB' "$TMP_ROOT/amir.txt"
grep -qF 'Function AdvancePageAllocation AS U64' "$TMP_ROOT/ast.txt"
grep -qF 'Function IsPageRangeValid AS BOOL' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/allocator.efi" --target uefi-x86_64 --entry AllocatePagesFromRegion >/dev/null
test -s "$TMP_ROOT/allocator.efi"
echo 'PASS: ArcoBASIC checked page-region allocation policy compiles freestanding'
