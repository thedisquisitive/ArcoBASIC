#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-memory-address-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/memory-address/memory-address.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry Main > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation ADDRESS.Physical' "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation MEMORY.Write32' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/amir.txt"
grep -qF 'ADDRESS.PHYSICAL' "$TMP_ROOT/amir.txt"
grep -qF 'ADDRESS.ALIGNDOWN' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.WRITE32' "$TMP_ROOT/amir.txt"
grep -qF 'CPU.READBARRIER' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt"
# Flatten the formatted hexdump so barrier instructions crossing display boundaries remain
# matched as contiguous byte sequences.
sed -n 's/^    [0-9a-f][0-9a-f]*: //p' "$TMP_ROOT/x86.txt" | tr '\n' ' ' > "$TMP_ROOT/x86-flat.txt"
grep -qF '0f ae e8' "$TMP_ROOT/x86-flat.txt"
grep -qF '0f ae f8' "$TMP_ROOT/x86-flat.txt"
grep -qF '0f ae f0' "$TMP_ROOT/x86-flat.txt"
cat > "$TMP_ROOT/hosted.abas" <<'SCRIPT'
LET p AS VIRTUALPTR = ADDRESS.Virtual(4096)
LET value AS U32 = MEMORY.Read32(p)
SCRIPT
if "$ARCOFISSION" compile-run "$TMP_ROOT/hosted.abas" > "$TMP_ROOT/hosted.out" 2>&1; then
    echo 'FAIL: hosted memory operation unexpectedly executed' >&2
    exit 1
fi
grep -qF 'available only on a freestanding target with memory support' "$TMP_ROOT/hosted.out" || grep -qF 'MEMORY/ADDRESS operation' "$TMP_ROOT/hosted.out"
echo 'PASS: address domains, MMIO memory semantics, and barriers'
