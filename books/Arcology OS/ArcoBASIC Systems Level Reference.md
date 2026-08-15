# ArcoBASIC Systems-Level Reference

Status: current implementation reference (UEFI x86-64 milestone)

This book documents the systems-level surface that is implemented today in ArcoBASIC and
ArcoFission. It covers the freestanding UEFI profile, fixed-width types, the bound UEFI calls,
CPU hardware semantics, and the command used to produce a bootable PE32+ EFI application.

Arcology Commons is a separate ArcoBASIC application. It is not part of this systems API.

## What is implemented

The current systems target is deliberately narrow:

- `#PROFILE UEFI`
- `#TARGET X86_64`
- `#RUNTIME NONE`
- `#CALLCONV UEFI`
- `#EXPORT "name"`
- fixed-width integer, Boolean, and pointer declarations
- `UEFI.Handle` and `UEFI.SystemTable` entry-point types
- `UEFI.SystemTable.ConsoleOut.Write(text)`
- `UEFI.SystemTable.BootServices.SetWatchdogTimer(timeout, watchdogCode, dataSize, watchdogData)`
- `CPU.Halt` and `CPU.HaltForever`
- `IOPORT`, `PORT.Address`, `PORT.Offset`, typed 8/16/32-bit port operations and aliases
- `CPU.Pause` for x86-64 polling loops
- `IF ... THEN ... ELSE ... END IF` and `WHILE ... WEND` control flow with explicit A-MIR blocks
- straight-line and multi-block freestanding functions compiled by ArcoFission into PE32+ EFI images

The implementation does not claim to be a general UEFI SDK. Unsupported fields and methods are
rejected during compilation rather than silently treated as ordinary host calls.

## Systems directives

These directives select the systems compilation contract:

| Directive | Current value | Meaning |
|---|---|---|
| `#PROFILE UEFI` | `UEFI` only | Enables the freestanding UEFI profile. |
| `#TARGET X86_64` | `X86_64` only under this profile | Selects the x86-64 machine-code backend. |
| `#RUNTIME NONE` | `NONE` | Prohibits hosted runtime services such as `PRINT`, `File.*`, and `Network.*`. |
| `#CALLCONV UEFI` | `UEFI` only | Selects the Microsoft x64 ABI used by x86-64 UEFI. |
| `#EXPORT "efi_main"` | one symbol name | Names the exported entry symbol recorded in the generated image metadata. |

The normal ArcoFission build path remains available when `--target uefi-x86_64` is omitted; that
path is not a freestanding hardware image.

## Fixed-width systems types

Typed locals and parameters may use the following types. Literal initializers are checked exactly,
without converting through a floating-point value.

| Type | Width | Range or meaning |
|---|---:|---|
| `U8` | 1 byte | `0` through `255` |
| `U16` | 2 bytes | `0` through `65535` |
| `U32` | 4 bytes | `0` through `4294967295` |
| `U64` | 8 bytes | `0` through `18446744073709551615` |
| `I8` | 1 byte | `-128` through `127` |
| `I16` | 2 bytes | `-32768` through `32767` |
| `I32` | 4 bytes | `-2147483648` through `2147483647` |
| `I64` | 8 bytes | `-9223372036854775808` through `9223372036854775807` |
| `BOOL` | 1 byte | Boolean value (`0` or `1`) |
| `PTR` | 8 bytes | An x86-64 pointer-sized value |

Example:

```basic
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT "efi_main"

FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64
    LET status AS U64 = 0
    LET enabled AS BOOL = TRUE
    LET width AS U16 = 80
    RETURN status
END FUNCTION
```

The systems code generator currently treats integer and pointer-class arguments uniformly. Floating
point arguments, SIMD values, and a general register allocator are not implemented.

## Integer expressions

Packet 003 adds typed fixed-width integer expressions to the freestanding path. Binary operands must
have the same declared fixed-width type; there is no implicit promotion. `PTR` is not an arithmetic
integer in this profile.

| Family | Source forms | Result |
|---|---|---|
| Arithmetic | `+`, `-`, `*`, `\`, `MOD` | operand width and type |
| Bitwise | `AND`, `OR`, `XOR` (also `BITAND`, `BITOR`, `BITXOR`) | operand width and type |
| Unary | `-value`, `NOT value` / `BITNOT value` | operand width and type |
| Shifts | `SHL`, `SHR`, `SAR` | operand width and type |
| Equality | `=`, `<>` | `BOOL` |
| Relational | `<`, `<=`, `>`, `>=` | `BOOL` |

`SHR` is always logical. `SAR` is always arithmetic. Arithmetic and left shifts wrap at the
declared width. A shift count greater than or equal to the operand width produces zero for `SHL` and
`SHR`; `SAR` produces an all-zero or all-one value according to the sign. Division by a runtime zero
uses the architecture-defined x86-64 fault; a constant zero divisor is rejected at compile time.

```basic
LET status AS U8 = 0x25
LET ready AS BOOL = (status AND 0x20) <> 0
LET lowBits AS U8 = status AND 0x0F

