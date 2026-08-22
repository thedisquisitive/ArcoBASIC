#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/gpt-sysvol-render-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0044 Phase 3: the combined chain -- real firmware ESP boot, real multi-handle BlockIo
# enumeration (Phase 1) selecting the ArcFS partition by known geometry (a real, honest finding
# from development: a single GPT disk yields 4 real handles here, not the 2 RFC-0044 Section 6's
# own literal "SelectDiscovered(1)" assumed -- see the fixture's own header comment), real
# ArcFS.FormatVolume/write/commit/ActivateSystemVolume (RFC-0039 Phase G's real boot policy, never
# before proven on the SAME disk the system booted from), and render-and-halt.abas's own real GOP
# render + pixel-readback self-verification (RFC-0006/WP-030) -- all in one boot, on the single
# real GPT-partitioned disk Phase 2 builds, not two separate -drive entries.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/gpt-sysvol-render/gpt-sysvol-render.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: gpt-sysvol-render does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/payload.efi" --target uefi-x86_64 --entry Main > /dev/null
python3 "$SOURCE_DIR/arcology-os/scripts/build/build-arcology-gpt-image.py" "$TMP_ROOT/payload.efi" "$TMP_ROOT/gpt-disk.img"

# The real proof: real firmware GPT-aware ESP boot -> real multi-handle enumeration -> real ArcFS
# format+write+commit+activate -> real GOP render+self-verify, on the real single-disk topology.
OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-image-with-usb-gpu.sh" "$TMP_ROOT/gpt-disk.img" "ARCOLOGY GPT SYSVOL DONE" 40)"
[ "$OUTPUT" = "PASS: ARCOLOGY GPT SYSVOL DONE" ] || { echo "FAIL: unexpected harness output: $OUTPUT" >&2; exit 1; }

# Determinism: 3x repeat.
for i in 1 2 3; do
    REPEAT_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-image-with-usb-gpu.sh" "$TMP_ROOT/gpt-disk.img" "ARCOLOGY GPT SYSVOL DONE" 40)"
    [ "$REPEAT_OUTPUT" = "PASS: ARCOLOGY GPT SYSVOL DONE" ] || { echo "FAIL: non-deterministic on repeat $i: $REPEAT_OUTPUT" >&2; exit 1; }
done

# Negative control: confirm the pixel-readback verification is real, not vacuous -- matching
# render-and-halt.abas's own established negative control.
sed 's/IF accentValue <> 65535 THEN/IF accentValue <> 99999 THEN/' "$FIXTURE" > "$TMP_ROOT/negative.abas"
"$ARCOFISSION" build "$TMP_ROOT/negative.abas" -o "$TMP_ROOT/negative.efi" --target uefi-x86_64 --entry Main > /dev/null
python3 "$SOURCE_DIR/arcology-os/scripts/build/build-arcology-gpt-image.py" "$TMP_ROOT/negative.efi" "$TMP_ROOT/negative-disk.img"
NEGATIVE_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-image-with-usb-gpu.sh" "$TMP_ROOT/negative-disk.img" "ARCOLOGY GPT SYSVOL VFAC" 40)"
[ "$NEGATIVE_OUTPUT" = "PASS: ARCOLOGY GPT SYSVOL VFAC" ] || { echo "FAIL: negative control did not report a real verify failure: $NEGATIVE_OUTPUT" >&2; exit 1; }

echo "PASS: real single-disk GPT ESP boot + real multi-handle enumeration selecting the ArcFS partition by geometry + real ArcFS format/write/commit/ActivateSystemVolume + real GOP render with pixel-readback self-verification, all in one boot; 3x determinism and negative control confirmed real"
