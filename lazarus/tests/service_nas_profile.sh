#!/bin/sh
set -eu

service=${1:?service binary required}
root=$(mktemp -d)
pid=
cleanup() {
	[ -z "$pid" ] || kill "$pid" 2>/dev/null || true
	rm -rf "$root"
}
trap cleanup EXIT INT TERM

profile="$root/bench.profile"
helper="$root/storage-helper"
marker="$root/helper-ran"
requests="$root/requests"
responses="$root/responses"
cat >"$profile" <<'EOF'
name=NAS Regression
image_storage=/mnt/lazarus-storage/images
nas_storage_protocol=smb
nas_storage_server=nas.example.local
nas_storage_share=Backups
nas_storage_username=lazarus-backup
nas_storage_domain=WORKGROUP
EOF
cat >"$helper" <<EOF
#!/bin/sh
touch '$marker'
sleep 30
EOF
chmod +x "$helper"

# Status must be a read-only, bounded operation. It may report offline, but it must not invoke the
# mount helper and hold the kiosk startup path hostage.
response=$(printf '%s\n' '{"command":"profile"}' |
	timeout 3 "$service" --stdio --config "$profile" --storage-helper "$helper" --security "$root/admin.auth")
printf '%s\n' "$response" | grep -q '"storage_online":false'
printf '%s\n' "$response" | grep -q 'configured NAS is offline'
[ ! -e "$marker" ]

mkfifo "$requests" "$responses"
"$service" --stdio --config "$profile" --storage-helper "$helper" --security "$root/admin.auth" \
	<"$requests" >"$responses" 2>"$root/service.log" &
pid=$!
exec 3>"$requests"
exec 4<"$responses"
call() {
	printf '%s\n' "$1" >&3
	IFS= read -r line <&4
	printf '%s\n' "$line"
}

setup=$(call '{"command":"admin_setup","new_password":"test"}')
token=$(printf '%s\n' "$setup" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ]
save=$(call "{\"command\":\"save_profile\",\"admin_token\":\"$token\",\"name\":\"NAS Regression Updated\"}")
printf '%s\n' "$save" | grep -q '"ok":true'
grep -qx 'image_storage=/mnt/lazarus-storage/images' "$profile"
grep -qx 'nas_storage_protocol=smb' "$profile"
grep -qx 'nas_storage_server=nas.example.local' "$profile"
grep -qx 'nas_storage_share=Backups' "$profile"
grep -qx 'nas_storage_username=lazarus-backup' "$profile"
grep -qx 'nas_storage_domain=WORKGROUP' "$profile"