LET packed AS U16 = 0xABCD
LET high AS U16 = packed SHR 8
LET signedValue AS I16 = -16
LET arithmetic AS I16 = signedValue SAR 2
LET wrapped AS U8 = 255 + 1       ' 0
LET quotient AS U16 = 100 \ 3
LET remainder AS U16 = 100 MOD 3
```

`ArcoFission reveal FILE at A-MIR` exposes typed operations using canonical names such as
`INT.ADD`, `INT.DIV_UNSIGNED`, `INT.CMP_LT_SIGNED`, and `INT.SAR`, including operand and result
types. Hosted bytecode behavior remains unchanged; these operations are only lowered to machine
code for the freestanding UEFI target.

## UEFI types and callable functions

### `UEFI.Handle`

`UEFI.Handle` is an opaque 8-byte UEFI handle. It is normally used as the first parameter of the
entry function. The current surface does not expose handle operations.

### `UEFI.SystemTable`

`UEFI.SystemTable` is the 120-byte x86-64 UEFI system table received by the entry point. Only these
fields are bound:

| ArcoBASIC field | UEFI field | Offset | Result |
|---|---|---:|---|
| `ConsoleOut` | `ConOut` | `0x40` | `UEFI.SimpleTextOutputProtocol` |
| `BootServices` | `BootServices` | `0x60` | `UEFI.BootServices` |

All other system-table fields, including `ConIn`, `StdErr`, and `RuntimeServices`, are currently
unsupported.

### `UEFI.SimpleTextOutputProtocol.Write`

```basic
systemTable.ConsoleOut.Write(text)
```

`Write` maps to UEFI `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString` at protocol offset `0x08`.
It accepts one ArcoBASIC string expression and returns the underlying UEFI status as a `U64`-class
value. The compiler supplies the protocol's implicit `This` pointer when lowering the external call.

String literals passed to this UEFI call are validated as UTF-8 and encoded as null-terminated
UTF-16. Invalid UTF-8, embedded NUL bytes, surrogate code points, truncated sequences, and code
points above U+10FFFF are rejected at compile time.

```basic
FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64
    systemTable.ConsoleOut.Write("Hello from ArcoBASIC\r\n")
    RETURN 0
END FUNCTION
```

### `UEFI.BootServices.SetWatchdogTimer`

```basic
systemTable.BootServices.SetWatchdogTimer(timeout, watchdogCode, dataSize, watchdogData)
```

`SetWatchdogTimer` maps to UEFI `EFI_BOOT_SERVICES.SetWatchdogTimer` at offset `0x100` in the
376-byte boot-services table. It has four explicit arguments and no implicit `This` argument. The
usual use in this milestone is to cancel the firmware watchdog before an intentional halt:

```basic
systemTable.BootServices.SetWatchdogTimer(0, 0, 0, 0)
```

No other boot services are bound yet.

## CPU hardware semantics

These are statements, not ordinary functions and not inline assembly.

### `CPU.Halt`

```basic
CPU.Halt
```

Lowers to x86-64 `HLT` (`F4`). It may resume when the target architecture permits it, so execution
falls through to the next statement in the language model.

### `CPU.HaltForever`

```basic
CPU.HaltForever
```

Lowers to `CLI; HLT; JMP -3` (`FA F4 EB FD`). It disables maskable interrupts, halts, and loops back
to the halt instruction if a non-maskable event resumes execution. It is terminal: later source
statements are not lowered and no implicit return is appended.

### `CPU.Pause`

```basic
CPU.Pause
```

Lowers to x86-64 `PAUSE` (`F3 90`) and falls through. It is intended for short polling loops and
is rejected by hosted targets.

## Direct port I/O

`IOPORT` is a distinct 16-bit systems type. Construct ports explicitly and use exact-width
operations:

```basic
LET com1 AS IOPORT = PORT.Address(0x3F8)
LET status AS IOPORT = PORT.Offset(com1, 5)
LET ready AS U8 = PORT.ReadByte(status)
PORT.Write8(com1, 65)
```

Canonical operations are `PORT.Read8/16/32` and `PORT.Write8/16/32`; `Byte`, `Word`, and `DWord`
spellings are semantic aliases. There is no 64-bit port transfer. Port reads and writes are
volatile and preserve source order. The current validation fixture configures COM1 and emits
`ARCOLOGY PORT I/O ONLINE` directly through QEMU's emulated UART.

## Complete minimal UEFI program

This program uses both currently bound UEFI call chains and the terminal hardware semantic:

```basic
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT "efi_main"

FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64
    systemTable.BootServices.SetWatchdogTimer(0, 0, 0, 0)
    systemTable.ConsoleOut.Write("ARCOLOGY HARDWARE TEST\r\n")
    CPU.HaltForever
