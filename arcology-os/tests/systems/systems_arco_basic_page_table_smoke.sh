#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-arco-basic-page-table-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/stdlib/address_space_policy.abas"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry SwitchAddressSpace > "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.WRITECR3' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.READCR3' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry SwitchAddressSpace > "$TMP_ROOT/x86.txt"
grep -qF '0f 22 d8' "$TMP_ROOT/x86.txt"
grep -qF '0f 20 d8' "$TMP_ROOT/x86.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry InvalidateVirtualPage > "$TMP_ROOT/invlpg-amir.txt"
grep -qF 'MEMORY.INVLPG' "$TMP_ROOT/invlpg-amir.txt"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry InvalidateVirtualPage > "$TMP_ROOT/invlpg-x86.txt"
grep -qF '0f 01 38' "$TMP_ROOT/invlpg-x86.txt"
echo 'PASS: ArcoBASIC address-space policy lowers CR3 and INVLPG mechanisms'
