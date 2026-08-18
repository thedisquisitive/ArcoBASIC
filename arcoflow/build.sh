#!/usr/bin/env bash
# Builds ArcoFlow's capsules into arcoflow/build/ -- run from the repository root or anywhere
# else, it locates itself. Native always builds; the web capsule only builds if
# ARCOFISSION_WEB_TOOLCHAIN_DIR is set (see "Web Capsules" in docs/arcofission.md for how to set
# one up) so a plain desktop checkout doesn't need Emscripten installed just to build ArcoFlow.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ARCOFISSION="${ARCOFISSION:-$REPO_ROOT/build/ArcoFission}"

if [ ! -x "$ARCOFISSION" ]; then
    echo "arcoflow/build.sh: $ARCOFISSION not found -- build the ArcoFission target first" >&2
    echo "  (cmake --build $REPO_ROOT/build --target ArcoFission), or set ARCOFISSION=/path/to/it" >&2
    exit 1
fi

mkdir -p "$SCRIPT_DIR/build/web"

echo "==> native: arcoflow/build/arcoflow"
"$ARCOFISSION" native "$SCRIPT_DIR/arcoflow.abas" -o "$SCRIPT_DIR/build/arcoflow"

if [ -n "${ARCOFISSION_WEB_TOOLCHAIN_DIR:-}" ]; then
    echo "==> web: arcoflow/build/web/arcoflow.html"
    "$ARCOFISSION" native "$SCRIPT_DIR/arcoflow.abas" -o "$SCRIPT_DIR/build/web/arcoflow.html" --target web
else
    echo "==> skipping web capsule (ARCOFISSION_WEB_TOOLCHAIN_DIR not set; see docs/arcofission.md)"
fi
