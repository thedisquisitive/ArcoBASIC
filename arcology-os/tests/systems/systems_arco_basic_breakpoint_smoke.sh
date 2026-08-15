#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/physical-hardware-readiness/breakpoint.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry TriggerBreakpoint > "$TMP_ROOT/amir.txt"
grep -qF 'BREAKPOINT' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry TriggerBreakpoint > "$TMP_ROOT/x86.txt"
grep -qF 'cc' "$TMP_ROOT/x86.txt"
echo 'PASS: CPU.Breakpoint lowers canonically to INT3'
