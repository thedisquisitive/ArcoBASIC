#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/physical-hardware-readiness/descriptor-loads.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry LoadDescriptorTables > "$TMP_ROOT/amir.txt"
grep -qF 'LGDT' "$TMP_ROOT/amir.txt"
grep -qF 'LIDT' "$TMP_ROOT/amir.txt"
grep -qF 'LTR' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry LoadDescriptorTables > "$TMP_ROOT/x86.txt"
grep -qF '0f 01 10' "$TMP_ROOT/x86.txt"
grep -qF '0f 01 18' "$TMP_ROOT/x86.txt"
grep -qF '66 0f 00 d8' "$TMP_ROOT/x86.txt"
echo 'PASS: GDT/IDT/TR descriptor mechanisms have exact lowering'
