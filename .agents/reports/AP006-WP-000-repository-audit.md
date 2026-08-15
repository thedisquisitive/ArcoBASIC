# Memory Architecture / Address Semantics WP-000 Audit

Date: 2026-08-07
Scope: audit of the current ArcoBASIC/Arcology-OS tree before Packet 006 implementation.

## Existing type system

`arcology-os/include/arco/fixed_width_types.hpp` currently defines `U8` through `I64`, `BOOL`, and
`PTR`. There are no executable definitions for `VIRTUALPTR`, `PHYSICALPTR`, or `MMIOPTR`.
`IOPORT` is implemented by the preceding port-I/O packet in compiler metadata, but is not part of
the fixed-width registry. There is no domain-aware conversion or arithmetic validator.

The checked-in `arcology-os/docs/systems/address-semantics.md` and
`volatile-memory-semantics.md` are design documents only. Their constructor names and conversion
examples are not yet reflected in parser, semantic, A-MIR, or backend code.

## Frontend and canonical AST

The shared parser accepts dotted function-like expressions as ordinary `Call`/`MethodCall` nodes.
The preceding port-I/O work adds an explicit `PortOperation` path, but there are no canonical AST
identities for `ADDRESS.*`, `MEMORY.*`, or `CPU.ReadBarrier`/`WriteBarrier`/`MemoryBarrier`.
No parser-level domain checking or alignment diagnostics exist.

## A-MIR and hosted behavior

A-MIR currently has typed integer, branch, call, and port instructions, plus CPU halt/pause
semantics. It has no address-domain, memory-read/write, mapping, or barrier instruction kinds.
The hosted bytecode backend has no memory/MMIO or barrier implementation and therefore cannot yet
provide the packet's required explicit diagnostics for these names.

## x86-64 backend and encoder

The spill-based x86-64 backend can lower integer operations, branches, UEFI external calls, port
I/O, and CPU hardware hints. The encoder has no width-specific volatile memory load/store forms,
fence instructions, or mapping/runtime primitives. Directly dereferencing an arbitrary physical
address would violate the RFC's domain rules and cannot be added as an implicit shortcut.

## Mapping and framebuffer proof

The current UEFI binding registry exposes only `ConsoleOut` and the narrow `BootServices` watchdog
method. It has no UEFI Graphics Output Protocol (GOP) binding, framebuffer discovery, or firmware
mapping service. Existing QEMU tests prove console/COM1 output only; there is no deterministic
framebuffer device address or mapping path in the repository.

Therefore the packet's framebuffer demonstration cannot honestly be completed by hard-coding a
physical address. A mapping/GOP decision or an explicit test-device contract is required before
hardware proof can be claimed.

## Existing baseline

The current full suite contains 20 tests, including integer core, control flow, port I/O, UEFI/PE,
QEMU/OVMF, hardware-artifact, and hosted regressions. The pre-existing suite is green before this
packet's changes.

## Conflicts requiring staged decisions

1. The new RFC says `ADDRESS.Physical`, `ADDRESS.Virtual`, `ADDRESS.Value`, and `ADDRESS.MMIO`,
   while the existing address design document also describes `ADDRESS.Pointer` and `ADDRESS.Port`.
   The public constructor set must be reconciled before code generation.
2. `MEMORY.Map`/`MapDevice` require a concrete UEFI or native Arcology memory service; none is
   currently bound.
3. A framebuffer proof requires GOP discovery and a deterministic mapping/cache policy, neither of
   which exists in the current UEFI surface.
4. x86-64 memory instruction encodings and barrier semantics need independent byte verification.

The safe next step is a binding/specification decision for mapping and framebuffer access. Address
domain types, conversions, and compiler diagnostics can be implemented independently, but the full
Packet 006 definition of done cannot be claimed until the mapping/GOP contract exists.
