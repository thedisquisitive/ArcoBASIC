#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/aps-arcology-seed-substrate-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0045 Phase 4/5: substrate unification + a real on-screen terminal. A single fixture, running
# under APS's own CR3/GDT/IDT (reusing aps-gdt-reload.abas's own proven sequence) after a real
# ExitBootServices, genuinely running RFC-0007's own HELP/OT/?SYNTAX ERROR/backspace command loop --
# fed by BOTH real input paths this RFC built (Phase 1 PS/2, Phase 3 USB HID), polled together,
# whichever produces a real character first. Output goes to BOTH the real serial port (this
# project's own established, QEMU-testable channel) AND a real GOP-framebuffer text terminal (an
# embedded 8x8 bitmap font, rasterized offline from a real monospace TrueType font) -- the second
# one exists specifically because real hardware doesn't expose the serial channel without extra
# equipment most laptops don't have; this makes the READY. session genuinely visible on a real
# screen too.
#
# Two real findings surfaced by this fixture's own development, not present in aps-gdt-reload.abas
# (which never touches device MMIO after its own CR3 switch): the minimal identity-mapped page
# tables that fixture established only cover the low 1GB -- not enough here, since BOTH the xHCI
# controller's own real MMIO BAR and the GOP framebuffer's own real base sit far outside that
# range, and both need to stay reachable post-substrate. Fixed with a reusable, dynamically-
# computed 1GB page mapping (MapExtra1GbRegion) applied to each real region's own discovered base
# address, never hardcoded.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/aps-arcology-seed-substrate/aps-arcology-seed-substrate.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: aps-arcology-seed-substrate does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q . || ! command -v socat > /dev/null 2>&1; then
    echo "SKIP: qemu-system-x86_64/OVMF/socat not installed; this fixture only proves anything when executed."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/substrate.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"

# sendkey one letter/word at a time, in order, once the fixture's own READY. marker appears.
inject_keys() {
    local monsock="$1"; shift
    for k in "$@"; do
        printf 'sendkey %s\n' "$k" | socat - UNIX-CONNECT:"$monsock" > /dev/null 2>&1 || true
        sleep 0.3
    done
}

run_once() {
    local outfile="$1" mode="$2"; shift 2
    rm -f "$TMP_ROOT/mon.sock"
    local boot_dir
    boot_dir="$(mktemp -d)"
    mkdir -p "$boot_dir/EFI/BOOT"
    cp "$TMP_ROOT/substrate.efi" "$boot_dir/EFI/BOOT/BOOTX64.EFI"
    case "$mode" in
        both)
            timeout 40 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
                -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
                -drive file="fat:rw:$boot_dir",format=raw \
                -net none -vga none -display none -serial stdio -no-reboot \
                -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
                > "$outfile" 2>&1 &
            ;;
        ps2only)
            timeout 40 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
                -drive file="fat:rw:$boot_dir",format=raw \
                -net none -vga none -display none -serial stdio -no-reboot \
                -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
                > "$outfile" 2>&1 &
            ;;
        usbonly)
            timeout 40 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 \
                -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
                -drive file="fat:rw:$boot_dir",format=raw,if=ide \
                -net none -vga none -display none -serial stdio -no-reboot \
                -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
                > "$outfile" 2>&1 &
            ;;
        none)
            timeout 20 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 \
                -drive file="fat:rw:$boot_dir",format=raw,if=ide \
                -net none -vga none -display none -serial stdio -no-reboot \
                -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
                > "$outfile" 2>&1 &
            ;;
        gop)
            timeout 40 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
                -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -vga std \
                -drive file="fat:rw:$boot_dir",format=raw \
                -net none -display none -serial stdio -no-reboot \
                -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
                > "$outfile" 2>&1 &
            ;;
    esac
    local qemu_pid=$!
    if [ "$mode" != "none" ]; then
        local i
        for i in $(seq 1 150); do
            grep -aq "READY." "$outfile" 2>/dev/null && break
            sleep 0.2
        done
        sleep 1
        inject_keys "$TMP_ROOT/mon.sock" "$@"
        sleep 1
        if [ -n "${SCREENSHOT_PATH:-}" ]; then
            printf 'screendump %s\n' "$SCREENSHOT_PATH" | socat - UNIX-CONNECT:"$TMP_ROOT/mon.sock" > /dev/null 2>&1 || true
            sleep 1
        fi
    else
        sleep 15
    fi
    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true
    rm -rf "$boot_dir"
}

