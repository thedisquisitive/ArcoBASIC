#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-utf16-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Packet WP-007 acceptance context: the hello-world string argument is UTF-16-encodable and the
# hello-world source is unaffected by this work package.
"$ARCOFISSION" reveal "$SOURCE_DIR/tests/systems/uefi-hello/hello.abas" at AST > "$TMP_ROOT/hello.out"
grep -q "SOURCE ACCEPTED" "$TMP_ROOT/hello.out"

# An embedded NUL byte in a string argument passed to a call through a UEFI-typed parameter is
# rejected clearly (it would silently truncate a null-terminated UTF-16 string).
printf '%s\n' \
    'FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64' \
    '    systemTable.ConsoleOut.Write("bad\0null")' \
    '    RETURN 0' \
    'END FUNCTION' > "$TMP_ROOT/bad_nul.abas"
if "$ARCOFISSION" reveal "$TMP_ROOT/bad_nul.abas" at AST > "$TMP_ROOT/bad_nul.out" 2>&1; then
    echo "FAIL: expected embedded-NUL string argument to be rejected" >&2
    cat "$TMP_ROOT/bad_nul.out" >&2
    exit 1
fi
grep -qF "string argument cannot be encoded as UTF-16" "$TMP_ROOT/bad_nul.out"
grep -qF "embedded NUL byte" "$TMP_ROOT/bad_nul.out"

# Regression: the same embedded-NUL string is unaffected outside a UEFI-typed call (an ordinary
# hosted PRINT is not a systems binding call, so this validation must not apply to it).
printf '%s\n' 'PRINT "bad\0null"' > "$TMP_ROOT/hosted_nul.abas"
if ! "$ARCOFISSION" compile-run "$TMP_ROOT/hosted_nul.abas" > "$TMP_ROOT/hosted_nul.out" 2>&1; then
    echo "FAIL: an embedded-NUL string outside a UEFI call must not be rejected" >&2
    cat "$TMP_ROOT/hosted_nul.out" >&2
    exit 1
fi

# A non-ASCII but well-formed string argument is accepted (UTF-16 encoding succeeds for real text,
# not just ASCII). Uses $'...' ANSI-C quoting so \xC3\xA9 becomes the real raw UTF-8 bytes for
# "e with acute" in the generated .abas file -- ArcoBASIC's own lexer has no \x escape (only \n,
# \r, \t, \", \\, \0), so a plain-quoted "\xC3\xA9" would reach the file as the literal six
# characters backslash-x-C-3-backslash-x-A-9, not real bytes.
printf '%s\n' \
    'FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64' \
    $'    systemTable.ConsoleOut.Write("caf\xC3\xA9")' \
    '    RETURN 0' \
    'END FUNCTION' > "$TMP_ROOT/accented.abas"
"$ARCOFISSION" reveal "$TMP_ROOT/accented.abas" at AST > "$TMP_ROOT/accented.out"
grep -q "SOURCE ACCEPTED" "$TMP_ROOT/accented.out"

# Malformed UTF-8 in a UEFI call's string argument is rejected. Same $'...' raw-byte requirement
# as above.
printf '%s\n' \
    'FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64' \
    $'    systemTable.ConsoleOut.Write("bad\xFFbyte")' \
    '    RETURN 0' \
    'END FUNCTION' > "$TMP_ROOT/bad_utf8.abas"
if "$ARCOFISSION" reveal "$TMP_ROOT/bad_utf8.abas" at AST > "$TMP_ROOT/bad_utf8.out" 2>&1; then
    echo "FAIL: expected malformed UTF-8 string argument to be rejected" >&2
    cat "$TMP_ROOT/bad_utf8.out" >&2
    exit 1
fi
grep -qF "string argument cannot be encoded as UTF-16" "$TMP_ROOT/bad_utf8.out"

echo "PASS: UTF-16 encoding smoke test"
