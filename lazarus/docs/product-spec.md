# Arcology Lazarus

Offline Imaging, Recovery, and Hardware Migration for Windows 10 and Windows 11 systems.

Arcology Lazarus is an in-house replacement for Acronis focused on one job: creating Windows system images that can actually be recovered, browsed, restored, and migrated when the original machine is dead.

The project begins as a native Linux application written in modern C++. Once the core imaging and recovery engine is proven, it can be integrated into a custom Alpine Linux-based recovery environment and deployed as a dedicated imaging-bench appliance.

## Core Philosophy

Lazarus assumes the technician may choose the wrong drive, misunderstand terms, ignore warnings, disconnect hardware mid-operation, leave no notes, or otherwise make avoidable mistakes. The software must prevent preventable damage instead of merely warning about it.

Design rules:

* The source drive is read-only.
* The destination is writable only when explicitly assigned.
* No imaging job can begin without a ticket number and customer name.
* The imaging bench may restrict physical ports to source-only or destination-only roles.
* An image is not verified until Lazarus can reopen it, inspect it, browse or mount it, and read representative data.
* Every status message must describe facts the software actually knows.
* The safe action must always be the easiest action.
* The recovery path is the primary product.

AI advisory integrations are deferred. The urgent path is deterministic local imaging and recovery.

## Initial Scope

Supported first:

* Offline Windows 10 and Windows 11 systems
* x86-64 systems
* UEFI/GPT systems
* Legacy/MBR systems for raw imaging and NTFS detection
* NTFS system partitions
* FAT32 EFI System Partitions
* Microsoft Reserved partitions
* Windows Recovery partitions
* Standard SATA SSDs and HDDs
* Standard NVMe drives
* Common USB-to-SATA and USB-to-NVMe adapters

Primary workflows:

* Create a full image before repair
* Image a non-booting Windows system
* Restore a system image
* Browse an image and recover individual files
* Clone an HDD to an SSD
* Migrate Windows to replacement hardware
* Recover from lightning-strike or motherboard failure
* Inject replacement hardware drivers
* Strip or neutralize incompatible hardware drivers
* Repair UEFI boot files and BCD configuration
* Verify that an image is recoverable

Deferred:

* Live Windows imaging
* VSS integration
* Incremental backups
* Cloud backup
* Scheduled backups
* macOS/APFS support
* RAID reconstruction
* Storage Spaces recovery
* Dynamic disk recovery
* Enterprise backup dashboards
* Forensic certification features

Unsupported configurations must stop safely and explain why. Lazarus must not offer a casual continue button for configurations it cannot handle reliably.

## Product Modes

The main interface exposes task-oriented workflows:

```text
Create Backup
Restore Backup
Move Windows to Replacement Computer
Recover Files
Verify Backup
```

Advanced terms such as raw copy, filesystem-aware copy, compression level, block size, and retry strategy stay hidden unless an administrator enables Expert Mode.

## Required Job Information

No operation may begin without:

* Ticket number
* Customer name
* Technician identity
* Job purpose

The Continue button remains disabled until all required fields are present.

Images are automatically named and organized:

```text
/images/
└── 45127/
    └── Smith_John/
        └── 2026-07-23_1041/
            ├── system.laz
            ├── metadata.json
            ├── imaging.log
            ├── source-smart.json
            ├── verification.json
            └── recovery-profile.json
```

The technician should not manually create filenames.

## Imaging Bench Lockdown

Bench Mode defines which physical ports are permitted for each role.

Example:

```text
Bench Alpha

SOURCE ONLY:
- Front-left SATA dock
- Front-left USB-C port

DESTINATION ONLY:
- Front-right SATA dock
- Front-right USB-C port

IMAGE STORAGE:
- /mnt/lazarus-storage

IGNORED:
- Keyboard
- Mouse
- Internal system drive
```

Ports are identified by physical device path and persistent hardware properties, not transient names such as `/dev/sdb`.

In Bench Mode, a source on a destination port or a destination on a source port blocks the workflow. There is no technician override. Administrative override, if ever implemented, must require authenticated administrator access and must be logged.

## Source Protection

Application layer:

* Never open the source with write permissions.
* Never mount source filesystems read-write.
* Never run repair utilities against the source.
* Never modify partition tables.
* Never update NTFS metadata.
* Never allow swap, automount, or desktop services to touch the source.

Operating-system layer:

