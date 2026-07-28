#!/bin/sh
set -eu

service=$1
root=$(mktemp -d)
pid=
cleanup() {
	[ -z "$pid" ] || kill "$pid" 2>/dev/null || true
	rm -rf "$root"
}
trap cleanup EXIT INT TERM

mkdir -p "$root/images"
printf 'name=Network Test\nimage_storage=%s/images\n' "$root" > "$root/bench.profile"
cat > "$root/network-helper" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> '$root/helper.log'
EOF
chmod +x "$root/network-helper"

mkfifo "$root/requests" "$root/responses"
"$service" --config "$root/bench.profile" --security "$root/admin.auth" \
	--network-config "$root/network.conf" --network-helper "$root/network-helper" --stdio \
	<"$root/requests" >"$root/responses" 2>"$root/service.log" &
pid=$!
exec 3>"$root/requests"
exec 4<"$root/responses"
call() {
	printf '%s\n' "$1" >&3
	IFS= read -r response <&4
	printf '%s\n' "$response"
}

call '{"command":"network_config"}' | grep -q '"mode":"dhcp"'
call '{"command":"network_apply","mode":"dhcp","interface":"auto"}' | grep -q '"ok":false'
setup=$(call '{"command":"admin_setup","new_password":"network administration password"}')
token=$(printf '%s\n' "$setup" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ]

call "{\"command\":\"network_apply\",\"admin_token\":\"$token\",\"mode\":\"static\",\"interface\":\"auto\",\"address\":\"192.0.2.10\",\"prefix\":\"24\"}" | grep -q 'Static addressing requires'
call "{\"command\":\"network_apply\",\"admin_token\":\"$token\",\"mode\":\"dhcp\",\"interface\":\"auto\"}" | grep -q '"ok":true'
grep -q '^mode=dhcp$' "$root/network.conf"
grep -q '^interface=auto$' "$root/network.conf"
grep -q -- "--config $root/network.conf --restart" "$root/helper.log"
