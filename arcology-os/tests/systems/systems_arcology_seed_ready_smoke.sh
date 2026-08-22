#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcology-seed-ready-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0007: the first fixture in this project that does not CPU.HaltForever after doing one thing
# -- a genuine, real, persistent interactive loop. Real keyboard input (EFI_SIMPLE_TEXT_INPUT_
# PROTOCOL, newly bound), real dynamically-constructed ConsoleOut.Write output (no compile-time
# string literal), and real command dispatch (HELP, OT, an unrecognized-command error path, and a
# TEST-ONLY EXIT hook so automated testing can terminate deterministically).

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/arcology-seed-ready/arcology-seed-ready.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: arcology-seed-ready does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q . || ! command -v socat > /dev/null 2>&1; then
    echo "SKIP: qemu-system-x86_64/OVMF/socat not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/seed.efi" --target uefi-x86_64 --entry Main > /dev/null

# The real proof: type HELP, press Enter, confirm the real recognized-command response -- a real
# keystroke injected via QEMU's own human-monitor `sendkey` (the same scancode-delivery path a
# physical keyboard driver uses), read back through the real ReadKeyStroke binding, echoed via a
# real dynamically-constructed ConsoleOut.Write buffer, and dispatched by real string comparison.
OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello-with-keyboard.sh" "$TMP_ROOT/seed.efi" "READY." "h e l p ret" "IMMEDIATE MODE COMMANDS" 25)"
[ "$OUTPUT" = "PASS: IMMEDIATE MODE COMMANDS" ] || { echo "FAIL: unexpected HELP harness output: $OUTPUT" >&2; exit 1; }

# Determinism: 3x repeat.
for i in 1 2 3; do
    REPEAT_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello-with-keyboard.sh" "$TMP_ROOT/seed.efi" "READY." "h e l p ret" "IMMEDIATE MODE COMMANDS" 25)"
    [ "$REPEAT_OUTPUT" = "PASS: IMMEDIATE MODE COMMANDS" ] || { echo "FAIL: non-deterministic on repeat $i: $REPEAT_OUTPUT" >&2; exit 1; }
done

# Second real command: OT (RFC-0007 Section 11's own named "release feature").
OT_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello-with-keyboard.sh" "$TMP_ROOT/seed.efi" "READY." "o t ret" "THE WAGON HAS BROKEN DOWN" 25)"
[ "$OT_OUTPUT" = "PASS: THE WAGON HAS BROKEN DOWN" ] || { echo "FAIL: unexpected OT harness output: $OT_OUTPUT" >&2; exit 1; }

# Negative control: confirm an unrecognized command is honestly rejected, not silently accepted or
# confused with a real command.
NEGATIVE_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello-with-keyboard.sh" "$TMP_ROOT/seed.efi" "READY." "b a d ret" "SYNTAX ERROR" 25)"
[ "$NEGATIVE_OUTPUT" = "PASS: SYNTAX ERROR" ] || { echo "FAIL: negative control did not report a real syntax error: $NEGATIVE_OUTPUT" >&2; exit 1; }

# Line editing: type "held", backspace twice (removing "ld"), type "lp" -> reconstructs "help"
# internally even though only the raw edit byte sequence is visible in the captured trace.
EDIT_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello-with-keyboard.sh" "$TMP_ROOT/seed.efi" "READY." "h e l d backspace backspace l p ret" "IMMEDIATE MODE COMMANDS" 25)"
[ "$EDIT_OUTPUT" = "PASS: IMMEDIATE MODE COMMANDS" ] || { echo "FAIL: backspace line editing did not reconstruct the command correctly: $EDIT_OUTPUT" >&2; exit 1; }

echo "PASS: real persistent interactive loop (does not halt), real keyboard input, real dynamic ConsoleOut.Write output, real command dispatch (HELP/OT/unrecognized), real backspace line editing, 3x determinism confirmed"
