# UefiBlockDevice Wired Into the FAT32 Filesystem Provider (RFC-0038 Section 6.1)

## Scope delivered

`UefiBlockDevice.*` (`.agents/reports/aps-blockio-disk-provider.md`) is now a genuine, selectable
ALTERNATIVE block device provider for `fat32_policy.abas`, alongside `RAMDisk.*` -- not a separate,
unused, parallel file. Both providers implement RFC-0038 Section 6.1's identical `BlockDevice`
contract shape (`ReadSectors(startSector AS U64, count AS U32, destination AS MMIOPTR) AS BOOL`,
`SectorCount() AS U64`), and `fat32_policy.abas` now dispatches to whichever one is selected rather
than calling `RAMDisk.*` directly.

- **`FAT32.SelectBlockDevice(provider AS U32) AS BOOL`** -- selects `RAMDisk` (0, the default) or
  `UefiBlockDevice` (1, via `FAT32.BlockDeviceProviderRAMDisk()`/`FAT32.BlockDeviceProviderUefi()`
  named constants). Rejects any other value (fail-closed, matching Requirement 6.1's own
  bounds-checking spirit). Must be called before `FAT32.Mount()` if UEFI is wanted -- `Mount`'s own
  boot-sector read is itself provider-dependent.
- **`FAT32.ActiveBlockDevice() AS U32`** -- reads the current selection back.
- **`FAT32BlockReadSectors`/`FAT32BlockSectorCount`** (internal, not part of the public `FAT32.`
  surface) -- the actual dispatch, a fixed two-way `IF`-chain on the stored selector. This is the
  SAME idiom `loop_policy.abas`'s `LoopInvokeSlot` and `volumes_policy.abas`'s own provider-tag
  dispatch already established for "pluggable behavior without a real interface value, because
  this backend has no runtime polymorphism" -- applied here, not a new pattern invented for this
  increment.
- All 6 of `fat32_policy.abas`'s own direct `RAMDisk.ReadSectors`/`RAMDisk.SectorCount` call sites
  (`Mount`, `ReadFatEntry`, `Open`, `Read`, `CountEntries`) now go through the dispatch functions
  instead.
- New fixture/smoke test (`blockio-fat32` / `systems_arco_basic_blockio_fat32_smoke`): mounts and
  reads a REAL FAT32 filesystem from a genuinely attached QEMU disk via `UefiBlockDevice`, using
  the SAME generator, SAME known file (`README.TXT`, 3333 bytes, byte-sum 211490), and SAME
  checksum the project's ORIGINAL RAMDisk-backed FAT32 proof (`aps-block-storage.abas`) already
  established as correct -- proving the read path itself is unchanged, only the block device
  underneath it is now real attached hardware instead of a RAM-preloaded region.

## Contract discipline (the specific thing this increment was asked to pay attention to)

- **Identical signatures, not assumed -- true by construction.** `RAMDisk.ReadSectors`/
  `SectorCount` and `UefiBlockDevice.ReadSectors`/`SectorCount` were written to the exact same
  RFC-0038 Section 6.1 shape from the start (`UefiBlockDevice.*`'s own report already noted this).
  The dispatch functions are therefore a clean, honest wrapper around two conformant
  implementations of the same contract, not papering over a signature mismatch.
- **Fail-closed by construction, not by a special case.** `FAT32.SelectBlockDevice` rejects
  unknown provider values outright. Selecting `UefiBlockDevice` without ever calling
  `UefiBlockDevice.Discover` first does not need its own guard in `fat32_policy.abas` at all:
  `UefiBlockDevice.SectorCount()` reads 0 from never-touched scratch state, so
  `UefiBlockDevice.ReadSectors`'s own Requirement 6.1 bounds check
  (`startSector + count > SectorCount()`) rejects every non-empty request, and `FAT32.Mount` itself
  already treats a rejected boot-sector read as "not this filesystem" and returns `FALSE`. Confirmed
  directly, not assumed: the new fixture's own negative control calls `FAT32.Mount()` with the
  DEFAULT provider (RAMDisk, never `Create`d) before ever discovering or selecting
  `UefiBlockDevice`, and it genuinely fails closed under real QEMU.
- **Zero breaking change for existing callers.** Fresh scratch memory reads as zero at boot in this
  project's own established QEMU/OVMF environment (every pre-existing `FAT32StateAddress` field
  already relies on the identical fact) -- so the new selector defaults to `RAMDisk` (0) for every
  caller that never calls `FAT32.SelectBlockDevice`, meaning every historical fixture and the
  original `aps-block-storage.abas` proof needed zero changes and were not touched.
