#!/usr/bin/env bash
set -euo pipefail
ARCOFISSION="$1"
SOURCE_DIR="$2"
TMP_ROOT="${TMPDIR:-/tmp}/virtio-net-transport-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# RFC-0048 Milestone N0 (Networking Development Steering Addendum, Section 11 acceptance
# direction: "the NIC can initialize; transmit and receive paths operate; frame ownership is
# defined"): real VirtIO-net legacy PCI transport including real virtqueue-based frame transmit
# and receive, consumed through the generic Network Interface Contract for setup and the driver's
# own real virtqueue functions for the data path (Transmit/Receive are deliberately not yet part
# of the generic contract -- see network_interface_contract.abas's own header comment).
#
# `disable-modern=on` forces QEMU's virtio-net-pci into pure legacy transport; `vectors=0`
# disables MSI-X, keeping the legacy device-specific config offset fixed at +0x14. A fixed `mac=`
# keeps the fixture's own transmitted frame deterministic.
#
# This test proves BOTH directions of the RFC's own N0 acceptance test (Section 18.1) using QEMU's
# `-netdev socket,udp=...,localaddr=...` backend, a plain UDP tunnel where every frame the guest
# transmits arrives at the configured peer as one UDP datagram, and every datagram received on
# QEMU's own bound `localaddr` is injected into the guest as one Ethernet frame (see
# virtio_net_frame_tool.py for the send/listen tooling this test drives):
#
#   TX proof: the fixture builds and transmits a known 60-byte frame right after RX/TX queue
#   setup. This test independently captures it off the wire (outside the driver entirely) and
#   checks it byte-for-byte.
#
#   RX proof: this test injects a known 60-byte frame only after the fixture's own RXREADY marker
#   appears (the RX buffer is posted and the device may deliver a frame from that point on) --
#   matching this project's own established "wait for a readiness marker, not just socket
#   existence" keystroke-injection discipline (run-uefi-hello-with-keyboard.sh). The fixture parses
#   the received frame (past the real 10-byte virtio_net_hdr the device itself prefixes) and
#   reports its real content over serial for this test to check byte-for-byte.

FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/virtio-net-transport/virtio-net-transport.abas"
"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/x86.txt" 2>&1
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/x86.txt" || {
    echo "FAIL: virtio-net-transport does not compile:" >&2
    cat "$TMP_ROOT/x86.txt" >&2
    exit 1
}

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi
if ! command -v python3 > /dev/null 2>&1; then
    echo "SKIP: python3 not installed; this test needs it to inject/capture raw frames."
    exit 0
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/probe.efi" --target uefi-x86_64 --entry Main > /dev/null

QEMU_BIN="$(command -v qemu-system-x86_64)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1)"
FRAME_TOOL="$SOURCE_DIR/arcology-os/scripts/run/virtio_net_frame_tool.py"

BOOT_DIR="$TMP_ROOT/boot"
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$TMP_ROOT/probe.efi" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

QEMU_LISTEN_PORT=$((30000 + ($$ % 10000)))
HOST_LISTEN_PORT=$((QEMU_LISTEN_PORT + 1))
SERIAL_FILE="$TMP_ROOT/serial.txt"
TX_CAPTURE_FILE="$TMP_ROOT/tx_capture.hex"
touch "$SERIAL_FILE"

# Real injected frame: dst=this fixture's own fixed MAC, src=an arbitrary test-host MAC,
# ethertype=0x88B5, payload "ARCOTEST" padded with zero bytes to the real Ethernet minimum
# payload (46 bytes) -- a real 60-byte frame, built the same programmatic way the expected
# fixture-output substrings below are derived, so there is exactly one source of truth for this
# frame's content.
INJECT_HEX="$(python3 -c "
dst = bytes.fromhex('525400123456')
src = bytes.fromhex('525400aabbcc')
ethertype = bytes.fromhex('88b5')
payload = b'ARCOTEST' + b'\x00' * 38
print((dst + src + ethertype + payload).hex())
")"