* Disable desktop automounting.
* Apply read-only block-device policy where possible.
* Use udev rules to identify source-only ports.
* Consider kernel or device-mapper write protection.
* Log every attempted write against a protected source.

The program must distinguish these statements:

* Lazarus did not write to the source.
* The drive is healthy.
* The customer data is safe.

Only the first may be asserted from application behavior alone.

## Lazarus Image Format

Working extension: `.laz`

A Lazarus image may be a directory-backed image set or a container with independently recoverable streams.

Logical contents:

```text
metadata.json
partition-table.bin
partition-0.img
partition-1.img
partition-2.img
block-map.dat
hashes.dat
bad-sector-map.dat
imaging.log
verification.json
source-hardware.json
migration-profile.json
```

Requirements:

* Versioned metadata
* Chunk-level hashes
* Independent partition streams
* Recoverable partition data even if optional indexes are damaged
* Resume support
* Sparse storage
* Optional compression
* Clear incomplete-image marker
* Clear finalization marker
* No single index whose corruption makes all partition data inaccessible
* Ability to export a partition as a standard raw image
* Ability to browse supported filesystems without restoring the entire image

Initial compression options:

* None
* LZ4
* Zstandard

Recoverability and speed outrank maximum compression.

## Imaging Engine

The core engine is independent of the GUI.

Suggested components:

```text
lazarus-core
├── device-discovery
├── port-policy
├── block-reader
├── filesystem-map
├── image-writer
├── image-reader
├── verifier
├── resume-manager
├── smart-reader
├── windows-inspector
├── driver-manager
├── boot-repair
├── migration-engine
├── job-logger
└── support-bundle
```

Imaging modes:

* Standard Imaging: filesystem-aware copying of used clusters for supported Windows filesystems.
* Raw Imaging: sector-by-sector acquisition.
* Rescue Imaging: retry ladder for unstable or failing drives.

Rescue Mode prioritizes readable data while minimizing unnecessary stress on the source drive.

## Verification

Verification must test recoverability, not merely compare stored checksums.

Stages:

1. Container integrity: reopen image, parse metadata, validate indexes, stream lengths, and finalization state.
2. Hash verification: re-read chunks, recompute hashes, record mismatches.
3. Partition validation: parse GPT, validate partition boundaries, identify EFI, MSR, Windows, and Recovery partitions.
4. Filesystem validation: open NTFS read-only, locate MFT, traverse directories, read representative files.
5. Browse test: use the same browsing layer technicians use, enumerate folders, read file ranges, confirm extraction works.
6. Restore test: restore selected blocks or a scratch partition and compare expected hashes.

Prefer factual results over vague labels:

```text
Image completed.
Container reopened successfully.
GPT parsed successfully.
All four partitions were accessible.
NTFS directory traversal completed.
256 sampled files were read successfully.
Full chunk verification passed.
No unreadable source sectors were recorded.
```

## Image Browsing

A technician must be able to browse an image without restoring it.

The browser should support partition selection, read-only NTFS browsing, file search, file extraction, preview metadata, user profile discovery, common customer-data shortcuts, and clear indication of unreadable or partially recovered files.

Common shortcuts:

```text
Desktop
Documents
Downloads
Pictures
User Profiles
QuickBooks Files
Outlook Data
Browser Profiles
Application Data
```

## Windows Inspection

Lazarus should detect Windows version, build number, edition, architecture, hostname, user profiles, boot mode, partition style, EFI System Partition, BCD store, Windows Recovery Environment, BitLocker state, Fast Startup state where detectable, storage/chipset/network/display drivers, boot-critical services, and offline registry hives.

## Hardware Migration

Hardware migration is a first-class workflow:

```text
Move Windows to Replacement Computer
```

Stages:

1. Load the customer image.
2. Load or generate a replacement-hardware profile.
3. Compare old and new hardware.
4. Identify boot risks.
5. Restore the image.
6. Stage required replacement drivers.
7. Neutralize incompatible boot-critical drivers.
8. Repair EFI and BCD configuration.
9. Validate the offline Windows installation.
10. Prepare first boot.
11. Generate a migration report.

## Driver Management

Driver sources include a local shop driver vault, extracted manufacturer packs, exports from working systems, or administrator-supplied folders.

Index drivers by INF name, provider, version, date, architecture, device class, hardware IDs, compatible IDs, supported Windows versions, signature state, and package hash.

Injection priority:

1. Storage controller
2. NVMe or SATA controller
3. Chipset
4. USB controller
5. Network
6. Display
7. Optional peripherals

