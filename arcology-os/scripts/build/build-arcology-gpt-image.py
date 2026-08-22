#!/usr/bin/env python3
"""Build a byte-reproducible, real GPT-partitioned disk image (RFC-0044 Section 5).

Two real partitions on one disk, exactly the shape a real installed OS uses:

  1. A protective MBR (LBA 0) so non-GPT-aware tools see one large, already-allocated partition
     rather than an apparently-blank disk (required by the GPT specification).
  2. A real primary GPT header (LBA 1) and partition entry array (LBA 2-33), plus a real backup
     header and array at the end of the disk -- both independently verifiable with a real,
     separate tool (`parted`/`sfdisk`), not just re-parsed by this same script's own logic.
  3. Partition 1, the EFI System Partition: FAT32, type GUID C12A7328-F81F-11D2-BA4B-00A0C93EC93B
     (the real, standard UEFI ESP type), containing /EFI/BOOT/BOOTX64.EFI. Built by
     fat32_esp_image.build_fat32_image -- the SAME logic build-arcology-hardware-image.py's own
     artifact already uses, refactored into a shared function rather than duplicated (RFC-0044
     Section 5, point 3).
  4. Partition 2, reserved space for ArcFS: left entirely zeroed. ArcFS formats it itself via
     ArcFS.FormatVolume() at first boot -- matching real installer convention of leaving the
     target partition raw until the OS's own installer/first-boot formats it (RFC-0044 Section 5,
     point 4). Type GUID EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, the standard Microsoft "Basic Data
     Partition" type -- reused here since ArcFS has no officially registered type GUID of its own;
     it is recognizable by any generic partitioning tool as "not the ESP, a distinct data
     partition" without requiring a new proprietary registration.

All GUIDs (disk and both partitions) are FIXED, not randomly generated (uuid4) -- this tool must
stay byte-reproducible like every other image builder in this project. Selection between the two
partitions at runtime is by discovery-order INDEX (RFC-0044 Non-Goal 1), not by inspecting these
GUIDs -- they exist for real-tool compatibility/recognizability, not because this project's own
fixture parses them.
"""

from __future__ import annotations

import argparse
import struct
import sys
import uuid
import zlib
from pathlib import Path

# Never write a __pycache__/*.pyc for this or the shared fat32_esp_image module: a stale cached
# .pyc from before a source edit was observed, during development, to make an otherwise fully
# deterministic build produce a DIFFERENT checksum than a fresh run of the same source -- not a
# real bug in the build logic itself (5 consecutive fresh-cache builds were byte-identical), but
# byte-reproducibility is this tool's entire point, so never risk hitting that class of false
# non-determinism again for a tool this determinism-sensitive.
sys.dont_write_bytecode = True

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fat32_esp_image import SECTOR_SIZE, build_fat32_image  # noqa: E402

# Fixed, deterministic identifiers (RFC-0044: this tool has no external dependencies beyond
# python3 and must produce byte-identical output on every run, matching every other image builder
# in this project -- a real uuid4() would break that).
DISK_GUID = "4152434F-4C4F-4759-0000-000000000001"
ESP_PARTITION_GUID = "4152434F-4C4F-4759-0000-000000000002"
ARCFS_PARTITION_GUID = "4152434F-4C4F-4759-0000-000000000003"

ESP_TYPE_GUID = "C12A7328-F81F-11D2-BA4B-00A0C93EC93B"  # standard UEFI ESP type (EDK2/UEFI spec)
ARCFS_TYPE_GUID = "EBD0A0A2-B9E5-4433-87C0-68B6B72699C7"  # standard Microsoft Basic Data type

ESP_PARTITION_NAME = "EFI System Partition"
ARCFS_PARTITION_NAME = "ARCOLOGY ARCFS SYSTEM VOLUME"

GPT_ARRAY_ENTRIES = 128
GPT_ENTRY_SIZE = 128
GPT_ARRAY_SECTORS = (GPT_ARRAY_ENTRIES * GPT_ENTRY_SIZE) // SECTOR_SIZE  # 32

ESP_START_LBA = 2048  # 1 MiB alignment, standard real-disk-partitioning convention
ESP_SECTORS = 131_072  # 64 MiB -- build-arcology-hardware-image.py's own original, proven geometry
ARCFS_START_LBA = ESP_START_LBA + ESP_SECTORS  # already 1 MiB aligned (both are multiples of 2048)
ARCFS_SECTORS = 16_384  # 8 MiB reserved, zeroed -- matches the size RFC-0043 Phase V's own real
# ArcFS.FormatVolume()/ActivateSystemVolume() proof already used (systems_arco_basic_arcfs_
# sysvol_persist_smoke.sh's own disk.img), just doubled for headroom since this fixture also
# renders, not just formats+mounts.

# Backup GPT array + header occupy the last GPT_ARRAY_SECTORS + 1 sectors of the disk.
TOTAL_SECTORS = ARCFS_START_LBA + ARCFS_SECTORS + GPT_ARRAY_SECTORS + 1
LAST_LBA = TOTAL_SECTORS - 1
BACKUP_HEADER_LBA = LAST_LBA
BACKUP_ARRAY_LBA = BACKUP_HEADER_LBA - GPT_ARRAY_SECTORS
FIRST_USABLE_LBA = 2 + GPT_ARRAY_SECTORS
LAST_USABLE_LBA = BACKUP_ARRAY_LBA - 1


def guid_bytes_le(hex_string: str) -> bytes:
    return uuid.UUID(hex_string).bytes_le