END FUNCTION
```

The entry function should return `0` (`EFI_SUCCESS`) when it completes normally. A program using
`CPU.HaltForever` intentionally does not return to firmware.

## Calling convention

The UEFI target uses the Microsoft x64 integer/pointer convention:

- argument 0 → `RCX`
- argument 1 → `RDX`
- argument 2 → `R8`
- argument 3 → `R9`
- return value → `RAX`
- 32 bytes of caller-reserved shadow space
- `RSP` is 16-byte aligned immediately before each `CALL`

The current x86-64 backend supports spill-based calls and deterministic multi-block control flow.
Programs that need a fifth or later parameter, floating-point classification, or unsupported
control-flow constructs are rejected by the current systems code generator.

Inspect the computed ABI placement with:

```sh
build/ArcoFission reveal arcology-os/tests/fixtures/uefi-hello/hello.abas at CALLCONV
```

Inspect other compiler stages with `at AST`, `at A-MIR`, `at BYTECODE`, or `at X86_64`.

## Building a UEFI hardware-level program with ArcoFission

### 1. Build ArcoFission

From the repository root:

```sh
cmake -S . -B build
cmake --build build --target ArcoFission
```

### 2. Create a systems source file

Save the complete program above as `hello-uefi.abas`. The source must have an entry function named
`Main` unless another entry is selected with `--entry`; `IF`/`ELSE` and `WHILE` blocks are allowed.

### 3. Compile the PE32+ EFI image

```sh
build/ArcoFission build hello-uefi.abas \
    -o hello-uefi.efi \
    --target uefi-x86_64
```

The systems path performs these stages:

1. parses the directives and canonical AST;
2. validates fixed-width literals, freestanding restrictions, UEFI field chains, and UTF-16 string literals;
3. lowers the AST to A-MIR, including ABI-bound external calls;
4. emits the reachable x86-64 block graph and its UTF-16 data, resolving internal rel32 branches;
5. writes a minimal PE32+ image with the EFI application subsystem.

Use `--entry OtherFunction` when the exported function is not named `Main`.

### 4. Inspect before booting

```sh
build/ArcoFission reveal hello-uefi.abas at AST
build/ArcoFission reveal hello-uefi.abas at A-MIR
build/ArcoFission reveal hello-uefi.abas at CALLCONV
build/ArcoFission reveal hello-uefi.abas at X86_64
```

These reports are deterministic inspection tools. `CALLCONV` shows register and stack placement;
`X86_64` shows the generated instruction/data view; neither is a replacement for booting the image.

### 5. Boot under QEMU/OVMF

Install `qemu-system-x86_64` and an OVMF firmware image, then run the repository harness:

```sh
arcology-os/scripts/run/run-uefi-hello.sh \
    hello-uefi.efi "ARCOLOGY HARDWARE TEST" 20
```

The harness creates a temporary FAT boot directory, places the image at `EFI/BOOT/BOOTX64.EFI`,
boots it with QEMU and OVMF, captures the serial/console output, and verifies the expected text.
It removes its temporary directory on exit.

The automated equivalent is:

```sh
ctest --test-dir build --output-on-failure -R systems_qemu_ovmf_harness_smoke
```

If QEMU or OVMF is unavailable, the test reports `SKIP`; compiling the EFI image itself does not
require firmware.

## Building the reproducible Arcology hardware artifact

For the checked-in hardware fixture rather than a custom program:

```sh
arcology-os/scripts/build/build-arcology-hardware-artifact.sh
arcology-os/scripts/run/run-arcology-hardware-image.sh \
    arcology-os/dist/arcology-seed-0.1/arcology-seed-0.1-x86_64.img
```

The artifact builder compiles the checked-in UEFI fixture, creates a deterministic FAT32 image,
and writes `BOOTX64.EFI`, the disk image, and `SHA256SUMS` under `arcology-os/dist/`.

## Current limitations

The following are intentionally outside the current implementation:

- arbitrary UEFI table fields or protocols;
- hosted runtime calls in `#RUNTIME NONE` programs;
- `ELSEIF` (not currently represented by the shared parser), `FOR`, `TRY`, and other unsupported
  multi-block constructs in the x86-64 code generator;
- classes, arrays, and objects in generated freestanding code;
- fifth-and-later function parameters;
- floating-point or SIMD ABI handling;
- real PE relocation sections and absolute-address fixups;
- ARM64, legacy BIOS, page tables, interrupts, MMIO, DMA, PCI, USB, networking, and multicore APIs.

For the detailed derivations and acceptance tests, see:

- `arcology-os/docs/systems/uefi-target.md`
- `arcology-os/docs/systems/uefi-bindings.md`
- `arcology-os/docs/systems/hardware-semantics.md`
- `arcology-os/docs/systems/calling-conventions.md`
- `arcology-os/docs/systems/x86-64-codegen.md`
- `arcology-os/docs/systems/pe32-image.md`
- `arcology-os/docs/systems/qemu-ovmf-harness.md`
