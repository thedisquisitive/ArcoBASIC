#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-integer-core-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/integer-core/integer-core.abas"
"$ARCOFISSION" reveal "$FIXTURE" at A-MIR > "$TMP_ROOT/amir.txt"
grep -q "INT.AND" "$TMP_ROOT/amir.txt"
grep -q "INT.CMP_NE" "$TMP_ROOT/amir.txt"
grep -q "INT.SHR" "$TMP_ROOT/amir.txt"
grep -q "INT.SAR" "$TMP_ROOT/amir.txt"
grep -q "INT.DIV_UNSIGNED" "$TMP_ROOT/amir.txt"
grep -q "INT.MOD_UNSIGNED" "$TMP_ROOT/amir.txt"
grep -q ":U8" "$TMP_ROOT/amir.txt"
grep -q ":I16" "$TMP_ROOT/amir.txt"

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/integer-core.efi" --target uefi-x86_64 >/dev/null
test -s "$TMP_ROOT/integer-core.efi"

cat > "$TMP_ROOT/mixed.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
FUNCTION Main() AS U64
    LET left AS U8 = 1
    LET right AS U16 = 2
    LET bad AS U8 = left + right
    RETURN bad
END FUNCTION
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/mixed.abas" -o "$TMP_ROOT/mixed.efi" --target uefi-x86_64 > "$TMP_ROOT/mixed.out" 2>&1; then
    echo "mixed-width integer expression unexpectedly compiled" >&2
    exit 1
fi
grep -q "matching fixed-width integer operands" "$TMP_ROOT/mixed.out"

cat > "$TMP_ROOT/zero.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
FUNCTION Main() AS U64
    LET bad AS U64 = 7 \ 0
    RETURN bad
END FUNCTION
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/zero.abas" -o "$TMP_ROOT/zero.efi" --target uefi-x86_64 > "$TMP_ROOT/zero.out" 2>&1; then
    echo "constant-zero integer division unexpectedly compiled" >&2
    exit 1
fi
grep -q "compile-time zero divisor" "$TMP_ROOT/zero.out"

echo "PASS: freestanding integer core smoke test"
