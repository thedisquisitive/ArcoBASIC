#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: CPU.InterruptPendingTableBase() lowers to a fixed compile-time-constant
# address, not a cross-symbol relocation -- unlike CPU.ExceptionVectorTableBase(), this table
# cannot live inside the (read-only/execute-only) PE image, since the shared handler must write to
# it on every tick. See fission.cpp's INTERRUPTPENDINGTABLEBASE codegen case.
cat > "$TMP_ROOT/pending.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION PendingTableBase() AS U64
    RETURN CPU.InterruptPendingTableBase()
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/pending.abas" at X86_64 --entry PendingTableBase > "$TMP_ROOT/pending.x86"
grep -qF 'INTERNAL_CALLS 0' "$TMP_ROOT/pending.x86"
# mov rax, 0x0000000002010000 (little-endian imm64: 48 b8 00 00 01 02 00 00 00 00)
awk '/^TEXT [0-9]+ bytes/{on=1; next} /^$/{on=0} on{ $1=""; print }' "$TMP_ROOT/pending.x86" | tr -d ' \n' > "$TMP_ROOT/pending_stream.txt"
grep -qF '48b80000010200000000' "$TMP_ROOT/pending_stream.txt"

# The extended interrupt-vector table now covers vectors 0-47 (32 CPU exceptions + 16 remapped
# hardware IRQ lines, RFC-0036 Requirement 6.3), not just 0-31. A golden byte count catches any
# accidental change to its shape.
FIXTURE_TABLE="$ROOT/tests/fixtures/aps-exception-entry/exception-vector-table.abas"
"$ARCOFISSION" reveal "$FIXTURE_TABLE" at X86_64 --entry ExceptionTableBase > "$TMP_ROOT/table.x86"
grep -qF 'TEXT 1045 bytes' "$TMP_ROOT/table.x86"

# The real proof: PIC remap -> PIT channel-0 programming -> the extended table's IRQ dispatch
# branch (mark pending, EOI, resume) -> Timer.Ticks() observing what the handler wrote, all under
# a real CPU. A missing or wrong EOI manifests as exactly one tick ever arriving (RFC-0036 Section
# 2) -- this fixture's own WHILE loop would spin forever instead of reaching 100, which is exactly
# the failure mode this proof is built to catch rather than let masquerade as "QEMU is just slow".
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-timer-tick.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-timer-tick.efi" --target uefi-x86_64 > /dev/null

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-timer-tick.efi" "APS TIMER OK" 30)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS TIMER OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: PIC remap + PIT programming + the extended interrupt-vector table's IRQ dispatch deliver and count at least 100 real hardware timer ticks under a real CPU (QEMU/OVMF)"
