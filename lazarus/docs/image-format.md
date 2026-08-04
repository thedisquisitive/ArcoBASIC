# Lazarus Image Format MVP

The current image writer creates a directory-backed image. Metadata format version 2 adds zero-elided logical chunks while retaining read compatibility with version 1 hash maps.

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
: Concatenated stored chunk stream. Nonzero chunks are stored byte-for-byte with `compression=none` or zstd-compressed with `compression=zstd`. Chunks that read back as entirely zero are represented as logical holes in `hashes.dat` and consume no bytes in `disk.raw`.

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

`metadata.json` records the selected compression mode, logical source bytes, stored bytes, and zero-filled bytes omitted from physical storage. Current `hashes.dat` records each chunk as:

```text
index source_offset source_size stored_offset stored_size source_sha256 stored_sha256 storage
```

`storage` is `data` for a payload in `disk.raw` or `zero` for a verified all-zero logical chunk. A zero record has `stored_size=0`; verification, browsing, and restore synthesize that logical range as zeros and validate its source hash. Seven-column compressed records and older four-column uncompressed records remain readable.

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
* Decompresses each stored chunk when required, or reconstructs a declared zero chunk.
* Recomputes SHA-256 for every logical source chunk.
* Writes `verification.json`.
* Reconstructs the partition layout from the logical image stream.
* Validates primary and backup GPT headers and GPT CRC values.
* Reopens supported filesystem boot sectors.
* Reads the first NTFS MFT record when NTFS is present.

Raw-capture integrity and source-layout health are separate results. A finalized image whose complete stored and logical streams pass SHA-256 verification is a verified raw backup even when the captured GPT, MBR, filesystem, or MFT was already damaged. Those structural results remain false and generate an escalation warning/report; Lazarus never repairs the source before capture.

Zero elision does not trust a filesystem allocation table and does not skip reading the source. Lazarus first reads the chunk through the normal raw or rescue policy and only omits its payload when every returned byte is zero. This preserves damaged and unsupported filesystems while reducing compression, image-storage, NAS, and verification work on TRIMmed SSDs and other zero-filled media.

## Read-only browsing and file recovery

Lazarum can browse a finalized image without reconstructing it first:

* Opening validates `FINALIZED`, rejects `INCOMPLETE`, and bounds-checks the complete chunk map.
* A read-only NBD bridge exposes the logical disk directly from the compressed chunk stream.
* Only chunks requested by the filesystem are decompressed and SHA-256 verified.
* Recently requested chunks are retained in a bounded memory cache; no logical-disk-sized cache is written.
* NTFS, FAT, exFAT, and ext volumes are mounted `ro,nosuid,nodev,noexec`; ext also uses `noload`.
* Directory traversal is confined beneath the selected volume root.
* Symbolic links and special files are not exportable.

Exports are service-owned and accept only a device assigned the `removable_media` role. Lazarus creates a unique recovery folder, refuses to overwrite existing files, flushes the destination filesystem, and unmounts it before returning `safe_to_disconnect=true`.

An image directory also contains `source-identity.json`, which binds interrupted-image resume to the original source, and `bad-sector-map.dat`, which records zero-filled source ranges produced by Rescue Mode.

## Portable storage access

Dedicated ext4 image storage is mounted with its repository readable and traversable by users who are not members of the appliance's local `lazarus` group. Before a safe unmount, Lazarus reapplies portable read permissions to existing image directories and regular files while removing public write permission. Appliance authentication files stored at the volume root remain mode `0600` and are not included in that permission pass.

The companion **Lazarum** Drive Viewer lives in `lazarum/`. Its portable core scans an accessible storage directory, reads job metadata and reports, and safely extracts reports without overwriting files. Linux read-only ext4 storage mounting uses UDisks, while image filesystems use the authenticated on-demand NBD provider. Windows and macOS providers remain explicit fail-closed adapters rather than placeholder success paths.

## Production Gaps

Complete the physical-hardware acceptance matrix in `bench-acceptance.md`, especially USB bridge behavior, BitLocker images, damaged NTFS volumes, filenames that are not representable on FAT destinations, and multi-terabyte on-demand browse performance.
