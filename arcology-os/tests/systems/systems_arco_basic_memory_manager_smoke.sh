#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-memory-manager-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/uefi_memory_manager.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry CountConventionalPages > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation MEMORY.Read32' "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation MEMORY.Read64' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry CountConventionalPages > "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.READ32' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.READ64' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/memory-manager.efi" --target uefi-x86_64 --entry CountConventionalPages >/dev/null
test -s "$TMP_ROOT/memory-manager.efi"
echo 'PASS: ArcoBASIC post-UEFI descriptor parsing compiles freestanding'
