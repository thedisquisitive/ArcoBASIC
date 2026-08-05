#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-callconv-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Packet WP-005 verification: golden test for the computed calling convention (the deterministic
# textual stand-in for generated assembly this milestone uses -- see render_calling_convention).
"$ARCOFISSION" reveal "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-hello/hello.abas" at CALLCONV > "$TMP_ROOT/hello.callconv"
grep -q "SOURCE ACCEPTED" "$TMP_ROOT/hello.callconv"
grep -q "CALLING CONVENTION COMPUTED" "$TMP_ROOT/hello.callconv"
grep -q "SHADOW_SPACE 32 bytes" "$TMP_ROOT/hello.callconv"
grep -q "STACK_ALIGNMENT 16 bytes at CALL" "$TMP_ROOT/hello.callconv"

# Correct argument registers: the two-parameter UEFI entry point uses RCX, RDX in order.
grep -q "imageHandle : RCX" "$TMP_ROOT/hello.callconv"
grep -q "systemTable : RDX" "$TMP_ROOT/hello.callconv"

# Correct return register.
grep -q "RETURNS RAX (U64)" "$TMP_ROOT/hello.callconv"

# Nested/external calls sufficient for UEFI text output: the call to ConsoleOut.Write gets its
# own argument register assignment, independent of Main's own parameter registers.
grep -q "systemTable.ConsoleOut.Write (external)" "$TMP_ROOT/hello.callconv"
grep -q "ARG0 : RCX" "$TMP_ROOT/hello.callconv"

# Shadow space and stack spilling for more than four arguments.
cat > "$TMP_ROOT/six.abas" <<'SCRIPT'
FUNCTION Many(a AS U8, b AS U8, c AS U8, d AS U8, e AS U8, f AS U8) AS U64
    RETURN 0
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/six.abas" at CALLCONV > "$TMP_ROOT/six.callconv"
grep -q "a : RCX" "$TMP_ROOT/six.callconv"
grep -q "b : RDX" "$TMP_ROOT/six.callconv"
grep -q "c : R8" "$TMP_ROOT/six.callconv"
grep -q "d : R9" "$TMP_ROOT/six.callconv"
grep -q "e : STACK+40" "$TMP_ROOT/six.callconv"
grep -q "f : STACK+48" "$TMP_ROOT/six.callconv"

echo "PASS: calling convention smoke test"
