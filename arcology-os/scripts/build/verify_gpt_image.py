#!/usr/bin/env python3
"""Independently verify a build-arcology-gpt-image.py artifact (RFC-0044 Section 7.3 Phase 2
acceptance evidence: "the built image's own GPT header and partition entries independently
verified... before ever being booted").

This is a real PARSER, reading the on-disk structures the GPT/FAT32 specifications define, not a
re-run of the builder's own writing logic being diffed against itself -- it recomputes CRC32s and
walks the FAT32 directory tree from raw bytes exactly as real firmware/tooling would. It needs no
root privileges (no loop mount) so it can run anywhere python3 runs, including inside a smoke test.

During Phase 2 development this exact image was ALSO independently cross-checked with two real,
separate system tools this script does not depend on -- `parted -s IMAGE unit s print` and
`sfdisk -l IMAGE` both parsed the same structure this script verifies, and a real loop-mounted
read of the ESP confirmed BOOTX64.EFI's content byte-for-byte -- see the RFC-0044 Phase 2 report
for that transcript. This script exists so the same class of check can run automatically and
repeatably, not only that one time.
"""

from __future__ import annotations

import argparse
import struct
import sys
import uuid
import zlib
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

import importlib.util

_spec = importlib.util.spec_from_file_location(
    "arcology_gpt_image_builder", Path(__file__).resolve().parent / "build-arcology-gpt-image.py"
)
_builder = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_builder)

SECTOR_SIZE = _builder.SECTOR_SIZE


class VerificationError(Exception):
    pass


def guid_string(raw_le: bytes) -> str:
    return str(uuid.UUID(bytes_le=raw_le)).upper()


def verify_header(image: bytes, lba: int, expected_alt_lba: int, expected_array_lba: int) -> dict:
    offset = lba * SECTOR_SIZE
    header = image[offset : offset + SECTOR_SIZE]
    if header[0:8] != b"EFI PART":
        raise VerificationError(f"LBA {lba}: bad GPT signature {header[0:8]!r}")

    stored_crc = struct.unpack_from("<I", header, 16)[0]
    zeroed = bytearray(header[0:92])
    struct.pack_into("<I", zeroed, 16, 0)
    computed_crc = zlib.crc32(bytes(zeroed)) & 0xFFFFFFFF
    if stored_crc != computed_crc:
        raise VerificationError(f"LBA {lba}: header CRC32 mismatch (stored {stored_crc:#x}, computed {computed_crc:#x})")

    my_lba = struct.unpack_from("<Q", header, 24)[0]
    alt_lba = struct.unpack_from("<Q", header, 32)[0]
    array_lba = struct.unpack_from("<Q", header, 72)[0]
    array_crc = struct.unpack_from("<I", header, 88)[0]
    entry_count = struct.unpack_from("<I", header, 80)[0]
    entry_size = struct.unpack_from("<I", header, 84)[0]

    if my_lba != lba:
        raise VerificationError(f"LBA {lba}: MyLBA field says {my_lba}")
    if alt_lba != expected_alt_lba:
        raise VerificationError(f"LBA {lba}: AlternateLBA {alt_lba} != expected {expected_alt_lba}")
    if array_lba != expected_array_lba:
        raise VerificationError(f"LBA {lba}: PartitionEntryLBA {array_lba} != expected {expected_array_lba}")

    array_bytes = image[array_lba * SECTOR_SIZE : array_lba * SECTOR_SIZE + entry_count * entry_size]
    computed_array_crc = zlib.crc32(array_bytes) & 0xFFFFFFFF
    if array_crc != computed_array_crc:
        raise VerificationError(
            f"LBA {lba}: partition array CRC32 mismatch (stored {array_crc:#x}, computed {computed_array_crc:#x})"
        )

    return {
        "disk_guid": guid_string(header[56:72]),
        "array_lba": array_lba,
        "entry_count": entry_count,
        "entry_size": entry_size,
        "array_bytes": array_bytes,
    }


def parse_partitions(array_bytes: bytes, entry_count: int, entry_size: int) -> list[dict]:
    partitions = []
    for i in range(entry_count):
        entry = array_bytes[i * entry_size : (i + 1) * entry_size]
        type_guid = entry[0:16]
        if type_guid == b"\x00" * 16:
            continue
        part_guid = entry[16:32]
        first_lba, last_lba = struct.unpack_from("<QQ", entry, 32)
        name_raw = entry[56:128]
        name = name_raw.decode("utf-16-le").split("\x00", 1)[0]
        partitions.append(
            {
                "index": i,
                "type_guid": guid_string(type_guid),
                "part_guid": guid_string(part_guid),
                "first_lba": first_lba,
                "last_lba": last_lba,
                "name": name,
            }
        )
    return partitions


