# Hardware-Backed BlockDevice Provider and Real-Disk QEMU Harness (RFC-0038 Section 18)

## Scope delivered

The follow-on to `.agents/reports/aps-blockio-discovery-binding.md` (which closed RFC-0038
Section 17.5's compiler-binding stop condition). This delivers the two things that report named
as explicitly deferred: a genuine `BlockDevice`-shaped ArcoBASIC stdlib provider backed by real
discovered hardware, and a QEMU harness variant that attaches a real emulated disk instead of the
RAM-preload trick `RAMDisk.*` uses.

- **`arcology-os/scripts/build/build-blockio-test-disk.py`**: builds a small (128-sector, 64 KiB),
  byte-reproducible raw disk image with a known magic pattern at sector 5 and an intentionally
  untouched sector 10 for a write+read-back proof.
- **`arcology-os/scripts/run/run-uefi-hello-with-blockio-disk.sh`**: a new QEMU/OVMF harness
  variant attaching TWO real, distinct virtio-blk-pci disks -- the disk under test and the boot
  media itself (also a real `EFI_BLOCK_IO_PROTOCOL` handle once OVMF enumerates it). Two real,
  empirically-discovered requirements, neither assumed:
  - `-nodefaults` is required. Without it, QEMU's default "pc" machine type auto-adds an empty
    IDE CD-ROM drive that ALSO exposes a real `EFI_BLOCK_IO_PROTOCOL` handle (`BlockSize=2048`,
    `MediaPresent=FALSE`) -- and that handle is what `LocateProtocol` returns FIRST, ahead of
    either explicit drive, defeating discovery entirely. Confirmed directly: the first real run
    without `-nodefaults` reported `BLOCKSIZE=2048 LASTBLOCK=0`, the unmistakable signature of an
    empty optical drive, not either intended disk.
  - Explicit PCI `addr=` on both real drives, disk-under-test lower than boot media. Empirically
    confirmed (not assumed) that OVMF's PCI bus walk binds drivers/installs BlockIo handles in
    ascending PCI address order, and `UEFI.BLOCKIO.DISCOVER`'s single `LocateProtocol` call
    returns the first handle in that resulting order.
- **`arcology-os/stdlib/uefi_block_device_policy.abas`**: `UefiBlockDevice.*`, matching RFC-0038
  Section 6.1's `BlockDevice` contract shape (`SectorSize`/`SectorCount`/`IsWritable`/
  `ReadSectors`/`WriteSectors`), genuinely backed by a real discovered `EFI_BLOCK_IO_PROTOCOL`
  device -- `UefiBlockDevice.Discover(systemTable)` performs real discovery, geometry validation,
  and persists identity/geometry to fixed scratch state; every other function reads that state.
- Two new fixtures/smoke tests, both real QEMU proofs (not structural-only):
  - `arcology-os/tests/fixtures/blockio-disk-discovery/` /
    `systems_arco_basic_blockio_disk_smoke` -- proves the raw `UEFI.BLOCKIO.*` intrinsics directly
    against real hardware (discovery, field reads, `ReadBlocks`, `WriteBlocks`).
  - `arcology-os/tests/fixtures/blockio-provider/` / `systems_arco_basic_blockio_provider_smoke`
    -- proves the `UefiBlockDevice.*` stdlib provider itself, inlining a frozen copy of
    `uefi_block_device_policy.abas` per this project's own established fixture convention.

## A real design-constraint correction, found while building the provider

The prior report (`aps-blockio-discovery-binding.md`) stated that `RAMDisk.*`'s "concrete
functions, fully hidden singleton state" pattern could not be copied for a UEFI-backed provider,
because `CallExternal`'s field-chain codegen "only resolves a method call whose receiver is a
genuine function parameter of a bound UEFI type." **This was an incomplete reading of the actual
resolution code, corrected here before it shaped the provider's design incorrectly.**

Reading `src/compiler/fission.cpp`'s `CallExternal` classification directly (not re-guessing from
the earlier summary) shows the real rule: a method call routes through `CallExternal` when its
receiver is EITHER a genuine function parameter OR a local variable whose type is tracked in the
per-function `types_` map -- and that map is populated by every typed `LET`, not only parameter
declarations, using the declaration's own explicit `AS Type` annotation in preference to inferring
a type from the right-hand side. This means a raw pointer reloaded from memory and re-declared
with an explicit `AS UEFI.BlockIoProtocol` annotation resolves identically to a genuine parameter.

