#!/usr/bin/env bash
set -euo pipefail

ARCOSH="$1"
SOURCE_DIR="$2"
BUILD_DIR="$3"
CMAKE_BIN="$4"

TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

HOME_DIR="$TMP_ROOT/home"
mkdir -p "$HOME_DIR"

run_arcosh() {
    ARCOSH_HOME="$HOME_DIR" "$ARCOSH" "$@"
}

run_arcosh --doctor > "$TMP_ROOT/doctor.txt"
grep -q "ArcoSH doctor" "$TMP_ROOT/doctor.txt"
grep -q "stdlib import" "$TMP_ROOT/doctor.txt"

run_arcosh --safe -c "printf safe-ok" > "$TMP_ROOT/safe.txt"
grep -q "safe-ok" "$TMP_ROOT/safe.txt"

run_arcosh --safe -c "printf chain-a && printf chain-b" > "$TMP_ROOT/chain-and.txt"
grep -q "chain-achain-b" "$TMP_ROOT/chain-and.txt"

run_arcosh --safe -c "false || printf chain-or" > "$TMP_ROOT/chain-or.txt"
grep -q "chain-or" "$TMP_ROOT/chain-or.txt"

run_arcosh --safe -c "printf chain-one; printf chain-two" > "$TMP_ROOT/chain-semi.txt"
grep -q "chain-onechain-two" "$TMP_ROOT/chain-semi.txt"

run_arcosh --safe -c "printf 'quoted;separator'" > "$TMP_ROOT/chain-quoted.txt"
grep -q "quoted;separator" "$TMP_ROOT/chain-quoted.txt"

printf 'false\nprintf status-$?\nEXIT\n' | run_arcosh --safe > "$TMP_ROOT/status.txt"
grep -q "status-1" "$TMP_ROOT/status.txt"

run_arcosh --version > "$TMP_ROOT/version.txt"
grep -q "alpha 0.1" "$TMP_ROOT/version.txt"

run_arcosh --init-profile > "$TMP_ROOT/init.txt"
test -f "$HOME_DIR/rc.abas"
test -f "$HOME_DIR/scripts/hello.abas"
test -f "$HOME_DIR/scripts/sysinfo.abas"

printf 'sysinfo\nEXIT\n' | run_arcosh --safe > "$TMP_ROOT/sysinfo.txt"
grep -q "Processes visible:" "$TMP_ROOT/sysinfo.txt"

ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/sysadmin_check.abas" > "$TMP_ROOT/example-sysadmin.txt"
grep -q "command-ok" "$TMP_ROOT/example-sysadmin.txt"

ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/file_log.abas" "$TMP_ROOT/example.log" > "$TMP_ROOT/example-log.txt"
grep -q "TRUE" "$TMP_ROOT/example-log.txt"
grep -q "example log entry" "$TMP_ROOT/example.log"

run_arcosh --safe "$SOURCE_DIR/examples/safe_refs.abas" > "$TMP_ROOT/safe-refs.txt"
grep -q "Reference" "$TMP_ROOT/safe-refs.txt"
grep -q "Grace:150" "$TMP_ROOT/safe-refs.txt"
grep -q "Miso" "$TMP_ROOT/safe-refs.txt"

env -u DISPLAY -u WAYLAND_DISPLAY ARCOSH_HOME="$HOME_DIR" "$ARCOSH" --safe "$SOURCE_DIR/examples/gui_cube.abas" > "$TMP_ROOT/gui-cube.txt"
grep -q "No Wayland or X11 GUI backend is available." "$TMP_ROOT/gui-cube.txt"

ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/arcocompydb_records.abas" > "$TMP_ROOT/example-compydb.txt"
grep -q "1042:Wanda Goodburger" "$TMP_ROOT/example-compydb.txt"

ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/arcodb_people.abas" "$TMP_ROOT/people.arcodb" > "$TMP_ROOT/example-arcodb.txt"
grep -q "wanda.goodburger@example.test" "$TMP_ROOT/example-arcodb.txt"
test -f "$TMP_ROOT/people.arcodb"

ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/arcodb_commands.abas" "$TMP_ROOT/commands.arcodb" 2026-07-13T11:30 > "$TMP_ROOT/example-arcodb-commands.txt"
grep -q "Ada @ Front Desk" "$TMP_ROOT/example-arcodb-commands.txt"
grep -q "Grace @ Workshop" "$TMP_ROOT/example-arcodb-commands.txt"
grep -q "Registered commands: 1" "$TMP_ROOT/example-arcodb-commands.txt"

ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/arcodb_pointers.abas" "$TMP_ROOT/pointers.arcodb" > "$TMP_ROOT/example-arcodb-pointers.txt"
grep -q "Customer#1" "$TMP_ROOT/example-arcodb-pointers.txt"
grep -q "Order#2" "$TMP_ROOT/example-arcodb-pointers.txt"
grep -q "ORDER-1001 missing customer" "$TMP_ROOT/example-arcodb-pointers.txt"

