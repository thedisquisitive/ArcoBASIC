# UEFI Block I/O Protocol Discovery: Compiler-Level Binding (RFC-0038 Section 17.5)

## Scope delivered

RFC-0038 Section 17.5 names, verbatim, "discovering that the boot-media RAM-disk-population step
needs UEFI protocol surface this project's frontend does not yet bind" as its own explicit stop
condition, and directs that `uefi-bindings.md` be extended "following that document's own
conventions rather than improvising a new binding style." This closes that stop condition at the
compiler-binding level.

- **`EFI_BLOCK_IO_PROTOCOL` and `EFI_BLOCK_IO_MEDIA` are now bound**
  (`arcology-os/include/arco/uefi_bindings.hpp`), field offsets and the protocol GUID
  hand-derived from `MdePkg/Include/Protocol/BlockIo.h` (TianoCore edk2, fetched directly), the
  same primary-source discipline every prior binding in this file follows.
- **`UEFI.BLOCKIO.DISCOVER(systemTable)`**, a new hand-rolled intrinsic mirroring
  `UEFI.GOP.DISCOVER` exactly (`src/frontend/parser.cpp` recognition, `src/compiler/fission.cpp`
  type resolution/AMIR-building/x86-64 codegen): materializes the real
  `EFI_BLOCK_IO_PROTOCOL_GUID` as two stack-scratch `U64` halves, calls `LocateProtocol` via
  `BootServices+0x140`, and returns a null typed value on failure. The GUID-to-`U64`-pair
  conversion was cross-verified against the already-proven GOP GUID's own conversion before being
  trusted for this new GUID.
- **Plain Media field accessors** (`UEFI.BLOCKIO.MediaId`/`MediaFlags`/`BlockSize`/`LastBlock`),
  hand-rolled the same way `UEFI.GOP.WIDTH`/`HEIGHT`/etc. are, since the generic `CallExternal`
  path only resolves chains ending in a method call, never a plain data field.
- **`ReadBlocks`/`WriteBlocks`/`Reset`/`FlushBlocks` need zero new calling-convention codegen.**
  These are real protocol methods with implicit `This`, registered in the `UEFI.BlockIoProtocol`
  `UefiType` entry exactly like `UEFI.SimpleTextOutputProtocol.Write` is, and reachable through the
  **already-generic, already-proven** `CallExternal` mechanism `systemTable.BootServices.GetMemoryMap(...)`
  already exercises (including its own stack-argument handling for a 5th argument) -- confirmed by
  inspecting the generated x86-64 directly: a `blockIo AS UEFI.BlockIoProtocol` function parameter
  calling `blockIo.ReadBlocks(...)` lowers to `call [r11+0x18]`, the exact registered `ReadBlocks`
  offset, resolved through the ordinary parameter-rooted field-chain path with no special-case code
  written for this binding at all.

## Verification

Machine-code-level, not just clean-compile-level, per this project's own established rigor:

- `ArcoFission reveal ... at AST` confirms `UEFI.BLOCKIO.Discover`/`MediaId`/`MediaFlags`/
  `BlockSize`/`LastBlock` all classify as `MemoryOperation` (the intrinsic path), and
  `blockIo.ReadBlocks(...)` classifies as an ordinary `MethodCall` resolved through
  `CallExternal`.
- `ArcoFission reveal ... at A-MIR` confirms `UEFI.BLOCKIO.DISCOVER`, the `MEMORY.BLOCKIO*` field
  ops, and `CALL_EXTERNAL blockIo.ReadBlocks` all appear with the correct typed operands.
- `ArcoFission reveal ... at X86_64` confirms, byte-for-byte, in the actual generated machine code:
  - GUID low half `48 ba 21 5b 4e 96 59 64 d2 11` (`mov rdx, 0x11D26459964E5B21`)
  - GUID high half `48 ba 8e 39 00 a0 c9 69 72 3b` (`mov rdx, 0x3B7269C9A000398E`)
  - `LocateProtocol` call `41 ff 93 40 01 00 00` (`call [r11+0x140]`)
  - Media field loads at the correct offsets (`8b 40 00` MediaId, `8b 40 04` MediaFlags, `8b 40 0c`
    BlockSize, `48 8b 40 18` LastBlock, all off the resolved `Media` pointer at `blockIo+0x08`)
  - `ReadBlocks` call `41 ff 53 18` (`call [r11+0x18]`) inside a helper function taking `blockIo`
    as its own parameter, proving `CallExternal` resolved the new registry entry correctly.
