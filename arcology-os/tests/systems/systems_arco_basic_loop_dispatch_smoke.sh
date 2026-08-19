#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# The extended interrupt-vector table (RFC-0036) is an unchanged dependency of this fixture; a
# golden byte count catches any accidental change to its shape leaking in through this RFC's work.
FIXTURE_TABLE="$ROOT/tests/fixtures/aps-exception-entry/exception-vector-table.abas"
"$ARCOFISSION" reveal "$FIXTURE_TABLE" at X86_64 --entry ExceptionTableBase > "$TMP_ROOT/table.x86"
grep -qF 'TEXT 1045 bytes' "$TMP_ROOT/table.x86"

# The real proof: hardware tick -> RFC-0036's top-half ISR -> Loop.RunUntil's CPU.Halt/dispatch
# cycle -> LoopDispatchPendingWork's clear-before-invoke -> the registered hook actually running,
# under a real CPU, exactly once per requested wake (20 wakes requested, 20 hook invocations and
# 20 observed wakes both asserted inside the fixture itself before it will print its marker at
# all -- a wrong count fails closed into CPU.HaltForever rather than printing a marker that would
# misrepresent what was actually proven). The fixture also asserts Requirement 6.3's
# replace-not-duplicate semantics before this point: a decoy hook registered against event source
# 0 first, then the real one, MUST leave exactly one active row with the second call's slot value
# -- also fails closed (a distinct "APS REGISTER REPLACE BAD"/"APS REGISTER SLOT BAD" marker over
# the UEFI console, not the serial marker this harness matches on) if that regresses.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-loop-dispatch.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-loop-dispatch.efi" --target uefi-x86_64 > /dev/null

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/aps-loop-dispatch.efi" "APS LOOP OK" 30)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS LOOP OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: the APS Substrate Dispatch Loop delivers a real hardware timer tick to a registered hook, exactly once per wake, under a real CPU (QEMU/OVMF)"
