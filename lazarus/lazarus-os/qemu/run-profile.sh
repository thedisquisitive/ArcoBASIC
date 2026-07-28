#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
repo="$(CDPATH= cd -- "$base/../.." && pwd)"
profile="${1:-$base/qemu/profiles/live.env}"

case "$profile" in
	/*) ;;
	*) profile="$repo/$profile" ;;
esac

if [ ! -f "$profile" ]; then
	echo "QEMU profile not found: $profile" >&2
	echo "Available profiles:" >&2
	find "$base/qemu/profiles" -maxdepth 1 -type f -name '*.env' -printf '  %p\n' 2>/dev/null >&2 || true
	exit 1
fi

set -a
. "$profile"
set +a

case "${MODE:-}" in
	live)
		iso="${ISO:-$base/build/arcology-lazarus-live.iso}"
		case "$iso" in
			/*) ;;
			*) iso="$repo/$iso" ;;
		esac
		exec "$base/qemu/run-live.sh" "$iso"
		;;
	installed)
		system_disk="${SYSTEM_DISK:-$base/build/qemu/lazarus-system.qcow2}"
		case "$system_disk" in
			/*) ;;
			*) system_disk="$repo/$system_disk" ;;
		esac
		exec "$base/qemu/run-installed.sh" "$system_disk"
		;;
	*)
		echo "Invalid or missing MODE in profile: ${MODE:-}" >&2
		echo "Expected MODE=live or MODE=installed." >&2
		exit 1
		;;
esac
