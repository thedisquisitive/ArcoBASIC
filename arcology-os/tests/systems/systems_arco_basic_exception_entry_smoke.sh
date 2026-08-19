#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/exception-vector-table.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry ExceptionTableBase > "$TMP_ROOT/amir.txt"
grep -qF 'EXCEPTIONVECTORTABLEBASE' "$TMP_ROOT/amir.txt"

"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry ExceptionTableBase > "$TMP_ROOT/x86.txt"

# The reference to the synthesized table is a RIP-relative LEA, resolved through the same
# internal_calls mechanism ordinary function calls use.
grep -qF 'REL32_TO $CPU.ExceptionVectorTable' "$TMP_ROOT/x86.txt"

# The table is always appended once: 32 fixed-stride (16 byte) per-vector stubs + a 74-byte
# shared handler (no RIP adjustment on the #BP recovery path -- INT3 is a trap, not a fault, so
# the pushed return RIP already points past the one-byte opcode) = 586 bytes, plus this fixture's
# own small entry function. A golden byte count catches any accidental change to the table's shape
# (a stub growing past its 16-byte slot, a register added/removed from the save list, ...).
grep -qF 'TEXT 612 bytes' "$TMP_ROOT/x86.txt"

# The TEXT dump wraps at 8 bytes/line, so flatten it into one contiguous byte stream before
# substring-matching multi-byte sequences that may straddle a wrap point.
awk '/^TEXT [0-9]+ bytes/{on=1; next} /^$/{on=0} on{ $1=""; print }' "$TMP_ROOT/x86.txt" | tr -d ' \n' > "$TMP_ROOT/text_stream.txt"

# Vectors with a hardware-pushed error code get a single-push stub (just the vector number);
# every other vector normalizes the frame with a placeholder push first. Spot-check one of each:
# vector 3 (#BP, no error code) and vector 14 (#PF, has one).
grep -qF '6a006a03e9' "$TMP_ROOT/text_stream.txt"
grep -qF '6a0ee9' "$TMP_ROOT/text_stream.txt"

# The shared handler ends with the register-restore sequence and IRETQ, never RET -- it is an
# interrupt handler, not an ordinary function.
grep -qF '4883c41048cf' "$TMP_ROOT/text_stream.txt"

echo 'PASS: CPU.ExceptionVectorTableBase addresses a synthesized, vector-aware exception-entry table'
