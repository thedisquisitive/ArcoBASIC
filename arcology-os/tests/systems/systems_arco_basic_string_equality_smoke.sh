#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Regression check for the mov_load16_rax encoder bug found alongside the STRING equality fix
# (.agents/reports/freestanding-string-equality.md): a spurious 0x66 prefix made the encoded
# instruction decode as MOVZX AX, WORD PTR [RAX] (16-bit destination) instead of the intended
# MOVZX EAX, WORD PTR [RAX] (32-bit destination, architecturally zero-extending all of RAX).
# Verified against a real assembler when the fix was authored (`nasm` encodes
# `movzx eax, word [rax]` as exactly these three bytes, and refuses to encode a 16-bit-destination
# form at all). Every MEMORY.Read16 caller was always correct despite this bug (its result is
# unconditionally re-masked to 16 bits immediately afterward), so this is a pure encoding check,
# not a claim that prior fixtures were ever wrong.
cat > "$TMP_ROOT/load16.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION Probe(address AS MMIOPTR) AS U16
    RETURN MEMORY.Read16(address)
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/load16.abas" at X86_64 --entry Probe > "$TMP_ROOT/load16.x86"
awk '/^TEXT [0-9]+ bytes/{on=1; next} /^$/{on=0} on{ $1=""; print }' "$TMP_ROOT/load16.x86" | tr -d ' \n' > "$TMP_ROOT/load16_stream.txt"
grep -qF '0fb700' "$TMP_ROOT/load16_stream.txt"
if grep -qF '660fb700' "$TMP_ROOT/load16_stream.txt"; then
    echo "FAIL: mov_load16_rax still emits the incorrect 0x66 prefix" >&2
    exit 1
fi

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: ten STRING equality/inequality cases -- exact match, same-length mismatch, both
# prefix directions, both-empty, empty-vs-non-empty both directions, <> on a match and a mismatch,
# and a single-trailing-character difference -- under a real CPU, confirming `=`/`<>` on STRING
# operands compares UTF-16 text content, not pointer identity (the original bug: `path = "HELLO"`
# silently returned FALSE for an exact match under real execution).
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/string-equality.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/string-equality.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/string-equality.efi" "APS STRING EQ OK" 15)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS STRING EQ OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: freestanding STRING equality compares UTF-16 text content, not pointer identity, under a real CPU (QEMU/OVMF)"
