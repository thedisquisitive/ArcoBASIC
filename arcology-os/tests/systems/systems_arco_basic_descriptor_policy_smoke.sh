#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/physical-hardware-readiness/descriptor-table-policy.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at AST --entry ActivateTables > "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry ActivateTables > "$TMP_ROOT/amir.txt"
grep -qF 'CALL ActivateDescriptorTables' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" build "$ROOT/stdlib/descriptor_table_policy.abas" -o "$TMP_ROOT/tables.efi" --target uefi-x86_64 --entry ActivateDescriptorTables >/dev/null
test -s "$TMP_ROOT/tables.efi"
echo 'PASS: APS descriptor-table policy compiles as freestanding ArcoBASIC'
