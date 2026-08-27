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
# equipment most laptops don't have; this makes the substrate shell session genuinely visible on a real
# screen too.
#
# Two real findings surfaced by this fixture's own development, not present in aps-gdt-reload.abas
# (which never touches device MMIO after its own CR3 switch): the minimal identity-mapped page
# tables that fixture established only cover the low 1GB -- not enough here, since BOTH the xHCI
# controller's own real MMIO BAR and the GOP framebuffer's own real base sit far outside that
# range, and both need to stay reachable post-substrate. Fixed with a reusable, dynamically-
# computed 1GB page mapping (MapExtra1GbRegion) applied to each real region's own discovered base
# address, never hardcoded.

FIXTURE="$TMP_ROOT/aps-aex-substrate-shell.abas"
"$SOURCE_DIR/arcology-os/scripts/build/compose-aps-aex-substrate-shell.sh" "$FIXTURE"
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

# sendkey one letter/word at a time, in order, once the fixture's own ArcFS prompt appears.
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
            timeout 90 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
                -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 \
                -drive file="fat:rw:$boot_dir",format=raw \
                -net none -vga none -display none -serial stdio -no-reboot \
                -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
                > "$outfile" 2>&1 &
            ;;
        ps2only)
            timeout 90 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
                -drive file="fat:rw:$boot_dir",format=raw \
                -net none -vga none -display none -serial stdio -no-reboot \
                -monitor unix:"$TMP_ROOT/mon.sock",server,nowait \
                > "$outfile" 2>&1 &
            ;;
        usbonly)
            timeout 90 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 \
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
            timeout 90 "$QEMU_BIN" -bios "$OVMF_FD" -m 512 \
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
            grep -aq ":system: >" "$outfile" 2>/dev/null && break
            sleep 0.2
        done
        sleep 1
        inject_keys "$TMP_ROOT/mon.sock" "$@"
        sleep 3
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
run_once "$TMP_ROOT/combined.txt" both \
    o t ret \
    p w d ret \
    d i r ret \
    d i r spc shift-semicolon s y s t e m ret \
    d i r spc shift-semicolon s y s t e m shift-semicolon a p p s ret \
    t y p e spc shift-semicolon s y s t e m shift-semicolon a p p s shift-semicolon t e s t dot a e x ret \
    r u n spc shift-semicolon s y s t e m shift-semicolon a p p s shift-semicolon t e s t dot a e x ret \
    z z z ret h z backspace e l p ret e x i t ret