- **`FAT32.IsWritable()` deliberately untouched.** It stays hardcoded `FALSE` regardless of which
  block device is selected -- this reflects RFC-0038's own Non-Goal (no FAT32 write support), not
  the selected block device's own writability, so it would be WRONG to make it provider-dependent.

## Verification

- `FAT32.SelectBlockDevice`/`ActiveBlockDevice`/`BlockDeviceProviderRAMDisk`/`BlockDeviceProviderUefi`
  and every pre-existing `FAT32.*` entry point compile cleanly, combined with both block device
  stdlib files.
- **Real QEMU proof** (`blockio-fat32.abas`, inlining frozen copies of `block_device_policy.abas`,
  `uefi_block_device_policy.abas`, and the updated `fat32_policy.abas`): `FAT32.Mount()` against
  the real attached disk with the DEFAULT provider genuinely fails closed (no `RAMDisk.Create` was
  ever called, matching real caller behavior for anyone who hasn't opted into hardware); real
  `UefiBlockDevice.Discover` succeeds; `FAT32.SelectBlockDevice(UEFI)` succeeds; `FAT32.Mount()`
  against the SAME real disk now succeeds; `FAT32.Open("README  TXT")` finds the real root-directory
  entry; a real multi-cluster read (7 clusters at 512 bytes/cluster, exercising genuine FAT
  chain-walking, not a single-cluster toy case) recovers exactly 3333 bytes summing to 211490,
  matching the independently-generated reference exactly; `FAT32.Close()`.
- **Negative control**: corrupting one byte inside the file's real on-disk content (at the
  precisely computed data-region offset, not a guess) makes the checksum check genuinely fail
  (`BLOCKIO FAT32 CHECKSUM BAD`), confirming the check is real, not vacuous.
- **Deterministic across 4 repeated runs** of the positive case.
- New smoke test passes through `ctest`; full regression suite re-run clean alongside it,
  including every pre-existing FAT32/RAMDisk-touching test (none needed changes, confirming the
  "frozen fixture" convention held and the live-stdlib edit's blast radius was exactly the files
  touched).

## Documented scope reductions

1. **No true runtime polymorphism** -- inherited, unavoidable on this backend, and explicitly the
   SAME deviation this project has made repeatedly (`RAMDisk.Create`, `Loop.RegisterHook`,
   `Volumes.Resolve`'s own tag dispatch). Adding a third provider in the future means adding a
   third `IF` branch to the two dispatch functions, not a structural rewrite.
2. **The selector is a single, global, "one instance" flag**, matching every other piece of state
   in this file (one mounted filesystem, one open file at a time). Cannot mount two FAT32 volumes
   simultaneously on two different block devices.
3. **`Volumes.Mount`/`Resolve` (volumes_policy.abas) are untouched and out of scope here.** They
   deal only with mount-point-to-provider-TAG resolution (which `FilesystemProvider` a path routes
   to), a layer entirely above and independent of which `BlockDevice` `FAT32.*` itself reads
   through -- there was nothing there that needed touching for this increment.
4. **`UefiBlockDevice.Discover`'s own pre-existing scope reductions still apply unchanged**
   (`BlockSize` must be exactly 512, single-handle `LocateProtocol` discovery, no multi-handle
   enumeration) -- see `.agents/reports/aps-blockio-disk-provider.md`.

## Remaining activation gate

RFC-0038's own `BlockDevice` contract now has two real, interchangeable implementations reachable
from its one real consumer (`fat32_policy.abas`), selected explicitly rather than hardcoded. A
future caller wanting the UEFI-backed path in a real boot sequence (as opposed to a test fixture)
needs only: `UefiBlockDevice.Discover(systemTable)`, `FAT32.SelectBlockDevice(FAT32.BlockDeviceProviderUefi())`,
then `FAT32.Mount()` -- no other code path changes. `volumes_policy.abas` remains provider-agnostic
and needs no changes to support this.
