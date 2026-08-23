#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/ps2-keyboard-driver-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0045 Phase 1: a real PS/2 keyboard driver using raw port I/O only -- no UEFI protocol
# dependency at all -- closing the real gap RFC-0045 Section 4 found: ConsoleIn.ReadKeyStroke
# crashes (not merely hangs, like ConsoleOut.Write) after ExitBootServices. This fixture reads one
# real injected keystroke BEFORE ExitBootServices and a second one AFTER, via the exact same
# driver, proving it has no Boot-Services dependency and genuinely works under the Polymorphic
# Substrate.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/ps2-keyboard-driver/ps2-keyboard-driver.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: ps2-keyboard-driver does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q . || ! command -v socat > /dev/null 2>&1; then
    echo "SKIP: qemu-system-x86_64/OVMF/socat not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/ps2.efi" --target uefi-x86_64 --entry Main > /dev/null

# The real proof: two genuinely distinct real keys (different scancodes, confirming the
# translation table itself, not a coincidental single-key match), one read before
# ExitBootServices, one after, via the exact same raw-port-I/O driver -- no ConsoleIn/ConsoleOut
# involved for the post-exit read at all.
QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"

run_once() {
    local key1="$1" key2="$2" outfile="$3"
    rm -f "$TMP_ROOT/mon.sock"
    local boot_dir
    boot_dir="$(mktemp -d)"
    mkdir -p "$boot_dir/EFI/BOOT"
    cp "$TMP_ROOT/ps2.efi" "$boot_dir/EFI/BOOT/BOOTX64.EFI"
    timeout 30 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 -drive file="fat:rw:$boot_dir",format=raw \
        -net none -vga none -display none -serial stdio -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
        -no-reboot > "$outfile" 2>&1 &
    local qemu_pid=$!
    local i
    for i in $(seq 1 100); do grep -aqF "PRE" "$outfile" 2>/dev/null && break; sleep 0.1; done
    sleep 2
    printf 'sendkey %s\n' "$key1" | socat - UNIX-CONNECT:"$TMP_ROOT/mon.sock" > /dev/null 2>&1 || true
    sleep 2
    for i in $(seq 1 100); do grep -aqF "EXOK" "$outfile" 2>/dev/null && break; sleep 0.1; done
    sleep 1
    printf 'sendkey %s\n' "$key2" | socat - UNIX-CONNECT:"$TMP_ROOT/mon.sock" > /dev/null 2>&1 || true
    sleep 2
    wait "$qemu_pid" 2>/dev/null || true
    rm -rf "$boot_dir"
}

check_result() {
    local outfile="$1" expected1="$2" expected2="$3"
    grep -aqF "P1=$expected1" "$outfile" || { echo "FAIL: pre-exit key not read correctly" >&2; cat "$outfile" >&2; exit 1; }
    grep -aqF "EXOK" "$outfile" || { echo "FAIL: ExitBootServices did not report success" >&2; cat "$outfile" >&2; exit 1; }
    grep -aqF "P2=$expected2" "$outfile" || { echo "FAIL: post-exit key not read correctly (this is the real proof this fixture exists for)" >&2; cat "$outfile" >&2; exit 1; }
    grep -aqF "DONE" "$outfile" || { echo "FAIL: fixture did not report DONE" >&2; cat "$outfile" >&2; exit 1; }
}

run_once "q" "z" "$TMP_ROOT/run1.txt"
check_result "$TMP_ROOT/run1.txt" "q" "z"

# Determinism: 2 more repeats with different key pairs (confirms the translation table generally,
# not one coincidentally-correct key).
run_once "j" "m" "$TMP_ROOT/run2.txt"
check_result "$TMP_ROOT/run2.txt" "j" "m"

run_once "5" "0" "$TMP_ROOT/run3.txt"
check_result "$TMP_ROOT/run3.txt" "5" "0"

echo "PASS: real PS/2 keyboard driver (raw port I/O, no UEFI protocol dependency) correctly reads real injected keystrokes both before and after ExitBootServices, confirmed with 3 different key pairs"
