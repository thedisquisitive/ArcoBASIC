# UEFI Bindings

Status: WP-006 (UEFI Bindings)
Depends on: `arcology-os/docs/systems/uefi-target.md`, `arcology-os/docs/systems/calling-conventions.md`

This document is the verification trail for `include/arco/uefi_bindings.hpp`. Packet section 9
requires ABI details to be verified from primary technical specifications rather than guessed;
this document records exactly what was verified, against what, and how each byte offset was
derived, so a future agent (or a human) can re-check it without re-deriving everything from
scratch.

Per Packet WP-006, this binds **only the smallest surface needed for text output** -- it does not
attempt to bind the rest of the UEFI specification. Any UEFI field not listed below is not bound in
this milestone; ArcoBASIC source that references it is rejected at compile time with a diagnostic
naming what actually is bound (`src/frontend/parser.cpp::Parser::validate_uefi_field_chain`), not silently
accepted or miscompiled.

## Sources

Fetched directly from the TianoCore EDK2 repository (`https://github.com/tianocore/edk2`), the
reference implementation of the UEFI Specification that essentially all real UEFI firmware and
bootloader development is built against:

- `MdePkg/Include/Uefi/UefiSpec.h` -- `EFI_SYSTEM_TABLE` field list
- `MdePkg/Include/Uefi/UefiMultiPhase.h` -- `EFI_TABLE_HEADER` field list
- `MdePkg/Include/Protocol/SimpleTextOut.h` -- `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` field list and the
  `EFI_TEXT_STRING` (`OutputString`) function pointer signature
- `MdePkg/Include/Base.h` -- base type sizes on a 64-bit (X64) target

Cross-checked against the public UEFI Specification's own field-order description (search results
summarizing `https://uefi.org/specs/UEFI/2.10/04_EFI_System_Table.html` and
`https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html`; direct fetch of uefi.org
returned HTTP 403 in this environment, so the edk2 reference headers -- which implement the same
spec verbatim -- were used as the primary source instead).

## Byte-Offset Derivation

All UEFI structures use natural C alignment on x86-64 (8-byte pointers, no `#pragma pack`). Offsets
below are computed field-by-field from the verified type list, not taken from a secondary source
that might already have made an arithmetic error.

### `EFI_TABLE_HEADER` (24 bytes)

| Field | Type | Size | Offset |
|---|---|---|---|
| `Signature` | `UINT64` | 8 | 0 |
| `Revision` | `UINT32` | 4 | 8 |
| `HeaderSize` | `UINT32` | 4 | 12 |
| `CRC32` | `UINT32` | 4 | 16 |
| `Reserved` | `UINT32` | 4 | 20 |

Total: 24 bytes (already 8-byte aligned; no trailing padding needed).

### `EFI_SYSTEM_TABLE` (120 bytes) -> ArcoBASIC `UEFI.SystemTable`

| Field | Type | Size | Offset |
|---|---|---|---|
| `Hdr` | `EFI_TABLE_HEADER` | 24 | 0 |
| `FirmwareVendor` | `CHAR16*` | 8 | 24 |
| `FirmwareRevision` | `UINT32` | 4 | 32 |
| *(padding)* | -- | 4 | 36 |
| `ConsoleInHandle` | `EFI_HANDLE` | 8 | 40 |
| `ConIn` | `EFI_SIMPLE_TEXT_INPUT_PROTOCOL*` | 8 | 48 |
| `ConsoleOutHandle` | `EFI_HANDLE` | 8 | 56 |
| **`ConOut`** | **`EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*`** | **8** | **64 (0x40)** |
| `StandardErrorHandle` | `EFI_HANDLE` | 8 | 72 |
| `StdErr` | `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*` | 8 | 80 |
| `RuntimeServices` | `EFI_RUNTIME_SERVICES*` | 8 | 88 |
| `BootServices` | `EFI_BOOT_SERVICES*` | 8 | 96 |
| `NumberOfTableEntries` | `UINTN` | 8 | 104 |
| `ConfigurationTable` | `EFI_CONFIGURATION_TABLE*` | 8 | 112 |

`FirmwareRevision` (a `UINT32`) leaves a 4-byte gap before `ConsoleInHandle` (a pointer, requiring
8-byte alignment) -- this padding is a real, necessary part of the layout, not an omission.

