#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Structural check: LEN/MID misuse (wrong arity, non-STRING first argument) is rejected cleanly at
# compile time, not silently miscompiled or left to the generic "not a declared function" error.
cat > "$TMP_ROOT/misuse.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION BadArity(s AS STRING) AS U64
    RETURN LEN(s, s)
END FUNCTION
SCRIPT
if "$ARCOFISSION" reveal "$TMP_ROOT/misuse.abas" at X86_64 --entry BadArity > "$TMP_ROOT/misuse.txt" 2>&1; then
    echo "FAIL: LEN(s, s) (wrong arity) should have been rejected at compile time" >&2
    cat "$TMP_ROOT/misuse.txt" >&2
    exit 1
fi
grep -qF 'LEN expects exactly 1 argument' "$TMP_ROOT/misuse.txt" || {
    echo "FAIL: expected a clear LEN arity diagnostic, got:" >&2
    cat "$TMP_ROOT/misuse.txt" >&2
    exit 1
}

# LEN/MID only intercept a call when the first argument is CONFIDENTLY STRING -- LEN(n) where n is
# a plain integer falls through unchanged to the ordinary generic path (no user FUNCTION LEN
# exists), the same generic diagnostic as before this feature existed. This is deliberate, not a
# missed case: LEN is ALREADY legitimately used elsewhere in this shared pipeline for arrays,
# ranges, bitvectors, and objects (arcofission_alpha_smoke exercises all of those), and a hard type
# error here would have broken every one of them.
cat > "$TMP_ROOT/badtype.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION BadType(n AS U64) AS U64
    RETURN LEN(n)
END FUNCTION
SCRIPT
if "$ARCOFISSION" reveal "$TMP_ROOT/badtype.abas" at X86_64 --entry BadType > "$TMP_ROOT/badtype.txt" 2>&1; then
    echo "FAIL: LEN(n) on a non-STRING argument should have been rejected at compile time" >&2
    cat "$TMP_ROOT/badtype.txt" >&2
    exit 1
fi
grep -qF "not a declared function" "$TMP_ROOT/badtype.txt" || {
    echo "FAIL: expected the generic not-a-declared-function diagnostic, got:" >&2
    cat "$TMP_ROOT/badtype.txt" >&2
    exit 1
}

# Structural check: the happy path compiles cleanly to real X86_64 machine code.
cat > "$TMP_ROOT/good.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION ProbeLen(s AS STRING) AS U64
    RETURN LEN(s)
END FUNCTION
FUNCTION ProbeMid(s AS STRING) AS STRING
    RETURN MID(s, 1, 3)
END FUNCTION
SCRIPT
for entry in ProbeLen ProbeMid; do
    "$ARCOFISSION" reveal "$TMP_ROOT/good.abas" at X86_64 --entry "$entry" > "$TMP_ROOT/good.txt" 2>&1
    grep -qF 'X86_64 GENERATED' "$TMP_ROOT/good.txt" || {
        echo "FAIL: $entry does not compile:" >&2
        cat "$TMP_ROOT/good.txt" >&2
        exit 1
    }
done

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof: LEN (UTF-16 unit count via both a literal and a variable-sourced STRING) and MID
# (classic 1-based indexing: whole-string, middle slice, prefix, suffix, length clamping past the
# end, start=0/past-end/empty-source/length=0 all producing "", a variable-sourced string, a
# nested MID(MID(...)) proving the shared result buffer's overlapping read/write is genuinely safe,
# and the documented single-shared-buffer aliasing scope reduction POSITIVELY proven, not just
# asserted) under a real CPU.
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/string-len-mid.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/string-len-mid.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/string-len-mid.efi" "APS LEN MID OK" 15)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS LEN MID OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: freestanding LEN/MID are real, hand-assembled STRING builtins -- length via UTF-16 unit count, classic 1-based MID\$ indexing with documented clamping/empty-result rules, and the single-shared-result-buffer aliasing tradeoff positively proven -- under a real CPU (QEMU/OVMF)"
