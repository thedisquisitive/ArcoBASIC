#!/usr/bin/env python3
"""Build a byte-reproducible FAT32 UEFI removable-media image.

The filesystem is intentionally minimal: /EFI/BOOT/BOOTX64.EFI is the only file. All geometry,
identifiers, allocation order, directory timestamps, and unused bytes are fixed.

The actual FAT32-building logic lives in fat32_esp_image.py (RFC-0044 Section 5, point 3: shared
with build-arcology-gpt-image.py's own ESP partition, rather than duplicated) -- this script is a
thin wrapper that calls it with this tool's own original fixed 64 MiB geometry, so its own output
stays byte-identical to every checksum already recorded against it (WP-026, WP-030, and any other
artifact built before this refactor).
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# See build-arcology-gpt-image.py's own identical guard for why: a stale __pycache__/*.pyc for the
# shared fat32_esp_image module was observed, during development, to make an otherwise fully
# deterministic build briefly produce the wrong checksum.
sys.dont_write_bytecode = True

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fat32_esp_image import DEFAULT_TOTAL_SECTORS, build_fat32_image  # noqa: E402


def build_image(efi: bytes) -> bytes:
    return build_fat32_image(efi, DEFAULT_TOTAL_SECTORS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("efi", type=Path, help="input x86-64 UEFI PE32+ application")
    parser.add_argument("image", type=Path, help="output 64 MiB FAT32 image")
    args = parser.parse_args()

    try:
        payload = args.efi.read_bytes()
        image = build_image(payload)
        args.image.parent.mkdir(parents=True, exist_ok=True)
        args.image.write_bytes(image)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
