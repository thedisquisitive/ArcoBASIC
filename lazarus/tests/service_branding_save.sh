#!/bin/sh
set -eu

service=$1
root=$(mktemp -d)
profile="$root/bench.profile"
security="$root/admin.auth"
requests="$root/requests"
responses="$root/responses"
pid=

cleanup() {
    [ -z "$pid" ] || kill "$pid" 2>/dev/null || true
    rm -rf "$root"
}
trap cleanup EXIT INT TERM

mkdir -p "$root/images"
cat >"$profile" <<EOF
name=Branding Test
image_storage=$root/images
source=/dev/disk/by-path/branding-test-source
EOF

mkfifo "$requests" "$responses"
"$service" --config "$profile" --security "$security" --stdio <"$requests" >"$responses" 2>"$root/service.log" &
pid=$!
exec 3>"$requests"
exec 4<"$responses"

call() {
    printf '%s\n' "$1" >&3
    IFS= read -r response <&4
    printf '%s\n' "$response"
}

setup=$(call '{"command":"admin_setup","new_password":"correct horse battery staple"}')
token=$(printf '%s\n' "$setup" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ]

common="\"admin_token\":\"$token\",\"name\":\"Branding Test\",\"branding_theme\":\"Test Theme\",\"branding_subtitle\":\"Offline Imaging\",\"branding_accent\":\"#f39a22\",\"branding_background\":\"#10161b\",\"branding_surface\":\"#171f25\",\"branding_text\":\"#edf1f3\",\"branding_icon\":\"#f39a22\",\"branding_report_footer\":\"Local report\",\"image_storage_text\":\"$root/images\",\"source_text\":\"/dev/disk/by-path/branding-test-source\",\"destination_text\":\"\",\"removable_text\":\"/dev/disk/by-path/recovery-usb\",\"ignored_text\":\"\",\"labels_text\":\"\""

invalid=$(call "{\"command\":\"save_profile\",$common,\"branding_product_name\":\"Broken\\nName\"}")
printf '%s\n' "$invalid" | grep -q '"ok":false'
printf '%s\n' "$invalid" | grep -q 'one line of text'
call '{"command":"ping"}' | grep -q '"ok":true'

valid=$(call "{\"command\":\"save_profile\",$common,\"branding_product_name\":\"Workshop Lazarus\"}")
printf '%s\n' "$valid" | grep -q '"ok":true'
grep -q '^branding_product_name=Workshop Lazarus$' "$profile"
grep -q '^removable_media=/dev/disk/by-path/recovery-usb$' "$profile"
[ ! -e "$profile.tmp.$pid" ]
profile_response=$(call '{"command":"profile"}')
printf '%s\n' "$profile_response" | grep -q '"branding_product_name":"Workshop Lazarus"'
printf '%s\n' "$profile_response" | grep -q '"removable_text":"/dev/disk/by-path/recovery-usb'
