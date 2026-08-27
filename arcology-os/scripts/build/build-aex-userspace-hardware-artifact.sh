#!/usr/bin/env bash
set -euo pipefail

ARCOLOGY_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
REPO_ROOT=$(cd "$ARCOLOGY_DIR/.." && pwd)
ARCOFISSION="${1:-$REPO_ROOT/build/ArcoFission}"
OUTPUT_DIR="${2:-$ARCOLOGY_DIR/dist/arcology-aex-userspace-live}"
TMP_ROOT="${TMPDIR:-/tmp}/arcology-aex-userspace-live-$$"

cleanup() {
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

if [ ! -x "$ARCOFISSION" ]; then
    echo "ERROR: ArcoFission executable not found: $ARCOFISSION" >&2
    echo "Build the repository first, or pass the executable as argument 1." >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required for deterministic FAT32 image construction." >&2
    exit 2
fi

mkdir -p "$OUTPUT_DIR" "$TMP_ROOT"

EMBEDDED_AEX="$TMP_ROOT/aex-userspace-embedded.abas"
COMPOSED="$TMP_ROOT/aex-userspace-hardware.abas"
python3 "$ARCOLOGY_DIR/scripts/build/generate-aex-userspace-smoke.py" "$EMBEDDED_AEX" --format abas

{
    sed -n '1,1578p' "$ARCOLOGY_DIR/stdlib/arcfs_policy.abas" | sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d'
    printf '\n'
    sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d' "$ARCOLOGY_DIR/stdlib/aex_format_policy.abas"
    printf '\n'
    cat "$EMBEDDED_AEX"
    printf '\n'
    awk '
        /^FUNCTION PopulatePreloadedAexForHardware\(\) AS U64$/ { skipping = 1; next }
        skipping == 1 && /^END FUNCTION$/ { skipping = 0; next }
        skipping == 0 { print }
    ' "$ARCOLOGY_DIR/tests/fixtures/aex-preloaded-boot/aex-preloaded-boot.abas" |
        sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d;/^#CALLCONV /d;/^#EXPORT /d'
} > "$COMPOSED"

"$ARCOFISSION" build "$COMPOSED" -o "$OUTPUT_DIR/BOOTX64.EFI" --target uefi-x86_64 --entry Main
python3 "$ARCOLOGY_DIR/scripts/build/build-arcology-hardware-image.py" \
    "$OUTPUT_DIR/BOOTX64.EFI" "$OUTPUT_DIR/arcology-aex-userspace-live-x86_64.img"
cat > "$OUTPUT_DIR/expected-output.txt" <<'EOF'
ARCOLOGY AEX USERSPACE TEST
AEX USERSPACE PASS
System halted intentionally.
EOF
(
    cd "$OUTPUT_DIR"
    sha256sum BOOTX64.EFI arcology-aex-userspace-live-x86_64.img expected-output.txt > SHA256SUMS
)

echo "AEX USERSPACE HARDWARE ARTIFACT WRITTEN $OUTPUT_DIR/arcology-aex-userspace-live-x86_64.img"
echo "CHECKSUMS WRITTEN $OUTPUT_DIR/SHA256SUMS"
