#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
ARCOFISSION="${1:-$SOURCE_DIR/build/ArcoFission}"
OUTPUT_DIR="${2:-$SOURCE_DIR/dist/arcology-seed-0.1}"
SOURCE="$SOURCE_DIR/tests/systems/arcology-hardware-test/hardware-test.abas"

if [ ! -x "$ARCOFISSION" ]; then
    echo "ERROR: ArcoFission executable not found: $ARCOFISSION" >&2
    echo "Build the repository first, or pass the executable as argument 1." >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required for deterministic FAT32 image construction." >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR"
"$ARCOFISSION" build "$SOURCE" -o "$OUTPUT_DIR/BOOTX64.EFI" --target uefi-x86_64
python3 "$SOURCE_DIR/scripts/build-arcology-hardware-image.py" \
    "$OUTPUT_DIR/BOOTX64.EFI" "$OUTPUT_DIR/arcology-seed-0.1-x86_64.img"
(
    cd "$OUTPUT_DIR"
    sha256sum BOOTX64.EFI arcology-seed-0.1-x86_64.img > SHA256SUMS
)

echo "HARDWARE ARTIFACT WRITTEN $OUTPUT_DIR/arcology-seed-0.1-x86_64.img"
echo "CHECKSUMS WRITTEN $OUTPUT_DIR/SHA256SUMS"
