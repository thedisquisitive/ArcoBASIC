# Lazarus Image Format MVP

The current image writer creates a directory-backed image. This is the first recoverable shape, not the final production format.

Example:

```text
example.laz/
├── FINALIZED
├── metadata.json
├── partition-table.bin
├── disk.raw
├── hashes.dat
└── imaging.log
```

During writes, `INCOMPLETE` exists. Lazarus only removes `INCOMPLETE` and writes `FINALIZED` after all expected bytes, metadata, hash records, and logs are written.

## Files

`metadata.json`
: Local job metadata, source identity, raw imaging stats, and disk inspection summary.

`partition-table.bin`
: Snapshot of the first 1 MiB of the source, capped by source size. This preserves the MBR, GPT header, normal GPT partition-entry area, and typical early boot-sector context for the MVP path.

`disk.raw`
: Concatenated stored chunk stream. With `compression=none`, stored chunks are byte-for-byte source chunks. With `compression=zstd`, stored chunks are zstd-compressed source chunks.

`hashes.dat`
: Text chunk map. Current records use SHA-256 for both source chunks and stored chunks.

`verification.json`
: Factual verification summary written by `verify-image`.

`imaging.log`
: Factual local imaging log.

## Resume

If an image has `INCOMPLETE`, `disk.raw`, and `hashes.dat`, Lazarus verifies the contiguous chunk prefix from `hashes.dat`, trims `disk.raw` to the last valid stored byte, rewrites the verified hash prefix, and resumes from the next source offset.

## Compression

Supported modes:

* `none`
* `zstd`

`metadata.json` records the selected compression mode and both logical source bytes and stored bytes. `hashes.dat` records each chunk as:

```text
index source_offset source_size stored_offset stored_size source_sha256 stored_sha256
```

Older four-column uncompressed records remain readable:

```text
index source_offset source_size source_sha256
```

## Verification

Verification reopens the image directory from scratch:

* Requires `FINALIZED`.
* Rejects `INCOMPLETE`.
* Reads `metadata.json`.
* Reads `disk.raw` stored length.
* Reads `hashes.dat`.
* Recomputes SHA-256 for every stored chunk.
* Decompresses each chunk when required.
* Recomputes SHA-256 for every logical source chunk.
* Writes `verification.json`.
* Reconstructs the partition layout from the logical image stream.
* Validates primary and backup GPT headers and GPT CRC values.
* Reopens supported filesystem boot sectors.
* Reads the first NTFS MFT record when NTFS is present.

## Read-only browsing and file recovery

The privileged service can prepare a browse cache from a finalized image:

* Full chunk verification runs before browsing.
* Compressed chunks are reconstructed into a sparse cache outside the image directory.
* The cache is fingerprinted against `metadata.json` and `hashes.dat` before reuse.
* The cache file is attached through a read-only loop device.
* NTFS, FAT, exFAT, and ext volumes are mounted `ro,nosuid,nodev,noexec`.
* Directory traversal is confined beneath the selected volume root.
* Symbolic links and special files are not exportable.

Exports are service-owned and accept only a device assigned the `removable_media` role. Lazarus creates a unique recovery folder, refuses to overwrite existing files, flushes the destination filesystem, and unmounts it before returning `safe_to_disconnect=true`.

An image directory also contains `source-identity.json`, which binds interrupted-image resume to the original source, and `bad-sector-map.dat`, which records zero-filled source ranges produced by Rescue Mode.

## Production Gaps

Complete the physical-hardware acceptance matrix in `bench-acceptance.md`, especially USB bridge behavior, BitLocker images, damaged NTFS volumes, filenames that are not representable on FAT destinations, and multi-terabyte image browse-cache performance.
