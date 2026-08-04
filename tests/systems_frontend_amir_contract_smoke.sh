#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-frontend-amir-contract-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

cat > "$TMP_ROOT/contract.abas" <<'SCRIPT'
LET ast_contract = 17

FUNCTION Echo(value AS U64) AS U64
    RETURN value
END FUNCTION

IF ast_contract == 17 THEN
    PRINT Echo(ast_contract)
ELSE
    PRINT "bad"
END IF
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/contract.abas" at A-MIR > "$TMP_ROOT/contract.amir"

grep -q 'STORE ast_contract' "$TMP_ROOT/contract.amir"
grep -q 'DECLARE_FUNCTION Echo' "$TMP_ROOT/contract.amir"
grep -q 'FUNCTION Echo(value AS U64) RETURNS U64' "$TMP_ROOT/contract.amir"
grep -q 'BLOCK IfThen' "$TMP_ROOT/contract.amir"
grep -q 'BRANCH' "$TMP_ROOT/contract.amir"

if grep -q 'unsupported lowering' "$TMP_ROOT/contract.amir"; then
    echo "FAIL: an accepted canonical-AST construct disappeared before A-MIR" >&2
    cat "$TMP_ROOT/contract.amir" >&2
    exit 1
fi

# RFC-0012 forbids the historical second parser in compiler/fission.cpp. These source checks are
# intentionally narrow: lexer tokens may still be created and passed into Parser, but A-MIR must
# never be built from them or from a token-oriented AmirBuilder.
if grep -q 'build_amir(tokens' "$SOURCE_DIR/compiler/fission.cpp"; then
    echo "FAIL: Fission still builds A-MIR directly from lexer tokens" >&2
    exit 1
fi
if grep -q '^class AmirBuilder' "$SOURCE_DIR/compiler/fission.cpp"; then
    echo "FAIL: the historical token-reinterpreting AmirBuilder still exists" >&2
    exit 1
fi
if grep -q '(void)parser.parse' "$SOURCE_DIR/compiler/fission.cpp"; then
    echo "FAIL: a Fission entry point still discards the parser AST" >&2
    exit 1
fi

echo "PASS: frontend-to-A-MIR contract smoke test"
