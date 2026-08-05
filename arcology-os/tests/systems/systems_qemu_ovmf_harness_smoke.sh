#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-qemu-harness-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Packet WP-010 objective: a deterministic automated run harness. Environment policy: skip
# gracefully rather than fail if the tools it needs are not installed in this environment (the
# same detection arcology-os/scripts/run/run-uefi-hello.sh itself performs, checked again here so ctest reports a
# clean SKIP instead of the harness's own installation-guidance error text).
if ! command -v qemu-system-x86_64 > /dev/null 2>&1; then
    echo "SKIP: qemu-system-x86_64 is not installed; see arcology-os/scripts/run/run-uefi-hello.sh for installation guidance."
    exit 0
fi
if ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: no OVMF firmware image was found; see arcology-os/scripts/run/run-uefi-hello.sh for installation guidance."
    exit 0
fi

"$ARCOFISSION" build "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-hello/hello.abas" -o "$TMP_ROOT/hello.efi" --target uefi-x86_64 > /dev/null

# Packet WP-010 "Preferred result": the harness prints exactly "PASS: Hello from ArcoBASIC" on
# success and exits nonzero otherwise.
OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/hello.efi" "Hello from ArcoBASIC" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: Hello from ArcoBASIC" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# Regression: a genuinely wrong expected string must fail (nonzero exit), proving the harness is
# not vacuously passing regardless of actual console output.
if "$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/hello.efi" "This text never appears" 15 > "$TMP_ROOT/negative.out" 2>&1; then
    echo "FAIL: harness reported success for a string that was never printed" >&2
    cat "$TMP_ROOT/negative.out" >&2
    exit 1
fi
grep -qF "FAIL: expected output not found" "$TMP_ROOT/negative.out"

echo "PASS: QEMU/OVMF harness smoke test"
