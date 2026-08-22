# ArcologyFS (ArcFS) Phases S and T: Real Persistent Block-Device Backing and a Cross-Boot Proof

## Scope delivered

RFC-0043 Phases S and T, delivered together (Phase T has nothing to prove without Phase S).

**Phase S** (Section 5): real, selectable persistent block-device backing for ArcFS, reusing
`fat32_policy.abas`'s own already-proven `FAT32.SelectBlockDevice` dispatch pattern verbatim.

- `ArcFS.SelectBlockDevice(provider)` / `ArcFS.ActiveBlockDevice()` / `ArcFS.
  BlockDeviceProviderRamDisk()`=0 / `ArcFS.BlockDeviceProviderUefi()`=1.
- `ArcFSBlockReadSectors`/`ArcFSBlockWriteSectors`/`ArcFSBlockSectorCount` dispatch wrappers,
  routing to `UefiBlockDevice.*` when provider 1 is selected, `RAMDisk.*` otherwise (default).
- All 37 real `RAMDisk.*` call sites in `arcfs_policy.abas` migrated to the new wrappers (`ArcFS.
  FormatVolume`, `MountImage`, `PrepareCommit`, every tree read/write, snapshot resolution, health
  scan -- every real storage access in the file). Default provider unchanged (0/RAMDisk), so every
  existing RFC-0039/0040/0042 fixture needed zero changes.

**Phase T** (Section 6): the actual load-bearing deliverable -- proof that ArcFS data survives
past the process that wrote it, for the first time in this project's history.

## The finding that motivated this: ArcFS had never once persisted anything

Confirmed by direct code inspection before writing a line of new code: `arcfs_policy.abas` called
`RAMDisk.*` at all 37 of its real storage call sites and `UefiBlockDevice.*` at none. Every
"commit + remount" proof across RFC-0039/0040/0042 -- dozens of fixtures -- read back from the
same pre-populated in-memory byte range within one QEMU boot. Nothing distinguished "the bytes
really left the process" from "the compiler simply never overwrote that memory region."

## A real, useful discovery made cheaply before committing to fixture design: no custom paging needed

Every prior ArcFS fixture (the `aps-exception-entry` family) carries its own custom page-table
construction and an `ExitBootServices` transition, inherited by copy-paste convention from this
project's very first exception-entry stub. Before designing the Phase T fixtures, a small
standalone probe (`ArcFS.SelectBlockDevice`/`FormatVolume`/`MountImage`, nothing else) was built
using `blockio-provider.abas`'s own much simpler shape instead -- no custom CR3 switch, no
`ExitBootServices`, staying entirely within UEFI Boot Services the whole run -- and it worked on
the first attempt, including against ArcFS's full RFC-0042 Phase Q address range (up to
`0x1A000000`, 416 MiB). Confirmed directly, not assumed: UEFI Boot Services' own default identity
mapping already covers this range on a machine with the harness's now-512 MiB RAM budget. Both
Phase T fixtures use this simpler shape -- it also removes `ExitBootServices` as a potential
confound in a proof that is specifically about the block device, not about freestanding execution.

## The Phase T proof

Two genuinely separate fixtures, compiled separately, run in two genuinely separate
`qemu-system-x86_64` process invocations against the same real disk image file
(`run-uefi-hello-with-blockio-disk.sh`'s existing real-`virtio-blk-pci`-drive harness shape,
unchanged):

- **Writer** (`aps-arcfs-persist-writer.abas`): discovers the real attached disk, selects it,
  formats a fresh ArcFS volume directly onto it, creates a real file (`hi.txt`, 10 bytes of real
  content) with a real integer attribute, commits, confirms `RAMDiskBaseAddress`'s own raw memory
  was never touched (the negative-side check that the write genuinely went to the real device, not
  a silent RAMDisk fallback -- `RAMDisk.Create` is never called anywhere in this fixture), and
  fully exits.
- **Independent host-side check**: after the writer's QEMU process has fully exited, the smoke
  test reads the real disk image FILE directly off the host filesystem and confirms its first 8
  bytes are the literal ArcFS superblock magic (`"ARCFSB02"`) -- not inferred from QEMU console
  output, read directly off the artifact the writer process left behind.
