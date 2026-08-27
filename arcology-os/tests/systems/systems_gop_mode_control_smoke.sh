#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/gop-mode-control-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Real, live GOP mode detection and a real, honest deferred resolution switch, added to the
# aps-arcology-seed-substrate shell as MODES / MODE <n>.
#
# A real, empirically-confirmed constraint governs this design: this shell runs after a real
# ExitBootServices, and BOTH EFI_GRAPHICS_OUTPUT_PROTOCOL.QueryMode and .SetMode hang when called
# from there (a real GOP driver's own implementation depends on Boot Services internally -- the
# same class of finding this project already documented for ConsoleOut.Write, just not previously
# known to reach GOP's own protocol methods). So enumeration happens ONLY at boot (cached for the
# shell's own MODES command to read), and MODE <n> cannot switch the display live -- it stages the
# request in a real UEFI NVRAM variable via RuntimeServices.SetVariable (confirmed empirically
# safe post-ExitBootServices, the one Boot-Services-independent persistence mechanism available
# here), applied for real at the NEXT boot before ExitBootServices runs again.
#
# This test proves that whole real chain end to end, using split OVMF CODE/VARS pflash (required
# for genuine cross-process NVRAM persistence -- a single combined -bios image keeps NVRAM
# entirely in-memory, never actually testing persistence): boot 1 lists real modes and stages a
# real mode switch from the shell; a completely separate boot 2, sharing only the same VARS file,
# is independently confirmed (via QEMU's own screendump, not this fixture's own self-report) to
# have actually applied it.

FIXTURE="$TMP_ROOT/aps-aex-substrate-shell.abas"
"$SOURCE_DIR/arcology-os/scripts/build/compose-aps-aex-substrate-shell.sh" "$FIXTURE"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: aps-arcology-seed-substrate (with GOP mode control) does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

CODE_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname '*CODE*4M*.fd' 2>/dev/null | grep -v -iE 'secboot|snakeoil|\.ms\.' | head -1 || true)"
VARS_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname '*VARS*4M*.fd' 2>/dev/null | grep -v -iE 'secboot|snakeoil|\.ms\.' | head -1 || true)"
if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || [ -z "$CODE_FD" ] || [ -z "$VARS_FD" ] || ! command -v socat > /dev/null 2>&1; then
    echo "SKIP: qemu-system-x86_64/split OVMF CODE+VARS pflash/socat unavailable; this test needs real, separate NVRAM persistence across two independent QEMU processes to prove anything."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/substrate.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
BOOT_DIR="$TMP_ROOT/boot"
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$TMP_ROOT/substrate.efi" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"
cp "$VARS_FD" "$TMP_ROOT/vars.fd"

inject_keys() {
    local monsock="$1"; shift
    for k in "$@"; do
        printf 'sendkey %s\n' "$k" | socat - UNIX-CONNECT:"$monsock" > /dev/null 2>&1 || true
        sleep 0.3
    done
}

# Boot 1: list real modes, stage a real switch to mode 1 from the shell.
MON1="$TMP_ROOT/mon1.sock"
OUT1="$TMP_ROOT/boot1.txt"
timeout 60 "$QEMU_BIN" -m 512 -vga std \
    -drive if=pflash,format=raw,readonly=on,file="$CODE_FD" \
    -drive if=pflash,format=raw,file="$TMP_ROOT/vars.fd" \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none -display none -serial file:"$OUT1" -no-reboot \
    -monitor unix:"$MON1",server,nowait > /dev/null 2>&1 &
QEMU1_PID=$!
for _ in $(seq 1 150); do
    grep -aq ":system: >" "$OUT1" 2>/dev/null && break
    sleep 0.2
done
sleep 1
inject_keys "$MON1" m o d e s ret
sleep 1
inject_keys "$MON1" m o d e spc 1 ret
sleep 2
kill "$QEMU1_PID" 2>/dev/null || true
wait "$QEMU1_PID" 2>/dev/null || true

grep -aqF "0: " "$OUT1" || { echo "FAIL: MODES did not list a real mode 0" >&2; cat "$OUT1" >&2; exit 1; }
grep -aqF "MODE 1 STAGED - REBOOT TO APPLY" "$OUT1" || { echo "FAIL: MODE 1 was not staged" >&2; cat "$OUT1" >&2; exit 1; }

# Boot 2: a completely separate QEMU process, sharing only the same (now-modified) vars.fd.
# Independent proof, not this fixture's own self-report: QEMU's own screendump encodes the real
# active resolution in the PPM header it writes.
MON2="$TMP_ROOT/mon2.sock"
OUT2="$TMP_ROOT/boot2.txt"
SHOT="$TMP_ROOT/shot.ppm"
timeout 30 "$QEMU_BIN" -m 512 -vga std \
    -drive if=pflash,format=raw,readonly=on,file="$CODE_FD" \
    -drive if=pflash,format=raw,file="$TMP_ROOT/vars.fd" \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none -display none -serial file:"$OUT2" -no-reboot \
    -monitor unix:"$MON2",server,nowait > /dev/null 2>&1 &
QEMU2_PID=$!
for _ in $(seq 1 100); do
    grep -aq ":system: >" "$OUT2" 2>/dev/null && break
    sleep 0.2
done
sleep 1
printf 'screendump %s\n' "$SHOT" | socat - UNIX-CONNECT:"$MON2" > /dev/null 2>&1 || true
sleep 1
kill "$QEMU2_PID" 2>/dev/null || true
wait "$QEMU2_PID" 2>/dev/null || true

grep -aqF "SUBSTRATE GOP READY" "$OUT2" || { echo "FAIL: GOP was not ready on the second boot" >&2; cat "$OUT2" >&2; exit 1; }
[ -s "$SHOT" ] || { echo "FAIL: screendump did not produce an image" >&2; exit 1; }
head -c 40 "$SHOT" | grep -aq "640 480" || {
    echo "FAIL: the staged mode 1 request was not actually applied on the next real boot (expected a real 640x480 framebuffer)" >&2
    head -c 40 "$SHOT" | od -c >&2
    exit 1
}

echo "PASS: real GOP mode enumeration (MODES) and a real, honest deferred resolution switch (MODE <n> stages a real UEFI NVRAM request via RuntimeServices.SetVariable, safe post-ExitBootServices) -- an independent second boot, sharing only the same NVRAM store, is confirmed via QEMU's own screendump to have actually applied it"
