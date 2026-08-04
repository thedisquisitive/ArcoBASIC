# Lazarum

Lazarum is the cross-platform, read-only companion for Lazarus image-storage drives. It is deliberately separate from the recovery appliance: connecting a backup disk to a normal workstation must be enough to review jobs and recover data without installing the privileged Lazarus service.

Current capabilities:

* Linux can discover `LAZARUS_STORAGE` and request a read-only ext4 mount through UDisks.
* Any platform can scan an already accessible folder for directory-backed Lazarus images.
* Job metadata and the recognized completion, escalation, image-creation, restore, and verification reports can be listed, viewed, and copied out without modifying the image.
* The GTK4 desktop application provides read-only drive mounting, mounted-folder selection, image search and inventory, job details, report reading, report extraction, image-volume selection, folder navigation, and customer-file extraction.
* On Linux, Lazarum exposes compressed Lazarus chunks as a read-only NBD disk, verifies and decompresses only requested chunks, and mounts NTFS, FAT, exFAT, and ext2/3/4 volumes with `nosuid,nodev,noexec`.
* Files and folders are confined beneath the selected volume. Symbolic links and special files are not extractable, and destination data is never overwritten.

## Build

Standalone, with no Lazarus appliance dependencies:

```sh
cmake -S lazarum -B lazarum/build
cmake --build lazarum/build
ctest --test-dir lazarum/build --output-on-failure
```

GTK 4.12 or newer is used for the native desktop application. The viewer core remains free of GTK dependencies so it can be packaged with GTK4 on Linux, Windows, and macOS. If GTK4 development files are unavailable, the portable core and `lazarum` command-line viewer still build.

Run the GTK application directly with `lazarum-gui`. An optional mounted storage path may be supplied as its only argument. Installed builds include a **Lazarum** desktop launcher.

```sh
lazarum capabilities
lazarum mount
lazarum scan /media/$USER/LAZARUS_STORAGE
lazarum reports /media/$USER/LAZARUS_STORAGE/images/example.laz
lazarum show-report /media/$USER/LAZARUS_STORAGE/images/example.laz completion-report.txt
lazarum extract-report /media/$USER/LAZARUS_STORAGE/images/example.laz completion-report.html ~/Desktop
lazarum volumes /media/$USER/LAZARUS_STORAGE/images/example.laz
lazarum files /media/$USER/LAZARUS_STORAGE/images/example.laz 0 Users
lazarum extract-files /media/$USER/LAZARUS_STORAGE/images/example.laz 0 ~/Recovered Users/example/Documents
```

The Linux storage-drive mount request always uses `ro,noload,nosuid,nodev,noexec`. Lazarum does not run `fsck`, repair an image, replay an ext journal, or modify files inside an image directory.

Browsing does not reconstruct the full image and does not require logical-image-sized free space. `lazarum-nbd` validates the finalized image envelope and chunk map at open, then verifies each stored and decompressed chunk when the mounted filesystem requests it. A bounded in-memory cache keeps recently requested chunks hot; no second disk image is written.

## Platform mount plan

| Platform | Initial provider | State |
|---|---|---|
| Linux | UDisks native ext4, forced read-only | Implemented |
| Windows | Signed read-only ext4 provider or constrained WSL adapter | Interface reserved |
| macOS | Bundled/notarized read-only ext4 FUSE provider | Interface reserved |

Image data access uses a separate provider boundary. The Linux provider is implemented. Windows and macOS providers remain fail-closed until their signed ext4 and image-filesystem adapters are implemented; they never report a file as extracted unless the destination copy completed.
