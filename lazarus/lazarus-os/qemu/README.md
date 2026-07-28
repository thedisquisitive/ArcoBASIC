# QEMU Test Harness

This directory contains local VM helpers for testing Arcology Lazarus OS without touching real
bench hardware.

## Test Modes

Build rootfs on a non-Alpine host:

```sh
SUDO_PASSWORD='...' lazarus/lazarus-os/scripts/build-rootfs-nspawn.sh
```

Build live ISO:

```sh
SUDO_PASSWORD='...' lazarus/lazarus-os/qemu/build-live-iso.sh
```

Live boot:

```sh
lazarus/lazarus-os/qemu/run-live.sh path/to/arcology-lazarus.iso
```

The default live profile presents the ISO as a USB mass-storage device, matching deployment to a
flash drive. Set `QEMU_BOOT_MEDIA=cdrom` only when explicitly testing optical-media compatibility.

Automated USB boot smoke test:

```sh
lazarus/lazarus-os/qemu/smoke-live-usb.sh path/to/arcology-lazarus.iso
```

The live ISO includes an installer boot entry. Select it at GRUB, or pass `lazarus.mode=install`
on the kernel command line to launch `lazarus-install-os` in a full-screen terminal.

Named profile:

```sh
lazarus/lazarus-os/qemu/run-profile.sh lazarus/lazarus-os/qemu/profiles/live.env
```

Installed appliance:

```sh
lazarus/lazarus-os/qemu/create-test-disks.sh
lazarus/lazarus-os/qemu/install-system-disk.sh lazarus/lazarus-os/build/qemu/lazarus-system.qcow2 ERASE
lazarus/lazarus-os/qemu/run-installed.sh
```

`install-system-disk.sh` erases only the selected QCOW2 system disk image. It partitions the image
with GPT, creates a FAT32 EFI System Partition and ext4 root partition, copies the assembled
rootfs, installs removable UEFI GRUB, and writes UUID-based boot configuration.

Full install-and-reboot smoke test:

```sh
SUDO_PASSWORD='...' lazarus/lazarus-os/qemu/smoke-installed.sh
```

This creates a fresh virtual system disk, installs the current root filesystem, boots it without
live media, and requires the installed OpenRC system to reach the GTK kiosk.

The destructive storage-preparation path is tested only against a disposable NBD image:

```sh
SUDO_PASSWORD='...' lazarus/tests/destructive/service_format_storage_nbd.sh
```

The test starts with a used MBR/FAT32 virtual disk and requires Lazarus to replace it with GPT,
create ext4 storage, mount it, and persist its stable storage assignment.

## Disk Layout

`create-test-disks.sh` creates:

- `build/qemu/lazarus-system.qcow2` - appliance system disk
- `build/qemu/customer-source.qcow2` - fake customer/source disk
- `build/qemu/destination.qcow2` - fake restore destination
- `build/qemu/image-storage.qcow2` - fake central image storage

The extra disks let us test source/destination/storage detection inside the VM without connecting
physical drives.

## Notes

- The scripts do not format or overwrite host disks.
- KVM is used automatically when `/dev/kvm` is available.
- Set `QEMU_DISPLAY=none` or `QEMU_EXTRA='-serial mon:stdio'` for console-focused runs.
- Profiles boot with OVMF/UEFI by default because the live ISO is UEFI bootable.
- `run-live.sh` requires an ISO that has already been built.
- `build-live-iso.sh` is a scaffold and requires Alpine package tooling plus ISO tooling.
