#!/bin/sh
set -eu

target=/etc/sudoers.d/lazarus-build

if [ "$(id -u)" -ne 0 ]; then
	echo "Run this once as root: sudo $0 [username]" >&2
	exit 1
fi

user="${1:-${SUDO_USER:-}}"
if [ -z "$user" ] || [ "$user" = root ]; then
	echo "Specify the non-root desktop user: sudo $0 daedalus" >&2
	exit 1
fi
case "$user" in
	*[!A-Za-z0-9_-]*)
		echo "Refusing unsafe sudoers username: $user" >&2
		exit 1
		;;
esac
if ! id "$user" >/dev/null 2>&1; then
	echo "User does not exist: $user" >&2
	exit 1
fi
if ! command -v visudo >/dev/null 2>&1; then
	echo "visudo is required to validate the sudoers rule." >&2
	exit 1
fi

temporary="$(mktemp /etc/sudoers.d/.lazarus-build.XXXXXX)"
trap 'rm -f "$temporary"' EXIT INT TERM
printf '%s ALL=(root) NOPASSWD: ALL\n' "$user" > "$temporary"
chmod 0440 "$temporary"
visudo -c -f "$temporary"
mv "$temporary" "$target"
trap - EXIT INT TERM

echo "Installed: $target"
echo "User '$user' can now run any command as root with sudo without a password."
echo "Test with: sudo -n true"