printf '1\nq\n' | ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/arcomart.abas" "$TMP_ROOT/arcomart.arcodb" > "$TMP_ROOT/arcomart.txt"
grep -q "ArcoMart INVENTORY" "$TMP_ROOT/arcomart.txt"
grep -q "Pocket Lightning Bolts" "$TMP_ROOT/arcomart.txt"
test -f "$TMP_ROOT/arcomart.arcodb"

cat > "$TMP_ROOT/arconav.html" <<'HTML'
<!doctype html>
<html>
  <head><title>ArcoNav Smoke</title></head>
  <body><h1>Terminal browser</h1><p>Local page.</p><a href="next.html">Next</a></body>
</html>
HTML

ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/arconav.abas" "file://$TMP_ROOT/arconav.html" --dump > "$TMP_ROOT/arconav.txt"
grep -q "ARCONAV" "$TMP_ROOT/arconav.txt"
grep -q "ArcoNav Smoke" "$TMP_ROOT/arconav.txt"
grep -q "1. Next" "$TMP_ROOT/arconav.txt"

cat > "$TMP_ROOT/arconav-open.html" <<'HTML'
<!doctype html>
<html><head><title>ArcoNav Open Command</title></head><body><p>opened</p></body></html>
HTML

printf 'links\nfind Terminal\nbookmark\nbookmarks\nhistory\nsave %s/arconav-saved.txt\nopen file://%s/arconav-open.html\nexit\n' "$TMP_ROOT" "$TMP_ROOT" | ARCOBASIC_STDLIB="$SOURCE_DIR/stdlib" run_arcosh --safe "$SOURCE_DIR/examples/arconav.abas" "file://$TMP_ROOT/arconav.html" > "$TMP_ROOT/arconav-open.txt"
grep -q "BOOKMARKS" "$TMP_ROOT/arconav-open.txt"
grep -q "HISTORY" "$TMP_ROOT/arconav-open.txt"
grep -q "Terminal browser" "$TMP_ROOT/arconav-saved.txt"
grep -q "ArcoNav Open Command" "$TMP_ROOT/arconav-open.txt"

cat > "$TMP_ROOT/executable.abas" <<SCRIPT
#!$ARCOSH --safe
PRINT "direct script"
FOR arg IN Args
    PRINT arg
NEXT
SCRIPT
chmod +x "$TMP_ROOT/executable.abas"
ARCOSH_HOME="$HOME_DIR" "$TMP_ROOT/executable.abas" alpha beta > "$TMP_ROOT/executable.txt"
grep -q "direct script" "$TMP_ROOT/executable.txt"
grep -q "alpha" "$TMP_ROOT/executable.txt"
grep -q "beta" "$TMP_ROOT/executable.txt"

cat > "$TMP_ROOT/launch.abas" <<'SCRIPT'
PRINT "launch-smoke"
PRINT Script.Name
FOR arg IN Args
    PRINT arg
NEXT
SCRIPT
printf '@%s/launch.abas alpha beta\nRUN %s/launch.abas gamma\nLOAD %s/launch.abas\nRUN\nLOAD %s/launch.abas; RUN\nEXIT\n' "$TMP_ROOT" "$TMP_ROOT" "$TMP_ROOT" "$TMP_ROOT" | run_arcosh --safe > "$TMP_ROOT/launch.txt"
grep -q "launch-smoke" "$TMP_ROOT/launch.txt"
grep -q "launch.abas" "$TMP_ROOT/launch.txt"
grep -q "alpha" "$TMP_ROOT/launch.txt"
grep -q "beta" "$TMP_ROOT/launch.txt"
grep -q "gamma" "$TMP_ROOT/launch.txt"
grep -q "Loaded $TMP_ROOT/launch.abas" "$TMP_ROOT/launch.txt"

cat > "$HOME_DIR/rc.abas" <<'RC'
PRINT "alpha rc loaded"
RC

cat > "$TMP_ROOT/smoke-mod.abas" <<'SCRIPT'
PRINT "mod smoke loaded"
smoke_mod_value = "active"
SCRIPT

