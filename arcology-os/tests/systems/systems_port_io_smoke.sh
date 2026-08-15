#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/arcofission-port-io-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/port-io-com1/port-io-com1.abas"

"$ARCOFISSION" reveal "$FIXTURE" at AST --entry Main > "$TMP_ROOT/ast.txt"
grep -qF 'PortOperation PORT.WriteByte' "$TMP_ROOT/ast.txt"
grep -qF 'HardwareSemantic CPU.Pause' "$TMP_ROOT/ast.txt"

"$ARCOFISSION" reveal "$FIXTURE" at A-MIR --entry Main > "$TMP_ROOT/amir.txt"
grep -qF 'PORT.ADDRESS' "$TMP_ROOT/amir.txt"
grep -qF 'PORT.OFFSET' "$TMP_ROOT/amir.txt"
grep -qF 'PORT.READ8' "$TMP_ROOT/amir.txt"
grep -qF 'PORT.WRITE8' "$TMP_ROOT/amir.txt"
grep -qF 'CPU.PAUSE' "$TMP_ROOT/amir.txt"

"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt"
sed -n 's/^    [0-9a-f][0-9a-f]*: //p' "$TMP_ROOT/x86.txt" | tr '\n' ' ' > "$TMP_ROOT/x86-flat.txt"
grep -qF 'ec' "$TMP_ROOT/x86-flat.txt"
grep -qF 'ee' "$TMP_ROOT/x86-flat.txt"
grep -qF 'f3 90' "$TMP_ROOT/x86-flat.txt"

cat > "$TMP_ROOT/canonical.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION Main() AS U64
    LET p AS IOPORT = PORT.Address(0x3F8)
    PORT.Write8(p, 65)
    LET value AS U8 = PORT.Read8(p)
    RETURN value
END FUNCTION
SCRIPT
cat > "$TMP_ROOT/alias.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION Main() AS U64
    LET p AS IOPORT = PORT.Address(0x3F8)
    PORT.WriteByte(p, 65)
    LET value AS U8 = PORT.ReadByte(p)
    RETURN value
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/canonical.abas" at A-MIR --entry Main > "$TMP_ROOT/canonical.amir"
"$ARCOFISSION" reveal "$TMP_ROOT/alias.abas" at A-MIR --entry Main > "$TMP_ROOT/alias.amir"
diff -u <(grep -E 'PORT\.(ADDRESS|READ8|WRITE8)' "$TMP_ROOT/canonical.amir") <(grep -E 'PORT\.(ADDRESS|READ8|WRITE8)' "$TMP_ROOT/alias.amir")
"$ARCOFISSION" reveal "$TMP_ROOT/canonical.abas" at X86_64 --entry Main > "$TMP_ROOT/canonical.x86"
"$ARCOFISSION" reveal "$TMP_ROOT/alias.abas" at X86_64 --entry Main > "$TMP_ROOT/alias.x86"
diff -u <(sed -n '/^TEXT /,/^RDATA /p' "$TMP_ROOT/canonical.x86") <(sed -n '/^TEXT /,/^RDATA /p' "$TMP_ROOT/alias.x86")

cat > "$TMP_ROOT/hosted.abas" <<'SCRIPT'
LET p AS IOPORT = PORT.Address(0x3F8)
LET value AS U8 = PORT.Read8(p)
SCRIPT
if "$ARCOFISSION" compile-run "$TMP_ROOT/hosted.abas" > "$TMP_ROOT/hosted.out" 2>&1; then
    echo 'FAIL: hosted port I/O unexpectedly executed' >&2
    exit 1
fi
grep -qF 'available only on a freestanding target with port-I/O support' "$TMP_ROOT/hosted.out"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo 'SKIP: port I/O image built and inspected; QEMU/OVMF unavailable'
    exit 0
fi

mkdir -p "$TMP_ROOT/EFI/BOOT"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/EFI/BOOT/BOOTX64.EFI" --target uefi-x86_64 --entry Main > "$TMP_ROOT/build.txt"
timeout 15 qemu-system-x86_64 \
    -bios "$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)" \
    -drive file=fat:rw:"$TMP_ROOT",format=raw \
    -net none -display none -vga none -serial "file:$TMP_ROOT/serial.log" -monitor none -no-reboot \
    >/dev/null 2>&1 || true

grep -aFq 'ARCOLOGY PORT I/O ONLINE' "$TMP_ROOT/serial.log"
echo 'PASS: direct COM1 port I/O and CPU.Pause'