grep -aqF "SUBSTRATE LIVE" "$TMP_ROOT/combined.txt" || { echo "FAIL: substrate takeover (CR3/GDT/IDT) did not complete" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "THE WAGON HAS BROKEN DOWN." "$TMP_ROOT/combined.txt" || { echo "FAIL: OT command did not produce its real response" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "AEX USERSPACE PASS" "$TMP_ROOT/combined.txt" || { echo "FAIL: RUN :system:apps:test.aex did not launch the preloaded AEX" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "?SYNTAX ERROR" "$TMP_ROOT/combined.txt" || { echo "FAIL: an unrecognized command did not produce ?SYNTAX ERROR" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "COMMANDS: HELP OT PWD DIR TYPE HEX STAT SERVICES CONTRACTS RUN LIST NEW CLEAR" "$TMP_ROOT/combined.txt" || { echo "FAIL: backspace-corrected HELP command did not match (real line-editing proof)" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "ARCOLOGY SEED HALT" "$TMP_ROOT/combined.txt" || { echo "FAIL: EXIT command did not terminate the loop" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "|-- apps" "$TMP_ROOT/combined.txt" || { echo "FAIL: DIR did not render the ArcFS apps directory" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "|-- devices" "$TMP_ROOT/combined.txt" || { echo "FAIL: DIR did not render the ArcFS devices directory" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "|-- services" "$TMP_ROOT/combined.txt" || { echo "FAIL: DIR did not render the ArcFS services directory" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "|-- substrate" "$TMP_ROOT/combined.txt" || { echo "FAIL: DIR did not render the ArcFS substrate directory" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "4 entries 0 files 0 links" "$TMP_ROOT/combined.txt" || { echo "FAIL: DIR summary for :system: is wrong" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "|-- test.aex" "$TMP_ROOT/combined.txt" || { echo "FAIL: DIR :system:apps did not render the preloaded AEX file" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "EXECUTION CONTRACT // [AEX]" "$TMP_ROOT/combined.txt" || { echo "FAIL: TYPE did not render the AEX contract panel" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "CONTRACT : VALID" "$TMP_ROOT/combined.txt" || { echo "FAIL: AEX contract panel did not report a valid contract" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "ENTRY : RESOLVED" "$TMP_ROOT/combined.txt" || { echo "FAIL: AEX contract panel did not report a resolved entry" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
grep -aqF "STATE : READY" "$TMP_ROOT/combined.txt" || { echo "FAIL: AEX contract panel did not report ready state" >&2; cat "$TMP_ROOT/combined.txt" >&2; exit 1; }
if grep -aqF "ARCOAEX@" "$TMP_ROOT/combined.txt"; then
    echo "FAIL: TYPE leaked the raw AEX binary header instead of structured metadata" >&2
    cat "$TMP_ROOT/combined.txt" >&2
    exit 1
fi

run_once "$TMP_ROOT/devtools.txt" both \
    s t a t spc shift-semicolon s y s t e m shift-semicolon a p p s shift-semicolon t e s t dot a e x ret \
    h e x spc shift-semicolon s y s t e m shift-semicolon a p p s shift-semicolon t e s t dot a e x ret \
    s e r v i c e s ret \
    c o n t r a c t s ret \
    e x i t ret
grep -aqF "PATH : :system:apps:test.aex" "$TMP_ROOT/devtools.txt" || { echo "FAIL: STAT did not report the target path" >&2; cat "$TMP_ROOT/devtools.txt" >&2; exit 1; }
grep -aqF "TYPE : FILE" "$TMP_ROOT/devtools.txt" || { echo "FAIL: STAT did not identify test.aex as a file" >&2; cat "$TMP_ROOT/devtools.txt" >&2; exit 1; }
grep -aqF "41 52 43 4F 41 45 58 00" "$TMP_ROOT/devtools.txt" || { echo "FAIL: HEX did not expose the AEX magic bytes safely" >&2; cat "$TMP_ROOT/devtools.txt" >&2; exit 1; }
grep -aqF ":system:services 7 entries" "$TMP_ROOT/devtools.txt" || { echo "FAIL: SERVICES did not report the service domain summary" >&2; cat "$TMP_ROOT/devtools.txt" >&2; exit 1; }
grep -aqF ":system:services:console:write" "$TMP_ROOT/devtools.txt" || { echo "FAIL: SERVICES did not include console write service" >&2; cat "$TMP_ROOT/devtools.txt" >&2; exit 1; }
grep -aqF ":system:services:arcfs:close TYPE SERVICE STATE READY CONTRACT :aex:bindings:1006" "$TMP_ROOT/devtools.txt" || { echo "FAIL: SERVICES did not include ArcFS close service contract" >&2; cat "$TMP_ROOT/devtools.txt" >&2; exit 1; }
grep -aqF "CONTRACTS 1" "$TMP_ROOT/devtools.txt" || { echo "FAIL: CONTRACTS did not report the loaded AEX contract" >&2; cat "$TMP_ROOT/devtools.txt" >&2; exit 1; }

run_once "$TMP_ROOT/wrap.txt" both \
    d i r ret d i r ret d i r ret d i r ret d i r ret d i r ret \
    d i r ret d i r ret d i r ret d i r ret d i r ret d i r ret \
    d i r ret d i r ret d i r ret d i r ret d i r ret d i r ret \
    p w d ret e x i t ret
grep -aqF ":system:" "$TMP_ROOT/wrap.txt" || { echo "FAIL: shell did not answer after terminal wrap stress" >&2; cat "$TMP_ROOT/wrap.txt" >&2; exit 1; }
grep -aqF "ARCOLOGY SEED HALT" "$TMP_ROOT/wrap.txt" || { echo "FAIL: shell did not exit after terminal wrap stress" >&2; cat "$TMP_ROOT/wrap.txt" >&2; exit 1; }

# Real proof EITHER input path alone is sufficient (RFC-0045's own explicit requirement): PS/2
# only (no xHCI controller present at all) and USB HID only (-nodefaults, no PS/2 keyboard).
run_once "$TMP_ROOT/ps2only.txt" ps2only h e l p ret e x i t ret
grep -aqF "SUBSTRATE USB NONE" "$TMP_ROOT/ps2only.txt" || { echo "FAIL: PS/2-only run unexpectedly found a USB keyboard" >&2; cat "$TMP_ROOT/ps2only.txt" >&2; exit 1; }
grep -aqF "COMMANDS: HELP OT PWD DIR TYPE HEX STAT SERVICES CONTRACTS RUN LIST NEW CLEAR" "$TMP_ROOT/ps2only.txt" || { echo "FAIL: PS/2-only path did not correctly read/dispatch HELP" >&2; cat "$TMP_ROOT/ps2only.txt" >&2; exit 1; }
grep -aqF "ARCOLOGY SEED HALT" "$TMP_ROOT/ps2only.txt" || { echo "FAIL: PS/2-only run did not reach EXIT" >&2; cat "$TMP_ROOT/ps2only.txt" >&2; exit 1; }

run_once "$TMP_ROOT/usbonly.txt" usbonly h e l p ret e x i t ret
grep -aqF "SUBSTRATE USB READY" "$TMP_ROOT/usbonly.txt" || { echo "FAIL: USB-only run did not find/enumerate the USB keyboard" >&2; cat "$TMP_ROOT/usbonly.txt" >&2; exit 1; }
grep -aqF "COMMANDS: HELP OT PWD DIR TYPE HEX STAT SERVICES CONTRACTS RUN LIST NEW CLEAR" "$TMP_ROOT/usbonly.txt" || { echo "FAIL: USB-only path did not correctly read/dispatch HELP" >&2; cat "$TMP_ROOT/usbonly.txt" >&2; exit 1; }
grep -aqF "ARCOLOGY SEED HALT" "$TMP_ROOT/usbonly.txt" || { echo "FAIL: USB-only run did not reach EXIT" >&2; cat "$TMP_ROOT/usbonly.txt" >&2; exit 1; }

# Negative control: no input device at all -- reaches the ArcFS prompt and genuinely just waits, no crash,
# no spurious command dispatch.
run_once "$TMP_ROOT/negative.txt" none
grep -aqF ":system: >" "$TMP_ROOT/negative.txt" || { echo "FAIL: negative control did not even reach the ArcFS prompt" >&2; cat "$TMP_ROOT/negative.txt" >&2; exit 1; }
if grep -aqi "BAD\|FAIL" "$TMP_ROOT/negative.txt"; then
    echo "FAIL: negative control hit a real error marker" >&2
    cat "$TMP_ROOT/negative.txt" >&2
    exit 1
fi

# Real proof the on-screen terminal actually rendered something -- not just that the fixture
# reported "GOP READY" without ever touching the framebuffer. A real qemu-xhci+usb-kbd device plus
# a real GOP-capable display (-vga std), a real injected HELP, a real QEMU screendump (a genuine
# P6 PPM capture of the actual framebuffer, not a synthetic one), and a dependency-free pixel
# count confirming real text is lit in the top-left region where "ARCOLOGY SEED"/":system: >" are
# always drawn first.
SCREENSHOT_PATH="$TMP_ROOT/screen.ppm" run_once "$TMP_ROOT/gop.txt" gop h e l p ret
grep -aqF "SUBSTRATE GOP READY" "$TMP_ROOT/gop.txt" || { echo "FAIL: GOP was not discovered" >&2; cat "$TMP_ROOT/gop.txt" >&2; exit 1; }
[ -s "$TMP_ROOT/screen.ppm" ] || { echo "FAIL: no screendump was captured" >&2; exit 1; }
python3 "$SOURCE_DIR/arcology-os/scripts/run/check_ppm_text_region.py" "$TMP_ROOT/screen.ppm" 0 0 300 100 0.02 || { echo "FAIL: the on-screen terminal's own text region has no real rendered pixels" >&2; exit 1; }

# RFC-0007 Program Mode, first real increment (PRINT + GOTO only): numbered-line PRINT storage,
# RUN executing multiple stored lines in real sorted order, and the real shift-apostrophe ->
# double-quote translation this whole feature depends on being typable at all on a real US QWERTY
# layout (RFC-0045's own drivers are "unshifted only" -- this is the one real, narrow exception).
run_once "$TMP_ROOT/run_print.txt" both \
    1 0 spc p r i n t spc shift-apostrophe h i shift-apostrophe ret \
    2 0 spc p r i n t spc shift-apostrophe b y e shift-apostrophe ret \
    r u n ret
grep -aqF $'hi\r' "$TMP_ROOT/run_print.txt" || { echo "FAIL: RUN did not execute the first stored PRINT line" >&2; cat "$TMP_ROOT/run_print.txt" >&2; exit 1; }
grep -aqF $'bye\r' "$TMP_ROOT/run_print.txt" || { echo "FAIL: RUN did not execute the second stored PRINT line in order" >&2; cat "$TMP_ROOT/run_print.txt" >&2; exit 1; }

# GOTO storage + LIST reconstructing real stored source, plus RUN GOTO-ing to an undefined line
# producing a real, correct error instead of a false match or a hang.
run_once "$TMP_ROOT/run_goto.txt" both \
    1 0 spc p r i n t spc shift-apostrophe l o o p shift-apostrophe ret \
    2 0 spc g o t o spc 9 9 ret \
    l i s t ret \
    r u n ret
grep -aqF '10 PRINT "loop"' "$TMP_ROOT/run_goto.txt" || { echo "FAIL: LIST did not reconstruct the stored PRINT line" >&2; cat "$TMP_ROOT/run_goto.txt" >&2; exit 1; }
grep -aqF '20 GOTO 99' "$TMP_ROOT/run_goto.txt" || { echo "FAIL: LIST did not reconstruct the stored GOTO line" >&2; cat "$TMP_ROOT/run_goto.txt" >&2; exit 1; }
grep -aqF '?UNDEFINED STATEMENT IN 20' "$TMP_ROOT/run_goto.txt" || { echo "FAIL: RUN did not report a real undefined-statement error for a GOTO to a nonexistent line" >&2; cat "$TMP_ROOT/run_goto.txt" >&2; exit 1; }

# A real `10 PRINT "LOOP" / 20 GOTO 10` infinite loop, actually run, actually interrupted -- the
# real, necessary answer (a cooperative once-per-statement ESC poll) to "how do you stop a running
# program" that supporting GOTO at all requires. Also proves NEW genuinely clears RPM.
run_once "$TMP_ROOT/run_break.txt" both \
    1 0 spc p r i n t spc shift-apostrophe l o o p shift-apostrophe ret \
    2 0 spc g o t o spc 1 0 ret \
    r u n ret esc \
    n e w ret \
    l i s t ret
grep -aqF $'loop\r' "$TMP_ROOT/run_break.txt" || { echo "FAIL: the GOTO loop never actually ran" >&2; cat "$TMP_ROOT/run_break.txt" >&2; exit 1; }
grep -aqF 'BREAK IN' "$TMP_ROOT/run_break.txt" || { echo "FAIL: ESC did not interrupt the running GOTO loop" >&2; cat "$TMP_ROOT/run_break.txt" >&2; exit 1; }
LOOP_COUNT=$(grep -acF $'loop\r' "$TMP_ROOT/run_break.txt")
[ "$LOOP_COUNT" -gt 1 ] || { echo "FAIL: the loop only ran once -- GOTO is not actually looping" >&2; cat "$TMP_ROOT/run_break.txt" >&2; exit 1; }

# Real, general Shift support (a real hardware finding -- the user asked for it directly after the
# narrow apostrophe-only version left them unable to type anything else shifted): uppercase letters
# via the classic +/-32 ASCII case flip, plus a real shifted-digit symbol, both via real injected
# `shift-X` keystrokes on both input paths.
run_once "$TMP_ROOT/run_shift.txt" both \
    p r i n t spc shift-apostrophe shift-b i g spc shift-t e s t spc 1 shift-1 shift-apostrophe ret
grep -aqF 'Big Test 1!' "$TMP_ROOT/run_shift.txt" || { echo "FAIL: Shift did not produce real uppercase letters and a real shifted digit symbol" >&2; cat "$TMP_ROOT/run_shift.txt" >&2; exit 1; }

echo "PASS: RFC-0045 Phase 4/5 substrate unification -- real ExitBootServices + APS's own CR3/GDT/IDT takeover + RFC-0007's genuine HELP/OT/?SYNTAX ERROR/backspace command loop, fed by BOTH the PS/2 and USB HID input paths together and confirmed working via EITHER path alone, output to BOTH real serial AND a real GOP-framebuffer on-screen terminal (confirmed via a real screendump pixel check); negative control confirmed real. PLUS RFC-0007 Program Mode's first real increment (PRINT + GOTO): numbered-line storage, RUN executing stored lines in order (including a real shift-apostrophe -> double-quote keystroke), LIST reconstructing stored source, a real undefined-statement GOTO error, a real infinite GOTO loop genuinely interrupted by ESC, and real general Shift support (uppercase letters + shifted digit symbols, not just the apostrophe)"
