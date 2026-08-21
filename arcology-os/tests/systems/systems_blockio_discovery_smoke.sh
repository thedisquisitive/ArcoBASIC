#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-blockio-discovery-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT
FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/blockio-discovery/blockio-discovery.abas"

"$ARCOFISSION" reveal "$FIXTURE" at AST --entry Main > "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation UEFI.BLOCKIO.Discover' "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation UEFI.BLOCKIO.MediaId' "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation UEFI.BLOCKIO.MediaFlags' "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation UEFI.BLOCKIO.BlockSize' "$TMP_ROOT/ast.txt"
grep -qF 'MemoryOperation UEFI.BLOCKIO.LastBlock' "$TMP_ROOT/ast.txt"

"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/amir.txt"
grep -qF 'UEFI.BLOCKIO.DISCOVER' "$TMP_ROOT/amir.txt"
grep -qF 'UEFI.BlockIoProtocol' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.BLOCKIOMEDIAID' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.BLOCKIOMEDIAFLAGS' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.BLOCKIOBLOCKSIZE' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.BLOCKIOLASTBLOCK' "$TMP_ROOT/amir.txt"
# ReadBlocks is a real protocol method call through the already-generic CallExternal mechanism --
# no new calling-convention codegen was written for this binding.
grep -qF 'CALL_EXTERNAL blockIo.ReadBlocks' "$TMP_ROOT/amir.txt"

"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt"
# Flatten the formatted hexdump before matching so instructions crossing an 8-byte display
# boundary do not make the smoke test fail spuriously (same rationale as the GOP smoke test).
sed -n 's/^    [0-9a-f][0-9a-f]*: //p' "$TMP_ROOT/x86.txt" | tr '\n' ' ' > "$TMP_ROOT/x86-flat.txt"
# LocateProtocol is an indirect call through EFI_BOOT_SERVICES+0x140 and the Block I/O GUID is
# materialized in the reserved compiler stack scratch area, using the same byte-layout convention
# already proven correct for the GOP GUID.
grep -qF '41 ff 93 40 01 00 00' "$TMP_ROOT/x86-flat.txt"
grep -qF '48 ba 21 5b 4e 96 59 64 d2 11' "$TMP_ROOT/x86-flat.txt"
grep -qF '48 ba 8e 39 00 a0 c9 69 72 3b' "$TMP_ROOT/x86-flat.txt"
# ReadOneBlock's own body must lower blockIo.ReadBlocks to an indirect call through the resolved
# This pointer (R11) at the registered ReadBlocks offset (0x18) -- confirming CallExternal
# resolved the new UEFI.BlockIoProtocol registry entry with no bespoke codegen.
grep -qF '41 ff 53 18' "$TMP_ROOT/x86-flat.txt"

echo 'PASS: typed Block I/O LocateProtocol discovery, Media field lowering, and ReadBlocks via the generic CallExternal path'
