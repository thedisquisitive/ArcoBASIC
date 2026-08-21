#!/usr/bin/env python3
"""Build a byte-reproducible raw disk image for proving real UEFI Block I/O Protocol discovery
and I/O (RFC-0038 Section 17.5's own stop condition, closed at the compiler-binding level in
`.agents/reports/aps-blockio-discovery-binding.md`; this is the follow-on hardware-side proof).

This is a genuinely SEPARATE QEMU block device from the boot media (the FAT-backed directory OVMF
boots BOOTX64.EFI from). Both end up exposed as real `EFI_BLOCK_IO_PROTOCOL` handles by OVMF, and
`UEFI.BLOCKIO.DISCOVER` (a single `LocateProtocol` call, matching `UEFI.GOP.DISCOVER`'s own
documented single-handle scope) returns whichever one firmware hands back first -- not something
this binding controls. The fixture that reads this image is written to handle either outcome
honestly rather than assume this disk is the one discovered.

- Sector 0-4: left zeroed (reserved, deliberately not touched -- keeps this image's own "known
  content" sectors away from anything a boot-sector-shaped scan might key on).
- Sector 5: a fixed, recognizable magic pattern (`MAGIC`), used as the read-side proof that a
  real ReadBlocks call returned genuine bytes from this specific disk.
- Sector 10: left zeroed at image-build time -- the fixture writes a second, different pattern
  here at runtime and reads it back, proving WriteBlocks for real. Safely far from sectors 0-5 so
  a write here can never plausibly corrupt anything firmware itself depends on.
"""

from __future__ import annotations

import argparse
from pathlib import Path

SECTOR_SIZE = 512
TOTAL_SECTORS = 128  # 64 KiB -- small, deterministic, fast to build/attach

MAGIC_SECTOR = 5
MAGIC = b"ARCOLOGY BLOCKIO DISK PROOF SECTOR 5 - REAL HARDWARE READ\x00"
assert len(MAGIC) <= SECTOR_SIZE


def build_image() -> bytes:
    image = bytearray(SECTOR_SIZE * TOTAL_SECTORS)

    def write_sector(sector: int, data: bytes) -> None:
        offset = sector * SECTOR_SIZE
        image[offset:offset + len(data)] = data

    padded_magic = MAGIC + b"\x00" * (SECTOR_SIZE - len(MAGIC))
    write_sector(MAGIC_SECTOR, padded_magic)

    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="output raw disk image")
    args = parser.parse_args()

    image = build_image()
    args.image.parent.mkdir(parents=True, exist_ok=True)
    args.image.write_bytes(image)
    print(f"wrote {len(image)} bytes ({TOTAL_SECTORS} sectors) to {args.image}")
    print(f"magic sector={MAGIC_SECTOR} magic={MAGIC!r}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
