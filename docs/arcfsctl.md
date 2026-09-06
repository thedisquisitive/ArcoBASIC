# arcfsctl

`arcfsctl` is the host utility for managing ArcFS support and ArcFS media. It is written in
ArcoBASIC at `arcfs-utils/tools/arcfsctl.abas` and built by ArcoFission into a native runtime capsule.
Arconaut, the graphical ArcFS administrator, is also an ArcoBASIC source compiled into a capsule
and uses this CLI as one of its Linux backends.

Current Linux commands:

```sh
arcfsctl status
arcfsctl install-support
arcfsctl devices
arcfsctl create-image arcfs.img 1073741824 --format
arcfsctl format /dev/sdXN --force
arcfsctl inspect arcfs.img
arcfsctl ls arcfs.img /
arcfsctl cat arcfs.img /hello.txt
sudo arcfsctl mount /dev/sdXN /mnt/arcfs
arcfsctl unmount /dev/sdXN
```

Linux support uses:

- `arcfs-linux` for ArcFS probing, formatting, reading, writing, and FUSE mounting;
- `ArcoFission` to compile `arcfs-utils/tools/arcfsctl.abas` into the `arcfsctl` capsule;
- `mount.arcfs` for `mount -t arcfs`;
- `mkfs.arcfs` for system formatter dispatch;
- `61-arcfs.rules` for udev and udisks2 filesystem discovery;
- FUSE 3 for mounted filesystem access.

Distribution package:

```sh
arcfs-utils/scripts/build-arcfs-deb.sh
sudo apt install ./dist/arcobasic-arcfs_0.1.0_amd64.deb
arcfsctl status
```

The Debian package reloads udev and restarts/reloads udisks2 after install/remove so desktop file
managers can discover ArcFS volumes without a manual system setup step.

Windows direction:

The Windows provider should use the same `arco::arcfs` parser/writer behind a WinFsp filesystem
frontend. The expected package shape is an ArcFS support installer that checks for WinFsp, installs
or enables the ArcFS provider service, registers ArcFS image/device associations, and exposes the
same image creation and inspection commands through `arcfsctl`.
