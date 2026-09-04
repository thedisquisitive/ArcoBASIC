#!/usr/bin/env bash
# Runs the real headless functional test (tests/test_headless.gd) against the actual Main.gd/
# A3DFormat.gd logic -- no display needed. Run from anywhere; it locates itself.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
GODOT="${GODOT:-$HOME/godot/godot}"

if [ ! -x "$GODOT" ]; then
    echo "run_tests.sh: $GODOT not found -- set GODOT=/path/to/godot" >&2
    exit 1
fi

"$GODOT" --headless --path "$PROJECT_DIR" --script tests/test_headless.gd
