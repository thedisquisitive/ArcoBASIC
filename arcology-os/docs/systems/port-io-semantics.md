# Port I/O Semantics

Status: implemented (Agent Packet 005)
Depends on: Systems Semantic Aliases, Address Semantics
Target initially supported: x86-64

## 1. Purpose

This RFC adds direct, typed access to architecture-defined I/O ports. It enables early serial output, legacy interrupt-controller work, timer configuration, keyboard-controller access, and other x86 hardware bring-up without inline assembly.

These operations are compiler-recognized systems semantics, not hosted library calls.

## 2. Public surface

```basic
PORT.Read8(port AS IOPORT) AS U8
PORT.Read16(port AS IOPORT) AS U16
PORT.Read32(port AS IOPORT) AS U32

PORT.Write8(port AS IOPORT, value AS U8)
PORT.Write16(port AS IOPORT, value AS U16)
PORT.Write32(port AS IOPORT, value AS U32)
```

Aliases:

```basic
PORT.ReadByte   -> PORT.Read8
PORT.ReadWord   -> PORT.Read16
PORT.ReadDWord  -> PORT.Read32

PORT.WriteByte  -> PORT.Write8
PORT.WriteWord  -> PORT.Write16
PORT.WriteDWord -> PORT.Write32
```

There is no 64-bit port transfer semantic on x86-64.

## 3. Example

```basic
LET com1Data AS IOPORT = &H3F8
LET value AS U8 = PORT.ReadByte(com1Data)
PORT.Write8(com1Data, value)
```

The alias and width forms compile identically.

## 4. Volatility and ordering

Every port read and write is observable and must occur exactly where written.

The compiler must not:

- remove an apparently unused read;
- combine consecutive reads;
- remove a repeated write;
- cache a read across another hardware operation;
- reorder port operations relative to each other.

Ordering relative to ordinary memory is controlled by the CPU/barrier semantics defined separately. The compiler itself must still preserve source order for explicit hardware semantics.

## 5. A-MIR

```text
PORT.READ8
PORT.READ16
PORT.READ32
PORT.WRITE8
PORT.WRITE16
PORT.WRITE32
```

Each instruction records typed operands and source location.

Aliases normalize to these same opcodes.

## 6. x86-64 lowering

The x86-64 backend lowers to `IN` and `OUT` instructions.

The backend may use immediate-port encodings where legal and profitable, or load the port into `DX`. Both forms are semantically identical.

The implementation must correctly handle:

- `AL`, `AX`, or `EAX` as the transfer register;
- 16-bit port values through `DX` when required;
- value movement between spill slots and the architectural transfer register.

## 7. Hosted behavior

Hosted bytecode and ordinary application targets reject these operations with an explicit diagnostic:

```text
PORT.Write8 is available only to a freestanding target with port-I/O support.
```

## 8. Capability declaration

This initial RFC does not require a new `#CAPABILITY` directive. Capability declarations should be introduced by a separate security-model RFC rather than smuggled into the first hardware-access implementation.

The compiler still knows the operation requires privileged hardware access and may expose that fact in reveal output and package metadata.

## 9. Validation

Minimum test fixture:

1. configure COM1 using `PORT.Write8` and alias spellings;
2. poll the line-status register using `PORT.ReadByte` in a bounded loop;
3. write a byte directly to COM1;
4. capture the serial output under QEMU;
5. compare alias and canonical builds for byte identity.

The test must disable or avoid the UEFI console path so success genuinely depends on port I/O.

## 10. Non-goals

This RFC does not define:

- a complete serial driver;
- I/O permission bitmaps;
- virtualization of port access;
- ARM64 equivalents;
- bulk string I/O instructions (`INS*`/`OUTS*`);
- device discovery;
- interrupt-driven I/O.

## 11. Acceptance

The implementation is complete when:

1. all six canonical operations parse and type-check;
2. aliases normalize before A-MIR;
3. hosted targets reject them;
4. x86-64 machine bytes are independently verified;
5. QEMU serial output proves real execution;
6. unused reads remain present in generated code;
7. no unsupported 64-bit port spelling is accepted.
