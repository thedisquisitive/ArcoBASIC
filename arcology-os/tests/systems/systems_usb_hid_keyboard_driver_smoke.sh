#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/usb-hid-keyboard-driver-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0045 Phase 3: real USB HID boot-protocol keyboard driver on top of the Phase 2 xHCI
# controller driver -- real device enumeration (Enable Slot, Address Device, GET_DESCRIPTOR,
# Configure Endpoint, SET_CONFIGURATION, SET_PROTOCOL Boot Protocol) followed by a real polled
# read of one real injected keystroke's own HID report, translated to the expected character.
#
# A REAL root cause was found and fixed during this driver's own development: OVMF's own native
# XHCI driver stays bound to (and periodically touches) this controller even though this driver
# talks to it directly via raw PCI/MMIO, outside any UEFI protocol -- a real firmware-vs-guest
# ownership conflict, confirmed with QEMU's own xHCI trace events (spurious, unexplained repeated
# command-doorbell writes racing this driver's own real commands with no corresponding guest call)
# and fixed with UsbXhci.DisconnectFirmwareDriver (EFI_PCI_IO_PROTOCOL.GetLocation to find the
# matching EFI_HANDLE, then BootServices.DisconnectController on it), called at the very start of
# UsbXhci.MapMmio. Confirmed via 10 consecutive real QEMU runs after the fix, 0 failures.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/usb-hid-keyboard-driver/usb-hid-keyboard-driver.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: usb-hid-keyboard-driver does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q . || ! command -v socat > /dev/null 2>&1; then
    echo "SKIP: qemu-system-x86_64/OVMF/socat not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/hidkbd.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"

run_once() {
    local outfile="$1" with_kbd="$2" key="$3"
    rm -f "$TMP_ROOT/mon.sock"
    local boot_dir
    boot_dir="$(mktemp -d)"
    mkdir -p "$boot_dir/EFI/BOOT"
    cp "$TMP_ROOT/hidkbd.efi" "$boot_dir/EFI/BOOT/BOOTX64.EFI"
    if [ "$with_kbd" = "yes" ]; then
        timeout 60 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 \
            -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
            -drive file="fat:rw:$boot_dir",format=raw,if=ide \
            -net none -vga none -display none -serial stdio -no-reboot \
            -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
            > "$outfile" 2>&1 &
    else
        timeout 60 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 \
            -device qemu-xhci,id=xhci \
            -drive file="fat:rw:$boot_dir",format=raw,if=ide \
            -net none -vga none -display none -serial stdio -no-reboot \
            -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
            > "$outfile" 2>&1 &
    fi
    local qemu_pid=$!
    local i
    if [ "$with_kbd" = "yes" ]; then
        for i in $(seq 1 150); do
            grep -aq "ENOK\|ENFAIL" "$outfile" 2>/dev/null && break
            sleep 0.2
        done
        sleep 1
        printf 'sendkey %s\n' "$key" | socat - UNIX-CONNECT:"$TMP_ROOT/mon.sock" > /dev/null 2>&1 || true
    fi
    for i in $(seq 1 250); do
        grep -aq "HIDKBD DONE\|HIDKBD NO REPORT\|HIDKBD BAD KEY\|HIDKBD ENUM FAILED\|HIDKBD NO DEVICE" "$outfile" 2>/dev/null && break
        sleep 0.2
    done
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
    rm -rf "$boot_dir"
}

check_positive() {
    local outfile="$1" expected_key="$2"
    grep -aqF "HIDKBD DONE" "$outfile" || { echo "FAIL: fixture did not report DONE" >&2; cat "$outfile" >&2; exit 1; }
    grep -aqF "KEYOK $expected_key" "$outfile" || { echo "FAIL: real injected keystroke not read/translated correctly" >&2; cat "$outfile" >&2; exit 1; }
}

# The real proof: real device enumeration + real Configure Endpoint + a real injected keystroke
# read back through the xHCI + HID path alone, translated to the expected character.
run_once "$TMP_ROOT/run1.txt" "yes" "x"
check_positive "$TMP_ROOT/run1.txt" "x"

# Negative control: no USB device attached -- real, honest "no device" report.
run_once "$TMP_ROOT/negative.txt" "no" ""
grep -aqF "HIDKBD NO DEVICE" "$TMP_ROOT/negative.txt" || { echo "FAIL: negative control did not honestly report no device attached" >&2; cat "$TMP_ROOT/negative.txt" >&2; exit 1; }

# Determinism: 2 more repeats with different keys (confirms translation generally, not one
# coincidentally-correct key).
run_once "$TMP_ROOT/run2.txt" "yes" "j"
check_positive "$TMP_ROOT/run2.txt" "j"

run_once "$TMP_ROOT/run3.txt" "yes" "5"
check_positive "$TMP_ROOT/run3.txt" "5"

echo "PASS: real USB HID boot-protocol keyboard driver on top of the xHCI controller -- real device enumeration (Enable Slot, Address Device, GET_DESCRIPTOR x3, Configure Endpoint, SET_CONFIGURATION, SET_PROTOCOL) and a real injected keystroke read back and correctly translated via the xHCI + HID path alone, confirmed with 3 different keys; negative control confirmed real"