**Bound:** `ConOut` at offset `0x40`, exposed as `UEFI.SystemTable.ConsoleOut`, and `BootServices`
at offset `0x60`, exposed as `UEFI.SystemTable.BootServices`. The latter exists solely for Packet
002's watchdog-disable call before an intentional permanent halt.

### `EFI_BOOT_SERVICES` (376 bytes) -> ArcoBASIC `UEFI.BootServices`

`SetWatchdogTimer` is the 30th function pointer after the 24-byte table header, so its x86-64 byte
offset is `24 + 29 * 8 = 256 (0x100)`. Its signature has four explicit parameters (`Timeout`,
`WatchdogCode`, `DataSize`, `WatchdogData`) and, unlike a protocol method, no implicit `This`
argument. The hardware test calls `SetWatchdogTimer(0, 0, 0, 0)` to cancel the watchdog.

### `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` (80 bytes) -> ArcoBASIC `UEFI.SimpleTextOutputProtocol`

| Field | Type | Size | Offset |
|---|---|---|---|
| `Reset` | `EFI_TEXT_RESET` (fn ptr) | 8 | 0 |
| **`OutputString`** | **`EFI_TEXT_STRING` (fn ptr)** | **8** | **8 (0x08)** |
| `TestString` | `EFI_TEXT_TEST_STRING` (fn ptr) | 8 | 16 |
| `QueryMode` | `EFI_TEXT_QUERY_MODE` (fn ptr) | 8 | 24 |
| `SetMode` | `EFI_TEXT_SET_MODE` (fn ptr) | 8 | 32 |
| `SetAttribute` | `EFI_TEXT_SET_ATTRIBUTE` (fn ptr) | 8 | 40 |
| `ClearScreen` | `EFI_TEXT_CLEAR_SCREEN` (fn ptr) | 8 | 48 |
| `SetCursorPosition` | `EFI_TEXT_SET_CURSOR_POSITION` (fn ptr) | 8 | 56 |
| `EnableCursor` | `EFI_TEXT_ENABLE_CURSOR` (fn ptr) | 8 | 64 |
| `Mode` | `SIMPLE_TEXT_OUTPUT_MODE*` | 8 | 72 |

**Bound in this milestone:** `OutputString` only, at offset `0x08`, exposed to ArcoBASIC as
`UEFI.SimpleTextOutputProtocol.Write`.

## `OutputString` Signature and the Implicit `This` Argument

Verified prototype (`EFI_TEXT_STRING` in `SimpleTextOut.h`):

```c
typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_STRING)(
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
  IN CHAR16 *String
);
```

This takes **two** arguments in the real C ABI: `This` (the protocol pointer itself -- the same
value `UEFI.SystemTable.ConsoleOut` resolves to) and `String` (a UTF-16 string pointer). ArcoBASIC
source calls this as `systemTable.ConsoleOut.Write("Hello from ArcoBASIC")` with only **one**
explicit argument. `This` is implicit, matching the method-call convention every other ArcoBASIC
call already uses (`SELF` is passed implicitly to class methods).

`UefiField::implicit_this_argument` in `include/arco/uefi_bindings.hpp` records this fact for
`Write`. **This work package does not yet act on it** -- A-MIR's `CallExternal` instruction for
`systemTable.ConsoleOut.Write("...")` still shows exactly one argument (the string), matching what
WP-004 already produces. Injecting the implicit `This` pointer as a real leading argument is left
for WP-008, when a call actually needs to be lowered to a real x86-64 `CALL` instruction with a
real argument list built via `include/arco/calling_convention.hpp::assign_argument_locations`.
Recorded here explicitly so it is a known, flagged gap rather than a silent one.

`String` is a `CHAR16*` (UTF-16, null-terminated) in the real ABI; ArcoBASIC source currently passes
a plain (non-UTF-16) string literal. Producing a real UTF-16 constant is WP-007's job
(`arcology-os/docs/systems/uefi-target.md` roadmap section 11).

## `EFI_STATUS` and `EFI_HANDLE`

Both are simple typedefs, not structures with fields, so neither needs a `UefiType` registry entry
with bound fields:

