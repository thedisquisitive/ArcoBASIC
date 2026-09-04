#!/usr/bin/env python3
"""RFC-0048 Milestone N0: minimal raw-Ethernet-frame injector/capture tool for QEMU's
`-netdev socket,udp=...,localaddr=...` backend.

That backend is a plain UDP tunnel between QEMU and one peer: every frame the guest transmits
arrives at the configured peer address as exactly one UDP datagram (no extra framing), and every
UDP datagram received on QEMU's own bound `localaddr` is injected into the guest as one Ethernet
frame. This tool is intentionally that simple too -- it does not understand Ethernet, ARP, IPv4,
or any other protocol; it only moves raw bytes, which is exactly what a driver-level transport
proof needs and no more (see systems_virtio_net_transport_smoke.sh for the real proof this
supports).

Usage:
    virtio_net_frame_tool.py send HOST PORT HEXFRAME
        Sends the given hex-decoded bytes as a single UDP datagram to HOST:PORT, then exits.

    virtio_net_frame_tool.py listen HOST PORT TIMEOUT_SECONDS OUTFILE
        Binds HOST:PORT and waits up to TIMEOUT_SECONDS for one datagram. Writes its hex encoding
        to OUTFILE on receipt, or writes the literal text "TIMEOUT" to OUTFILE if none arrives.
        Always exits 0 -- the calling smoke test inspects OUTFILE's content, not this process's
        exit status, since "no packet arrived in time" is a real test failure to report, not a
        tool crash.
"""
import socket
import sys


def send(host: str, port: int, hex_frame: str) -> int:
    frame = bytes.fromhex(hex_frame)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.sendto(frame, (host, port))
    s.close()
    return 0


def listen(host: str, port: int, timeout_seconds: float, outfile: str) -> int:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((host, port))
    s.settimeout(timeout_seconds)
    try:
        data, _addr = s.recvfrom(65536)
        with open(outfile, "w") as f:
            f.write(data.hex())
    except socket.timeout:
        with open(outfile, "w") as f:
            f.write("TIMEOUT")
    finally:
        s.close()
    return 0


def main(argv: list) -> int:
    if len(argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    mode = argv[1]
    if mode == "send" and len(argv) == 5:
        return send(argv[2], int(argv[3]), argv[4])
    if mode == "listen" and len(argv) == 6:
        return listen(argv[2], int(argv[3]), float(argv[4]), argv[5])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
