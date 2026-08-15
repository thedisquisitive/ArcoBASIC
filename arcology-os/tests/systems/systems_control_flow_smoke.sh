#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-control-flow-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/control-flow/control-flow.abas"

"$ARCOFISSION" reveal "$FIXTURE" at A-MIR > "$TMP_ROOT/control.amir"
grep -qF 'BLOCK WhileCond' "$TMP_ROOT/control.amir"
grep -qF 'BLOCK WhileBody' "$TMP_ROOT/control.amir"
grep -qF 'BLOCK WhileEnd' "$TMP_ROOT/control.amir"
grep -qF 'BLOCK IfThen' "$TMP_ROOT/control.amir"
grep -qF 'BRANCH' "$TMP_ROOT/control.amir"
grep -qF 'JUMP WhileCond' "$TMP_ROOT/control.amir"

cat > "$TMP_ROOT/bad-condition.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
LET count AS U32 = 1
IF count THEN
    RETURN 0
END IF
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/bad-condition.abas" at A-MIR > "$TMP_ROOT/bad.out" 2>&1
grep -qF 'IF condition must be BOOL' "$TMP_ROOT/bad.out"

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/control.efi" --target uefi-x86_64 > "$TMP_ROOT/build.out"
grep -qF 'PE32+ WRITTEN' "$TMP_ROOT/build.out"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo 'SKIP: control-flow image built and A-MIR validated; QEMU/OVMF unavailable'
    exit 0
fi

"$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/control.efi" middle 20
"$SOURCE_DIR/arcology-os/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/control.efi" done 20

echo 'PASS: freestanding IF/WHILE control flow'
