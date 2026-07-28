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

mkdir -p "$root/bin" "$root/images"
printf 'name=Printer Test\nimage_storage=%s/images\n' "$root" > "$root/bench.profile"
cat > "$root/bin/lpstat" <<'EOF'
#!/bin/sh
if [ "$1" = "-h" ]; then shift 2; fi
case "$1" in
    -r) echo 'scheduler is running' ;;
    -p) echo 'printer Bench_Printer is idle. enabled since today'; echo 'system default destination: Bench_Printer' ;;
    -v) echo 'device for Bench_Printer: ipp://printer.local/ipp/print' ;;
    *) exit 2 ;;
esac
EOF
cat > "$root/bin/lpadmin" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> '$root/lpadmin.log'
EOF
cat > "$root/bin/lp" <<EOF
#!/bin/sh
printf '%s\n' "\$*" >> '$root/lp.log'
echo 'request id is Bench_Printer-1'
EOF
cat > "$root/bin/ippfind" <<'EOF'
#!/bin/sh
printf 'Xerox B230\tipp://xerox-b230.local:631/ipp/print\txerox-b230.local\n'
printf 'Front Office Printer\tipps://front-office.local:443/ipp/print\tfront-office.local\n'
EOF
cat > "$root/bin/avahi-resolve-host-name" <<'EOF'
#!/bin/sh
for value in "$@"; do host=$value; done
case "$host" in
    xerox-b230.local) address=192.0.2.60 ;;
    front-office.local) address=192.0.2.61 ;;
    printer.example) address=192.0.2.70 ;;
    *) exit 1 ;;
esac
printf '%s\t%s\n' "$host" "$address"
EOF
chmod +x "$root/bin/lpstat" "$root/bin/lpadmin" "$root/bin/lp" "$root/bin/ippfind" "$root/bin/avahi-resolve-host-name"

mkfifo "$root/requests" "$root/responses"
PATH="$root/bin:$PATH" "$service" --config "$root/bench.profile" --security "$root/admin.auth" --stdio \
    <"$root/requests" >"$root/responses" 2>"$root/service.log" &
pid=$!
exec 3>"$root/requests"
exec 4<"$root/responses"
call() {
    printf '%s\n' "$1" >&3
    IFS= read -r response <&4
    printf '%s\n' "$response"
}

setup=$(call '{"command":"admin_setup","new_password":"printer administration password"}')
token=$(printf '%s\n' "$setup" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
[ -n "$token" ]

call '{"command":"printers"}' | grep -q '"ok":false'
call "{\"command\":\"printers\",\"admin_token\":\"$token\"}" | grep -q 'Bench_Printer.*default.*ipp://printer.local/ipp/print'
call '{"command":"printer_discover"}' | grep -q '"ok":false'
call "{\"command\":\"printer_discover\",\"admin_token\":\"$token\"}" | grep -q 'Xerox B230.*Xerox_B230.*ipp://192.0.2.60:631/ipp/print'
call "{\"command\":\"printer_add\",\"admin_token\":\"$token\",\"name\":\"Bad Printer\",\"uri\":\"file:///tmp/output\"}" | grep -q '"ok":false'
call "{\"command\":\"printer_add\",\"admin_token\":\"$token\",\"name\":\"Bad_Address\",\"address\":\"http://printer.example\",\"connection\":\"auto\"}" | grep -q '"ok":false'
call "{\"command\":\"printer_add\",\"admin_token\":\"$token\",\"name\":\"Office_Laser\",\"address\":\"192.0.2.50\",\"connection\":\"auto\"}" | grep -q 'ipp://192.0.2.50/ipp/print'
grep -q -- '-h /run/cups/cups.sock -p Office_Laser -E -v ipp://192.0.2.50/ipp/print -m everywhere' "$root/lpadmin.log"
grep -q -- '-h /run/cups/cups.sock -d Office_Laser' "$root/lpadmin.log"
call "{\"command\":\"printer_add\",\"admin_token\":\"$token\",\"name\":\"Legacy_Laser\",\"address\":\"printer.example\",\"connection\":\"socket\"}" | grep -q 'socket://192.0.2.70:9100'
grep -q -- '-h /run/cups/cups.sock -p Legacy_Laser -E -v socket://192.0.2.70:9100 -m drv:///sample.drv/generpcl.ppd' "$root/lpadmin.log"
call "{\"command\":\"printer_add\",\"admin_token\":\"$token\",\"name\":\"Front_Desk\",\"uri\":\"ipps://printer.example/ipp/print\"}" | grep -q '"ok":true'
grep -q -- '-h /run/cups/cups.sock -p Front_Desk -E -v ipps://192.0.2.70/ipp/print -m everywhere' "$root/lpadmin.log"
call "{\"command\":\"printer_add\",\"admin_token\":\"$token\",\"display_name\":\"Xerox B230\",\"uri\":\"ipp://xerox-b230.local:631/ipp/print\"}" | grep -q '"printer":"Xerox_B230"'
grep -q -- '-h /run/cups/cups.sock -p Xerox_B230 -E -v ipp://192.0.2.60:631/ipp/print -m everywhere' "$root/lpadmin.log"
call "{\"command\":\"printer_set_default\",\"admin_token\":\"$token\",\"name\":\"Bench_Printer\"}" | grep -q '"ok":true'
call "{\"command\":\"printer_test_page\",\"admin_token\":\"$token\",\"name\":\"Bench_Printer\"}" | grep -q 'request id is Bench_Printer-1'
call "{\"command\":\"printer_remove\",\"admin_token\":\"$token\",\"name\":\"Bench_Printer\"}" | grep -q '"ok":false'
call "{\"command\":\"printer_remove\",\"admin_token\":\"$token\",\"name\":\"Bench_Printer\",\"confirmation\":\"REMOVE\"}" | grep -q '"ok":true'
grep -q -- '-h /run/cups/cups.sock -x Bench_Printer' "$root/lpadmin.log"
