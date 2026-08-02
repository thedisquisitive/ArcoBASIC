#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-x86-64-codegen-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Packet WP-008 acceptance context: correct machine code for the hello-world program's A-MIR.
#
# Every byte asserted below (other than the still-unpatched relocation placeholder, asserted
# separately) was independently cross-checked against `nasm -f bin` output for the equivalent
# hand-written assembly -- see .agents/reports/WP-008-x86-64-codegen.md for the exact .asm source
# and the reasoning that patching the relocation under a contiguous text+rdata layout reproduces
# nasm's own computed displacement (0x37) exactly. This test only needs to assert the compiler's
# output is unchanged from that already-verified baseline, so it does not itself depend on nasm
# being installed (nasm is a development-time verification aid, not a build or test dependency --
# see docs/systems/uefi-target.md section 10's "no new dependency" decision).
"$ARCOFISSION" reveal "$SOURCE_DIR/tests/systems/uefi-hello/hello.abas" at X86_64 > "$TMP_ROOT/hello.x86_64"
grep -q "SOURCE ACCEPTED" "$TMP_ROOT/hello.x86_64"
grep -q "X86_64 GENERATED" "$TMP_ROOT/hello.x86_64"
grep -q "^ENTRY Main$" "$TMP_ROOT/hello.x86_64"
grep -q "^TEXT 76 bytes$" "$TMP_ROOT/hello.x86_64"
grep -q "^RDATA 42 bytes$" "$TMP_ROOT/hello.x86_64"

# Full instruction sequence, byte for byte (prologue; spill imageHandle/systemTable; LEA the UTF-16
# string with its still-unpatched relocation placeholder; dereference ConsoleOut/OutputString;
# indirect call with the implicit This argument; materialize and return 0; epilogue).
grep -qF "0000: 48 83 ec 48 48 89 4c 24" "$TMP_ROOT/hello.x86_64"
grep -qF "0008: 20 48 89 54 24 28 48 8d" "$TMP_ROOT/hello.x86_64"
grep -qF "0010: 05 00 00 00 00 48 89 44" "$TMP_ROOT/hello.x86_64"
grep -qF "0018: 24 30 48 8b 44 24 28 48" "$TMP_ROOT/hello.x86_64"
grep -qF "0020: 8b 40 40 48 89 c1 48 8b" "$TMP_ROOT/hello.x86_64"
grep -qF "0028: 54 24 30 ff 50 08 48 89" "$TMP_ROOT/hello.x86_64"
grep -qF "0030: 44 24 38 48 b8 00 00 00" "$TMP_ROOT/hello.x86_64"
grep -qF "0038: 00 00 00 00 00 48 89 44" "$TMP_ROOT/hello.x86_64"
grep -qF "0040: 24 40 48 8b 44 24 40 48" "$TMP_ROOT/hello.x86_64"
grep -qF "0048: 83 c4 48 c3" "$TMP_ROOT/hello.x86_64"

# The UTF-16 encoding of "Hello from ArcoBASIC" plus its null terminator (Packet WP-007), placed
# verbatim in the data section.
grep -qF "0000: 48 00 65 00 6c 00 6c 00" "$TMP_ROOT/hello.x86_64"
grep -qF "0008: 6f 00 20 00 66 00 72 00" "$TMP_ROOT/hello.x86_64"
grep -qF "0010: 6f 00 6d 00 20 00 41 00" "$TMP_ROOT/hello.x86_64"
grep -qF "0018: 72 00 63 00 6f 00 42 00" "$TMP_ROOT/hello.x86_64"
grep -qF "0020: 41 00 53 00 49 00 43 00" "$TMP_ROOT/hello.x86_64"
grep -qF "0028: 00 00" "$TMP_ROOT/hello.x86_64"

# Exactly one relocation: the LEA's disp32 field at TEXT+0x11 (17), whose instruction ends at
# TEXT+0x15 (21), referring to RDATA+0 (the start of the UTF-16 string).
grep -qF "RELOCATIONS 1" "$TMP_ROOT/hello.x86_64"
grep -qF "TEXT+11 RIP_REL32_TO RDATA+0 (instruction ends at TEXT+15)" "$TMP_ROOT/hello.x86_64"

# A function with control flow beyond a single straight-line block is rejected with a clear error
# rather than silently mis-encoded (Packet WP-008 non-goal: general-purpose instruction selection).
cat > "$TMP_ROOT/branchy.abas" <<'SCRIPT'
FUNCTION Branchy(flag AS BOOL) AS U64
    IF flag THEN
        RETURN 1
    END IF
    RETURN 0
END FUNCTION
SCRIPT
if "$ARCOFISSION" reveal "$TMP_ROOT/branchy.abas" at X86_64 --entry Branchy > "$TMP_ROOT/branchy.out" 2>&1; then
    echo "FAIL: expected a function with control flow to be rejected by the code generator" >&2
    cat "$TMP_ROOT/branchy.out" >&2
    exit 1
fi
grep -qF "control flow beyond a single straight-line block" "$TMP_ROOT/branchy.out"

echo "PASS: x86-64 code generation smoke test"
