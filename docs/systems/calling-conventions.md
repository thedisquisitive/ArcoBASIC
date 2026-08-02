# Calling Conventions

Status: WP-005 (Microsoft x64 Calling Convention)
Depends on: `docs/systems/uefi-target.md` section 4

This document specifies the ABI model implemented by `include/arco/calling_convention.hpp` and
exposed via `ArcoFission reveal FILE at CALLCONV`. It is the authoritative reference for how
ArcoBASIC systems functions map arguments and return values onto Microsoft x64 registers and stack
slots. Verify these facts against the Microsoft x64 software conventions reference and the UEFI
Specification before relying on them for real hardware/firmware work (Packet section 9) -- this
document restates well-established public ABI facts, but real-hardware verification is WP-008/010's
job, not a substitute for checking primary sources when implementing the actual encoder.

## Scope

This milestone's calling-convention model covers **integer and pointer-class arguments and return
values only**. There is no floating-point argument classification (no `XMM0`-`XMM3` argument
registers) because no floating-point ABI work is in scope (`docs/systems/uefi-target.md`,
Packet WP-002 non-goals). Every fixed-width type in `include/arco/fixed_width_types.hpp`
(`U8`..`I64`, `BOOL`, `PTR`) is treated identically by this model: an 8-byte-or-smaller
integer/pointer value occupying one argument slot.

## Argument Registers

The first four integer/pointer-class arguments to a function, in declaration order, are passed in:

| Position | Register |
|---|---|
| 0 | `RCX` |
| 1 | `RDX` |
| 2 | `R8`  |
| 3 | `R9`  |

Arguments beyond the fourth are passed on the stack. At the callee's entry point -- immediately
after the `CALL` instruction transfers control, before the callee's own prologue runs -- the stack
looks like:

```text
[RSP + 0]   return address (pushed by CALL)
[RSP + 8]   \
   ...       > 32-byte shadow space, reserved by the caller
[RSP + 39] /
[RSP + 40]  5th argument (1st stack-passed argument)
[RSP + 48]  6th argument
[RSP + 8*n] (n-4)th stack argument, for position n >= 4
```

`include/arco/calling_convention.hpp::assign_argument_locations` implements exactly this: position
`p < 4` maps to the corresponding register; position `p >= 4` maps to stack offset
`32 + 8 + 8 * (p - 4)`.

## Return Register

Integer, pointer, and `BOOL` return values are returned in `RAX`. This is the only return
convention needed for this milestone (`docs/systems/uefi-target.md` section 5: the UEFI entry
point's `AS U64` return maps to `EFI_STATUS` in `RAX`).

## Shadow Space

The caller must always reserve 32 bytes of stack space ("shadow space") immediately below the
return address before making a call, regardless of how many arguments are actually passed in
registers. This space exists so the callee may spill its register arguments there if needed; the
caller is not required to write anything into it. `kShadowSpaceBytes = 32` in
`include/arco/calling_convention.hpp`.

## Stack Alignment

`RSP` must be 16-byte aligned immediately before a `CALL` instruction. Because `CALL` pushes an
8-byte return address, this means `RSP % 16 == 8` at the callee's entry point, before its own
prologue adjusts the stack. `kStackAlignmentAtCallBytes = 16` and `kEntryRspMod16 = 8` in
`include/arco/calling_convention.hpp` record these two related facts.

## Preserved (Callee-Saved) and Volatile (Caller-Saved) Registers

```text
Callee-saved (must be preserved across a call):
    RBX, RBP, RDI, RSI, R12, R13, R14, R15
    XMM6-XMM15

Caller-saved (may be freely clobbered by a call):
    RAX, RCX, RDX, R8, R9, R10, R11
    XMM0-XMM5
```

`include/arco/calling_convention.hpp::callee_saved_registers()` and `::caller_saved_registers()`
expose these lists (unit-tested to be disjoint in `tests/runtime_tests.cpp`). No register
allocator exists yet (that is WP-008's job); these lists are recorded now so that work does not
have to re-derive them from the ABI spec.

## External/ABI-Bound Calls

A-MIR distinguishes a call through a declared function parameter (`AmirInstruction::Kind::
CallExternal`, docs/systems/uefi-target.md via `.agents/reports/WP-004-amir-systems-primitives.md`)
from an ordinary namespaced host/stdlib call. The calling-convention model applies identically to
both: `CALLING CONVENTION MICROSOFT_X64`'s `CALL SITES` section computes register/stack assignment
for each `CallExternal` instruction's argument list the same way it computes them for a function's
own declared parameters. This is what satisfies "nested calls sufficient for UEFI text output"
(Packet WP-005): the call to `systemTable.ConsoleOut.Write("...")` inside `Main`'s body gets its own
independent argument-register assignment (`ARG0 : RCX`), verified in
`tests/systems_calling_convention_smoke.sh`.

## `reveal ... at CALLCONV`

```text
arcofission reveal FILE at CALLCONV
```

Renders the computed calling convention for every function declared in `FILE`: each parameter's
register or stack location, the return register, and -- for each `CallExternal` call site inside
the function body -- that call's own argument locations. Example, for
`tests/systems/uefi-hello/hello.abas`:

```text
CALLING CONVENTION MICROSOFT_X64
SHADOW_SPACE 32 bytes
STACK_ALIGNMENT 16 bytes at CALL

FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64
    PARAMETERS
        imageHandle : RCX
        systemTable : RDX
    RETURNS RAX (U64)
    CALL SITES
        systemTable.ConsoleOut.Write (external)
            ARG0 : RCX
END FUNCTION
```

This is a deterministic **textual stand-in** for real generated assembly or machine-code
disassembly (Packet WP-005 verification requirement) -- it answers "where does each argument live,"
which is the calling-convention question. It does not encode real x86-64 instructions; that is
WP-008's job, and WP-008 should treat `assign_argument_locations` as the source of truth for where
to move each argument into place.

## What This Work Package Does Not Do

- No instruction encoding, no register allocation beyond argument placement, no prologue/epilogue
  generation. Those are WP-008.
- No floating-point argument/return classification.
- No modeling of the hidden `This`/self pointer that real UEFI protocol methods (e.g.
  `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString`) take as their first argument in the underlying C
  ABI -- `CALL SITES` shows exactly the argument list ArcoBASIC source provides today. Modeling the
  protocol's real signature (including any implicit `This` argument) is WP-006's job once
  `UEFI.SystemTable.ConsoleOut`'s actual protocol shape is defined.