def partition_entry(type_guid: str, part_guid: str, first_lba: int, last_lba: int, name: str) -> bytes:
    entry = bytearray(GPT_ENTRY_SIZE)
    entry[0:16] = guid_bytes_le(type_guid)
    entry[16:32] = guid_bytes_le(part_guid)
    struct.pack_into("<Q", entry, 32, first_lba)
    struct.pack_into("<Q", entry, 40, last_lba)
    struct.pack_into("<Q", entry, 48, 0)  # attributes
    name_bytes = name.encode("utf-16-le")
    if len(name_bytes) > 72:
        raise ValueError(f"partition name too long for a 72-byte UTF-16LE field: {name!r}")
    entry[56 : 56 + len(name_bytes)] = name_bytes
    return bytes(entry)


def partition_array() -> bytes:
    entries = [
        partition_entry(ESP_TYPE_GUID, ESP_PARTITION_GUID, ESP_START_LBA,
                         ESP_START_LBA + ESP_SECTORS - 1, ESP_PARTITION_NAME),
        partition_entry(ARCFS_TYPE_GUID, ARCFS_PARTITION_GUID, ARCFS_START_LBA,
                         ARCFS_START_LBA + ARCFS_SECTORS - 1, ARCFS_PARTITION_NAME),
    ]
    array = b"".join(entries) + bytes((GPT_ARRAY_ENTRIES - len(entries)) * GPT_ENTRY_SIZE)
    assert len(array) == GPT_ARRAY_SECTORS * SECTOR_SIZE
    return array


def gpt_header(my_lba: int, alternate_lba: int, partition_entry_lba: int, array_crc32: int) -> bytes:
    header = bytearray(SECTOR_SIZE)
    header[0:8] = b"EFI PART"
    struct.pack_into("<I", header, 8, 0x00010000)  # revision 1.0
    struct.pack_into("<I", header, 12, 92)  # header size
    struct.pack_into("<I", header, 16, 0)  # CRC32 placeholder, filled in below
    struct.pack_into("<I", header, 20, 0)  # reserved
    struct.pack_into("<Q", header, 24, my_lba)
    struct.pack_into("<Q", header, 32, alternate_lba)
    struct.pack_into("<Q", header, 40, FIRST_USABLE_LBA)
    struct.pack_into("<Q", header, 48, LAST_USABLE_LBA)
    header[56:72] = guid_bytes_le(DISK_GUID)
    struct.pack_into("<Q", header, 72, partition_entry_lba)
    struct.pack_into("<I", header, 80, GPT_ARRAY_ENTRIES)
    struct.pack_into("<I", header, 84, GPT_ENTRY_SIZE)
    struct.pack_into("<I", header, 88, array_crc32)
    header_crc = zlib.crc32(bytes(header[0:92])) & 0xFFFFFFFF
    struct.pack_into("<I", header, 16, header_crc)
    return bytes(header)


def protective_mbr(total_sectors: int) -> bytes:
    mbr = bytearray(SECTOR_SIZE)
    size_in_lba = min(total_sectors - 1, 0xFFFFFFFF)
    entry = bytearray(16)
    entry[0] = 0x00  # not bootable
    entry[1:4] = b"\x00\x02\x00"  # starting CHS, unused by GPT-aware firmware
    entry[4] = 0xEE  # GPT protective partition type
    entry[5:8] = b"\xFF\xFF\xFF"  # ending CHS, unused
    struct.pack_into("<I", entry, 8, 1)  # starting LBA
    struct.pack_into("<I", entry, 12, size_in_lba)
    mbr[446:462] = entry
    mbr[510:512] = b"\x55\xAA"
    return bytes(mbr)


def build_gpt_image(efi: bytes) -> bytes:
    esp_fs = build_fat32_image(efi, ESP_SECTORS)
    if len(esp_fs) != ESP_SECTORS * SECTOR_SIZE:
        raise AssertionError("ESP filesystem builder returned unexpected size")

    array = partition_array()
    array_crc = zlib.crc32(array) & 0xFFFFFFFF

    primary_header = gpt_header(1, BACKUP_HEADER_LBA, 2, array_crc)
    backup_header = gpt_header(BACKUP_HEADER_LBA, 1, BACKUP_ARRAY_LBA, array_crc)

    image = bytearray(TOTAL_SECTORS * SECTOR_SIZE)
    image[0 * SECTOR_SIZE : 1 * SECTOR_SIZE] = protective_mbr(TOTAL_SECTORS)
    image[1 * SECTOR_SIZE : 2 * SECTOR_SIZE] = primary_header
    image[2 * SECTOR_SIZE : (2 + GPT_ARRAY_SECTORS) * SECTOR_SIZE] = array
    image[ESP_START_LBA * SECTOR_SIZE : (ESP_START_LBA + ESP_SECTORS) * SECTOR_SIZE] = esp_fs
    # ARCFS_SECTORS region is left zeroed -- ArcFS.FormatVolume() formats it at runtime.
    image[BACKUP_ARRAY_LBA * SECTOR_SIZE : (BACKUP_ARRAY_LBA + GPT_ARRAY_SECTORS) * SECTOR_SIZE] = array
    image[BACKUP_HEADER_LBA * SECTOR_SIZE : (BACKUP_HEADER_LBA + 1) * SECTOR_SIZE] = backup_header

    return bytes(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("efi", type=Path, help="input x86-64 UEFI PE32+ application for the ESP")
    parser.add_argument("image", type=Path, help="output raw GPT-partitioned disk image")
    args = parser.parse_args()

    try:
        payload = args.efi.read_bytes()
        image = build_gpt_image(payload)
        args.image.parent.mkdir(parents=True, exist_ok=True)
        args.image.write_bytes(image)
    except (OSError, ValueError, AssertionError) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
