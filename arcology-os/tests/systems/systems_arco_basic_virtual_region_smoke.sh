#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-virtual-region-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/virtual_region_manager.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry VMAPAdd > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation MEMORY.Write64' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry VMAPAdd > "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.WRITE64' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/virtual-region.efi" --target uefi-x86_64 --entry VMAPAdd >/dev/null
test -s "$TMP_ROOT/virtual-region.efi"
echo 'PASS: ArcoBASIC virtual-region database policy compiles freestanding'
