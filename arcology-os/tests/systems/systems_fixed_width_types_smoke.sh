#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-systems-types-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

expect_accept() {
    local name="$1"
    local source="$2"
    local file="$TMP_ROOT/$name.abas"
    printf '%s\n' "$source" > "$file"
    if ! "$ARCOFISSION" reveal "$file" at AST > "$TMP_ROOT/$name.out" 2>&1; then
        echo "FAIL: expected $name to be accepted, but it was rejected:" >&2
        cat "$TMP_ROOT/$name.out" >&2
        exit 1
    fi
    if ! grep -q "SOURCE ACCEPTED" "$TMP_ROOT/$name.out"; then
        echo "FAIL: $name did not report SOURCE ACCEPTED" >&2
        exit 1
    fi
}

expect_reject() {
    local name="$1"
    local source="$2"
    local expected_substring="$3"
    local file="$TMP_ROOT/$name.abas"
    printf '%s\n' "$source" > "$file"
    if "$ARCOFISSION" reveal "$file" at AST > "$TMP_ROOT/$name.out" 2>&1; then
        echo "FAIL: expected $name to be rejected, but it was accepted:" >&2
        cat "$TMP_ROOT/$name.out" >&2
        exit 1
    fi
    if ! grep -qF "$expected_substring" "$TMP_ROOT/$name.out"; then
        echo "FAIL: $name diagnostic did not contain expected text: $expected_substring" >&2
        cat "$TMP_ROOT/$name.out" >&2
        exit 1
    fi
}

# Valid boundary values for every required fixed-width type (Packet WP-002 acceptance tests,
# adapted to LET ... AS Type per arcology-os/docs/systems/uefi-target.md section 3).
expect_accept "u8_max" 'LET a AS U8 = 255'
expect_accept "u16_max" 'LET a AS U16 = 65535'
expect_accept "u32_max" 'LET a AS U32 = 4294967295'
expect_accept "u64_max" 'LET a AS U64 = 18446744073709551615'
expect_accept "u8_min" 'LET a AS U8 = 0'
expect_accept "i8_min" 'LET a AS I8 = -128'
expect_accept "i8_max" 'LET a AS I8 = 127'
expect_accept "i16_min" 'LET a AS I16 = -32768'
expect_accept "i16_max" 'LET a AS I16 = 32767'
expect_accept "i32_min" 'LET a AS I32 = -2147483648'
expect_accept "i32_max" 'LET a AS I32 = 2147483647'
expect_accept "i64_min" 'LET a AS I64 = -9223372036854775808'
expect_accept "i64_max" 'LET a AS I64 = 9223372036854775807'
expect_accept "bool_zero" 'LET a AS BOOL = 0'
expect_accept "bool_one" 'LET a AS BOOL = 1'
expect_accept "non_fixed_width_type_untouched" 'LET a AS String = "hi"'

# Must reject: out-of-range and sign-mismatched literals (Packet WP-002 acceptance tests).
expect_reject "u8_overflow" 'LET a AS U8 = 256' \
    "U8 literal out of range: expected 0..255, received 256"
expect_reject "u16_negative" 'LET b AS U16 = -1' \
    "U16 cannot represent negative values"
expect_reject "u32_overflow" 'LET a AS U32 = 4294967296' \
    "U32 literal out of range"
expect_reject "u64_overflow" 'LET a AS U64 = 18446744073709551616' \
    "too large to represent in any 64-bit systems type"
expect_reject "i8_overflow" 'LET a AS I8 = 128' \
    "I8 literal out of range"
expect_reject "i8_underflow" 'LET a AS I8 = -129' \
    "I8 literal out of range"
expect_reject "i64_underflow" 'LET a AS I64 = -9223372036854775809' \
    "No wider fixed-width signed type is available"
expect_reject "bool_out_of_range" 'LET a AS BOOL = 2' \
    "BOOL literal must be 0 or 1"
expect_reject "ptr_from_literal" 'LET a AS PTR = 5' \
    "PTR cannot be initialized from an integer literal"
expect_reject "float_into_integer_type" 'LET a AS U8 = 3.5' \
    "U8 requires an integer literal"

echo "PASS: fixed-width systems types smoke test"
