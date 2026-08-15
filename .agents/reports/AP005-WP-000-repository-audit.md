# Agent Packet 005 WP-000 Repository Audit

Date: 2026-08-06
Scope: repository audit before implementation.

## Baseline

- Fixed-width systems metadata lives in `arcology-os/include/arco/fixed_width_types.hpp` and
  currently defines `U8` through `I64`, `BOOL`, and `PTR`. There is no `IOPORT` type.
- `Parser::parse_type_name` accepts identifier spellings generically, but fixed-width initializer
  validation only knows the existing fixed-width registry. `IOPORT` therefore needs explicit
  systems typing and initializer rules rather than an ordinary identifier alias.
- Dotted function-like expressions already parse as canonical `Call`/`MethodCall` nodes. The
  compiler's current `lower_call` classifies only method calls through declared parameters as
  `CallExternal`; ordinary `PORT.*` names would otherwise become hosted-style calls.
- Hardware statements are recognized in `Parser::statement()` only for `CPU.Halt` and
  `CPU.HaltForever`, producing `AstKind::HardwareSemantic`. `CPU.Pause` is not yet recognized.
- A-MIR already carries typed result and operand metadata for integer operations and has explicit
  hardware-halt instruction kinds. No port instruction kinds exist.
- The x86-64 encoder has fixed-register and spill helpers, but no `IN`, `OUT`, or `PAUSE` methods.
  The multi-block spill backend from Packet 004 is the integration point for port lowering.
- Existing QEMU scripts route the serial device to `stdio` with `-serial stdio`, primarily to
  capture the OVMF/UEFI console. A direct COM1 fixture will need a dedicated serial capture path
  and a marker that is not emitted through `ConsoleOut.Write`.

## Parser and source-syntax conflicts

The RFC examples use `&H3F8`, while the current lexer supports decimal, `0x`/`0X` hexadecimal,
and `0b`/`0B` binary literals. The implementation must either use the existing accepted literal
spellings in fixtures or make a deliberate shared-lexer decision; a systems-only numeric grammar
must not be added.

`PORT.*` calls can use existing call syntax without new public grammar, but they must be recognized
as explicit canonical hardware semantics before ordinary hosted-call lowering.

## Hosted behavior

The hosted evaluator currently executes arbitrary calls through the runtime namespace and has no
port-I/O or `CPU.Pause` semantics. Packet 005 requires explicit rejection rather than host emulation.

## Current tests and validation

The prior baseline contains 19 CTest tests, including fixed-width integer, control-flow, UEFI/PE,
QEMU/OVMF, hardware artifact, and hosted regression coverage. The existing QEMU harness can be
extended, but its current serial capture is not sufficient by itself to attribute a marker solely
to direct COM1 writes.

## Conflicts and decisions to resolve during implementation

1. Add `IOPORT` as systems-only type metadata without changing ordinary hosted type behavior.
2. Normalize `PORT.ReadByte`/`WriteByte` aliases to canonical A-MIR port operations.
3. Add `CPU.Pause` as a bare hardware semantic, with hosted rejection and x86-64 `F3 90` lowering.
4. Use `0x` literals in checked-in fixtures unless shared lexer support for `&H` is separately
   authorized.
5. Capture COM1 output through a separate QEMU serial endpoint or filtered serial file so the
   direct-I/O marker cannot be confused with UEFI console output.

No external assembler, linker, runtime shim, or new control-flow syntax is required by the
current architecture.
