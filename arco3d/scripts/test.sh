#!/usr/bin/env bash
# Runs every arco3d test as a real, standalone compiled capsule (arco3d/build/<name>, built by
# arco3d/build.sh) -- no ArcoFission toolchain needed at run time, only at build time. Run from
# anywhere (it locates itself); builds everything first if arco3d/build/ is missing or stale.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ARCO3D_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

cd "$ARCO3D_DIR"

status=0
for test_file in tests/*.abas; do
    name="$(basename "$test_file" .abas)"
    capsule="build/$name"
    if [ ! -x "$capsule" ] || [ "$test_file" -nt "$capsule" ]; then
        "$ARCO3D_DIR/build.sh"
    fi
    echo "== $name =="
    if ! "./$capsule"; then
        status=1
    fi
    echo
done

exit "$status"
