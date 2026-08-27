#!/usr/bin/env bash
set -euo pipefail

ARCOLOGY_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUTPUT="${1:?usage: compose-aps-aex-substrate-shell.sh OUTPUT.abas}"
TMP_ROOT="${TMPDIR:-/tmp}/aps-aex-substrate-compose-$$"

cleanup() {
    rm -rf "$TMP_ROOT"
}
trap cleanup EXIT

mkdir -p "$TMP_ROOT" "$(dirname "$OUTPUT")"
EMBEDDED_AEX="$TMP_ROOT/aex-userspace-embedded.abas"
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
    ' "$ARCOLOGY_DIR/tests/fixtures/aps-arcology-seed-substrate/aps-arcology-seed-substrate.abas" |
        sed '/^#PROFILE /d;/^#TARGET /d;/^#RUNTIME /d;/^#CALLCONV /d;/^#EXPORT /d'
} > "$OUTPUT"
