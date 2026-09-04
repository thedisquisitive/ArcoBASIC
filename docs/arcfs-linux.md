# ArcFS Linux Host Mounting

`arcfs-linux` is the first host-side ArcFS access tool. It can inspect, list, extract, format, and,
when built with FUSE 3, mount an ArcFS FMV2 volume with basic read-write support.

Supported sources:

- an ArcFS partition device, for example `/dev/sdb2`;
- an ArcFS volume image;
- a whole-disk image or device with `--partition N`;
- an embedded volume with `--offset BYTES`.

Examples:

```sh
arcfs-linux inspect /dev/sdb2
arcfs-linux ls disk.img / --partition 2
arcfs-linux cat disk.img /home/hello.txt --partition 2
truncate -s 64M arcfs.img
arcfs-linux mkfs arcfs.img --force
mkdir -p /mnt/arcfs
arcfs-linux mount /dev/sdb2 /mnt/arcfs -- -f
```

The FUSE mount command is available only when CMake finds `fuse3` through `pkg-config` at build
time. On Debian/Ubuntu-style systems, install `libfuse3-dev` and rebuild.

System installation:

```sh
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --target arcfs-linux
sudo cmake --install build
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=block
sudo mkfs.arcfs /dev/sdXN --force
sudo mount -t arcfs /dev/sdb2 /mnt/arcfs
sudo mount -t arcfs disk.img /mnt/arcfs -o partition=2
```

The install step places `arcfs-linux` in the configured binary directory and `mount.arcfs` in the
configured system binary directory. Linux `mount -t arcfs` dispatches to `mount.arcfs`, which then
launches the FUSE frontend. `partition=N` and `offset=BYTES` are ArcFS-specific helper options;
other mount options are forwarded to FUSE where appropriate.

Use a system prefix such as `/usr` for source installs. With the default `/usr/local` prefix,
`mount.arcfs` may install outside the helper directories searched by `mount(8)` on some distros.

Desktop discovery:

The install also places `61-arcfs.rules` under `lib/udev/rules.d`. Because libblkid does not know
ArcFS yet, the rule runs:

```sh
arcfs-linux probe /dev/sdXN
```

for unrecognized block devices. If the ArcFS superblock ring, checkpoint, and B+trees validate, the
probe emits `ID_FS_TYPE=arcfs`, `ID_FS_USAGE=filesystem`, version, label, and UUID-style metadata
for udev/udisks2.

Formatting:

```sh
mkfs.arcfs TARGET --force
arcfs-linux mkfs TARGET --force
arcfs-linux mkfs TARGET --force --offset BYTES --size BYTES
arcfs-linux mkfs TARGET --force --volume-id 0123456789abcdef
```

`mkfs` refuses to run without `--force`. The Linux host formatter sizes the on-disk allocation
bitmap for the selected target, so full USB devices and large image files can be initialized. The
current freestanding Arcology OS policy still has a smaller live in-memory bitmap ceiling for
write-side operation; use the Linux host mount path for large-volume writes until that OS policy
grows to match.

Desktop file managers typically mount removable media through `udisks2`. The `mount.arcfs` helper
provides the system-level filesystem entry point, and the udev rule provides ArcFS filesystem
identity. Some distros may still need a udisks2 mount-options entry allowing `arcfs` as a mountable
filesystem signature.

Current scope:

- FMV2 reader and basic copy-on-write host writer;
- validates the superblock ring, checkpoint checksum, and B+tree node checksums;
- supports Object, Namespace, Attribute, and Extent trees;
- supports sparse file reads by returning zero-filled holes;
- supports GPT partition offset selection for whole-disk images/devices;
- supports `mount -t arcfs` after system installation;
- exports ArcFS `ID_FS_*` metadata through a udev probe rule for desktop storage stacks.
- formats root-only ArcFS FMV2 images/partitions/devices with a target-sized allocation bitmap.
- writes regular files larger than the original bootstrap extent window; current writes are staged
  in host memory until FUSE flush/release/fsync.

Planned follow-on work:

- grow the freestanding Arcology OS live bitmap policy to match full-device host-formatted volumes;
- incremental dirty-range commits instead of whole-file host staging;
- free-space reclamation for overwritten and deleted copy-on-write extents;
- safer partition discovery by ArcFS signature, label, and GUID policy;
- optional `udisks2` mount-options packaging for distros that require an explicit filesystem allow-list;
- Windows support using the same parser behind a WinFsp or native-driver front end;
- packaging rules that expose the FUSE dependency as an optional Linux feature.
