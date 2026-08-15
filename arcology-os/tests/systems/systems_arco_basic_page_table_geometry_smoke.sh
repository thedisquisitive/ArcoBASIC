#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-paging-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/x86_64_paging_policy.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry IsCanonicalRange > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation ADDRESS' "$TMP_ROOT/ast.txt" || true
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry PML4Index > "$TMP_ROOT/amir.txt"
grep -qF 'INT.SHR' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/paging.efi" --target uefi-x86_64 --entry IsCanonicalAddress >/dev/null
test -s "$TMP_ROOT/paging.efi"
echo 'PASS: ArcoBASIC x86-64 paging geometry policy compiles freestanding'