- `EFI_HANDLE` is `VOID*` -- an opaque pointer. `UEFI.Handle` is registered in
  `include/arco/uefi_bindings.hpp` with zero fields, so any attempt to access a field on a
  `UEFI.Handle`-typed value is correctly rejected (verified in
  `arcology-os/tests/systems/systems_uefi_bindings_smoke.sh`).
- `EFI_STATUS` is `UINTN` (8 bytes on x86-64), with `EFI_SUCCESS = 0` and error codes having the
  high bit set. This is already captured by `arcology-os/docs/systems/uefi-target.md` section 5's mapping of
  the entry point's `AS U64` return type to `EFI_STATUS`/`RAX`; no separate binding entry is needed.

## Compile-Time Field Resolution

`Parser::validate_uefi_field_chain` (`src/frontend/parser.cpp`) walks a dotted call chain
(`systemTable.ConsoleOut.Write`) against this registry whenever the chain's root identifier is a
declared function parameter with a known `UEFI.*` type (tracked per-function via
`current_function_parameter_types_`, populated in `Parser::function_statement`). Each segment must
resolve to a bound field; the first segment that does not produces a diagnostic naming the type it
failed to resolve against and every field that *is* bound, for example:

```text
UEFI.SystemTable has no bound field or method "ConIn" in this milestone. Bound fields: ConsoleOut.
See arcology-os/docs/systems/uefi-bindings.md.
```

This satisfies Packet WP-006's acceptance criterion ("the compiler can type-check the hello-world
source and resolve the required UEFI fields") without requiring a general type-inference system:
the check only activates for identifiers whose declared parameter type is a recognized UEFI type,
and is a no-op for every other type name (ordinary hosted types, class instances, user-defined
types), so it cannot produce false positives outside the systems surface it is scoped to.

## What This Work Package Does Not Do

- Does not bind `ConIn`, `StdErr`, `RuntimeServices`, or any other `EFI_SYSTEM_TABLE` field beyond
  `ConOut` and the narrow `BootServices` chain.
- Does not bind any other `EFI_BOOT_SERVICES` operation beyond `SetWatchdogTimer`.
- Does not bind `Reset`, `TestString`, `QueryMode`, `SetMode`, `SetAttribute`, `ClearScreen`,
  `SetCursorPosition`, `EnableCursor`, or `Mode` on `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` beyond
  `OutputString`.
- Does not inject the implicit `This` argument into A-MIR (see above -- WP-008).
- Does not produce UTF-16 constants (WP-007).
- The compile-time DIAGNOSTIC check (`Parser::validate_uefi_field_chain`, giving a named-field
  error at the call site) only fires for chains rooted at a declared function parameter --
  `current_function_parameter_types_` tracks parameters only, not arbitrary locals.
  **Correction, found while building the Block I/O hardware provider
  (`.agents/reports/aps-blockio-disk-provider.md`): this does NOT mean actual CODEGEN requires a
  parameter.** `CallExternal`'s own classification in `src/compiler/fission.cpp` also accepts a
  local variable whose declared type (tracked per-function in a separate `types_` map, populated
  by every typed `LET`, not only parameters) starts with `"UEFI."` -- a local reloaded from memory
  and re-declared with an explicit `AS UEFI.SomeType` annotation resolves and lowers identically
  to a genuine parameter, confirmed directly in generated machine code
  (`stdlib/uefi_block_device_policy.abas` relies on exactly this). The practical effect of the
  narrower parser-level check is only that such a call skips the nice named-field pre-flight
  diagnostic -- an unbound field on a locally-retyped chain still fails, just later, at
  `CallExternal` codegen (`"external call field ... is not bound on ..."`), not at parse time.

The binding registry also contains minimal GOP mode metadata for the framebuffer phase:
`UEFI.GraphicsOutputProtocol.Mode`, `UEFI.GraphicsOutputMode.FrameBufferBase` (`PHYSICALPTR`),
`FrameBufferSize`, and the resolution, stride, and pixel-format fields of
`UEFI.GraphicsOutputModeInformation`. Protocol discovery and a complete GOP wrapper remain deferred.
`UEFI.BootServices.LocateProtocol` is now recorded at offset `0x140` (the verified table index for
`EFI_BOOT_SERVICES.LocateProtocol`). Its raw GUID/output-pointer ABI is intentionally not wrapped by
the source language directly. `UEFI.GOP.Discover(systemTable)` is the narrow typed compiler intrinsic:
it materializes the standard GOP GUID and an internal output-pointer temporary, invokes
`LocateProtocol`, checks its `EFI_STATUS`, and returns a null `UEFI.GraphicsOutputProtocol` value on
failure. General pointer-to-pointer syntax is not exposed.
The same narrow namespace exposes `FrameBufferBase`, `FrameBufferSize`, `Width`, `Height`,
`PixelsPerScanLine`, and `PixelFormat` accessors on a discovered GOP value; these lower to the
verified mode-structure offsets and preserve the address-domain types.

