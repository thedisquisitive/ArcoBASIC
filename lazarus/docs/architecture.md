# Lazarus Architecture Stub

This is the initial C++ shape for Lazarus. Source access is intentionally non-destructive: approved sources are opened read-only and there is no source write API. Destination writes exist only through a separate destination handle, destination-only bench policy, and explicit destructive confirmation.

## First Components

```text
lazarus_core
├── job validation
├── bench profile model
├── device identity model
├── sysfs block-device discovery
├── persistent /dev/disk identity lookup
├── mounted system-disk detection
├── port safety checks
├── image plan model
├── progress event model
├── verification result model
└── support bundle manifest model
```

## Near-Term Buildout

Implemented discovery baseline:

* Enumerate physical block devices from `/sys/block`.
* Skip virtual loop, RAM, and floppy-style block devices.
* Read model, size, logical block size, removable state, rotational state, serial ending, transport, and partitions.
* Prefer `/dev/disk/by-path` for physical identity and retain `/dev/disk/by-id` for device identity.
* Mark mounted disks as system disks using `/proc/self/mountinfo`.
* Load simple bench profiles with `source=`, `destination=`, and `ignored=` physical paths.
* Assign bench roles by matching physical path, by-path, by-id, or Linux path.
* Open approved sources through a movable read-only handle using `O_RDONLY | O_CLOEXEC`.
* Inspect approved sources through the read-only handle.
* Read the first sector and detect protective MBR markers.
* Read the GPT header at LBA 1 and validate basic metadata.
* Read GPT partition entries and classify Windows-relevant partition types.
* Parse primary MBR partition entries when GPT is not present.
* Probe partition boot sectors for NTFS, FAT12, FAT16, FAT32, and exFAT signatures.
* Detect whole-device FAT/exFAT-style media without requiring an MBR partition table.
* Analyze supported source filesystems through short-lived read-only mounts with journal replay disabled.
* Confirm Linux installations from confined `os-release` data and Windows installations from offline registry hives; report filesystem evidence and layout-only candidates at lower confidence.
* Report boot mode, partition style, filesystem/label/capacity facts, BitLocker signatures, and SMART health in one technician-facing drive analysis.
* Write a directory-backed raw MVP image from an approved read-only source.
* Treat corrupt or unsupported partition/filesystem structures as source evidence to preserve, not as a reason to refuse raw capture.
* Create `INCOMPLETE` before data writes and replace it with `FINALIZED` only after a clean close.
* Write `metadata.json`, `partition-table.bin`, `disk.raw`, `hashes.dat`, and `imaging.log`.
* Use SHA-256 chunk hashes.
* Resume interrupted image writes from a verified contiguous hash prefix.
* Reopen and verify directory-backed images by checking markers, metadata, raw stream length, and every chunk hash.
* Restore verified images to destination-only devices after exact `ERASE` confirmation.
* Emit structured progress events from imaging, verification, and restore loops.
* Serve independent IPC clients concurrently so status and hotplug queries remain responsive during disk operations.
* Track active devices in the privileged service and expose factual `safe`, `in-use`, `flushing`, `mounted-storage`, and `system` disconnect states.
* Persist `job-journal.json` before imaging begins so interrupted jobs retain ticket, customer, technician, purpose, policy, and source identity.
* Discover interrupted images from configured storage and resume only after source identity and captured-prefix hashes match.
* Enforce named technician job presets in the service, including automatic full verification for standard presets.
* Generate branded text and print-ready HTML reports inside each image directory.

## Device Lifecycle

The service is authoritative for device activity. The GTK client polls a lightweight device-generation endpoint and refreshes workflow choices after connection changes. Linux device names are display details only; resume and policy checks use serial, by-id, and physical by-path identity.

Create Backup launches drive analysis asynchronously when its source selection changes. Ticket entry remains usable while the service holds the source read-only. The client validates the returned persistent identity and capacity before rendering the result, ignores superseded asynchronous responses, and caches a successful response for no more than five minutes and only until device generation changes. Analysis errors recommend Rescue imaging rather than becoming a raw-capture blocker.