- **Reader** (`aps-arcfs-persist-reader.abas`): a genuinely separate fixture and QEMU launch,
  sharing no process memory with the writer. Selects the real block device, mounts the SAME disk
  image, looks up `hi.txt` by name, reads its content back, and compares every byte against
  literal constants written directly into the reader's own source -- not derived from or shared
  with the writer's source in any way (the same "disambiguation is a pure function of on-disk
  bytes alone" discipline RFC-0042 Section 9's own backward-compat fixture already established).
  Also confirms the attribute's type and value survived.

## Validation

- All touched Phase S entry points compile cleanly at X86_64 codegen level.
- The real proof, executed under QEMU/OVMF: writer confirms `RAMDISK CLEAN`; the real disk image
  file genuinely starts with `ARCFSB02` after the writer process fully exited; the reader,
  launched as a completely separate process, reads back the real content and attribute
  byte-for-byte.
- Negative control: a corrupted expected attribute value in a separately-built reader variant
  produces a real, non-vacuous mismatch report, not a false pass.
- Deterministic across 3 full writer+reader cycles, each against a freshly zeroed disk image.
- New smoke test `systems_arco_basic_arcfs_persist_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest` (76.67s).
- Full regression suite re-run -- see this report's own follow-up note or the RFC-0043 revision
  history for the final count; confirms the block-device dispatch migration broke nothing in any
  of the existing RAMDisk-backed ArcFS fixtures.

## A real regression found by the full suite, not by inspection

The first full regression run after Phase S/T caught a real break: 28 existing ArcFS smoke tests
failed, all in ~0.1s (a compile-time failure, not a QEMU/behavioral one). Root cause: each of
those scripts does its own "structural compile check" by concatenating LIVE `block_device_policy.
abas` + LIVE `arcfs_policy.abas` and revealing an entry point -- a pattern `systems_arco_basic_
arcfs_phase_a_smoke.sh`'s own header comment already documents as having been needed once before
(when `ArcFS.MountImage` first started depending on `RAMDisk.*`). This compiler's `reveal`
typechecks the WHOLE combined module regardless of which `--entry` is requested, and now that
`ArcFSBlockReadSectors`/`WriteSectors`/`SectorCount` reference `UefiBlockDevice.*` symbols (even
though that branch is only reached at runtime when provider 1 is selected), every one of those 28
scripts' own two-file combine was missing the THIRD file those symbols now live in
(`uefi_block_device_policy.abas`) -- "freestanding call target ... is not a declared function,"
since this backend resolves every call target at compile time with no runtime-polymorphic
dispatch. Fixed uniformly: all 28 scripts now also concatenate `uefi_block_device_policy.abas`
into their combine step, matching the shape this session's own new smoke tests
(`systems_arco_basic_arcfs_string_blob_attribute_smoke.sh`'s Phase S predecessor pattern) already
used. Confirmed the fix directly (re-revealed the combined module, re-ran the previously-failing
scripts individually) before re-running the full suite.

## Documented scope reductions

1. **`ArcFSBlockIsWritable` was not added**, despite being named in RFC-0043 Section 5.2's own
   text. `RAMDisk.IsWritable` has zero call sites anywhere in `arcfs_policy.abas` today -- nothing
   consumes it -- so adding a dispatch wrapper for it would be pure speculative generality with no
   current caller, against this project's own established discipline of only adding what a real
   call site needs. Left for whenever a real consumer (e.g. a read-only-media mount path) exists.
2. **The disk image used for the proof is small (262144 bytes / 512 sectors)**, matching the size
   every RAMDisk-backed ArcFS fixture already uses -- proving persistence itself, not capacity.
   RFC-0043 Section 7 (Allocation Bitmap multi-sector redesign, Phase U) is the concrete next step
   for volumes larger than the current ~254 KB tracked-sector ceiling.
3. **One file, one attribute.** The proof is deliberately minimal -- enough to exercise the real
   commit/mount/lookup/read/attribute path end-to-end across a real process boundary, not a stress
   test. RFC-0043 Section 9 (long-session durability, Phase W) is the separate, already-scoped
   place for many-cycle proof.

## RFC-0043 status

Phase S and Phase T are both implemented and QEMU-proven. Remaining named phases: U (Allocation
Bitmap multi-sector redesign), V (boot-path integration), W (long-session durability), X
(physical-hardware validation package, depends only on Phase T).
