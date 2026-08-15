# Volatile Memory Access Semantics

Status: proposed
Depends on: Systems Semantic Aliases, Address Semantics, x86-64 Freestanding Control Flow

## 1. Purpose

This RFC adds explicit volatile memory access for memory-mapped hardware and low-level memory inspection. It does not add general pointer dereference syntax.

These operations are compiler-recognized semantics whose accesses must remain observable.

## 2. Public surface

```basic
MEMORY.Read8(address) AS U8
MEMORY.Read16(address) AS U16
MEMORY.Read32(address) AS U32
MEMORY.Read64(address) AS U64

MEMORY.Write8(address, value AS U8)
MEMORY.Write16(address, value AS U16)
MEMORY.Write32(address, value AS U32)
MEMORY.Write64(address, value AS U64)
```

Accepted address types:

- `VIRTUALPTR`
- `MMIOPTR`

Aliases:

```basic
ReadByte  -> Read8
ReadWord  -> Read16
ReadDWord -> Read32
ReadQWord -> Read64

WriteByte  -> Write8
WriteWord  -> Write16
WriteDWord -> Write32
WriteQWord -> Write64
```

## 3. Volatile contract

Every operation performs exactly one access of the stated width.

The compiler must not:

- eliminate an unused read;
- merge adjacent accesses into a wider access;
- split one access into narrower accesses unless the target cannot otherwise implement it and the target contract explicitly allows that behavior;
- cache a value across another volatile access;
- invent speculative accesses;
- reorder volatile accesses relative to one another.

A 32-bit write is not interchangeable with four 8-bit writes. Device registers may assign different meaning to each width.

## 4. Ordinary memory versus MMIO

Both `VIRTUALPTR` and `MMIOPTR` use volatile access semantics in this namespace.

`MMIOPTR` additionally communicates device-memory intent and may require stronger architecture-specific ordering. A later mapping RFC will define cache attributes and how an address becomes safely usable as MMIO.

This RFC does not claim that converting an arbitrary virtual address with `ADDRESS.MMIO` makes the hardware mapping correct.

## 5. Alignment

Initial rules:

- naturally aligned accesses are supported;
- a constant address known to violate natural alignment produces a compile-time diagnostic;
- dynamic alignment cannot always be proven and remains the developer's responsibility;
- the backend must not silently replace a misaligned access with a sequence having different device semantics.

No alignment-mode directive is introduced in this RFC.

## 6. A-MIR

```text
MEMORY.READ8
MEMORY.READ16
MEMORY.READ32
MEMORY.READ64
MEMORY.WRITE8
MEMORY.WRITE16
MEMORY.WRITE32
MEMORY.WRITE64
```

The address operand retains its domain type.

## 7. x86-64 lowering

The backend emits one load or store of the requested width.

The existing spill-based value model may be used. The backend must ensure temporary register use does not accidentally widen, narrow, or duplicate the access.

## 8. Bulk operations are separate

This RFC does not define `Copy`, `Move`, `Fill`, or `Zero`. Those are ordinary memory-range operations with different optimization and overlap rules.

Bulk operations must not be implemented by repeatedly invoking volatile MMIO semantics unless an explicit later RFC defines such behavior.

## 9. Hosted behavior

Hosted targets reject `MEMORY.Read*` and `MEMORY.Write*` from this namespace. A future hosted testing namespace may emulate devices, but it is not part of this RFC.

## 10. Validation

Minimum validation includes:

- exact machine-code tests for each width;
- proof that an unused read remains emitted;
- proof that two consecutive reads remain two reads;
- proof that `WriteByte` and `Write8` compile identically;
- a QEMU test against a known MMIO or test-device register when a deterministic target is selected.

The RFC does not guess a universal safe physical address for testing.

## 11. Non-goals

This RFC does not define:

- physical memory mapping;
- arbitrary structure layout;
- typed pointer dereference;
- atomics;
- cache-control operations;
- bulk memory operations;
- DMA coherency;
- user-mode fault recovery.

## 12. Acceptance

The implementation is complete when:

1. all eight canonical operations are represented explicitly through AST, A-MIR, and x86-64 lowering;
2. aliases share one canonical path;
3. address-domain type checking is enforced;
4. volatile access count and width are preserved;
5. hosted targets reject the operations;
6. diagnostics identify invalid width, value type, address type, and known misalignment.
