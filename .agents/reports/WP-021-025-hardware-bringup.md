# WP-021 through WP-025: Hardware Semantics, Test Program, and Boot Artifact

**Date:** 2026-08-04
**Baseline:** `b52e6b816d7d061fc37c37ddd3a274a10652e58d`

## Result

Implemented the Packet 002 software and virtual-firmware scope:

- `CPU.Halt` and terminal `CPU.HaltForever` are parser semantics with dedicated canonical AST and
  A-MIR kinds.
- The x86-64 backend owns instruction selection: `F4` and `FA F4 EB FD`.
- Hosted bytecode reports deterministic unsupported diagnostics.
- Added the narrow UEFI `BootServices.SetWatchdogTimer` binding required to prevent a firmware
  watchdog reset during the intentional halt. Verified offsets are `0x60` and `0x100`; the service
  takes four explicit Microsoft x64 ABI arguments and no protocol-style implicit `This`.
- `tests/systems/arcology-hardware-test/hardware-test.abas` disables the watchdog, writes the exact
  Packet 002 banner once, then executes `CPU.HaltForever`.
- `scripts/build-arcology-hardware-artifact.sh` produces a deterministic 64 MiB FAT32 image with
  `EFI/BOOT/BOOTX64.EFI` plus checksums.

## Determinism Definition

Given byte-identical `BOOTX64.EFI` input, output image bytes are identical. FAT32 geometry, volume
ID, allocation order, timestamps, directory entries, and unused bytes are fixed. The regression
test builds two independent EFI/image/checksum sets and compares each pair byte-for-byte.

## Validation

- Machine encoder unit tests assert exact instruction bytes, including the new `disp32` indirect
  call needed for Boot Services offset `0x100`.
- The generated EFI application is 12,288 bytes.
- The generated raw FAT32 image is 67,108,864 bytes.
- EFI SHA-256: `ea9f2bd5bffd26bcfe57b825c8c82b9d5aca9bb01eef0c7a0322463db643542b`.
- Raw-image SHA-256: `a64394c421f25ea545c0cefc06ee55b99acb0ccc7f92faecf2e0ad48502e447d`.
- `mtools` independently reads `EFI/BOOT/BOOTX64.EFI` back byte-identically when available.
- QEMU 10.0.8 + `/usr/share/ovmf/OVMF.fd` boots the exact raw image, displays all expected text, and
  remains in the intentional halt until the harness timeout.
- Full repository suite: 15/15 tests passed (`ctest --test-dir build --output-on-failure`, 65.44 s).

## Scope Discipline

No `CPU.Pause`, inline assembly, `ExitBootServices`, control-flow expansion, OS runtime, filesystem,
or additional Boot Services were added. Physical hardware success is not claimed here.
