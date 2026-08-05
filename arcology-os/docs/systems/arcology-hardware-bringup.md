# Arcology Seed 0.1 Hardware Bring-Up

This workflow builds Packet 002's deterministic, USB-ready x86-64 UEFI image. QEMU/OVMF validation
is automated; physical-hardware results must be recorded by a human and must never be inferred from
the virtual-machine result.

## Build

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
arcology-os/scripts/build/build-arcology-hardware-artifact.sh
```

Outputs:

```text
arcology-os/dist/arcology-seed-0.1/BOOTX64.EFI
arcology-os/dist/arcology-seed-0.1/arcology-seed-0.1-x86_64.img
arcology-os/dist/arcology-seed-0.1/SHA256SUMS
```

The image is a 64 MiB FAT32 superfloppy with the application at the UEFI removable-media path
`EFI/BOOT/BOOTX64.EFI`. Its volume ID, geometry, allocation order, timestamps, and zero-filled
space are fixed. Identical EFI input therefore produces byte-identical media. Python 3 is the only
image-builder dependency; `mtools` is used opportunistically by tests as an independent reader.

Verify and run the exact raw artifact in QEMU/OVMF:

```sh
(cd arcology-os/dist/arcology-seed-0.1 && sha256sum -c SHA256SUMS)
arcology-os/scripts/run/run-arcology-hardware-image.sh \
  arcology-os/dist/arcology-seed-0.1/arcology-seed-0.1-x86_64.img
```

Expected display:

```text
ARCOLOGY HARDWARE TEST

Hello from ArcoBASIC

System halted intentionally.
```

The application first calls `BootServices.SetWatchdogTimer(0, 0, 0, 0)`, writes that banner once,
then executes `CPU.HaltForever`.

## Write Removable Media

Writing the image overwrites the selected device. Resolve the USB device with `lsblk`, unmount all
of its mounted partitions, and substitute the whole removable device (for example `/dev/sdX`), not
a partition and never an internal system disk:

```sh
lsblk -o NAME,SIZE,MODEL,TRAN,RM,MOUNTPOINTS
sudo dd if=arcology-os/dist/arcology-seed-0.1/arcology-seed-0.1-x86_64.img \
  of=/dev/sdX bs=4M conv=fsync status=progress
sync
```

There is deliberately no automatic device-writing script: selecting a block device requires human
confirmation and an incorrect target is destructive.

## Physical Test

1. Record artifact checksums and the current commit.
2. Record laptop model, CPU, firmware vendor/version, and Secure Boot state.
3. Disable Secure Boot for this unsigned development image if necessary.
4. Select the USB device from the firmware's one-time boot menu.
5. Photograph the banner and stable halted display.
6. Classify the outcome using RFC-0006 section 9.
7. Complete `.agents/reports/WP-026-hardware-validation-package.md`.
8. Add any real firmware quirk to RFC-0006 Appendix A; do not add conjectural entries.

The image does not call `ExitBootServices`, alter internal storage, or install persistent firmware
state. Power cycling is the expected way to leave the intentional halt.