def read_fat32_file(image: bytes, partition_start_lba: int, path_components: list[str]) -> bytes:
    """Walk a FAT32 filesystem embedded at partition_start_lba and return one file's bytes, using
    only the boot sector's own declared geometry -- no assumptions duplicated from the builder."""
    part_offset = partition_start_lba * SECTOR_SIZE
    boot = image[part_offset : part_offset + SECTOR_SIZE]
    if boot[510:512] != b"\x55\xAA":
        raise VerificationError("ESP: missing FAT32 boot signature")

    bytes_per_sector = struct.unpack_from("<H", boot, 11)[0]
    sectors_per_cluster = boot[13]
    reserved_sectors = struct.unpack_from("<H", boot, 14)[0]
    fat_count = boot[16]
    sectors_per_fat = struct.unpack_from("<I", boot, 36)[0]
    root_cluster = struct.unpack_from("<I", boot, 44)[0]
    if bytes_per_sector != SECTOR_SIZE:
        raise VerificationError(f"ESP: unexpected bytes-per-sector {bytes_per_sector}")

    data_start_sector = reserved_sectors + fat_count * sectors_per_fat
    fat_offset = part_offset + reserved_sectors * SECTOR_SIZE

    def cluster_offset(cluster: int) -> int:
        return part_offset + (data_start_sector + (cluster - 2) * sectors_per_cluster) * SECTOR_SIZE

    def fat_entry(cluster: int) -> int:
        raw = image[fat_offset + cluster * 4 : fat_offset + cluster * 4 + 4]
        return struct.unpack("<I", raw)[0] & 0x0FFFFFFF

    def read_dir(cluster: int) -> list[bytes]:
        entries = []
        current = cluster
        while current < 0x0FFFFFF8:
            start = cluster_offset(current)
            chunk = image[start : start + sectors_per_cluster * SECTOR_SIZE]
            for i in range(0, len(chunk), 32):
                entry = chunk[i : i + 32]
                if entry[0] == 0x00:
                    return entries
                if entry[0] != 0xE5:
                    entries.append(entry)
            current = fat_entry(current)
        return entries

    def find_entry(entries: list[bytes], short_name: bytes) -> bytes:
        for entry in entries:
            if entry[0:11] == short_name:
                return entry
        raise VerificationError(f"ESP: directory entry {short_name!r} not found")

    cluster = root_cluster
    entry = None
    for i, component in enumerate(path_components):
        entries = read_dir(cluster)
        entry = find_entry(entries, component)
        cluster_high = struct.unpack_from("<H", entry, 20)[0]
        cluster_low = struct.unpack_from("<H", entry, 26)[0]
        cluster = (cluster_high << 16) | cluster_low

    size = struct.unpack_from("<I", entry, 28)[0]
    data = bytearray()
    current = cluster
    while len(data) < size and current < 0x0FFFFFF8:
        start = cluster_offset(current)
        data.extend(image[start : start + sectors_per_cluster * SECTOR_SIZE])
        current = fat_entry(current)
    return bytes(data[:size])


def verify(image_path: Path) -> None:
    image = image_path.read_bytes()
    total_sectors = len(image) // SECTOR_SIZE

    mbr = image[0:SECTOR_SIZE]
    if mbr[510:512] != b"\x55\xAA":
        raise VerificationError("protective MBR: missing boot signature")
    if mbr[450] != 0xEE:
        raise VerificationError(f"protective MBR: partition type {mbr[450]:#x} != 0xEE (GPT protective)")
    print("OK: protective MBR present, type 0xEE")

    primary = verify_header(image, 1, total_sectors - 1, 2)
    print(f"OK: primary GPT header valid (disk GUID {primary['disk_guid']})")

    backup_array_lba = total_sectors - 1 - _builder.GPT_ARRAY_SECTORS
    backup = verify_header(image, total_sectors - 1, 1, backup_array_lba)
    print(f"OK: backup GPT header valid (disk GUID {backup['disk_guid']})")

    if primary["disk_guid"] != backup["disk_guid"]:
        raise VerificationError("primary/backup disk GUID mismatch")
    if primary["array_bytes"] != backup["array_bytes"]:
        raise VerificationError("primary/backup partition array content mismatch")
    print("OK: primary and backup GPT structures agree")

    partitions = parse_partitions(primary["array_bytes"], primary["entry_count"], primary["entry_size"])
    if len(partitions) != 2:
        raise VerificationError(f"expected exactly 2 partitions, found {len(partitions)}")

    esp, arcfs = partitions[0], partitions[1]
    if esp["type_guid"] != _builder.ESP_TYPE_GUID.upper():
        raise VerificationError(f"partition 0 type GUID {esp['type_guid']} != expected ESP type")
    if arcfs["type_guid"] != _builder.ARCFS_TYPE_GUID.upper():
        raise VerificationError(f"partition 1 type GUID {arcfs['type_guid']} != expected ArcFS/basic-data type")
    if esp["first_lba"] != _builder.ESP_START_LBA:
        raise VerificationError(f"ESP starting LBA {esp['first_lba']} != expected {_builder.ESP_START_LBA}")
    if arcfs["first_lba"] != _builder.ARCFS_START_LBA:
        raise VerificationError(f"ArcFS starting LBA {arcfs['first_lba']} != expected {_builder.ARCFS_START_LBA}")
    print(f"OK: partition 0 (ESP) LBA {esp['first_lba']}-{esp['last_lba']}, type {esp['type_guid']}, name {esp['name']!r}")
    print(f"OK: partition 1 (ArcFS) LBA {arcfs['first_lba']}-{arcfs['last_lba']}, type {arcfs['type_guid']}, name {arcfs['name']!r}")

    efi_bytes = read_fat32_file(image, esp["first_lba"], [b"EFI        ", b"BOOT       ", b"BOOTX64 EFI"])
    print(f"OK: parsed ESP's own FAT32 directory tree, EFI/BOOT/BOOTX64.EFI is {len(efi_bytes)} bytes")

    import hashlib

    print(f"OK: EFI/BOOT/BOOTX64.EFI SHA-256 = {hashlib.sha256(efi_bytes).hexdigest()}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    try:
        verify(args.image)
    except VerificationError as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print("PASS: all structural checks succeeded")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
