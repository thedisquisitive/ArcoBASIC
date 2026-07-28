# Arcology Lazarus

Offline imaging, recovery, and hardware migration for Windows 10 and Windows 11 systems.

Lazarus is an official Arcology ecosystem tool focused on making sysadmin and repair-bench work safer, clearer, and less painful. The first priority is the core recovery path: identify offline Windows disks, protect the source, write recoverable images, verify them by reopening and reading them, and make file recovery possible without restoring the whole disk.

## Current Status

This directory is a C++20 scaffold. It can discover devices, enforce bench policy, open approved sources read-only, inspect GPT and primary MBR layouts, detect NTFS partition boot sectors, write and verify a directory-backed raw `.laz` MVP image, and restore a verified image to a destination-only device after explicit `ERASE` confirmation. It does not write to source block devices.

Implemented now:

* CMake project layout
* Core safety data model
* Linux block-device discovery through sysfs
* Persistent device identity through `/dev/disk/by-id` and `/dev/disk/by-path`
* Running system-disk detection through mount information
* Partition listing for discovered disks
* Required job information checks
* Bench profile loading and role assignment
* Custom bench port labels such as `Left USB3` or `Rear USB-C`
* Bench source/destination/ignored/system-disk policy checks
* Read-only source handle using `O_RDONLY | O_CLOEXEC`
* Basic disk inspection through the read-only source handle
* Protective MBR detection
* GPT header validation and partition-entry parsing
* Windows partition classification for EFI, MSR, Basic Data, and Recovery partitions
* Primary MBR partition parsing
* NTFS, FAT12, FAT16, FAT32, and exFAT boot-sector detection
* Whole-device FAT/exFAT-style media detection
* Directory-backed `.laz` MVP writer
* `none` and `zstd` image compression modes
* `INCOMPLETE` and `FINALIZED` image markers
* `metadata.json`, `partition-table.bin`, `disk.raw`, `hashes.dat`, and `imaging.log`
* SHA-256 chunk hash recording
* Resume of interrupted image writes from verified chunk prefixes
* Directory-backed image verification with cold reopen, marker checks, raw stream length checks, and SHA-256 recomputation
* Restore of verified directory-backed images to destination-only devices
* Core progress events for imaging, verification, and restore
* CLI progress rendering for long-running image and restore jobs
* Dependency-light terminal UI for common bench workflows
* Native GTK GUI starter for device and backup inspection
* Install script for local system installation
* Read-only source imaging plan stub
* Verification/report/support-bundle data structures
* Minimal CLI commands
* C++ smoke tests

Explicitly not implemented yet:

* NTFS browsing
* FAT/exFAT browsing
* Extended/logical MBR partition parsing
* Rich resume journals beyond hash-map based resume
* SMART collection
* Driver injection
* Boot repair
* GUI
* AI/Gemini integration

## Build

```sh
cmake -S lazarus -B lazarus/build
cmake --build lazarus/build
ctest --test-dir lazarus/build --output-on-failure
```

Run the stub CLI:

```sh
lazarus/build/lazarus version
lazarus/build/lazarus devices
lazarus/build/lazarus bench-check lazarus/examples/bench-alpha.profile
lazarus/build/lazarus smart lazarus/examples/bench-alpha.profile /dev/disk/by-path/...
lazarus/build/lazarus inspect-source lazarus/examples/bench-alpha.profile /dev/disk/by-path/...
lazarus/build/lazarus image-source lazarus/examples/bench-alpha.profile /dev/disk/by-path/... /mnt/lazarus-storage/example.laz 45127 Smith tech "Backup Before Repair"
LAZARUS_COMPRESSION=zstd lazarus/build/lazarus image-source lazarus/examples/bench-alpha.profile /dev/disk/by-path/... /mnt/lazarus-storage/example-zstd.laz 45127 Smith tech "Backup Before Repair"
lazarus/build/lazarus verify-image /mnt/lazarus-storage/example.laz
lazarus/build/lazarus restore-image lazarus/examples/bench-alpha.profile /mnt/lazarus-storage/example.laz /dev/disk/by-path/... ERASE
lazarus/build/lazarus plan-demo
```

Run the local service directly:

```sh
printf '%s\n' '{"command":"ping"}' | lazarus/build/lazarus-service --stdio --config lazarus/examples/bench-alpha.profile
printf '%s\n' '{"command":"devices"}' | lazarus/build/lazarus-service --stdio --config lazarus/examples/bench-alpha.profile
lazarus/build/lazarus-service --config lazarus/examples/bench-alpha.profile --socket /tmp/lazarus-service.sock
```

Initial service commands:

* `ping`
* `bench`
* `devices`
* `smart`
* `inspect_source`
* `image_source`
* `verify_image`
* `restore_image`

Run the TUI:

```sh
lazarus/build/lazarus-tui lazarus/examples/bench-alpha.profile
```

Run the GUI:

```sh
lazarus/build/lazarus-gui lazarus/examples/bench-alpha.profile
```

The first GUI pass exposes:

* Bench profile and configured image storage display
* Bench profile editor for storage roots, port roles, and labels
* Device list with bench roles and port labels
* Backup search/list from configured image storage
* Backup creation from a selected source-only device
* Non-destructive verification of a selected backup
* Restore of a selected backup to a selected destination-only device with `ERASE` confirmation

The GUI is now a service client. It does not link `lazarus_core`; device discovery, profile
loading/saving, backup search, backup creation, verification, and restore all go through
`lazarus-service`.

The GUI is functional but intentionally plain. The TUI remains the more complete bench-control surface while GUI safety screens are built out.

The TUI currently exposes:

* Create Backup
* Restore Backup
* Verify Backup
* Inspect Source
* Show Devices
* List Backups
* Bench Check
* Edit Bench Profile

It uses the same Lazarus core and progress callbacks as the CLI. Create Backup defaults to zstd compression and can choose uncompressed storage. Restore still requires destination-only bench policy and exact `ERASE` confirmation.
Verify and Restore select backups by searching configured image storage roots for ticket number, customer name, technician, purpose, or path text. If a search has no matches, the TUI lists all backups found in image storage.

## Install

Install locally:

```sh
sudo lazarus/scripts/install.sh
```

Useful options:

```sh
lazarus/scripts/install.sh --prefix "$HOME/.local" --bench-dir "$HOME/.config/arcology-lazarus"
lazarus/scripts/install.sh --skip-tests
```

Installed commands:

```sh
lazarus
lazarus-tui
lazarus-gui
lazarus-service
```

## Arcology Lazarus OS

The dedicated Alpine/OpenRC appliance scaffold lives in:

```sh
lazarus/lazarus-os
```

It defines the target boot/runtime shape, package split, OpenRC services, kiosk Xorg session,
udev automount suppression, default bench profile, and conservative rootfs assembly helpers.
The current GUI is still GTK3; Lazarus OS keeps the package/session boundary ready for the planned
GTK4/gtkmm4 GUI.

Validate the scaffold:

```sh
lazarus/lazarus-os/tests/validate-os-layout.sh
```

## UI and Capsule Boundary

The Lazarus core reports long-running work through structured progress events, not terminal-specific text. The CLI prints those events today. A future ArcoBASIC TUI/GUI should consume the same event shape from a local service or JSONL bridge.

Arco app capsules may bundle the Lazarus UI, ArcoBASIC scripts, client bindings, docs, and non-privileged image tools. Raw block-device access should remain behind the installed Lazarus service or recovery environment, with OS-level permissions and bench policy deciding what can be read or erased.

## Design Priority

The safe path has to be the easy path. Lazarus should make accidental source-drive destruction difficult by design, not by warning text alone.
