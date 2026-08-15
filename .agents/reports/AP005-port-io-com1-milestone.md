# Agent Packet 005 Completion Report

Date: 2026-08-06

## Implemented surface

- Added the distinct systems type `IOPORT` with 16-bit width semantics.
- Added `PORT.Address(U16)` and checked, literal-only `PORT.Offset(IOPORT, I16)` construction.
- Added canonical `PORT.Read8/16/32` and `PORT.Write8/16/32` operations.
- Added and normalized `Byte`, `Word`, and `DWord` intent aliases before A-MIR.
- Added canonical AST `PortOperation` nodes and explicit typed A-MIR `PORT.*` instructions.
- Added `CPU.Pause` as a nonterminal hardware semantic.
- Added x86-64 `IN`/`OUT` DX-form and `PAUSE` encoder primitives.
- Integrated port operations with the multi-block spill backend, including branches and polling
  loops.
- Hosted execution rejects port operations and `CPU.Pause` with explicit hardware-target errors.

## Encoding verification

The encoder unit test verifies the exact byte sequence:

```text
IN AL,DX    EC
IN AX,DX    66 ED
IN EAX,DX   ED
OUT DX,AL   EE
OUT DX,AX   66 EF
OUT DX,EAX  EF
PAUSE       F3 90
```

The implementation uses DX forms for the complete 16-bit port range.

## COM1 evidence

`arcology-os/tests/fixtures/port-io-com1/port-io-com1.abas` configures the QEMU 16550-compatible
COM1 UART, polls line-status readiness with fresh `PORT.ReadByte` operations and `CPU.Pause`, then
writes the marker one byte at a time through `PORT.WriteByte`:

```text
ARCOLOGY PORT I/O ONLINE
```

The QEMU/OVMF serial capture contained that marker. The fixture does not call
`systemTable.ConsoleOut.Write`; the marker is emitted solely through COM1 port writes.

## Validation

- Alias and canonical forms produce identical canonical A-MIR operations and identical generated
  `.text` sections.
- Unused reads remain represented and emitted as `IN` instructions.
- Hosted port use fails with an explicit `PORT.* is available only on a freestanding target with
  port-I/O support` diagnostic.
- Full CTest suite passed: **20/20 tests**.
- Existing integer, control-flow, UEFI, PE, hardware-artifact, QEMU, and hosted regressions all
  remained passing.

## Deviations and limitations

- Checked-in fixtures use the existing lexer’s `0x` hexadecimal syntax. The RFC examples’ `&H`
  spelling is not currently part of the shared lexer and no systems-only numeric syntax was added.
- Dynamic `PORT.Offset` displacement is rejected for this milestone; statically known displacements
  are required. The backend does not silently expose dynamic wraparound.
- No 64-bit/QWord port transfers, immediate-port optimization, bulk string I/O, MMIO, interrupts,
  DMA, PCI, or serial-driver abstraction were added.
- The broader proposed CPU interrupt-state operations remain outside this packet; only `CPU.Pause`
  was implemented.