run_arcosh --safe "$SOURCE_DIR/examples/arcosh_mods.abas" install "$TMP_ROOT/smoke-mod.abas" smoke-mod > "$TMP_ROOT/mod-install.txt"
grep -q "Installed smoke-mod" "$TMP_ROOT/mod-install.txt"
run_arcosh --safe "$SOURCE_DIR/examples/arcosh_mods.abas" activate smoke-mod > "$TMP_ROOT/mod-activate.txt"
grep -q "Activated smoke-mod" "$TMP_ROOT/mod-activate.txt"
run_arcosh --safe "$SOURCE_DIR/examples/arcosh_mods.abas" list > "$TMP_ROOT/mod-list.txt"
grep -q "smoke-mod" "$TMP_ROOT/mod-list.txt"
run_arcosh -c "printf mods-ready" > "$TMP_ROOT/mod-startup.txt"
grep -q "mod smoke loaded" "$TMP_ROOT/mod-startup.txt"
grep -q "mods-ready" "$TMP_ROOT/mod-startup.txt"

ARCOSH_HOME="$TMP_ROOT/gotchi-home" "$ARCOSH" --safe "$SOURCE_DIR/examples/arcosh_mods.abas" install-builtin arcogotchi > "$TMP_ROOT/gotchi-install.txt"
grep -q "Installed arcogotchi" "$TMP_ROOT/gotchi-install.txt"
ARCOSH_HOME="$TMP_ROOT/gotchi-home" "$ARCOSH" --safe "$SOURCE_DIR/examples/arcosh_mods.abas" activate arcogotchi > "$TMP_ROOT/gotchi-activate.txt"
grep -q "Activated arcogotchi" "$TMP_ROOT/gotchi-activate.txt"
printf 'gotchi\ngotchi-feed\nEXIT\n' | ARCOSH_HOME="$TMP_ROOT/gotchi-home" "$ARCOSH" > "$TMP_ROOT/gotchi.txt"
grep -q "ARCOGOTCHI" "$TMP_ROOT/gotchi.txt"
grep -q "crunches happily" "$TMP_ROOT/gotchi.txt"

run_arcosh -c "printf profile-command" > "$TMP_ROOT/profile-command.txt"
grep -q "alpha rc loaded" "$TMP_ROOT/profile-command.txt"
grep -q "profile-command" "$TMP_ROOT/profile-command.txt"

cat > "$HOME_DIR/scripts/admin-status.abas" <<'SCRIPT'
PRINT "admin-status"
FOR arg IN Args
    PRINT arg
NEXT
SCRIPT

printf 'admin-status first second\nEXIT\n' | run_arcosh --safe > "$TMP_ROOT/profile-script.txt"
grep -q "admin-status" "$TMP_ROOT/profile-script.txt"
grep -q "first" "$TMP_ROOT/profile-script.txt"
grep -q "second" "$TMP_ROOT/profile-script.txt"

printf 'PRINT "history-smoke"\nEXIT\n' | run_arcosh --safe > "$TMP_ROOT/history-out.txt"
test -s "$HOME_DIR/history"

FIFO="$TMP_ROOT/sigint.in"
mkfifo "$FIFO"
ARCOSH_HOME="$HOME_DIR" "$ARCOSH" --safe < "$FIFO" > "$TMP_ROOT/sigint.txt" 2>&1 &
SIGINT_PID=$!
exec 9>"$FIFO"
printf 'PRINT "before-int"\n' >&9
sleep 0.1
kill -INT "$SIGINT_PID"
sleep 0.1
printf 'PRINT "after-int"\nEXIT\n' >&9
exec 9>&-
wait "$SIGINT_PID"
grep -q "before-int" "$TMP_ROOT/sigint.txt"
grep -q "after-int" "$TMP_ROOT/sigint.txt"

printf '10 PRINT "before-stop"\n20 STOP\n30 PRINT "after-stop"\nRUN\nPRINT "still-here"\nEXIT\n' | run_arcosh --safe > "$TMP_ROOT/stop.txt"
grep -q "before-stop" "$TMP_ROOT/stop.txt"
grep -q "still-here" "$TMP_ROOT/stop.txt"
if grep -q "after-stop" "$TMP_ROOT/stop.txt"; then
    echo "STOP did not halt numbered program" >&2
    exit 1
fi