# Listener started BEFORE QEMU so it is guaranteed bound before the guest could possibly transmit
# (this backend is plain UDP -- a datagram sent before anyone is listening is simply lost).
python3 "$FRAME_TOOL" listen 127.0.0.1 "$HOST_LISTEN_PORT" 15 "$TX_CAPTURE_FILE" &
LISTENER_PID=$!
sleep 0.3

timeout 20 "$QEMU_BIN" -nodefaults -bios "$OVMF_FD" -m 512 -vga none \
    -drive file="fat:rw:$BOOT_DIR",format=raw,if=ide \
    -netdev socket,id=n0,udp=127.0.0.1:"$HOST_LISTEN_PORT",localaddr=127.0.0.1:"$QEMU_LISTEN_PORT" \
    -device virtio-net-pci,netdev=n0,mac=52:54:00:12:34:56,disable-modern=on,vectors=0 \
    -display none -serial file:"$SERIAL_FILE" -no-reboot > /dev/null 2>&1 &
QEMU_PID=$!

# Wait for the real RXREADY marker (the RX buffer is posted and the device may now deliver a
# frame) before injecting -- not merely for QEMU/the netdev socket to exist.
MARKER_DEADLINE=$((SECONDS + 15))
while ! grep -aqF "RXREADY" "$SERIAL_FILE" 2>/dev/null; do
    if ! kill -0 "$QEMU_PID" 2>/dev/null; then
        break
    fi
    if [ "$SECONDS" -ge "$MARKER_DEADLINE" ]; then
        break
    fi
    sleep 0.1
done

python3 "$FRAME_TOOL" send 127.0.0.1 "$QEMU_LISTEN_PORT" "$INJECT_HEX"

wait "$QEMU_PID" 2>/dev/null || true
wait "$LISTENER_PID" 2>/dev/null || true

# TX proof: the fixture's own transmitted frame, captured independently off the wire.
EXPECTED_TX_HEX="$(python3 -c "
dst = bytes.fromhex('525400aabbcc')
src = bytes.fromhex('525400123456')
ethertype = bytes.fromhex('88b6')
payload = b'ARCOECHO' + b'\x00' * 38
print((dst + src + ethertype + payload).hex())
")"
CAPTURED_TX_HEX="$(cat "$TX_CAPTURE_FILE" 2>/dev/null || true)"
if [ "$(printf '%s' "$CAPTURED_TX_HEX" | tr 'A-F' 'a-f')" != "$EXPECTED_TX_HEX" ]; then
    echo "FAIL: the fixture's own transmitted frame did not match byte-for-byte when captured independently off the wire" >&2
    echo "expected: $EXPECTED_TX_HEX" >&2
    echo "captured: $CAPTURED_TX_HEX" >&2
    echo "--- serial output ---" >&2
    cat "$SERIAL_FILE" >&2
    exit 1
fi

# RX proof: the fixture's own real parsed report of the injected frame.
grep -aqF "TXOK" "$SERIAL_FILE" || { echo "FAIL: the fixture never confirmed its own transmit completion" >&2; cat "$SERIAL_FILE" >&2; exit 1; }
grep -aqF "RXOK LEN=70 DST=525400123456 SRC=525400AABBCC ET=88B5 PAY=4152434F54455354" "$SERIAL_FILE" || {
    echo "FAIL: the fixture did not correctly receive and parse the real injected frame" >&2
    cat "$SERIAL_FILE" >&2
    exit 1
}
grep -aqF "DONE" "$SERIAL_FILE" || { echo "FAIL: probe did not complete" >&2; cat "$SERIAL_FILE" >&2; exit 1; }

echo "PASS: RFC-0048 Milestone N0 real VirtIO-net virtqueue transport -- a real known frame transmitted by the guest was captured byte-for-byte off the wire, and a real known frame injected from outside the guest was received, correctly parsed past its real virtio_net_hdr, and reported byte-for-byte -- both directions proven against a real QEMU virtio-net-pci device"
