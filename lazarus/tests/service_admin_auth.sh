#!/bin/sh
set -eu

service=$1
root=$(mktemp -d)
security="$root/admin.auth"
backup="$root/persistent/admin.auth"
technicians="$root/technicians.auth"
technicians_backup="$root/persistent/technicians.auth"
profile="$root/bench.profile"
requests="$root/requests"
responses="$root/responses"
pid=
cleanup() {
    [ -z "$pid" ] || kill "$pid" 2>/dev/null || true
    rm -rf "$root"
}
trap cleanup EXIT INT TERM

printf 'name=Security Test\nimage_storage=%s/images\n' "$root" > "$profile"
mkdir -p "$root/images" "$(dirname "$backup")"
mkfifo "$requests" "$responses"
"$service" --config "$profile" --security "$security" --security-backup "$backup" \
    --technicians "$technicians" --technicians-backup "$technicians_backup" \
    --stdio <"$requests" >"$responses" 2>"$root/service.log" &
pid=$!
exec 3>"$requests"
exec 4<"$responses"

call() {
    printf '%s\n' "$1" >&3
    IFS= read -r response <&4
    printf '%s\n' "$response"
}

call '{"command":"admin_status"}' | grep -q '"configured":false'
call '{"command":"admin_setup","new_password":""}' | grep -q '"ok":false'
setup=$(call '{"command":"admin_setup","new_password":"x"}')
printf '%s\n' "$setup" | grep -q '"ok":true'
token=$(printf '%s\n' "$setup" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
recovery=$(printf '%s\n' "$setup" | sed -n 's/.*"recovery_key":"\([^"]*\)".*/\1/p')
[ -n "$token" ]
[ -n "$recovery" ]
[ "$(stat -c '%a' "$security")" = 600 ]
cmp "$security" "$backup"

call '{"command":"save_profile","name":"Unauthorized"}' | grep -q '"ok":false'
call '{"command":"admin_login","password":"incorrect password"}' | grep -q '"ok":false'
call '{"command":"admin_login","password":"x"}' | grep -q '"ok":true'
call "{\"command\":\"admin_change_password\",\"admin_token\":\"$token\",\"new_password\":\"y\"}" | grep -q '"ok":true'
cmp "$security" "$backup"
call '{"command":"admin_login","password":"x"}' | grep -q '"ok":false'
login=$(call '{"command":"admin_login","password":"y"}')
printf '%s\n' "$login" | grep -q '"ok":true'
token=$(printf '%s\n' "$login" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ]
call "{\"command\":\"admin_login\",\"recovery_key\":\"$recovery\"}" | grep -q '"recovery_login":true'
rotate=$(call "{\"command\":\"admin_rotate_recovery\",\"admin_token\":\"$token\"}")
printf '%s\n' "$rotate" | grep -q '"recovery_key_shown_once":true'
new_recovery=$(printf '%s\n' "$rotate" | sed -n 's/.*"recovery_key":"\([^"]*\)".*/\1/p')
[ -n "$new_recovery" ]
cmp "$security" "$backup"
call "{\"command\":\"admin_login\",\"recovery_key\":\"$recovery\"}" | grep -q '"ok":false'
call "{\"command\":\"admin_login\",\"recovery_key\":\"$new_recovery\"}" | grep -q '"ok":true'
call '{"command":"technician_add","name":"Bench Tech","pin":"2468"}' | grep -q '"ok":false'
call "{\"command\":\"technician_add\",\"admin_token\":\"$token\",\"name\":\"Bench Tech\",\"pin\":\"24\"}" |
    grep -q 'exactly 4 digits'
call "{\"command\":\"technician_add\",\"admin_token\":\"$token\",\"name\":\"Bench Tech\",\"pin\":\"2468\"}" |
    grep -q '"pin_assigned":true'
call '{"command":"technician_verify","name":"Bench Tech","pin":"2468"}' | grep -q '"ok":true'
call "{\"command\":\"technician_set_pin\",\"admin_token\":\"$token\",\"name\":\"Bench Tech\",\"pin\":\"1357\"}" |
    grep -q '"pin_assigned":true'
call '{"command":"technician_verify","name":"Bench Tech","pin":"2468"}' | grep -q '"ok":false'
call '{"command":"technician_verify","name":"Bench Tech","pin":"1357"}' | grep -q '"ok":true'
cmp "$technicians" "$technicians_backup"

# Simulate the next live boot restoring the persistent credential mirror.
exec 3>&-
exec 4<&-
kill "$pid"
wait "$pid" 2>/dev/null || true
pid=
rm -f "$requests" "$responses" "$security" "$technicians"
cp "$backup" "$security"
cp "$technicians_backup" "$technicians"
mkfifo "$requests" "$responses"
"$service" --config "$profile" --security "$security" --security-backup "$backup" \
    --technicians "$technicians" --technicians-backup "$technicians_backup" \
    --stdio <"$requests" >"$responses" 2>>"$root/service.log" &
pid=$!
exec 3>"$requests"
exec 4<"$responses"
login=$(call '{"command":"admin_login","password":"y"}')
printf '%s\n' "$login" | grep -q '"ok":true'
token=$(printf '%s\n' "$login" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ]
call "{\"command\":\"admin_login\",\"recovery_key\":\"$new_recovery\"}" | grep -q '"ok":true'
call '{"command":"technician_verify","name":"Bench Tech","pin":"1357"}' | grep -q '"ok":true'
call "{\"command\":\"configure_image_storage\",\"admin_token\":\"$token\",\"selector\":\"/dev/does-not-exist\",\"mode\":\"format\",\"confirmation\":\"NO\"}" |
    grep -q 'Type ERASE before formatting the entire storage disk'
call "{\"command\":\"configure_image_storage\",\"admin_token\":\"$token\",\"selector\":\"/dev/does-not-exist\",\"mode\":\"format\",\"confirmation\":\"ERASE\"}" |
    grep -q 'selected storage disk is no longer connected'
call "{\"command\":\"configure_image_storage\",\"admin_token\":\"$token\",\"selector\":\"/dev/does-not-exist\",\"mode\":\"erase\",\"confirmation\":\"ERASE\"}" |
    grep -q 'selected storage disk is no longer connected'
call "{\"command\":\"configure_nas_storage\",\"admin_token\":\"$token\",\"protocol\":\"ftp\",\"server\":\"nas.example\",\"share\":\"Backups\",\"directory\":\"images\"}" |
    grep -q 'Choose SMB or NFS'
call "{\"command\":\"configure_nas_storage\",\"admin_token\":\"$token\",\"protocol\":\"smb\",\"server\":\"bad/server\",\"share\":\"Backups\",\"directory\":\"images\",\"username\":\"backup\",\"password\":\"secret\"}" |
    grep -q 'hostname or IP address without slashes'
call "{\"command\":\"configure_nas_storage\",\"admin_token\":\"$token\",\"protocol\":\"nfs\",\"server\":\"nas.example\",\"share\":\"relative/export\",\"directory\":\"images\"}" |
    grep -q 'absolute path beginning with /'
