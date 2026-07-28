# Arcology Lazarus OS

Arcology Lazarus OS is the dedicated Alpine Linux appliance for Lazarus imaging benches.
It boots directly into the Lazarus interface, keeps raw-disk access in a privileged backend,
and treats source/destination port policy as operating-system infrastructure.

## Goals

- Alpine Linux base
- OpenRC init
- Xorg kiosk session initially
- Lazarus GUI launches full screen without a desktop environment
- GUI runs unprivileged
- privileged service owns raw block access, mounts, imaging, restore, and boot repair
- no browser, terminal, desktop panels, or desktop automount
- bench profile loaded from `/etc/arcology-lazarus/bench.profile`
- image storage mounted before the UI starts

The current workflow GUI uses GTK 4. The appliance build installs both GTK 4 and gtkmm 4 so
reusable C++ widgets can be introduced without changing the service/session contract.

## Runtime Shape

```text
OpenRC
  -> lazarus-service
      -> Lazarus core and raw block devices
  -> lazarus-session
      -> Xorg
          -> unprivileged lazarus-gui
              -> Unix domain socket
                  -> lazarus-service
```

The GUI must not open raw disks. The service exposes newline-delimited JSON IPC commands over
`/run/arcology-lazarus/service.sock`. The GTK GUI is a service client and does not link the
Lazarus core library; device discovery, profile loading/saving, backup search, backup creation,
SMART diagnostics, verification, and restore go through the service.

## Deployment Targets

Installed appliance:

- read-only root filesystem
- future A/B root partitions
- persistent state partition mounted at `/var/lib/arcology-lazarus`
- automatic bench profile
- automatic central storage mount
- source/destination port enforcement

Live USB:

- boots to RAM
- stateless by default
- optional persistence
- portable recovery and hardware profiling

## Repository Layout

```text
lazarus-os/
├── packages/       Alpine package recipes and package notes
├── rootfs/         files copied into the target filesystem
├── openrc/         init scripts
├── udev/           bench and automount-control rules
├── boot/           bootloader/profile notes
├── scripts/        build/install helper scripts
├── profiles/       bench profile templates
└── tests/          scaffold validation
```

## MVP Checklist

- boot x86-64 Alpine
- create `lazarus` user and runtime directories
- start privileged Lazarus service
- start kiosk Xorg session
- launch Lazarus GUI full screen
- detect drives
- enforce bench policies
- create verified images
- restore images
- browse images
- generate support bundles

## Build Host Notes

The helper scripts are intentionally conservative. They validate layout and describe the Alpine
steps, but they do not format disks or overwrite media.

Native Alpine/apk build host tools:

- `apk`
- `abuild`
- `mkinitfs`
- `xorriso` or Alpine's image tooling
- root privileges for rootfs assembly

On a Debian-like host without `apk-tools`, use the nspawn builder instead. It downloads an Alpine
minirootfs and runs `apk` inside that builder:

```sh
lazarus/lazarus-os/scripts/build-rootfs-nspawn.sh
```

Required host tools for that path:

- `systemd-nspawn`
- `curl` or `wget`
- `tar`
- `sudo` or a root shell

Run scaffold validation:

```sh
lazarus/lazarus-os/tests/validate-os-layout.sh
```

Create local QEMU test disks:

```sh
lazarus/lazarus-os/qemu/create-test-disks.sh
```

Run an installed-appliance VM once a system disk has been installed:

```sh
lazarus/lazarus-os/qemu/run-installed.sh
```

Run a live ISO:

```sh
lazarus/lazarus-os/qemu/run-live.sh lazarus/lazarus-os/build/arcology-lazarus-live.iso
```

To go straight into the installation workflow from the live ISO, choose the installer boot entry
or pass `lazarus.mode=install` on the kernel command line. The kiosk will launch
`lazarus-install-os` in a full-screen terminal.

Install from the live USB/session to an appliance disk:

```sh
lazarus-install-os
```

The installer enumerates whole disks, requires typing `ERASE`, creates a UEFI/GPT appliance layout,
and installs Lazarus OS so the machine reboots directly into the kiosk. The installed layout is:

- EFI System Partition mounted at `/boot/efi`
- Lazarus OS root partition mounted at `/`
- persistent Lazarus state partition mounted at `/var/lib/arcology-lazarus`

For automated QEMU testing, the same installer can be invoked with an explicit target:

```sh
lazarus-install-os /dev/vda ERASE
```

Stage the current Lazarus build into an assembled rootfs:

```sh
lazarus/lazarus-os/scripts/build-rootfs.sh lazarus/lazarus-os/build/rootfs
```

`build-rootfs.sh` installs the Alpine package set from `packages/world`, copies the OpenRC/kiosk
configuration, and runs `scripts/stage-current-lazarus.sh` using
`lazarus-os/build/lazarus-current` by default so `/usr/bin/lazarus-gui`,
`/usr/bin/lazarus`, `/usr/bin/lazarus-tui`, and `/usr/sbin/lazarus-service` match the current
source tree. `smartmontools` is included both in the package world and Lazarus package dependencies
because SMART collection is now a service-side feature.
