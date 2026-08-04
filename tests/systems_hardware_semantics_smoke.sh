#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-hardware-semantics-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

printf '%s\n' \
    '#PROFILE UEFI' \
    '#TARGET X86_64' \
    '#RUNTIME NONE' \
    '#CALLCONV UEFI' \
    '#EXPORT "efi_main"' \
    '' \
    'FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64' \
    '    CPU.Halt' \
    '    RETURN 0' \
    'END FUNCTION' > "$TMP_ROOT/halt.abas"

"$ARCOFISSION" reveal "$TMP_ROOT/halt.abas" at AST > "$TMP_ROOT/halt.ast"
grep -qF 'HardwareSemantic CPU.Halt' "$TMP_ROOT/halt.ast"

"$ARCOFISSION" reveal "$TMP_ROOT/halt.abas" at A-MIR > "$TMP_ROOT/halt.amir"
grep -qF 'CPU.HALT' "$TMP_ROOT/halt.amir"
grep -qF 'RETURN U64' "$TMP_ROOT/halt.amir"

"$ARCOFISSION" reveal "$TMP_ROOT/halt.abas" at X86_64 --entry Main > "$TMP_ROOT/halt.x86"
grep -qE '(^| )f4( |$)' "$TMP_ROOT/halt.x86"

printf '%s\n' \
    '#PROFILE UEFI' \
    '#TARGET X86_64' \
    '#RUNTIME NONE' \
    '#CALLCONV UEFI' \
    '#EXPORT "efi_main"' \
    '' \
    'FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64' \
    '    CPU.HaltForever' \
    '    RETURN 99' \
    'END FUNCTION' > "$TMP_ROOT/halt-forever.abas"

"$ARCOFISSION" reveal "$TMP_ROOT/halt-forever.abas" at A-MIR > "$TMP_ROOT/halt-forever.amir"
grep -qF 'CPU.HALT_FOREVER' "$TMP_ROOT/halt-forever.amir"
if grep -qF 'RETURN U64' "$TMP_ROOT/halt-forever.amir"; then
    echo 'FAIL: CPU.HaltForever did not terminate A-MIR fallthrough' >&2
    exit 1
fi

"$ARCOFISSION" reveal "$TMP_ROOT/halt-forever.abas" at X86_64 --entry Main > "$TMP_ROOT/halt-forever.x86"
TEXT_HEX=$(sed -n '/^TEXT /,/^RDATA /p' "$TMP_ROOT/halt-forever.x86" | sed '1d;$d' | cut -d: -f2- | tr -d ' \n')
case "$TEXT_HEX" in
    *faf4ebfd*) ;;
    *) echo 'FAIL: x86-64 output does not contain CLI; HLT; JMP -3' >&2; exit 1 ;;
esac

"$ARCOFISSION" reveal "$TMP_ROOT/halt-forever.abas" at BYTECODE > "$TMP_ROOT/halt-forever.bytecode"
grep -qF 'hardware semantic CPU.HaltForever is unsupported by hosted bytecode backend' "$TMP_ROOT/halt-forever.bytecode"

echo 'PASS: hardware semantic AST, A-MIR, and x86-64 lowering smoke test'