# The real combined proof: full RFC-0007 command loop (HELP, OT, a garbage command producing
# ?SYNTAX ERROR, backspace-corrected line editing, then the TEST-ONLY EXIT) running under APS's
# own CR3/GDT/IDT after a real ExitBootServices, with BOTH real input paths present at once.
run_once "$TMP_ROOT/combined.txt" both o t ret z z z ret h z backspace e l p ret e x i t ret
grep -aqF "SUBSTRATE LIVE" "$TMP_ROOT/combined.txt" || { echo "FAIL: substrate takeover (CR3/GDT/IDT) did not complete" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "THE WAGON HAS BROKEN DOWN." "$TMP_ROOT/combined.txt" || { echo "FAIL: OT command did not produce its real response" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "?SYNTAX ERROR" "$TMP_ROOT/combined.txt" || { echo "FAIL: an unrecognized command did not produce ?SYNTAX ERROR" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "IMMEDIATE MODE COMMANDS: HELP  OT" "$TMP_ROOT/combined.txt" || { echo "FAIL: backspace-corrected HELP command did not match (real line-editing proof)" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "ARCOLOGY SEED HALT" "$TMP_ROOT/combined.txt" || { echo "FAIL: EXIT command did not terminate the loop" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }

# Real proof EITHER input path alone is sufficient (RFC-0045's own explicit requirement): PS/2
# only (no xHCI controller present at all) and USB HID only (-nodefaults, no PS/2 keyboard).
run_once "$TMP_ROOT/ps2only.txt" ps2only h e l p ret e x i t ret
grep -aqF "SUBSTRATE USB NONE" "$TMP_ROOT/ps2only.txt" || { echo "FAIL: PS/2-only run unexpectedly found a USB keyboard" >&2; cat "$TMP_ROOT/ps2only.txt" >&2; exit 1; }
grep -aqF "IMMEDIATE MODE COMMANDS: HELP  OT" "$TMP_ROOT/ps2only.txt" || { echo "FAIL: PS/2-only path did not correctly read/dispatch HELP" >&2; cat "$TMP_ROOT/ps2only.txt" >&2; exit 1; }
grep -aqF "ARCOLOGY SEED HALT" "$TMP_ROOT/ps2only.txt" || { echo "FAIL: PS/2-only run did not reach EXIT" >&2; cat "$TMP_ROOT/ps2only.txt" >&2; exit 1; }

run_once "$TMP_ROOT/usbonly.txt" usbonly h e l p ret e x i t ret
grep -aqF "SUBSTRATE USB READY" "$TMP_ROOT/usbonly.txt" || { echo "FAIL: USB-only run did not find/enumerate the USB keyboard" >&2; cat "$TMP_ROOT/usbonly.txt" >&2; exit 1; }
grep -aqF "IMMEDIATE MODE COMMANDS: HELP  OT" "$TMP_ROOT/usbonly.txt" || { echo "FAIL: USB-only path did not correctly read/dispatch HELP" >&2; cat "$TMP_ROOT/usbonly.txt" >&2; exit 1; }
grep -aqF "ARCOLOGY SEED HALT" "$TMP_ROOT/usbonly.txt" || { echo "FAIL: USB-only run did not reach EXIT" >&2; cat "$TMP_ROOT/usbonly.txt" >&2; exit 1; }

# Negative control: no input device at all -- reaches READY. and genuinely just waits, no crash,
# no spurious command dispatch.
run_once "$TMP_ROOT/negative.txt" none
grep -aqF "READY." "$TMP_ROOT/negative.txt" || { echo "FAIL: negative control did not even reach READY." >&2; cat "$TMP_ROOT/negative.txt" >&2; exit 1; }
if grep -aqi "BAD\|FAIL" "$TMP_ROOT/negative.txt"; then
    echo "FAIL: negative control hit a real error marker" >&2
    cat "$TMP_ROOT/negative.txt" >&2
    exit 1
fi

# Real proof the on-screen terminal actually rendered something -- not just that the fixture
# reported "GOP READY" without ever touching the framebuffer. A real qemu-xhci+usb-kbd device plus
# a real GOP-capable display (-vga std), a real injected HELP, a real QEMU screendump (a genuine
# P6 PPM capture of the actual framebuffer, not a synthetic one), and a dependency-free pixel
# count confirming real text is lit in the top-left region where "ARCOLOGY SEED"/"READY." are
# always drawn first.
SCREENSHOT_PATH="$TMP_ROOT/screen.ppm" run_once "$TMP_ROOT/gop.txt" gop h e l p ret
grep -aqF "SUBSTRATE GOP READY" "$TMP_ROOT/gop.txt" || { echo "FAIL: GOP was not discovered" >&2; cat "$TMP_ROOT/gop.txt" >&2; exit 1; }
[ -s "$TMP_ROOT/screen.ppm" ] || { echo "FAIL: no screendump was captured" >&2; exit 1; }
python3 "$SOURCE_DIR/arcology-os/scripts/run/check_ppm_text_region.py" "$TMP_ROOT/screen.ppm" 0 0 300 100 0.02 || { echo "FAIL: the on-screen terminal's own text region has no real rendered pixels" >&2; exit 1; }

echo "PASS: RFC-0045 Phase 4/5 substrate unification -- real ExitBootServices + APS's own CR3/GDT/IDT takeover + RFC-0007's genuine HELP/OT/?SYNTAX ERROR/backspace command loop, fed by BOTH the PS/2 and USB HID input paths together and confirmed working via EITHER path alone, output to BOTH real serial AND a real GOP-framebuffer on-screen terminal (confirmed via a real screendump pixel check); negative control confirmed real"
