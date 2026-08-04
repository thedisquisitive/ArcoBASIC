# Boot Notes

Initial boot target:

```text
UEFI -> GRUB or syslinux -> Alpine Linux -> OpenRC -> lazarus-service -> lazarus-session -> Xorg -> lazarus-gui
```

Installed appliance target:

- EFI System Partition
- A root partition
- B root partition
- persistent state partition mounted at `/var/lib/arcology-lazarus`; live USB media uses the appended `LAZARUS_STATE` partition from its own boot device and bind-mounts the persistent bench and network profiles before OpenRC starts
- optional central image storage mount under `/mnt/lazarus-storage`

Live USB target:

- boot to RAM
- read-only squashfs root
- optional persistence overlay

The destructive disk-image writer for installation media is intentionally not present yet.

The current rootfs helper stages the locally built Lazarus binaries into `/usr` after the Alpine
base packages are installed. That keeps demo images aligned with the current source tree until the
APK packaging flow is finalized.

QEMU helpers live in `lazarus-os/qemu/`. Use them to test both the live ISO and the installed
appliance disk before writing any physical USB media or bench appliance storage.
