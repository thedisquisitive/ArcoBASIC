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

cat > "$HOME_DIR/rc.abas" <<'RC'
PRINT "alpha rc loaded"
RC

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
test -f "$PREFIX/share/arcosh/tutorials/arcosh_sysadmin.abas"
test -f "$PREFIX/share/arcobasic/examples/sysadmin_check.abas"
test -f "$PREFIX/share/doc/arcobasic/readme.md"

INSTALLED_HOME="$TMP_ROOT/installed-home"
mkdir -p "$INSTALLED_HOME"
ARCOSH_HOME="$INSTALLED_HOME" "$PREFIX/bin/arcosh" --doctor > "$TMP_ROOT/installed-doctor.txt"
grep -q "stdlib import" "$TMP_ROOT/installed-doctor.txt"

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
