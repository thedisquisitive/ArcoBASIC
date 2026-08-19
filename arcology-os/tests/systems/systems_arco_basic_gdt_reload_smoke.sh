#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: CPU.ReloadCodeSegment lowers to the "push CS; push RIP; far-return" sequence,
# and the pushed RIP is a RIP-relative LEA that resolves to right after the RETFQ (a same-function
# forward reference, patched immediately, not a cross-symbol relocation).
cat > "$TMP_ROOT/reload.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION ReloadTest(gdtPseudo AS U64, codeSelector AS U64) AS U64
    CPU.LoadGDT(gdtPseudo)
    CPU.ReloadCodeSegment(codeSelector)
    CPU.ReloadStackSegment(codeSelector)
    RETURN 0
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/reload.abas" at A-MIR --entry ReloadTest > "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.RELOADCS' "$TMP_ROOT/amir.txt"
grep -qF 'MEMORY.RELOADSS' "$TMP_ROOT/amir.txt"

"$ARCOFISSION" reveal "$TMP_ROOT/reload.abas" at X86_64 --entry ReloadTest > "$TMP_ROOT/x86.txt"
awk '/^TEXT [0-9]+ bytes/{on=1; next} /^$/{on=0} on{ $1=""; print }' "$TMP_ROOT/x86.txt" | tr -d ' \n' > "$TMP_ROOT/stream.txt"
# push rax (50); lea rax,[rip+disp32] (488d05........); push rax (50); retfq (48cb)
grep -qE '50488d05[0-9a-f]{8}5048cb' "$TMP_ROOT/stream.txt"
# mov ss, ax (8ed0)
grep -qF '8ed0' "$TMP_ROOT/stream.txt"

# The real proof: builds a genuinely new, independent GDT and TSS (not just reusing the firmware's
# own selector value the way aps-breakpoint-recovery.abas deliberately does), activates them via
# CPU.LoadGDT + CPU.ReloadCodeSegment (a far return -- the only way to change CS in 64-bit mode)
# + CPU.ReloadStackSegment, reads CS back live to confirm it names the *new* table's own code
# selector (not the firmware's), and recovers a deliberate breakpoint using IDT gates built against
# that new selector -- proving the new GDT is genuinely what the CPU is running under, not just
# loaded and ignored.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-gdt-reload.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-gdt-reload.efi" --target uefi-x86_64 > /dev/null

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-gdt-reload.efi" "APS BP RECOVERED UNDER NEW GDT" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS BP RECOVERED UNDER NEW GDT" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: a freshly built GDT is genuinely activated (CS and SS both reloaded) under a real CPU (QEMU/OVMF)"