Image storage may be a protected local disk, an SMB share, or an NFS export. All three are mounted at `/mnt/lazarus-storage`, so imaging, verification, ticket review, recovery, and restore use the same confined path contract. NAS configuration is accepted only after the service mounts the share and creates or opens the selected image folder read-write. SMB passwords are stored only in `/var/lib/arcology-lazarus/nas-storage.auth` with mode `0600`; they are never written into the bench profile, returned over IPC, or copied into the shared repository. A failed connection test restores the previous storage profile.

`SAFE TO DISCONNECT` means Lazarus has no open operation for that device. System disks and mounted image storage are never marked safe. During image finalization and destination flush, the service explicitly reports `FLUSHING - DO NOT DISCONNECT`.

Unexpected source removal leaves `INCOMPLETE`, `job-journal.json`, `source-identity.json`, and the verified chunk map in place. A resumed job must match the original source and revalidate the stored prefix before writing another chunk.

## Job Presets

The service defines the supported presets rather than trusting UI labels:

* Backup Before Repair
* SSD Upgrade
* Data Recovery
* Hardware Migration
* Custom

Standard presets use Zstandard compression and require full post-image verification. Backup Before Repair and Data Recovery select Rescue Mode so unreadable ranges are retried, recorded, and preserved without requiring a technician to diagnose the drive first. Custom jobs expose advanced mode selection and do not silently claim verification.

## Reports

Image creation writes an image-creation report. Successful preset verification promotes the factual result to `completion-report.html` and `completion-report.txt`. If the source layout is corrupt, a filesystem cannot be reopened, or unreadable ranges were recorded, successful raw capture instead produces `escalation-report.html` and `escalation-report.txt`. The report tells the technician to preserve the image and hand the case to data recovery/forensics without repairing the source. Restore writes a separate restore report. Reports use the active branding profile and remain local to configured image storage. The service only prints report files located beneath those configured roots.

## Progress Events

Long-running core operations accept a `ProgressCallback` and emit `ProgressEvent` records with:

* `operation`: image, verify, restore, and later clone.
* `phase`: stable workflow state such as start, write, hash, flush, finalize, complete, or failed.
* `message`: factual human-readable status.
* byte and chunk counters for progress bars.
* `indeterminate` for phases where Lazarus does not yet know a useful total.

The CLI renders these events to stderr. A future ArcoBASIC TUI/GUI should consume the same records through a local Lazarus service API or JSONL event stream, so UI code does not need to infer state from command output.

## Arco App Capsules

Arco app capsules can include Lazarus-facing application code, ArcoBASIC scripts, static assets, docs, and non-privileged Lazarus libraries for image metadata, verification, and browsing. They should not become the authority that opens raw block devices.

The safer model is:

```text
ArcoBASIC app capsule
    ↓ local IPC / JSONL events
lazarus-service
    ↓ narrow privileged API
raw block devices and image storage
```

That makes capsules closer to deterministic application bundles than general containers. If a recovery appliance later bundles the Lazarus service inside a signed capsule-like package, OS policy still owns device permissions, mount namespaces, and destructive capability.

Next concrete modules:

* `device_discovery`: add udev/blkid enrichment for filesystem and partition metadata.
* `block_reader`: add rescue-mode retry policy on top of the read-only source handle.
* `image_reader`: expose structured image open APIs beyond verification.
* `resume_manager`: add richer resume journals and bad-sector maps.
* `mbr`: parse extended/logical MBR partitions.
* `ntfs_browser`: browse supported NTFS images read-only.
* `job_log`: expand factual JSON and text logs.
* `service_api`: expose progress events and privileged operations to ArcoBASIC clients without granting arbitrary device access.

## Non-Negotiable Boundary

The first real raw-device API should only expose read-only source handles. Destination writes must use separate types and explicit assignment paths so the compiler helps prevent accidental source writes.
