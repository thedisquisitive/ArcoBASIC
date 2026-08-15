# Amendment: Memory Architecture and Address Semantics

Status: accepted implementation amendment

This amendment supersedes conflicting address-semantics notes. The canonical public constructors are:

```text
ADDRESS.Physical(U64) -> PHYSICALPTR
ADDRESS.Virtual(U64)  -> VIRTUALPTR
ADDRESS.Value(address) -> U64
ADDRESS.MMIO(VIRTUALPTR) -> MMIOPTR
```

`ADDRESS.Pointer` and `ADDRESS.Port` are not public APIs. Port I/O continues to use `PORT.Address` and `PORT.Offset`.

## Mapping contract

```text
MEMORY.Map(PHYSICALPTR, U64, MEMORYMAPFLAGS) -> VIRTUALPTR
MEMORY.MapDevice(PHYSICALPTR, U64) -> MMIOPTR
```

`MapDevice` guarantees device-memory/cache semantics; architecture-specific page-table flags remain implementation details. UEFI bootstrap services may provide the initial implementation, after which the native Arcology memory manager owns these operations without source incompatibility.

## Framebuffer proof

Framebuffer validation must discover the device through UEFI Graphics Output Protocol rather than a hard-coded physical address. The minimal binding exposes `BaseAddress AS PHYSICALPTR`, `Size`, `Width`, `Height`, `PixelsPerScanLine`, and `PixelFormat`. The proof maps `BaseAddress` with `MEMORY.MapDevice` and performs volatile writes through `MMIOPTR` under QEMU/OVMF.

The implementation uses the narrow compiler intrinsic `UEFI.GOP.Discover(systemTable)` for
`LocateProtocol` discovery. Its GUID and output-pointer temporary are backend-managed; general
pointer-to-pointer syntax is not part of the public language.

## Ordering

`CPU.ReadBarrier`, `CPU.WriteBarrier`, and `CPU.MemoryBarrier` lower on x86-64 to `LFENCE`, `SFENCE`, and `MFENCE` respectively, with exact-byte encoder tests.

## Phasing

Implementation proceeds through address types and frontend/A-MIR, memory/MMIO lowering and barriers, mapping/bootstrap support, then GOP framebuffer discovery and regression validation. Completion requires every phase and the original packet definition of done.
