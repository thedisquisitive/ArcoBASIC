#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-freestanding-smoke-$$"
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

# Packet WP-003 acceptance: a trivial function with no host dependencies compiles
# under the freestanding profile.
expect_accept "trivial_freestanding_function" '#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT "efi_main"

FUNCTION Main() AS U64
    RETURN 0
END FUNCTION'

# The full hello-world fixture (unresolved UEFI.SystemTable binding is not itself
# a hosted-runtime feature, so it must still parse -- WP-006 adds real bindings).
expect_accept "hello_world_fixture" '#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT "efi_main"

FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64
    systemTable.ConsoleOut.Write("Hello from ArcoBASIC")
    RETURN 0
END FUNCTION'

# A dotted parameter/receiver name that happens to start with the same letters as
# a forbidden namespace must not be mistaken for it (segment match, not prefix match).
expect_accept "no_false_positive_on_systemtable" '#PROFILE UEFI
#RUNTIME NONE

FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64
    systemTable.ConsoleOut.Write("hi")
    RETURN 0
END FUNCTION'

# Packet WP-003 acceptance: a program using an unavailable host-dependent feature
# fails with a clear message.
expect_reject "print_under_freestanding" '#PROFILE UEFI
#RUNTIME NONE

FUNCTION Main() AS U64
    PRINT "hi"
    RETURN 0
END FUNCTION' \
    "UEFI target does not provide the standard console runtime"

expect_reject "file_under_freestanding" '#RUNTIME NONE
LET x = File.Exists("x")' \
    "File.* calls is not available under #RUNTIME NONE"

expect_reject "network_under_freestanding" '#RUNTIME NONE
LET x = Network.Get("http://example.com")' \
    "Network.* calls is not available under #RUNTIME NONE"

expect_reject "system_under_freestanding" '#RUNTIME NONE
LET x = System.Capabilities()' \
    "System.* calls is not available under #RUNTIME NONE"

# Directive validation: only the documented values are accepted.
expect_reject "unknown_profile" '#PROFILE FOO
PRINT "hi"' \
    "Unknown compilation profile: FOO"

expect_reject "unknown_runtime_value" '#RUNTIME HOSTED
PRINT "hi"' \
    "Unknown #RUNTIME value: HOSTED"

expect_reject "unsupported_architecture" '#PROFILE UEFI
#TARGET ARM64
PRINT "hi"' \
    "Unsupported architecture for the UEFI profile: ARM64"

# Regression: ordinary hosted programs (no #RUNTIME NONE) are entirely unaffected.
expect_accept "hosted_program_unaffected" 'PRINT "hello"
LET exists = File.Exists("x")'

echo "PASS: freestanding profile smoke test"
