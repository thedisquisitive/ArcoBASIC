#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-substrate-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/graphics-substrate/graphics-substrate.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry Main > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation MEMORY.Write32' "$TMP_ROOT/ast.txt"
grep -qF 'HardwareSemantic CPU.MemoryBarrier' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.MAPDEVICE' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.WRITE32' "$TMP_ROOT/amir.txt"
grep -qF 'CPU.MEMORYBARRIER' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/graphics-substrate.efi" --target uefi-x86_64 --entry Main >/dev/null
test -s "$TMP_ROOT/graphics-substrate.efi"
"$ARCOFISSION" reveal "$SOURCE_DIR/arcology-os/stdlib/graphics_substrate.abas" at X86_64 --entry FillMappedSurface > "$TMP_ROOT/library.x86.txt"
grep -qF '48 8b 84 24' "$TMP_ROOT/library.x86.txt"
echo 'PASS: ArcoBASIC freestanding substrate source compiles through PE32+ UEFI lowering'

CALL_FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/graphics-substrate/graphics-substrate-call.abas"
"$ARCOFISSION" reveal "$CALL_FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/call.amir.txt"
grep -qF 'CALL FillMappedSurface' "$TMP_ROOT/call.amir.txt"
"$ARCOFISSION" reveal "$CALL_FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/call.x86.txt"
grep -qF 'INTERNAL_CALLS 1' "$TMP_ROOT/call.x86.txt"
"$ARCOFISSION" build "$CALL_FIXTURE" -o "$TMP_ROOT/graphics-substrate-call.efi" --target uefi-x86_64 --entry Main >/dev/null
test -s "$TMP_ROOT/graphics-substrate-call.efi"
echo 'PASS: freestanding internal ArcoBASIC call and multi-function PE emission'
