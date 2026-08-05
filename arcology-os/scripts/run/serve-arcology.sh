#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ARCOLOGY_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
REPO_ROOT="$(cd -- "$ARCOLOGY_DIR/.." && pwd)"

DB_PATH="$ARCOLOGY_DIR/var/local/arcology-v01a.arcodb"
OUT_DIR="$ARCOLOGY_DIR/dist/commons"
HOST="127.0.0.1"
PORT="8080"
MAX_REQUESTS="0"
BUILD_IF_MISSING=1

usage() {
    cat <<USAGE
Usage: arcology-os/scripts/run/serve-arcology.sh [options]

Options:
  --db PATH             Arcology database path (default: ./arcology-os/var/local/arcology-v01a.arcodb)
  --out DIR             Static export directory (default: ./arcology-os/dist/commons)
  --host HOST           Host/interface to bind (default: 127.0.0.1)
  --port PORT           TCP port to bind (default: 8080)
  --max-requests N      Stop after N requests; 0 means run until Ctrl-C
  --no-build            Do not build arcosh if build/arcosh is missing
  -h, --help            Show this help

Examples:
  arcology-os/scripts/run/serve-arcology.sh
  arcology-os/scripts/run/serve-arcology.sh --port 8088
  arcology-os/scripts/run/serve-arcology.sh --host 0.0.0.0 --port 8080
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --db)
            DB_PATH="$2"
            shift 2
            ;;
        --out)
            OUT_DIR="$2"
            shift 2
            ;;
        --host)
            HOST="$2"
            shift 2
            ;;
        --port)
            PORT="$2"
            shift 2
            ;;
        --max-requests)
            MAX_REQUESTS="$2"
            shift 2
            ;;
        --no-build)
            BUILD_IF_MISSING=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "serve-arcology: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

ARCOSH="$REPO_ROOT/build/arcosh"
if [[ ! -x "$ARCOSH" ]]; then
    if [[ "$BUILD_IF_MISSING" == "1" ]]; then
        cmake -S "$REPO_ROOT" -B "$REPO_ROOT/build"
        cmake --build "$REPO_ROOT/build" --target arcosh
    elif command -v arcosh >/dev/null 2>&1; then
        ARCOSH="$(command -v arcosh)"
    else
        echo "serve-arcology: build/arcosh is missing; run cmake --build build --target arcosh" >&2
        exit 1
    fi
fi

mkdir -p "$(dirname -- "$DB_PATH")"
mkdir -p "$OUT_DIR"

export ARCOBASIC_STDLIB="$ARCOLOGY_DIR/stdlib"
cd "$REPO_ROOT"

exec "$ARCOSH" --safe "$ARCOLOGY_DIR/examples/arcology_serve_static.abas" \
    "$DB_PATH" "$OUT_DIR" "$PORT" "$HOST" "$MAX_REQUESTS"
