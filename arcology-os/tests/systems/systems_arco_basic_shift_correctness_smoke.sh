#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/integer-core/shift-correctness.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/shift-correctness.efi" --target uefi-x86_64 > /dev/null

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# Regression proof for a real x86-64 backend bug: INT.SHR/INT.SHL's out-of-range-shift-count
# safety check computed the shift into RAX, then let it be clobbered before the result was ever
# consumed -- silently discarding every dynamic-count SHR/SHL and substituting the unshifted
# original value instead. A compile-time shape check (does SHR appear in the A-MIR/x86-64 output)
# cannot catch this; only executing the generated code and checking the actual numeric result can.
OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/shift-correctness.efi" "12345678ABCDEEF0" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: 12345678ABCDEEF0" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: SHR/SHL with a dynamic shift count produce the correct shifted value"
