#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-amir-primitives-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Packet WP-004 acceptance: the hello-world source lowers to valid, deterministic A-MIR.
"$ARCOFISSION" reveal "$SOURCE_DIR/tests/systems/uefi-hello/hello.abas" at A-MIR > "$TMP_ROOT/hello.amir"
grep -q "SOURCE ACCEPTED" "$TMP_ROOT/hello.amir"
grep -q "A-MIR GENERATED" "$TMP_ROOT/hello.amir"

# Function returns (docs/systems/uefi-target.md section 5): the declared U64 return type must
# reach both the function header and the RETURN instruction itself, not just be a "VALUE" tag.
grep -q "FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) RETURNS U64" "$TMP_ROOT/hello.amir"
grep -q "RETURN U64 %t2" "$TMP_ROOT/hello.amir"

# External/ABI-bound function calls: a call through a declared parameter (systemTable) is tagged
# CALL_EXTERNAL, distinct from an ordinary namespaced host/stdlib CALL.
grep -q "CALL_EXTERNAL systemTable.ConsoleOut.Write" "$TMP_ROOT/hello.amir"

# Regression: an ordinary host/stdlib call (not through a parameter) must stay a plain CALL, and an
# untyped function's return must stay "VALUE" exactly as before this work package.
cat > "$TMP_ROOT/ordinary.abas" <<'SCRIPT'
FUNCTION Check(path AS String) AS BOOL
    File.Exists(path)
    RETURN TRUE
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/ordinary.abas" at A-MIR > "$TMP_ROOT/ordinary.amir"
grep -q "CALL File.Exists" "$TMP_ROOT/ordinary.amir"
if grep -q "CALL_EXTERNAL File.Exists" "$TMP_ROOT/ordinary.amir"; then
    echo "FAIL: File.Exists (not a parameter) was misclassified as CALL_EXTERNAL" >&2
    exit 1
fi
grep -q "RETURN BOOL %t2" "$TMP_ROOT/ordinary.amir"

cat > "$TMP_ROOT/plain.abas" <<'SCRIPT'
FUNCTION Greet(name)
    RETURN name
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/plain.abas" at A-MIR > "$TMP_ROOT/plain.amir"
grep -q "RETURN VALUE" "$TMP_ROOT/plain.amir"

# A call through a parameter is external even through a deeper dotted chain.
cat > "$TMP_ROOT/chain.abas" <<'SCRIPT'
FUNCTION Use(handle AS UEFI.Handle) AS U64
    handle.Something.DeepChain.Call(1)
    RETURN 0
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/chain.abas" at A-MIR > "$TMP_ROOT/chain.amir"
grep -q "CALL_EXTERNAL handle.Something.DeepChain.Call" "$TMP_ROOT/chain.amir"

echo "PASS: A-MIR systems primitives smoke test"
