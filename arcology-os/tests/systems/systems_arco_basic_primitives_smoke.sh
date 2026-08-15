#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-primitives-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/graphics_primitives.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry FillRectClipped > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation MEMORY.Write32' "$TMP_ROOT/ast.txt"
grep -qF 'HardwareSemantic CPU.MemoryBarrier' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry FillRectClipped > "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.WRITE32' "$TMP_ROOT/amir.txt"
grep -qF 'CPU.MEMORYBARRIER' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/graphics-primitives.efi" --target uefi-x86_64 --entry FillRectClipped >/dev/null
test -s "$TMP_ROOT/graphics-primitives.efi"
echo 'PASS: ArcoBASIC clipped graphics primitives compile through freestanding lowering'
