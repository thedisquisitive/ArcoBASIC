#!/usr/bin/env python3
"""Generate the tiny AEX userspace smoke executable.

The raw form is used by QEMU's loader preload. The ArcoBASIC form embeds the same bytes directly
into a hardware-test EFI so physical machines do not need an external memory preloader.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def build_aex() -> bytes:
    data = bytearray(864)
    data[0:8] = b"ARCOAEX\0"
    struct.pack_into("<I", data, 8, 65536)
    struct.pack_into("<I", data, 12, 64)
    struct.pack_into("<Q", data, 16, 1229782938247303441)
    struct.pack_into("<Q", data, 24, 2459565876494606882)
    struct.pack_into("<Q", data, 32, 64)
    struct.pack_into("<I", data, 40, 6)
    struct.pack_into("<I", data, 44, 1)

    types = {
        "manifest": (14719843082355629295, 9271930309043522079),
        "component": (17360645306394565346, 10985166509681775976),
        "implementation": (7866732109378180226, 11707123199072077656),
        "lifecycle": (11836149854792734762, 12071940426696027757),
        "code": (859404424522585693, 12635621945936320080),
        "integrity": (5265016724168201421, 11438856236987991108),
    }

    entries = [
        (1, "manifest", 640, 80),
        (2, "component", 720, 48),
        (10, "implementation", 768, 40),
        (22, "lifecycle", 808, 16),
        (30, "code", 824, 3),
        (90, "integrity", 832, 32),
    ]
    for i, (chunk_id, type_name, offset, length) in enumerate(entries):
        entry = 64 + i * 96
        high, low = types[type_name]
        struct.pack_into("<I", data, entry + 0, chunk_id)
        struct.pack_into("<Q", data, entry + 4, high)
        struct.pack_into("<Q", data, entry + 12, low)
        struct.pack_into("<I", data, entry + 20, 1)
        struct.pack_into("<Q", data, entry + 24, offset)
        struct.pack_into("<Q", data, entry + 32, length)
        struct.pack_into("<Q", data, entry + 40, length)
        struct.pack_into("<I", data, entry + 48, 8)

    manifest = 640
    struct.pack_into("<Q", data, manifest + 0, 1229782938247303441)
    struct.pack_into("<Q", data, manifest + 8, 2459565876494606882)
    struct.pack_into("<I", data, manifest + 16, 1)
    struct.pack_into("<Q", data, manifest + 28, 3689348814741910323)
    struct.pack_into("<Q", data, manifest + 36, 4919131752989213764)
    struct.pack_into("<I", data, manifest + 44, 1)
    struct.pack_into("<I", data, manifest + 52, 1)
    struct.pack_into("<I", data, manifest + 60, 2)
    struct.pack_into("<I", data, manifest + 64, 1)
    struct.pack_into("<I", data, manifest + 76, 2)

    component = 720
    struct.pack_into("<I", data, component + 0, 2)
    struct.pack_into("<Q", data, component + 4, 1229782938247303441)
    struct.pack_into("<Q", data, component + 12, 2459565876494606882)
    struct.pack_into("<I", data, component + 20, 1)
    struct.pack_into("<I", data, component + 28, 1)
    struct.pack_into("<I", data, component + 44, 10)

    impl = 768
    struct.pack_into("<I", data, impl + 0, 2)
    struct.pack_into("<I", data, impl + 4, 1)
    struct.pack_into("<I", data, impl + 8, 1)
    struct.pack_into("<I", data, impl + 12, 1)
    struct.pack_into("<I", data, impl + 16, 0)
    struct.pack_into("<I", data, impl + 28, 7)
    struct.pack_into("<I", data, impl + 32, 30)
    struct.pack_into("<I", data, impl + 36, 0)

    lifecycle = 808
    struct.pack_into("<I", data, lifecycle + 0, 2)
    struct.pack_into("<I", data, lifecycle + 4, 10)
    struct.pack_into("<Q", data, lifecycle + 8, 0)

    code = bytes([0x8B, 0x01, 0xC3])
    data[824 : 824 + len(code)] = code

    integrity = 832
    struct.pack_into("<I", data, integrity + 0, 90)
    struct.pack_into("<I", data, integrity + 4, 1)
    struct.pack_into("<I", data, integrity + 8, 30)
    struct.pack_into("<I", data, integrity + 12, sum(code))
    struct.pack_into("<Q", data, integrity + 16, len(code))
    return bytes(data)


def write_abas_function(path: Path, data: bytes) -> None:
    lines: list[str] = []
    chunk_size = 48
    chunk_count = (len(data) + chunk_size - 1) // chunk_size
    for chunk in range(chunk_count):
        start = chunk * chunk_size
        end = min(len(data), start + chunk_size)
        lines.append(f"FUNCTION PopulatePreloadedAexChunk{chunk}() AS U64")
        lines.append("    LET base AS MMIOPTR = PreloadedAexAddress()")
        for offset in range(start, end):
            lines.append(f"    MEMORY.Write8(ADDRESS.Offset(base, {offset}), {data[offset]})")
        lines.append("    RETURN 0")
        lines.append("END FUNCTION")
        lines.append("")

    lines.append("FUNCTION PopulatePreloadedAexForHardware() AS U64")
    for chunk in range(chunk_count):
        lines.append(f"    PopulatePreloadedAexChunk{chunk}()")
    lines.append("    RETURN 0")
    lines.append("END FUNCTION")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--format", choices=("raw", "abas"), default="raw")
    args = parser.parse_args()

    data = build_aex()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.format == "raw":
        args.output.write_bytes(data)
    else:
        write_abas_function(args.output, data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