PREFIX="$TMP_ROOT/install"
"$CMAKE_BIN" --install "$BUILD_DIR" --prefix "$PREFIX" > "$TMP_ROOT/install.txt"
test -x "$PREFIX/bin/arcosh"
test -f "$PREFIX/share/arcobasic/stdlib/sysadmin.abas"
test -f "$PREFIX/share/arcobasic/stdlib/compydb.abas"
test -f "$PREFIX/share/arcobasic/stdlib/arcodb.abas"
test -f "$PREFIX/share/arcobasic/stdlib/commons.abas"
test -f "$PREFIX/share/arcobasic/stdlib/arcology.abas"
test -f "$PREFIX/share/arcosh/tutorials/arcosh_sysadmin.abas"
test -f "$PREFIX/share/arcosh/tutorials/arcosh_game.abas"
test -f "$PREFIX/share/arcosh/tutorials/arcosh_tool.abas"
test -f "$PREFIX/share/arcosh/tutorials/arcoadventures_intro.abas"
test -f "$PREFIX/share/arcosh/tutorials/arcoadventure_badge_bureau.abas"
test -f "$PREFIX/share/arcosh/tutorials/arcoadventure_snackstorm.abas"
test -f "$PREFIX/share/arcosh/tutorials/arcoadventure_evidence_locker.abas"
test -f "$PREFIX/share/arcosh/scripts/install-login-shell.abas"
test -f "$PREFIX/share/arcobasic/examples/sysadmin_check.abas"
test -f "$PREFIX/share/arcobasic/examples/arcology-commons/arcology_v01a.abas"
test -f "$PREFIX/share/doc/arcobasic/README.md"
test -f "$PREFIX/share/doc/arcobasic/arcology-os/systems/README.md"
test -f "$PREFIX/share/doc/arcobasic/arcology-commons/README.md"
test -f "$PREFIX/lib/libarcology_os.a"
test -f "$PREFIX/include/arco/uefi_bindings.hpp"
test -x "$PREFIX/share/arcobasic/scripts/install-deb-wizard.sh"
"$PREFIX/share/arcobasic/scripts/install-deb-wizard.sh" --help > "$TMP_ROOT/deb-wizard-help.txt"
grep -q "interactive Debian installer" "$TMP_ROOT/deb-wizard-help.txt"

INSTALLED_HOME="$TMP_ROOT/installed-home"
mkdir -p "$INSTALLED_HOME"
ARCOSH_HOME="$INSTALLED_HOME" "$PREFIX/bin/arcosh" --doctor > "$TMP_ROOT/installed-doctor.txt"
grep -q "stdlib import" "$TMP_ROOT/installed-doctor.txt"

printf '\n\n\n\n\nn\nn\n\n\n' | ARCOSH_HOME="$INSTALLED_HOME" "$PREFIX/bin/arcosh" --safe --install-shell > "$TMP_ROOT/install-shell.txt"
grep -q "ArcoSH Login Shell Wizard" "$TMP_ROOT/install-shell.txt"
grep -q "DRY RUN COMPLETE" "$TMP_ROOT/install-shell.txt"
test -d "$INSTALLED_HOME/scripts"

printf 'install-login\n\n\n\n\n\nn\nn\n\n\nEXIT\n' | ARCOSH_HOME="$INSTALLED_HOME" "$PREFIX/bin/arcosh" --safe > "$TMP_ROOT/install-login-repl.txt"
grep -q "ArcoSH Login Shell Wizard" "$TMP_ROOT/install-login-repl.txt"
grep -q "DRY RUN COMPLETE" "$TMP_ROOT/install-login-repl.txt"
test -d "$INSTALLED_HOME/plugins"

cat > "$TMP_ROOT/import-smoke.abas" <<'SCRIPT'
#IMPORT "sysadmin"
PRINT SysAdmin.CommandExists("printf")
SCRIPT

(cd "$TMP_ROOT" && ARCOSH_HOME="$INSTALLED_HOME" "$PREFIX/bin/arcosh" --safe "$TMP_ROOT/import-smoke.abas") > "$TMP_ROOT/installed-import.txt"
grep -q "TRUE" "$TMP_ROOT/installed-import.txt"

printf 'VERSION\nHELP shell\nHELP doctor\nEXIT\n' | run_arcosh --safe > "$TMP_ROOT/help.txt"
grep -q "alpha 0.1" "$TMP_ROOT/help.txt"
grep -q "ArcoSH usage" "$TMP_ROOT/help.txt"
grep -q "ArcoSH doctor" "$TMP_ROOT/help.txt"
