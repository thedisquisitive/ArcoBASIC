#!/bin/sh
set -eu

base="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
sock="${1:-$base/build/qemu/service-smoke.sock}"
mkdir -p "$(dirname "$sock")"
rm -f "$sock"

"$base/build/lazarus-current/lazarus-service" \
	--config "$base/qemu/bench.profile" \
	--socket "$sock" &
pid="$!"

cleanup() {
	kill "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true
	rm -f "$sock"
}
trap cleanup EXIT INT TERM

for _ in $(seq 1 50); do
	[ -S "$sock" ] && break
	sleep 0.1
done

python3 - "$sock" <<'PY'
import socket
import sys

sock = sys.argv[1]
for request in [
    b'{"command":"ping"}\n',
    b'{"command":"profile"}\n',
    b'{"command":"devices"}\n',
]:
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.connect(sock)
    client.sendall(request)
    data = client.recv(65536).decode()
    print(data.splitlines()[0])
    client.close()
PY
