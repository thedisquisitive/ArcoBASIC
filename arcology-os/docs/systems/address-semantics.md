# Address Semantics

Status: proposed
Depends on: fixed-width systems types

## 1. Purpose

The current `PTR` type is an opaque pointer-sized value suitable for firmware handles and ABI-bound pointers. Direct hardware access requires stronger distinctions so ports, virtual addresses, physical addresses, and MMIO regions cannot be mixed accidentally.

## 2. Address types

Add the following freestanding systems types:

| Type | Width on x86-64 | Meaning |
|---|---:|---|
| `PTR` | 8 bytes | Opaque generic pointer-sized value; existing behavior. |
| `VIRTUALPTR` | 8 bytes | Address interpreted in the current virtual address space. |
| `PHYSICALPTR` | 8 bytes | Physical machine address. |
| `MMIOPTR` | 8 bytes | Virtual address mapped to device memory with volatile access semantics. |
| `IOPORT` | 2 bytes logical range | Architecture I/O port identifier. |

`IOPORT` accepts values from `0` through `65535` on x86-64.

## 3. No implicit cross-domain conversion

The following conversions are forbidden without an explicit operation:

- integer to runtime address;
- `PTR` to `VIRTUALPTR`;
- `VIRTUALPTR` to `PHYSICALPTR`;
- `PHYSICALPTR` to `VIRTUALPTR`;
- `VIRTUALPTR` to `MMIOPTR`;
- address to `IOPORT`;
- `IOPORT` to address.

Address literals require explicit construction. Port literals continue to use `PORT.Address`; there is no public `ADDRESS.Port` constructor.

## 4. Explicit constructors

```basic
ADDRESS.Virtual(value AS U64) AS VIRTUALPTR
ADDRESS.Physical(value AS U64) AS PHYSICALPTR
ADDRESS.Value(address) AS U64
ADDRESS.MMIO(value AS VIRTUALPTR) AS MMIOPTR

PORT.Address(value AS U16) AS IOPORT
```

These are compiler-recognized systems conversions under `#RUNTIME NONE`. They do not allocate objects.

`ADDRESS.MMIO` converts an already mapped virtual address to the volatile MMIO domain. It does not create a page-table mapping.

## 5. Intent-bearing arithmetic

Ordinary arithmetic operators are not initially defined for address types.

Use:

```basic
ADDRESS.Offset(address, byteCount)
ADDRESS.Distance(first, second)
ADDRESS.AlignUp(address, alignment)
ADDRESS.AlignDown(address, alignment)
ADDRESS.IsAligned(address, alignment)
ADDRESS.Local(variable)
```

### Rules

- `Offset` preserves the input address domain.
- `ADDRESS.Local` constructs a typed `PTR` to a compiler-managed local spill slot for ABI output
  parameters; it is not ordinary pointer arithmetic and does not expose frame offsets.
- `Distance` requires matching address domains and returns `I64`.
- alignment must be a nonzero power of two;
- constant invalid alignments fail at compile time;
- address overflow must not silently wrap when detectable.

## 6. Accepted consumers

| Type | Accepted by |
|---|---|
| `PTR` | ABI-bound calls that explicitly require an opaque pointer. |
| `VIRTUALPTR` | ordinary volatile memory operations. |
| `PHYSICALPTR` | mapping and physical-memory operations defined by later RFCs. |
| `MMIOPTR` | volatile memory operations, with MMIO ordering rules. |
| `IOPORT` | `PORT.*` operations only. |

`MEMORY.Read*` does not accept a plain `U64`.

## 7. Canonical AST and A-MIR

Address constructors and operations have explicit canonical AST kinds and typed A-MIR operations. They must not lower as ordinary hosted namespace calls.

The backend may represent all x86-64 pointer-width address values in 64-bit machine values, but their source and A-MIR types remain distinct.

## 8. Diagnostics

Examples:

```text
PORT.Read8 expects IOPORT; received U16.
Use ADDRESS.Port(value) or declare the value AS IOPORT.
```

```text
MEMORY.Write32 expects VIRTUALPTR or MMIOPTR; received PHYSICALPTR.
Map the physical address before accessing it.
```

## 9. Non-goals

This RFC does not define:

- page tables;
- physical-to-virtual mapping;
- ownership or lifetime;
- typed pointers to structures;
- pointer dereference syntax;
- element-scaled pointer arithmetic;
- memory allocation.

## 10. Acceptance

The implementation is complete when:

1. all five types are represented distinctly in the frontend and A-MIR;
2. invalid domain mixing fails during compilation;
3. constant range checks are exact;
4. address operations lower without hosted runtime support;
5. reveal output preserves address-domain types;
6. existing `PTR` ABI behavior remains compatible.
