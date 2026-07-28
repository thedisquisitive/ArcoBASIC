#!/bin/sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
lazarus="$root/lazarus"
runner="$lazarus/lazarus-os/qemu/run-live.sh"
iso="$lazarus/lazarus-os/build/arcology-lazarus-live.iso"

if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then
	echo "qemu-system-x86_64 was not found." >&2
	echo "Install QEMU before launching Arcology Lazarus." >&2
	exit 1
fi

if [ ! -x "$runner" ]; then
	echo "Lazarus QEMU runner was not found or is not executable:" >&2
	echo "  $runner" >&2
	exit 1
fi

if [ ! -f "$iso" ]; then
	echo "The Lazarus live ISO has not been built:" >&2
	echo "  $iso" >&2
	echo "Build it from the Lazarus project before launching." >&2
	exit 1
fi

if pgrep -f '^qemu-system-x86_64 .*lazarus-os/build/qemu' >/dev/null 2>&1; then
	echo "A Lazarus QEMU session is already using the test drives." >&2
	echo "Close that VM before launching another one." >&2
	exit 1
fi

echo "Launching Arcology Lazarus from:"
echo "  $iso"

exec "$runner" "$iso"