Confirmed directly (not assumed) before relying on it: a minimal standalone test
(`LET blockIo AS UEFI.BlockIoProtocol = MEMORY.Read64(addr)` followed by `blockIo.ReadBlocks(...)`
in the SAME function) produces identical A-MIR (`CALL_EXTERNAL blockIo.ReadBlocks ...`) and
identical machine code (`call [r11+0x18]`, the exact registered offset) as the genuine-parameter
case already proven in the prior report. This is what makes `UefiBlockDevice.*`'s design possible
at all: the discovered protocol pointer is persisted as a plain `U64` in fixed scratch state
(alongside `mediaId`/`sectorCount`/writability, exactly like `RAMDisk.*`'s own state) and reloaded
+ re-annotated in `ReadSectors`/`WriteSectors`, with no need to thread it through as an explicit
parameter at every call site as the earlier report assumed would be necessary.

## Verification

- `UefiBlockDevice.*`'s all 7 entry points compile cleanly at X86_64 level individually.
- `ReadBlocks`/`WriteBlocks` calls confirmed byte-for-byte correct in generated machine code:
  `call [r11+0x18]` (ReadSectors) and `call [r11+0x20]` (WriteSectors), the exact registered
  offsets, exactly as proven for the raw intrinsic in the prior report.
- **Real QEMU proof, `systems_arco_basic_blockio_disk_smoke`** (raw intrinsics): genuine discovery
  against the real attached disk reports `MEDIAID=0 FLAGS=256 BLOCKSIZE=512 LASTBLOCK=127` --
  exactly matching the real 128-sector test image, not a guess or a RAM-preload placeholder; a
  real `ReadBlocks` call recovers the sector-5 magic pattern; a real `WriteBlocks` + fresh
  `ReadBlocks` round trip on sector 10 matches exactly.
- **Real QEMU proof, `systems_arco_basic_blockio_provider_smoke`** (the stdlib provider itself):
  `UefiBlockDevice.Discover` succeeds; `SectorSize=512 SectorCount=128 Writable=1` (real, not
  hardcoded); Requirement 6.1's out-of-bounds rejection (`ReadSectors(127, 5, ...)` against a
  128-sector device) is genuinely rejected; a real read recovers the sector-5 magic pattern
  through the provider (not the raw intrinsic); a real write+read-back round trip at sector 10
  matches through the provider.
- **Negative controls, both fixtures**: a corrupted test disk (sector 5's magic bytes overwritten)
  is confirmed to make BOTH fixtures genuinely report `NOMATCH` rather than vacuously passing
  either way -- proving the checks are real, not decorative.
- **Deterministic across 3 repeated runs**, both fixtures.
- Both new smoke tests pass through `ctest`; full regression suite re-run clean alongside them.

## Documented scope reductions

1. **Same two `RAMDisk.*`-inherited deviations**: concrete functions instead of an abstract
   `BlockDevice` interface value (no runtime polymorphism on this backend); one instance / fixed
   scratch state (no support for multiple simultaneously-active block devices).
2. **A NEW deviation this provider specifically required**: the discovered protocol pointer must
   be reloaded and re-annotated with an explicit type in every function that calls a real protocol
   method -- it cannot simply be treated as an ordinary persisted value the way `mediaId` can. See
   the design-constraint correction above.
3. **`UefiBlockDevice.Discover` requires the discovered media's `BlockSize` to be exactly 512**,
   returning `FALSE` (a fail-closed, not partially-usable, provider) otherwise. A future increment
   supporting other sector sizes would need `ReadSectors`/`WriteSectors` to scale their own
   `bufferSize`/bounds arithmetic by the ACTUAL discovered `BlockSize`, not the literal `512` this
   increment hardcodes.
4. **No multi-handle enumeration/selection**, inherited unchanged from the discovery binding
   itself (`UEFI.BLOCKIO.DISCOVER` is a single `LocateProtocol` call). A caller needing a specific
   device among several needs a future, separate enumeration binding.
5. **The QEMU harness's own PCI-address-ordering trick is empirical, not a spec guarantee.** It is
   confirmed correct and deterministic for this project's own OVMF/QEMU pairing (the whole basis
   this project's testing discipline rests on throughout), not a portable guarantee about every
   possible firmware's handle-enumeration order.
6. **Not wired into `fat32_policy.abas`/`volumes_policy.abas`.** `RAMDisk.*` remains the block
   device those modules call directly (same "no runtime polymorphism" reason `RAMDisk.*`'s own
   header already documents) -- `UefiBlockDevice.*` is a parallel, equally-real provider, not (yet)
   a drop-in replacement wired into the existing FAT32/Volumes call sites. Swapping it in would
   need either a second FAT32/Volumes call path or the same kind of "no interface abstraction on
   this backend" workaround RAMDisk itself already lives with -- left as a genuinely separate,
   explicitly out-of-scope follow-on.

## Remaining activation gate

RFC-0038's Section 18 "Real block-device drivers: AHCI, NVMe, virtio-blk" item can now be marked
addressed for virtio-blk specifically -- discovery, read, and write are all genuinely proven
against real emulated hardware, both directly and through a real stdlib provider matching the
RFC's own contract shape. AHCI/NVMe remain unaddressed (would need their own protocol-independent
discovery is already covered by this same binding -- `EFI_BLOCK_IO_PROTOCOL` does not differ by
underlying transport, so no NEW compiler binding work is expected there, only a QEMU harness
variant using `-device ahci`/`-device nvme` instead of `virtio-blk-pci`, likely hitting the same
"`-nodefaults` + explicit PCI ordering" empirical requirements this report already found). Wiring
`UefiBlockDevice.*` into the FAT32/Volumes call path remains the concrete next increment if a real
boot-time FAT32-over-real-hardware proof (as opposed to today's RAM-preloaded proof) is wanted.
