#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Builds the .deb via build-deb.sh (streaming its output normally) while still
# capturing the package path it prints on its final line, then installs it.
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

"$SCRIPT_DIR/build-deb.sh" | tee "$LOG"
DEB="$(tail -n 1 "$LOG")"

if [[ ! -f "$DEB" ]]; then
    echo "build-and-install: could not determine built .deb path (got '$DEB')" >&2
    exit 1
fi

echo "Installing $DEB ..."
sudo dpkg -i "$DEB" || sudo apt-get install -f -y

echo
command -v arco_cli
echo "Installed."
