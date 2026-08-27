#!/usr/bin/env bash
set -euo pipefail

ARCOLOGY_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
REPO_ROOT=$(cd "$ARCOLOGY_DIR/.." && pwd)
ARCOFISSION="${1:-$REPO_ROOT/build/ArcoFission}"
OUTPUT_DIR="${2:-$ARCOLOGY_DIR/dist/aps-aex-substrate-shell}"
TMP_ROOT="${TMPDIR:-/tmp}/aps-aex-substrate-shell-artifact-$$"

cleanup() {
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

if [ ! -x "$ARCOFISSION" ]; then
    echo "ERROR: ArcoFission executable not found: $ARCOFISSION" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required for deterministic FAT32 image construction." >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR" "$TMP_ROOT"
COMPOSED="$TMP_ROOT/aps-aex-substrate-shell.abas"
"$ARCOLOGY_DIR/scripts/build/compose-aps-aex-substrate-shell.sh" "$COMPOSED"

"$ARCOFISSION" build "$COMPOSED" -o "$OUTPUT_DIR/BOOTX64.EFI" --target uefi-x86_64 --entry Main
python3 "$ARCOLOGY_DIR/scripts/build/build-arcology-hardware-image.py" \
    "$OUTPUT_DIR/BOOTX64.EFI" "$OUTPUT_DIR/aps-aex-substrate-shell-x86_64.img"
cat > "$OUTPUT_DIR/expected-output.txt" <<'EOF'
SUBSTRATE LIVE
:system: >
EOF
(
    cd "$OUTPUT_DIR"
    sha256sum BOOTX64.EFI aps-aex-substrate-shell-x86_64.img expected-output.txt > SHA256SUMS
)

echo "APS AEX SUBSTRATE SHELL ARTIFACT WRITTEN $OUTPUT_DIR/aps-aex-substrate-shell-x86_64.img"
echo "CHECKSUMS WRITTEN $OUTPUT_DIR/SHA256SUMS"
