#!/usr/bin/env python3
"""Build a byte-reproducible, minimal FAT32 test image for RFC-0038's read-only FAT32 provider.

Deliberately not `build-arcology-hardware-image.py`: that script's fixed geometry places its one
file three directories deep (root -> EFI -> BOOT -> BOOTX64.EFI), exercising subdirectory
traversal RFC-0038's reference implementation explicitly does not attempt (root-directory-only is
this milestone's documented scope reduction -- see RFC-0038 Section 19's Open Question on
long-filename support, and `.agents/reports/aps-block-storage.md` for the analogous root-only
scope note). This script instead puts one known file directly in the root directory, with content
long enough (multiple 512-byte clusters) to genuinely exercise FAT chain-walking, not just a
single-cluster read.

The 8 MiB image size is smaller than the ~33 MiB Microsoft's own FAT32 specification recommends as
a soft minimum cluster count (65,525) to keep a reader from ambiguously guessing FAT16 vs FAT32 by
cluster count alone -- RFC-0038's own Mount requirement does not rely on that heuristic at all (it
validates the "FAT32   " filesystem-type label string directly, Requirement 6.3/6.4), so an
undersized-but-structurally-valid image is a legitimate, deliberately fast-to-preload test target,
not a compatibility gap in the reader being tested.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


SECTOR_SIZE = 512
TOTAL_SECTORS = 16_384  # 8 MiB
RESERVED_SECTORS = 32
FAT_COUNT = 2
SECTORS_PER_CLUSTER = 1
ROOT_CLUSTER = 2
FILE_FIRST_CLUSTER = 3
VOLUME_ID = 0xA2C0_0300
FAT_DATE = ((2026 - 1980) << 9) | (1 << 5) | 1
FAT_TIME = 0

# Deterministic file content: known length, known repeating pattern, known byte-sum -- the
# ArcoBASIC test fixture sums every byte it reads back and compares against FILE_BYTE_SUM rather
# than printing/matching the content itself, the same "assert internally, print one fixed marker"
# discipline every proof in this RFC chain since RFC-0036 has used.
FILE_NAME = b"README  TXT"  # 8.3 short name, space-padded, no dot
FILE_CONTENT = (b"APS FAT32 READ PROOF. " * 200)[:3333]
FILE_BYTE_SUM = sum(FILE_CONTENT)


def sectors_per_fat() -> int:
    value = 1
    while True:
        data_sectors = TOTAL_SECTORS - RESERVED_SECTORS - FAT_COUNT * value
        clusters = data_sectors // SECTORS_PER_CLUSTER
        required = (4 * (clusters + 2) + SECTOR_SIZE - 1) // SECTOR_SIZE
        if required <= value:
            return value
        value = required


def directory_entry(name: bytes, attributes: int, cluster: int, size: int = 0) -> bytes:
    if len(name) != 11:
        raise ValueError(f"FAT short name must be exactly 11 bytes: {name!r}")
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attributes
    struct.pack_into("<H", entry, 14, FAT_TIME)
    struct.pack_into("<H", entry, 16, FAT_DATE)
    struct.pack_into("<H", entry, 18, FAT_DATE)
    struct.pack_into("<H", entry, 20, (cluster >> 16) & 0xFFFF)
    struct.pack_into("<H", entry, 22, FAT_TIME)
    struct.pack_into("<H", entry, 24, FAT_DATE)
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def write_cluster(image: bytearray, data_start_sector: int, cluster: int, data: bytes) -> None:
    if len(data) > SECTOR_SIZE * SECTORS_PER_CLUSTER:
        raise ValueError("cluster payload is too large")
    offset = (data_start_sector + (cluster - 2) * SECTORS_PER_CLUSTER) * SECTOR_SIZE
    image[offset : offset + len(data)] = data


def build_image() -> bytes:
    fat_sectors = sectors_per_fat()
    data_start = RESERVED_SECTORS + FAT_COUNT * fat_sectors
    data_sectors = TOTAL_SECTORS - data_start
    cluster_count = data_sectors // SECTORS_PER_CLUSTER

    file_clusters = (len(FILE_CONTENT) + SECTOR_SIZE - 1) // SECTOR_SIZE
    file_last_cluster = FILE_FIRST_CLUSTER + file_clusters - 1
    if file_last_cluster >= cluster_count + 2:
        raise ValueError("test file content does not fit in the fixed 8 MiB image")

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    boot = bytearray(SECTOR_SIZE)
    boot[0:3] = b"\xEB\x58\x90"
    boot[3:11] = b"ARCOTEST"
    struct.pack_into("<H", boot, 11, SECTOR_SIZE)
    boot[13] = SECTORS_PER_CLUSTER
    struct.pack_into("<H", boot, 14, RESERVED_SECTORS)
    boot[16] = FAT_COUNT
    struct.pack_into("<H", boot, 17, 0)
    struct.pack_into("<H", boot, 19, 0)
    boot[21] = 0xF8
    struct.pack_into("<H", boot, 22, 0)
    struct.pack_into("<H", boot, 24, 63)
    struct.pack_into("<H", boot, 26, 255)
    struct.pack_into("<I", boot, 28, 0)
    struct.pack_into("<I", boot, 32, TOTAL_SECTORS)
    struct.pack_into("<I", boot, 36, fat_sectors)
    struct.pack_into("<H", boot, 40, 0)
    struct.pack_into("<H", boot, 42, 0)
    struct.pack_into("<I", boot, 44, ROOT_CLUSTER)
    struct.pack_into("<H", boot, 48, 1)
    struct.pack_into("<H", boot, 50, 6)
    boot[64] = 0x80
    boot[66] = 0x29
    struct.pack_into("<I", boot, 67, VOLUME_ID)
    boot[71:82] = b"APSTESTFAT "
    boot[82:90] = b"FAT32   "
    boot[510:512] = b"\x55\xAA"
    image[0:SECTOR_SIZE] = boot
    image[6 * SECTOR_SIZE : 7 * SECTOR_SIZE] = boot

    allocated_clusters = 1 + file_clusters
    fsinfo = bytearray(SECTOR_SIZE)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<I", fsinfo, 484, 0x61417272)
    struct.pack_into("<I", fsinfo, 488, cluster_count - allocated_clusters)
    struct.pack_into("<I", fsinfo, 492, file_last_cluster + 1)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    image[SECTOR_SIZE : 2 * SECTOR_SIZE] = fsinfo
    image[7 * SECTOR_SIZE : 8 * SECTOR_SIZE] = fsinfo

    fat = bytearray(fat_sectors * SECTOR_SIZE)
    struct.pack_into("<I", fat, 0, 0x0FFFFFF8)
    struct.pack_into("<I", fat, 4, 0x0FFFFFFF)
    struct.pack_into("<I", fat, ROOT_CLUSTER * 4, 0x0FFFFFFF)
    for cluster in range(FILE_FIRST_CLUSTER, file_last_cluster):
        struct.pack_into("<I", fat, cluster * 4, cluster + 1)
    struct.pack_into("<I", fat, file_last_cluster * 4, 0x0FFFFFFF)
    for index in range(FAT_COUNT):
        offset = (RESERVED_SECTORS + index * fat_sectors) * SECTOR_SIZE
        image[offset : offset + len(fat)] = fat

    root = directory_entry(FILE_NAME, 0x20, FILE_FIRST_CLUSTER, len(FILE_CONTENT))
    write_cluster(image, data_start, ROOT_CLUSTER, root)
    for index in range(file_clusters):
        begin = index * SECTOR_SIZE
        write_cluster(image, data_start, FILE_FIRST_CLUSTER + index, FILE_CONTENT[begin : begin + SECTOR_SIZE])

    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="output 8 MiB FAT32 test image")
    args = parser.parse_args()

    image = build_image()
    args.image.parent.mkdir(parents=True, exist_ok=True)
    args.image.write_bytes(image)
    print(f"wrote {len(image)} bytes to {args.image}")
    print(f"test file: name={FILE_NAME!r} size={len(FILE_CONTENT)} byte_sum={FILE_BYTE_SUM} "
          f"first_cluster={FILE_FIRST_CLUSTER}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