`UEFI.BootServices.ExitBootServices(imageHandle, mapKey)` is also bound at table offset `0xE8`.
This is currently a binding/lowering surface only: a correct handoff still requires a valid memory
map key, which will be supplied by the future `GetMemoryMap` bootstrap layer.

The first source-level wrapper now lives in `stdlib/uefi_bootstrap.abas` as
`AcquireMemoryMap(systemTable)`. It probes the map, allocates boot-services data with headroom, and
returns the acquired key. It is a bootstrap primitive and does not yet perform the final handoff.
The same module now exposes `ExitBootServicesSafe(imageHandle, systemTable)`, which performs the
acquisition and immediate handoff sequence. It must only be used when a post-handoff runtime is
ready to take ownership; the checked-in fixture is a PE32+ proof and is not an unattended boot test.
The acquisition path retries failed final map reads up to three times with additional buffer
headroom and refuses the handoff when retries are exhausted.
It also validates the returned descriptor size/map extent and frees the allocated buffer on every
pre-handoff failure path.
`stdlib/uefi_memory_manager.abas` provides the first post-handoff map walker, counting conventional
memory pages from the retained descriptor buffer using typed `VIRTUALPTR` reads.
It now also finds a conventional region and performs checked 4 KiB page-region allocation in
ArcoBASIC, forming the initial post-handoff allocator policy.
The same source module exposes checked allocation-index advancement and page-range validation for
future free/coalescing management.
RFC-0018 now formalizes the transition contract, and `stdlib/physical_region_database.abas` adds
source-level region-end overflow checks, page alignment validation, and overlap rejection before
firmware descriptors become allocator input.
RFC-0019 extends this with reservation eligibility, split validity, and adjacent-free coalescing
predicates in `stdlib/physical_region_allocator.abas`; persistent region records remain the next
language-storage phase.
That storage phase has begun in `stdlib/physical_region_database_runtime.abas`: a caller-owned
64-byte record buffer now persists base/pages/state/owner/provider/reason/flags and exposes count,
insert, and state queries without consulting firmware descriptors.
The buffer now also drives ArcoBASIC reservation, first-fit split allocation, release, and adjacent
free-region coalescing operations.
RFC-0020/FMAP-0001 now establish the virtual-memory layer; `stdlib/virtual_region_manager.abas`
provides the first persistent virtual-region record and map/protect/unmap policy. Physical PRD state
remains authoritative for backing allocation.

The prerequisite raw service entries are now recorded as well: `GetMemoryMap` at `0x38`,
`AllocatePool` at `0x40`, and `FreePool` at `0x48`. These accept the firmware ABI's pointer-shaped
arguments, but a typed ArcoBASIC memory-map wrapper has not yet been claimed; callers must not pass
placeholder zeros on real hardware.

## `EFI_BLOCK_IO_PROTOCOL` -- closing RFC-0038 Section 17.5's own named stop condition

RFC-0038 (APS Block Storage and Filesystem Provider Substrate) Section 17.5 names, verbatim,
"discovering that the boot-media RAM-disk-population step needs UEFI protocol surface this
project's frontend does not yet bind" as its own explicit stop condition, and directs that this
document ("the source of truth for what's currently bound") be extended "following that document's
own conventions rather than improvising a new binding style." This section is that extension.

### Source

`MdePkg/Include/Protocol/BlockIo.h` (TianoCore edk2, fetched directly, the same primary-source
discipline every other binding above follows), plus `MdePkg/Include/Uefi/UefiBaseType.h` for
`EFI_LBA` (`UINT64`) and `MdePkg/Include/X64/ProcessorBind.h` for `BOOLEAN` (1 byte) and `UINTN`
(`UINT64` on X64).

### `EFI_BLOCK_IO_PROTOCOL_GUID`

