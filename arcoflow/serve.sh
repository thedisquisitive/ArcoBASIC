#!/usr/bin/env bash
# Serves the web capsule over plain HTTP and prints the URL to open. Opening arcoflow.html
# directly via a file:// URL doesn't work -- browsers refuse to fetch a .wasm file across the
# file:// origin (CORS), so the page aborts with "both async and sync fetching of the wasm
# failed" before it ever runs. Build the web capsule first with arcoflow/build.sh (needs
# ARCOFISSION_WEB_TOOLCHAIN_DIR set -- see "Web Capsules" in docs/arcofission.md).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WEB_DIR="$SCRIPT_DIR/build/web"
PORT="${1:-8000}"

if [ ! -f "$WEB_DIR/arcoflow.html" ]; then
    echo "arcoflow/serve.sh: $WEB_DIR/arcoflow.html not found -- build it first:" >&2
    echo "  ARCOFISSION_WEB_TOOLCHAIN_DIR=/path/to/build-wasm arcoflow/build.sh" >&2
    exit 1
fi

echo "Serving $WEB_DIR at http://127.0.0.1:$PORT/arcoflow.html"
echo "(Ctrl+C to stop)"
cd "$WEB_DIR"
exec python3 -m http.server "$PORT"
