#!/usr/bin/env bash
# QEMU/OVMF harness for ArcoBASIC UEFI applications that read real keyboard input
# (systemTable.ConsoleIn.ReadKeyStroke) -- RFC-0007's own real prerequisite. A near-identical twin
# of run-uefi-hello.sh (see that script for the base rationale on every flag this one also uses);
# the addition is a QEMU human-monitor UNIX socket used to inject real key events via `sendkey`,
# the same mechanism a real keyboard driver in QEMU delivers scancodes through -- not a
# ConsoleIn-specific or ArcoBASIC-specific shortcut.
#
# A real, empirically-found timing requirement (not assumed): QEMU's monitor socket exists and
# accepts connections well before OVMF's own PS/2 keyboard driver is ready to receive/queue
# scancodes -- injecting immediately after the socket appears silently drops the keystroke. This
# harness instead waits for READY_MARKER to appear in the guest's own captured output first (proof
# the application itself is actively polling for input), then a fixed settle delay, before sending
# any keys -- confirmed empirically to reach 100% delivery in repeated local testing.
#
# Usage: run-uefi-hello-with-keyboard.sh EFI_FILE READY_MARKER KEY_SEQUENCE EXPECTED_OUTPUT [TIMEOUT_SECONDS]
#   READY_MARKER: text that must appear in captured output before any key is injected.
#   KEY_SEQUENCE: space-separated QEMU `sendkey` key names, injected in order with a short pause
#                 between each (e.g. "h e l p ret" types "help" then presses Enter).
#
# Exit status: identical contract to run-uefi-hello.sh (0 pass, 1 output mismatch, 2 environment
# problem).
set -euo pipefail

EFI_FILE="${1:?usage: run-uefi-hello-with-keyboard.sh EFI_FILE READY_MARKER KEY_SEQUENCE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
READY_MARKER="${2:?usage: run-uefi-hello-with-keyboard.sh EFI_FILE READY_MARKER KEY_SEQUENCE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
KEY_SEQUENCE="${3:?usage: run-uefi-hello-with-keyboard.sh EFI_FILE READY_MARKER KEY_SEQUENCE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
EXPECTED="${4:?usage: run-uefi-hello-with-keyboard.sh EFI_FILE READY_MARKER KEY_SEQUENCE EXPECTED_OUTPUT [TIMEOUT_SECONDS]}"
TIMEOUT_SECONDS="${5:-30}"

QEMU_BIN="$(command -v qemu-system-x86_64 || true)"
if [ -z "$QEMU_BIN" ]; then
    cat >&2 <<'EOF'
ERROR: qemu-system-x86_64 was not found on PATH.

Install QEMU's x86 system emulator to run this harness, for example:
    apt install qemu-system-x86     (Debian/Ubuntu)
    dnf install qemu-system-x86     (Fedora)
    pacman -S qemu-system-x86       (Arch Linux)

This harness never downloads or installs anything automatically.
EOF
    exit 2
fi

if ! command -v socat > /dev/null 2>&1; then
    cat >&2 <<'EOF'
ERROR: socat was not found on PATH.

Install socat to script QEMU's human-monitor socket, for example:
    apt install socat     (Debian/Ubuntu)
    dnf install socat      (Fedora)
    pacman -S socat        (Arch Linux)
EOF
    exit 2
fi

OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1 || true)"
if [ -z "$OVMF_FD" ]; then
    cat >&2 <<'EOF'
ERROR: no OVMF UEFI firmware image was found under /usr/share/ovmf or /usr/share/OVMF.

Install OVMF to run this harness, for example:
    apt install ovmf                (Debian/Ubuntu)
    dnf install edk2-ovmf           (Fedora)
    pacman -S edk2-ovmf              (Arch Linux)

This harness never downloads firmware automatically.
EOF
    exit 2
fi

if [ ! -f "$EFI_FILE" ]; then
    echo "ERROR: EFI application not found: $EFI_FILE" >&2
    exit 2
fi

BOOT_DIR="$(mktemp -d)"
MON_SOCK="$(mktemp -u)"
OUTFILE="$(mktemp)"
trap 'rm -rf "$BOOT_DIR"; rm -f "$MON_SOCK" "$OUTFILE"' EXIT
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$EFI_FILE" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

"$QEMU_BIN" \
    -bios "$OVMF_FD" \
    -m 512 \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none \
    -vga none \
    -display none \
    -serial stdio \
    -monitor unix:"$MON_SOCK",server,nowait \
    -no-reboot > "$OUTFILE" 2>/dev/null &
QEMU_PID=$!

# Wait for the monitor socket to exist (fast), then for the guest's own READY_MARKER to appear in
# captured output (the real readiness signal -- see header comment), then a fixed settle delay
# before injecting any keys.
SOCKET_DEADLINE=$((SECONDS + 10))
while [ ! -S "$MON_SOCK" ]; do
    if [ "$SECONDS" -ge "$SOCKET_DEADLINE" ]; then
        kill "$QEMU_PID" 2>/dev/null || true
        wait "$QEMU_PID" 2>/dev/null || true
        echo "FAIL: QEMU monitor socket never appeared" >&2
        exit 1
    fi
    sleep 0.1
done

MARKER_DEADLINE=$((SECONDS + TIMEOUT_SECONDS))
while ! grep -aqF "$READY_MARKER" "$OUTFILE" 2>/dev/null; do
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        break
    fi
    if [ "$SECONDS" -ge "$MARKER_DEADLINE" ]; then
        break
    fi
    sleep 0.2
done
sleep 2

if kill -0 "$QEMU_PID" 2>/dev/null; then
    for key in $KEY_SEQUENCE; do
        printf 'sendkey %s\n' "$key" | socat - UNIX-CONNECT:"$MON_SOCK" > /dev/null 2>&1 || true
        sleep 0.3
    done
fi

matched=0
if timeout "$TIMEOUT_SECONDS" bash -c 'tail -n +1 -f "$1" | grep -m1 -qF "$2"' _ "$OUTFILE" "$EXPECTED"; then
    matched=1
fi

kill "$QEMU_PID" 2>/dev/null || true
wait "$QEMU_PID" 2>/dev/null || true

captured="$(cat "$OUTFILE" 2>/dev/null || true)"

if [ "$matched" = "0" ] && printf '%s' "$captured" | grep -aqF "$EXPECTED"; then
    matched=1
fi

if [ "$matched" = "1" ]; then
    echo "PASS: $EXPECTED"
    exit 0
fi

echo "FAIL: expected output not found: $EXPECTED" >&2
echo "--- captured console output (last 4000 bytes) ---" >&2
printf '%s' "$captured" | tail -c 4000 >&2
exit 1
