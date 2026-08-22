#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/gpt-image-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0044 Phase 2: a real GPT-partitioned disk image (protective MBR + real primary/backup GPT
# header+partition array + a real ESP containing BOOTX64.EFI + a reserved, zeroed ArcFS partition)
# -- the actual shape a real USB stick or disk gets, not two separate -drive entries.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/render-and-halt/render-and-halt.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/payload.efi" --target uefi-x86_64 > /dev/null

# Determinism: this tool must be byte-reproducible like every other image builder in this project
# (a stale __pycache__ was found to violate this during development -- see the tool's own header
# comment and its `sys.dont_write_bytecode = True` guard; both scripts clear any pre-existing cache
# here too, so this check can never be fooled by a leftover cache from an unrelated earlier run).
find "$SOURCE_DIR/arcology-os/scripts/build" -iname "__pycache__" -exec rm -rf {} + 2>/dev/null || true
python3 "$SOURCE_DIR/arcology-os/scripts/build/build-arcology-gpt-image.py" "$TMP_ROOT/payload.efi" "$TMP_ROOT/gpt-a.img"
python3 "$SOURCE_DIR/arcology-os/scripts/build/build-arcology-gpt-image.py" "$TMP_ROOT/payload.efi" "$TMP_ROOT/gpt-b.img"
SUM_A="$(sha256sum "$TMP_ROOT/gpt-a.img" | awk '{print $1}')"
SUM_B="$(sha256sum "$TMP_ROOT/gpt-b.img" | awk '{print $1}')"
[ "$SUM_A" = "$SUM_B" ] || { echo "FAIL: build-arcology-gpt-image.py is not byte-reproducible ($SUM_A != $SUM_B)" >&2; exit 1; }
echo "PASS: byte-reproducible ($SUM_A)"

# Independent structural verification (RFC-0044 Section 7.3 Phase 2 acceptance evidence): a real
# parser -- not the builder's own writing logic re-run and diffed -- recomputes both GPT CRC32s,
# confirms primary/backup agree, confirms both partitions' type GUIDs/LBA ranges, and walks the
# ESP's own real FAT32 directory tree from raw bytes to extract and checksum BOOTX64.EFI.
python3 "$SOURCE_DIR/arcology-os/scripts/build/verify_gpt_image.py" "$TMP_ROOT/gpt-a.img"

# Negative control: confirm the structural verifier is a real check, not vacuous -- corrupt one
# byte inside the primary partition array and confirm it is caught via CRC32 mismatch.
python3 - "$TMP_ROOT/gpt-a.img" "$TMP_ROOT/gpt-corrupt.img" <<'PYEOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
data[1024] ^= 0xFF
open(sys.argv[2], "wb").write(bytes(data))
PYEOF
if python3 "$SOURCE_DIR/arcology-os/scripts/build/verify_gpt_image.py" "$TMP_ROOT/gpt-corrupt.img" > "$TMP_ROOT/corrupt-verify.txt" 2>&1; then
    echo "FAIL: verifier did not detect a corrupted partition array" >&2
    cat "$TMP_ROOT/corrupt-verify.txt" >&2
    exit 1
fi
grep -qF "CRC32 mismatch" "$TMP_ROOT/corrupt-verify.txt" || { echo "FAIL: verifier failed for the wrong reason" >&2; cat "$TMP_ROOT/corrupt-verify.txt" >&2; exit 1; }
echo "PASS: negative control confirmed real (corrupted partition array correctly detected)"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; the real boot proof below only runs when executed."
    exit 0
fi

# The real proof: real firmware GPT-aware boot-device enumeration finds the ESP inside a real
# multi-partition disk (not the superfloppy convention every other artifact in this project uses)
# and boots it -- via a real USB Mass Storage Class device (the actual real-hardware driver path),
# with a real GOP device attached so render-and-halt's own full render+self-verification also runs.
OUTPUT="$("$SOURCE_DIR/arcology-os/scripts/run/run-uefi-image-with-usb-gpu.sh" "$TMP_ROOT/gpt-a.img" "ARCOLOGY RENDER AND HALT DONE" 30)"
[ "$OUTPUT" = "PASS: ARCOLOGY RENDER AND HALT DONE" ] || { echo "FAIL: unexpected boot harness output: $OUTPUT" >&2; exit 1; }

echo "PASS: real GPT-partitioned disk image (protective MBR + real primary/backup GPT + real ESP + reserved ArcFS partition) is byte-reproducible, independently structurally verified with a real negative control, and boots via real USB Mass Storage Class + real GOP to a full render-and-halt completion"
