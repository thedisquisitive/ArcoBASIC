#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-prd-runtime-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/physical_region_database_runtime.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry PRDAddRegion > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation MEMORY.Write64' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry PRDAddRegion > "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.WRITE64' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.READ32' "$TMP_ROOT/amir.txt"
grep -qF 'PRDAllocateFirstFree' "$TMP_ROOT/amir.txt"
grep -qF 'PRDReleaseRegion' "$TMP_ROOT/amir.txt"
grep -qF 'PRDCoalescePair' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/prd-runtime.efi" --target uefi-x86_64 --entry PRDAddRegion >/dev/null
test -s "$TMP_ROOT/prd-runtime.efi"
echo 'PASS: ArcoBASIC persistent PRD record layout compiles freestanding'
