#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/physical-hardware-readiness/prd-reservation-split.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at AST --entry Main > "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/amir.txt"
grep -qF 'PRDReserveRange' "$TMP_ROOT/ast.txt"
grep -qF 'CALL PRDReserveRange' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$ROOT/stdlib/physical_region_database_runtime.abas" -o "$TMP_ROOT/prd.efi" --target uefi-x86_64 --entry PRDReserveRange >/dev/null
test -s "$TMP_ROOT/prd.efi"
echo 'PASS: PRD bootstrap reservations can split a free descriptor transactionally'
