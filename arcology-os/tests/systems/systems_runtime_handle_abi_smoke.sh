#!/usr/bin/env bash
set -euo pipefail
ARCO="$1"
SOURCE_DIR="$2"
ARCOFISSION="$3"
FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/runtime-handle-graphics/runtime-handle-graphics.abas"
OUTPUT="${TMPDIR:-/tmp}/runtime-handle-abi-$$.out"
trap 'rm -f "$OUTPUT"' EXIT
if ! "$ARCO" "$FIXTURE" >"$OUTPUT" 2>&1; then
    cat "$OUTPUT" >&2
    exit 1
fi
grep -qF 'RUNTIME HANDLE ABI ONLINE' "$OUTPUT"
INVALID="$SOURCE_DIR/arcology-os/tests/fixtures/runtime-handle-graphics/runtime-handle-invalid.abas"
if "$ARCO" "$INVALID" >"$OUTPUT" 2>&1; then
    echo 'FAIL: destroyed SURFACE handle was accepted' >&2
    exit 1
fi
grep -qF 'invalid SURFACE handle' "$OUTPUT"
PRIMARY="$SOURCE_DIR/arcology-os/tests/fixtures/runtime-handle-graphics/primary-surface.abas"
if ! "$ARCO" "$PRIMARY" >"$OUTPUT" 2>&1; then
    cat "$OUTPUT" >&2
    exit 1
fi
grep -qF 'PRIMARY SURFACE ABI ONLINE' "$OUTPUT"
grep -qF 'Software Graphics Provider' "$OUTPUT"
grep -qF 'Primary Display Backend' "$OUTPUT"
"$ARCOFISSION" reveal "$PRIMARY" at A-MIR --entry Main > "$OUTPUT"
grep -qF ':SURFACE = UEFI.GOP.DISCOVER' "$OUTPUT"
grep -qF '{Borrowed}' "$OUTPUT"
grep -qF '{Consumed}' "$OUTPUT"
BOOT="$SOURCE_DIR/arcology-os/tests/fixtures/runtime-handle-graphics/primary-surface-freestanding-bootstrap.abas"
EFI="${TMPDIR:-/tmp}/runtime-handle-primary-$$.efi"
trap 'rm -f "$OUTPUT" "$EFI"' EXIT
"$ARCOFISSION" build "$BOOT" -o "$EFI" --target uefi-x86_64 --entry Main >/dev/null
test -s "$EFI"
echo 'PASS: Runtime Library Handle ABI graphics constructor/binding/destruction'
