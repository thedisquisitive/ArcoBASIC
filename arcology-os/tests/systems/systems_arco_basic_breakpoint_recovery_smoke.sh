#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-breakpoint-recovery.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-breakpoint-recovery.efi" --target uefi-x86_64 > /dev/null

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The full proof: builds the APS CR3-cutover identity-mapped hierarchy, exits boot services,
# installs an IDT addressing the compiler-synthesized exception-entry table, and executes a
# deliberate CPU.Breakpoint. "APS BP RECOVERED" -- printed by a *called function*, not inline code
# -- only appears if the #BP recovery path resumed execution at exactly the right address. An
# earlier version of the exception-entry table's recovery path resumed one byte late (treating
# INT3, a trap, as if it were fault-shaped), which happened to be survivable for some instruction
# shapes and silently corrupted execution for others (a CALL right after the breakpoint, as here).
OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-breakpoint-recovery.efi" "APS BP RECOVERED" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS BP RECOVERED" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: CPU.Breakpoint recovers and resumes at the correct address under a real CPU (QEMU/OVMF)"