Boot-critical storage drivers must be staged before first boot.

## Boot Repair

The migration engine should detect UEFI/legacy mismatch, GPT/MBR mismatch, missing storage drivers, Intel VMD/RST and AMD RAID requirements, BitLocker state, Secure Boot conflicts, EFI validity, BCD validity, and Windows partition paths.

Avoid promising that a machine will boot. Prefer factual wording:

```text
Ready to attempt first boot.

Required storage drivers were staged.
EFI boot files were rebuilt.
BCD entries were validated.
No known boot-blocking condition was detected.
```

## User Interface

The UI should be simple and resistant to misuse:

* Dark, calm interface
* Large buttons
* Large readable text
* Minimal animation
* No ribbon interface
* No nested configuration maze
* No unexplained acronyms
* Clear workflow progress
* Red reserved for destructive actions
* Yellow reserved for risk or incomplete recovery
* Green used only for completed factual checks

Drive presentation should use human-readable identity and bench port labels instead of only `/dev/sdX`.

Destructive confirmation must be explicit, for example typing `ERASE` after showing the exact drive that will be erased.

## Error Messages

Error messages must distinguish:

* What was observed
* What Lazarus did
* What Lazarus did not do
* What remains unknown
* What action is recommended

Avoid claims such as `Your data is safe`.

## Logging and Audit Trail

Every job should record ticket number, customer name, technician, bench, source device identity, source physical port, destination identity, SMART snapshots, imaging mode, bytes read/written, read errors, retry counts, unreadable ranges, compression mode, verification stages, browse test, restore test, driver changes, boot-repair actions, administrative overrides, and final factual result.

Exports:

* JSON
* Plain text
* Optional PDF report later
* Support bundle ZIP

## Support Bundle

A support bundle should contain logs, verification data, SMART snapshots, hardware profile, port profile, image metadata, migration plan, and other diagnostic artifacts. It must exclude customer file contents.

## Linux-First Architecture

Language and tooling:

* C++20 initially
* C++23 where compiler and Alpine compatibility permit
* Strong type safety
* RAII for all device and file handles
* Exceptions limited at subsystem boundaries
* Explicit error/result types in the imaging core
* No hidden global mutable state
* CMake and Ninja
* GCC and Clang support
* Unit tests
* clang-tidy and clang-format
* Sanitizer builds for development

Likely Linux interfaces:

* `libudev`
* `libblkid`
* `libfdisk` or direct GPT parsing
* `libmount`
* `smartctl` wrapper initially
* `libarchive`
* `liblz4`
* `libzstd`
* OpenSSL or another vetted hash library
* Maintained Linux NTFS implementation
* Minimal privileged helper for raw block access

Privilege separation:

```text
lazarus-ui
    ↓ local authenticated IPC
lazarus-service
    ↓
raw block devices
mount namespace
driver operations
image storage
```

The GUI must not issue arbitrary shell commands.

## Custom Alpine Environment

Future recovery environment goals:

* Fast boot
* Small attack surface
* Read-only root filesystem
* Automatic launch into Lazarus
* No desktop distractions
* No general-purpose browser
* No package manager exposed to technicians
* Bench profile loaded automatically
* Central image storage mounted automatically
* Restricted network access
* Persistent logs stored separately
* Signed update process

## Initial MVP

The first usable internal build should:

1. Run on Linux.
2. Detect a Windows 10 or Windows 11 drive.
3. Require ticket number and customer name.
4. Enforce source and destination port policy.
5. Image the source to central storage.
6. Resume after interruption.
7. Record unreadable sectors.
8. Reopen the completed image.
9. Verify every stored chunk.
10. Parse the partition table.
11. Browse the NTFS partition read-only.
12. Extract a test file.
13. Produce a factual completion report.

## Success Criteria

Lazarus succeeds when:

* A technician cannot accidentally overwrite a source drive during the normal workflow.
* Every image is tied to a ticket and customer.
* A completed image can be reopened without rituals or repeated popups.
* Verification proves that browsing and extraction work.
* A partially damaged image remains useful.
* Interrupted jobs can resume.
* Migration to replacement hardware requires fewer manual boot-repair steps.
* Driver injection is based on detected hardware and signed matching packages.
* The application can eventually boot as a dedicated Alpine-based recovery appliance.
* The recovery process is simpler than Acronis for an inexperienced technician.
* Verified means something factual.