```c
#define EFI_BLOCK_IO_PROTOCOL_GUID \
  {0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
```

Converted to the two little-endian `U64` halves the `UEFI.BLOCKIO.DISCOVER` intrinsic materializes
on the stack before calling `LocateProtocol`, using the identical byte-layout convention already
proven correct for the GOP GUID (`data1` LE + `data2` LE + `data3` LE, concatenated with `data4`'s
raw bytes, then split into two 8-byte little-endian words):

- Low `U64` (bytes 0-7): `0x11D26459964E5B21`
- High `U64` (bytes 8-15): `0x3B7269C9A000398E`

Confirmed directly in the generated machine code
(`ArcoFission reveal ... at X86_64`): `48 ba 21 5b 4e 96 59 64 d2 11` (`mov rdx,
0x11D26459964E5B21`) immediately followed by `48 ba 8e 39 00 a0 c9 69 72 3b` (`mov rdx,
0x3B7269C9A000398E`), then `41 ff 93 40 01 00 00` (`call [r11+0x140]`, `LocateProtocol`) -- the same
instruction shape the GOP smoke test already checks for its own GUID.

### `EFI_BLOCK_IO_PROTOCOL` (48 bytes) -> ArcoBASIC `UEFI.BlockIoProtocol`

| Field | Type | Size | Offset |
|---|---|---|---|
| `Revision` | `UINT64` | 8 | 0 |
| `Media` | `EFI_BLOCK_IO_MEDIA*` | 8 | 8 (0x08) |
| **`Reset`** | `EFI_BLOCK_RESET` (fn ptr) | 8 | 16 (0x10) |
| **`ReadBlocks`** | `EFI_BLOCK_READ` (fn ptr) | 8 | 24 (0x18) |
| **`WriteBlocks`** | `EFI_BLOCK_WRITE` (fn ptr) | 8 | 32 (0x20) |
| **`FlushBlocks`** | `EFI_BLOCK_FLUSH` (fn ptr) | 8 | 40 (0x28) |

`Reset`, `ReadBlocks`, `WriteBlocks`, and `FlushBlocks` are real protocol methods (implicit `This`,
verified `EFIAPI` signatures below) and are registered as `is_method = true` fields in
`UEFI.BlockIoProtocol`, bound the same way `UEFI.SimpleTextOutputProtocol.Write` is. Unlike `Write`,
these are called through the **already-generic** `CallExternal` codegen (`src/compiler/fission.cpp`)
that `systemTable.BootServices.GetMemoryMap(...)` already exercises for a 5-argument, stack-passed
call -- confirmed by `ArcoFission reveal ... at X86_64` on a `blockIo AS UEFI.BlockIoProtocol`
parameter producing `41 ff 53 18` (`call [r11+0x18]`), the exact registered `ReadBlocks` offset,
with **no new calling-convention codegen written for this binding**.

Verified prototypes (`BlockIo.h`):

```c
typedef EFI_STATUS (EFIAPI *EFI_BLOCK_RESET)(IN EFI_BLOCK_IO_PROTOCOL *This, IN BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_BLOCK_READ)(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId, IN EFI_LBA Lba, IN UINTN BufferSize, OUT VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_BLOCK_WRITE)(IN EFI_BLOCK_IO_PROTOCOL *This, IN UINT32 MediaId, IN EFI_LBA Lba, IN UINTN BufferSize, IN VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_BLOCK_FLUSH)(IN EFI_BLOCK_IO_PROTOCOL *This);
```

### `EFI_BLOCK_IO_MEDIA` (48 bytes) -> documented as `UEFI.BlockIoMedia`, read through hand-rolled accessors

```c
typedef struct {
  UINT32     MediaId;
  BOOLEAN    RemovableMedia;
  BOOLEAN    MediaPresent;
  BOOLEAN    LogicalPartition;
  BOOLEAN    ReadOnly;
  BOOLEAN    WriteCaching;
  UINT32     BlockSize;
  UINT32     IoAlign;
  EFI_LBA    LastBlock;
  EFI_LBA    LowestAlignedLba;
  UINT32     LogicalBlocksPerPhysicalBlock;
  UINT32     OptimalTransferLengthGranularity;
} EFI_BLOCK_IO_MEDIA;
```

