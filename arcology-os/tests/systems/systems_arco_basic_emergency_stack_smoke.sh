#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: CPU.Interrupt requires a compile-time-constant vector (the same PORT.Offset
# precedent this backend already has for other statically-known operands).
cat > "$TMP_ROOT/bad.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION Main() AS U64
    LET vector AS U64 = 0
    CPU.Interrupt(vector)
    RETURN 0
END FUNCTION
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/bad.abas" -o "$TMP_ROOT/bad.efi" --target uefi-x86_64 > "$TMP_ROOT/bad.out" 2>&1; then
    echo "FAIL: CPU.Interrupt accepted a non-constant vector" >&2
    exit 1
fi
grep -qF 'CPU.Interrupt requires a statically known vector' "$TMP_ROOT/bad.out"

cat > "$TMP_ROOT/int.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION IntTest() AS U64
    CPU.Interrupt(8)
    RETURN 0
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/int.abas" at X86_64 --entry IntTest > "$TMP_ROOT/int.x86"
grep -qF 'cd 08' "$TMP_ROOT/int.x86"

# The real proof: builds a genuine, dedicated 16 KiB emergency stack and supplies its top to the
# TSS's IST1 slot, then confirms an IST=1 gate actually causes the CPU to switch onto it (recorded
# by the exception-entry table's shared handler, read back afterward) -- not merely that LTR
# accepts a table with a plausible-looking value in it. See aps-emergency-stack.abas's header
# comment for why this uses CPU.Interrupt(0) (#DE) as the test vehicle rather than vector 8 (#DF,
# the vector IST1 exists for): software INT never pushes a hardware error code, even for a vector
# whose fault-raised form normally would, and vector 8's stub expects one already on the stack.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-emergency-stack.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-emergency-stack.efi" --target uefi-x86_64 > /dev/null

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-emergency-stack.efi" "APS IST1 STACK OK" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS IST1 STACK OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: an IST=1 IDT gate switches RSP onto its TSS-supplied emergency stack under a real CPU (QEMU/OVMF)"
