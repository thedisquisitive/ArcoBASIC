#!/usr/bin/env python3
"""Build a byte-reproducible ArcFS Phase B test image (RFC-0039 Section 78, Phase B: "read-only
ArcFS image... superblock parser; checkpoint parser; object/namespace trees; extent reads;
checksums").

This is Phase B's OWN minimal, documented subset of RFC-0039's real on-disk format (Sections
11-17) -- not the format a future Phase C formatter will actually write. In particular:

- One superblock, not the redundant ring Section 14 wants (Phase F's "recovery" concern).
- One committed checkpoint, not the generation history a real copy-on-write writer maintains.
- Object/Namespace "trees" are flat arrays, one fixed-size record per 512-byte sector (not a real
  B+tree -- Phase B's job is proving the read/parse contract, not real large-volume performance).
- Checksums are a simple additive sum-of-bytes mod 2^32, not a cryptographic-strength algorithm
  (RFC-0039 Section 20 requires *a* checksum exists and is checked; it does not mandate which).
- File data lives in one contiguous 8-sector (4096-byte) extent per file, matching Phase A's own
  fixed-file-capacity scope reduction -- no fragmentation/chain-walking needed yet.

See .agents/reports/aps-arcfs-phase-b.md for the full rationale and `stdlib/arcfs_policy.abas`'s
ArcFS.MountImage for the reader this image is built to be read by.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


SECTOR_SIZE = 512
TOTAL_SECTORS = 64  # 32 KiB

SUPERBLOCK_SECTOR = 0
CHECKPOINT_SECTOR = 1
OBJECT_ROOT_SECTOR = 2

MAGIC = b"ARCFSB01"
FORMAT_MAJOR = 1
FORMAT_MINOR = 0
VOLUME_UUID_HIGH = 0xA2C00001  # placeholder 64-bit halves, not a real UUID -- Phase B doesn't
VOLUME_UUID_LOW = 0x00000002   # need real UUID generation, just a fixed, deterministic value

TYPE_FILE = 1
TYPE_DIRECTORY = 2

# Test tree: root (OID 1, implicit, no namespace entry of its own -- matches Phase A) ->
# :home (OID 2) -> :home:documents (OID 3) -> :home:documents:notes.txt (OID 4, 97 bytes,
# the exact same repeating-pattern content and checksum (6142) Phase A's own proof fixture uses).
OBJECTS = [
    # (oid, type, size, has_data)
    (1, TYPE_DIRECTORY, 0, False),  # root
    (2, TYPE_DIRECTORY, 0, False),  # home
    (3, TYPE_DIRECTORY, 0, False),  # documents
    (4, TYPE_FILE, 97, True),       # notes.txt
]

NAMESPACE_ENTRIES = [
    # (parentOID, name, childOID)
    (1, b"home", 2),
    (2, b"documents", 3),
    (3, b"notes.txt", 4),
]

FILE_CONTENT = (b"ARCFS PHASE A DATA. " * 5)[:97]
FILE_BYTE_SUM = sum(FILE_CONTENT)


def checksum32(data: bytes) -> int:
    return sum(data) & 0xFFFFFFFF


def build_superblock(checkpoint_sector: int) -> bytes:
    block = bytearray(SECTOR_SIZE)
    block[0:8] = MAGIC
    struct.pack_into("<I", block, 8, FORMAT_MAJOR)
    struct.pack_into("<I", block, 12, FORMAT_MINOR)
    struct.pack_into("<Q", block, 16, VOLUME_UUID_HIGH)
    struct.pack_into("<Q", block, 24, VOLUME_UUID_LOW)
    struct.pack_into("<I", block, 32, SECTOR_SIZE)
    struct.pack_into("<Q", block, 40, TOTAL_SECTORS)
    struct.pack_into("<Q", block, 48, 0)  # featureFlags: none defined yet
    struct.pack_into("<Q", block, 56, checkpoint_sector)
    struct.pack_into("<I", block, 64, checksum32(bytes(block[0:64])))
    return bytes(block)


def build_checkpoint(object_root: int, object_count: int, namespace_root: int,
                      namespace_count: int, data_region_start: int) -> bytes:
    block = bytearray(SECTOR_SIZE)
    struct.pack_into("<Q", block, 0, 1)  # generation
    struct.pack_into("<Q", block, 8, object_root)
    struct.pack_into("<Q", block, 16, object_count)
    struct.pack_into("<Q", block, 24, namespace_root)
    struct.pack_into("<Q", block, 32, namespace_count)
    struct.pack_into("<Q", block, 40, data_region_start)
    struct.pack_into("<I", block, 48, checksum32(bytes(block[0:48])))
    return bytes(block)


def build_object_record(oid: int, obj_type: int, size: int, extent_sector: int) -> bytes:
    block = bytearray(SECTOR_SIZE)
    struct.pack_into("<Q", block, 0, oid)
    struct.pack_into("<Q", block, 8, obj_type)
    struct.pack_into("<Q", block, 16, size)
    struct.pack_into("<Q", block, 24, extent_sector)
    struct.pack_into("<I", block, 32, checksum32(bytes(block[0:32])))
    return bytes(block)


def build_namespace_record(parent_oid: int, name: bytes, child_oid: int) -> bytes:
    if len(name) > 32:
        raise ValueError(f"name too long for a 32-byte field: {name!r}")
    block = bytearray(SECTOR_SIZE)
    struct.pack_into("<Q", block, 0, parent_oid)
    block[8:8 + len(name)] = name
    struct.pack_into("<Q", block, 40, len(name))
    struct.pack_into("<Q", block, 48, child_oid)
    struct.pack_into("<I", block, 56, checksum32(bytes(block[0:56])))
    return bytes(block)


def build_image() -> bytes:
    object_count = len(OBJECTS)
    namespace_root = OBJECT_ROOT_SECTOR + object_count
    namespace_count = len(NAMESPACE_ENTRIES)
    data_region_start = namespace_root + namespace_count

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)

    def write_sector(sector: int, data: bytes) -> None:
        offset = sector * SECTOR_SIZE
        image[offset:offset + SECTOR_SIZE] = data

    write_sector(SUPERBLOCK_SECTOR, build_superblock(CHECKPOINT_SECTOR))
    write_sector(CHECKPOINT_SECTOR, build_checkpoint(
        OBJECT_ROOT_SECTOR, object_count, namespace_root, namespace_count, data_region_start))

    for index, (oid, obj_type, size, has_data) in enumerate(OBJECTS):
        extent_sector = data_region_start + index * 8
        write_sector(OBJECT_ROOT_SECTOR + index, build_object_record(oid, obj_type, size, extent_sector))
        if has_data:
            padded = FILE_CONTENT + b"\x00" * (4096 - len(FILE_CONTENT))
            for chunk_index in range(8):
                chunk = padded[chunk_index * SECTOR_SIZE:(chunk_index + 1) * SECTOR_SIZE]
                write_sector(extent_sector + chunk_index, chunk)

    for index, (parent_oid, name, child_oid) in enumerate(NAMESPACE_ENTRIES):
        write_sector(namespace_root + index, build_namespace_record(parent_oid, name, child_oid))

    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="output ArcFS Phase B test image")
    args = parser.parse_args()

    image = build_image()
    args.image.parent.mkdir(parents=True, exist_ok=True)
    args.image.write_bytes(image)
    print(f"wrote {len(image)} bytes to {args.image}")
    print(f"objects={len(OBJECTS)} namespace_entries={len(NAMESPACE_ENTRIES)} "
          f"file_content_len={len(FILE_CONTENT)} file_byte_sum={FILE_BYTE_SUM}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