| Field | Type | Size | Offset |
|---|---|---|---|
| `MediaId` | `UINT32` | 4 | 0 |
| `RemovableMedia` | `BOOLEAN` | 1 | 4 |
| `MediaPresent` | `BOOLEAN` | 1 | 5 |
| `LogicalPartition` | `BOOLEAN` | 1 | 6 |
| `ReadOnly` | `BOOLEAN` | 1 | 7 |
| `WriteCaching` | `BOOLEAN` | 1 | 8 |
| *(padding)* | -- | 3 | 9 |
| `BlockSize` | `UINT32` | 4 | 12 (0x0C) |
| `IoAlign` | `UINT32` | 4 | 16 |
| *(padding)* | -- | 4 | 20 |
| `LastBlock` | `EFI_LBA` (`UINT64`) | 8 | 24 (0x18) |
| `LowestAlignedLba` | `EFI_LBA` | 8 | 32 |
| `LogicalBlocksPerPhysicalBlock` | `UINT32` | 4 | 40 |
| `OptimalTransferLengthGranularity` | `UINT32` | 4 | 44 |

Like `UEFI.GraphicsOutputMode`'s own registry entry, `UEFI.BlockIoMedia` is registered in
`uefi_bindings.hpp` for documentation/consistency but is **not consulted** by its own accessors:
`CallExternal` only resolves chains ending in a method call, so a struct that is read-only data
throughout still needs hand-rolled accessors regardless of what is registered. `UEFI.BLOCKIO.*`
field intrinsics are bound the same way `UEFI.GOP.WIDTH`/`HEIGHT`/etc. are:

- `UEFI.BLOCKIO.MEDIAID(blockIo)` -> `U32`, `Media+0x00`
- `UEFI.BLOCKIO.MEDIAFLAGS(blockIo)` -> `U32`, `Media+0x04` -- the raw packed
  `RemovableMedia`/`MediaPresent`/`LogicalPartition`/`ReadOnly` word, **not** four separate bit
  accessors. The x86-64 encoder has no displacement-addressed single-byte load primitive, and this
  binding does not add one purely for this: adding a new primitive to shared encoder infrastructure
  for a single caller is a real risk for no real gain when the existing 32-bit displacement load
  (already proven by `UEFI.GOP.WIDTH`) can read the whole packed word instead. Callers extract the
  byte they want with an ordinary `SHR`/`AND` (both already-proven ArcoBASIC operators) at the
  stdlib level -- an honest, named, minimal-risk scope reduction, not a silently missing feature.
- `UEFI.BLOCKIO.BLOCKSIZE(blockIo)` -> `U32`, `Media+0x0C`
- `UEFI.BLOCKIO.LASTBLOCK(blockIo)` -> `U64`, `Media+0x18`

`IoAlign`, `LowestAlignedLba`, `LogicalBlocksPerPhysicalBlock`, and
`OptimalTransferLengthGranularity` are not bound in this milestone -- not needed by the reference
`ReadSectors`/`WriteSectors` provider this binding exists to support.

### What This Binding Does Not Do

- Does not bind `Revision` behind a source-level accessor (recorded in the registry for
  completeness/future chaining only).
- Does not bind `IoAlign`, `LowestAlignedLba`, `LogicalBlocksPerPhysicalBlock`, or
  `OptimalTransferLengthGranularity`.
- Does not expose `RemovableMedia`/`MediaPresent`/`LogicalPartition`/`ReadOnly` as individual typed
  fields -- only the packed `MediaFlags` word (see above).
- Does not attempt multi-handle enumeration (`LocateHandleBuffer`/`HandleProtocol` over every
  `EFI_BLOCK_IO_PROTOCOL` handle); `UEFI.BLOCKIO.DISCOVER` calls `LocateProtocol`, which returns the
  first matching instance firmware offers, matching `UEFI.GOP.DISCOVER`'s own established scope.
- Does not attach a real disk device in the existing QEMU test harness by itself -- the harness's
  RAM-preload trick (`-device loader,file=...,addr=...`) still populates `stdlib/block_device_policy.abas`'s
  `RAMDisk.*` provider; a genuine end-to-end proof against an emulated disk (virtio-blk/AHCI via
  `-drive`) is separate follow-on work, tracked in the RFC-0038 status update alongside this binding.
