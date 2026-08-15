#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/physical-hardware-readiness/read-cr2.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry ReadFaultAddress > "$TMP_ROOT/amir.txt"
grep -qF 'READCR2' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry ReadFaultAddress > "$TMP_ROOT/x86.txt"
grep -qF '0f 20 d0' "$TMP_ROOT/x86.txt"
echo 'PASS: CPU.ReadCR2 has canonical A-MIR and x86-64 lowering'
