# Lazarus Bench Policy

Bench policy is the first hard safety layer above device discovery.

The current profile format is intentionally small:

```text
name=Bench Alpha
image_storage=/mnt/lazarus-storage
image_storage_device=/dev/disk/by-id/usb-Storage_Disk_SERIAL
image_storage_volume=/dev/disk/by-uuid/FILESYSTEM-UUID
image_storage_port=port:pci-0000:00:14.0-usb-0:3
image_storage=/mnt/lazarus-secondary
source=/dev/disk/by-path/pci-0000:00-usb-0:1:1.0-scsi-0:0:0:0
destination=/dev/disk/by-path/pci-0000:00-usb-0:2:1.0-scsi-0:0:0:0
removable_media=/dev/disk/by-path/pci-0000:00-usb-0:4:1.0-scsi-0:0:0:0
ignored=/dev/disk/by-path/pci-0000:00-nvme-1
port_label=/dev/disk/by-path/pci-0000:00-usb-0:1:1.0-scsi-0:0:0:0|Left USB3
port_label=/dev/disk/by-path/pci-0000:00-usb-0:2:1.0-scsi-0:0:0:0|Rear USB-C
```

Supported keys:

* `name`: human-readable bench name.
* `image_storage`: where `.laz` images may be stored. Repeat for multiple configured storage roots.
* `image_storage_device`: stable identity of the assigned storage disk.
* `image_storage_volume`: stable filesystem identity mounted for image storage.
* `image_storage_port`: the physical connection reserved for image storage.
* `source`: source-only physical path. Repeat for multiple ports.
* `destination`: destination-only physical path. Repeat for multiple ports.
* `removable_media`: temporary media permitted to receive recovered files. It is never a restore or OS-install target.
* `ignored`: ignored physical path. Repeat for internal disks and unrelated devices.
* `port_label`: friendly label for a persistent identity. Format is `identity|label`.

Matching checks these identities:

* Discovered physical path
* `/dev/disk/by-path`
* `/dev/disk/by-id`
* Linux device path

Role precedence:

1. Running system disk
2. Image storage
3. Ignored
4. Source-only
5. Destination-only
6. Removable media
7. Unknown

Normal source imaging only accepts `source-only`. System disks, destination ports, ignored ports, and unknown ports block the workflow.

Labels do not grant permissions or change roles. They are display names for technician clarity, such as `Left USB3`, `Front SATA Dock`, or `Rear USB-C`.

The Administration > Physical Port Roles page writes persistent `/dev/disk/by-path` identities when available. Each connection may have exactly one role. Recovered-file export accepts only `removable_media`, mounts it through the privileged service, flushes it, and unmounts it before reporting that it is safe to disconnect.

The TUI backup workflow selects from configured `image_storage` roots and creates the image directory automatically:

```text
<image_storage>/<ticket>/<customer>/<YYYY-MM-DD_HHMM>/
```

Technicians should not manually type raw image paths during normal backup creation.
