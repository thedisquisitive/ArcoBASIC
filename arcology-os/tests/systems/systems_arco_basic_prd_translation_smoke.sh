#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/physical-hardware-readiness/prd-translation.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at AST --entry Main > "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/amir.txt"
grep -qF 'PRDTranslateFirmwareMap' "$TMP_ROOT/ast.txt"
grep -qF 'CALL PRDTranslateFirmwareMap' "$TMP_ROOT/amir.txt"
echo 'PASS: firmware descriptor translation is represented in ArcoBASIC'
