#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/render-and-halt-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/render-and-halt/render-and-halt.abas"

"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: render-and-halt does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0006): a real GOP framebuffer, discovered dynamically (not a hardcoded
# resolution), filled with a two-tone test card covering the full real screen, then read back and
# verified pixel-for-pixel -- not just "GOP discovery succeeded," a real render genuinely happened.
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/render-and-halt.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello-with-gpu.sh" "$TMP_ROOT/render-and-halt.efi" "ARCOLOGY RENDER AND HALT DONE" 25)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: ARCOLOGY RENDER AND HALT DONE" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# Negative control: confirm the pixel-readback verification is real, not vacuous.
sed 's/IF accentValue <> 65535 THEN/IF accentValue <> 99999 THEN/' "$FIXTURE" > "$TMP_ROOT/negative.abas"
"$ARCOFISSION" build "$TMP_ROOT/negative.abas" -o "$TMP_ROOT/negative.efi" --target uefi-x86_64 > /dev/null
NEGATIVE_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello-with-gpu.sh" "$TMP_ROOT/negative.efi" "ARCOLOGY RENDER AND HALT VFAC" 25)"
[ "$NEGATIVE_OUTPUT" = "PASS: ARCOLOGY RENDER AND HALT VFAC" ] || { echo "FAIL: negative control did not report a real verify failure" >&2; exit 1; }

# The real USB-boot proof: this fixture is meant to ship as a dd-writable USB stick image, and
# real UEFI firmware routes USB boot media through its own USB Mass Storage Class driver stack --
# genuinely distinct from the plain block-device path every check above this line exercises.
# Wraps the SAME compiled .efi into the SAME real, byte-reproducible FAT32 image
# build-arcology-hardware-image.py produces (the actual artifact a real USB stick gets), then boots
# THAT image through a real qemu-xhci + usb-storage device, not a `-drive`.
python3 "$SOURCE_DIR/arcology-os/scripts/build/build-arcology-hardware-image.py" \
    "$TMP_ROOT/render-and-halt.efi" "$TMP_ROOT/render-and-halt-usb.img"
USB_OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-image-with-usb-gpu.sh" "$TMP_ROOT/render-and-halt-usb.img" "ARCOLOGY RENDER AND HALT DONE" 30)"
[ "$USB_OUTPUT" = "PASS: ARCOLOGY RENDER AND HALT DONE" ] || { echo "FAIL: unexpected USB-boot harness output" >&2; exit 1; }

echo "PASS: real GOP framebuffer render (full real screen, dynamic resolution, pixel-verified) AND real USB Mass Storage Class boot of the actual dd-writable image, negative control confirmed real"
