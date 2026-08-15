#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-gop-discovery-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/gop-discovery/gop-discovery.abas"
"$ARCOFISSION" reveal "$FIXTURE" at AST --entry Main > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation UEFI.GOP.Discover' "$TMP_ROOT/ast.txt"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/amir.txt"
grep -qF 'UEFI.GOP.DISCOVER' "$TMP_ROOT/amir.txt"
grep -qF 'UEFI.GraphicsOutputProtocol' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.GOPFRAMEBUFFERBASE' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.GOPFRAMEBUFFERSIZE' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.WRITE32' "$TMP_ROOT/amir.txt"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt"
# Flatten the formatted hexdump before matching so instructions crossing an 8-byte display
# boundary do not make the smoke test fail spuriously.
sed -n 's/^    [0-9a-f][0-9a-f]*: //p' "$TMP_ROOT/x86.txt" | tr '\n' ' ' > "$TMP_ROOT/x86-flat.txt"
# LocateProtocol is an indirect call through EFI_BOOT_SERVICES+0x140 and the GOP GUID is
# materialized in the reserved compiler stack scratch area.
grep -qF '41 ff 93 40 01 00 00' "$TMP_ROOT/x86-flat.txt"
grep -qF '48 ba de a9 42 90 dc 23 38 4a' "$TMP_ROOT/x86-flat.txt"
grep -qF '48 ba 96 fb 7a de d0 80 51 6a' "$TMP_ROOT/x86-flat.txt"
# PixelsPerScanLine must be an exact-width dword load: mov eax,[rax+0x20].
grep -qF '48 8b 40 08 8b 40 20' "$TMP_ROOT/x86-flat.txt"
echo 'PASS: typed GOP LocateProtocol discovery and exact-width mode-info lowering'