- New smoke test `systems_blockio_discovery_smoke` (registered in `arcology-os/cmake/Testing.cmake`)
  asserts all of the above automatically, at the same tier `systems_gop_discovery_smoke` already
  established for GOP. Fixture: `arcology-os/tests/fixtures/blockio-discovery/blockio-discovery.abas`.
- Full compiler build (`arco_runtime`, `arco_compiler`, `ArcoFission`, `arco_cli`, `arcosh`, and all
  test binaries) succeeds cleanly with these changes.
- Full `ctest` regression suite re-run; no other test's behavior changed.

## Documented scope reductions

1. **`MediaFlags` returns a packed word, not four separate bit accessors.** The x86-64 encoder has
   no displacement-addressed single-byte load primitive (`mov_load8_rax()` only loads from `[RAX]`
   with zero displacement). Adding one to shared encoder infrastructure for this one caller was
   judged a real risk (new primitive, new test surface) for no real gain, since the existing
   32-bit displacement load -- already proven correct by `UEFI.GOP.WIDTH` -- can read the whole
   packed `RemovableMedia`/`MediaPresent`/`LogicalPartition`/`ReadOnly` word in one instruction.
   Callers extract the specific byte they want with an ordinary `SHR`/`AND` at the ArcoBASIC level
   (both already-proven operators). Named explicitly in `uefi-bindings.md` rather than left silent.
2. **`IoAlign`, `LowestAlignedLba`, `LogicalBlocksPerPhysicalBlock`, and
   `OptimalTransferLengthGranularity` are not bound.** Not needed by the reference
   `ReadSectors`/`WriteSectors` provider this binding exists to eventually support.
3. **No multi-handle enumeration.** `UEFI.BLOCKIO.DISCOVER` calls `LocateProtocol`, which returns
   only the first matching handle firmware offers -- matching `UEFI.GOP.DISCOVER`'s own established
   scope exactly, not a new limitation introduced here.
4. **This closes the compiler-binding gap only, not the whole stop condition's spirit.** A genuine
   ArcoBASIC `BlockDevice`-shaped stdlib provider backed by real discovered hardware, and a QEMU
   harness variant that attaches a real emulated disk (virtio-blk/AHCI via `-drive`) instead of the
   existing RAM-preload trick, are both explicitly **not** part of this increment. A real
   architectural wrinkle surfaced while scoping that follow-on work: `RAMDisk.*`'s own
   "concrete functions, fully hidden singleton state" pattern (documented in
   `block_device_policy.abas`'s own header comment) cannot be copied directly for a UEFI-backed
   provider, because `CallExternal`'s field-chain validation and codegen only resolve a method call
   whose receiver is a genuine function *parameter* of a bound UEFI type -- a `blockIo` pointer
   loaded back out of hidden scratch state as a plain local variable cannot have `.ReadBlocks(...)`
   called on it. A real provider will need to thread the typed `blockIo` value through as an
   explicit parameter at each call site that touches the protocol, rather than hiding it the way
   `RAMDisk.SectorCount()` hides its own state. This is a genuine, load-bearing design constraint
   for that follow-on work, not a defect in this binding -- named here so it is not silently
   rediscovered later.

## Remaining activation gate

RFC-0038's own Section 17.5 stop condition is closed at the binding level; the RFC's `Future
Extensions` list (Section 18) still names "Real block-device drivers: AHCI, NVMe, virtio-blk" as
open work, and that remains open -- this increment provides the compiler-level surface such a
driver would need, not the driver itself. The concrete next increment is the stdlib provider plus
the real-disk QEMU harness described above.
