# x86-64 Code Generation

Status: WP-008 (x86-64 Code Generation)
Depends on: `docs/systems/calling-conventions.md`, `docs/systems/uefi-bindings.md`,
`docs/systems/utf16-encoding.md`

`include/arco/x86_64_encoder.hpp` provides a small instruction encoder; `compiler/fission.cpp`'s
`generate_x86_64_function` (exposed as `ArcoFission reveal FILE at X86_64`) walks a single A-MIR
function and lowers it to real x86-64 machine code using that encoder. This is the first work
package in the mission to produce actual machine code rather than an intermediate representation.

## Scope

Per Packet WP-008's non-goals ("optimization," "register allocator sophistication beyond
correctness," "general-purpose instruction selection"), this is deliberately not a general
compiler backend. It supports exactly the A-MIR shapes the hello-world program produces:

- A single straight-line block (no branches; `IF`/`WHILE`/`FOR`/etc. produce multiple A-MIR blocks
  and are rejected with a clear error rather than silently mishandled).
- `CONST` for string literals (UTF-16 encoded per WP-007, placed in a data section, referenced by
  a RIP-relative `LEA`) and exact integer literals (loaded via a 64-bit immediate `MOV`).
- `LOAD` of a named value.
- `CALL_EXTERNAL` through a UEFI-bound field chain (WP-006), including injecting the implicit
  `This` argument real UEFI protocol methods require (see "Implicit This Argument" below --
  this closes the gap `docs/systems/uefi-bindings.md` flagged as deferred).
- `RETURN` of a value or of nothing.
- Function parameters passed in registers (the first four, per
  `docs/systems/calling-conventions.md`); a 5th+ stack-passed parameter is rejected with a clear
  error rather than silently mishandled, since no hello-world-shaped function needs one.

Any other A-MIR instruction kind, or any of the above cases outside what is listed, produces a
clear `ok = false` error naming what is unsupported, per Packet section 13's diagnostic quality
bar -- never a silently wrong encoding.

## Register Allocation: Uniform Spilling

Every named A-MIR value (each function parameter and each `%tN` temporary) gets its own 8-byte
stack slot, assigned once per function in first-appearance order (parameters first, then
instruction results in the order they are produced). Every value is stored to its slot immediately
after being computed and reloaded into `RAX` (or, for call arguments, directly into the argument
register) immediately before use. Nothing is ever kept live in a register across instructions.

This is the "simple spill-based implementation" Packet WP-008 explicitly permits. It is not
efficient -- the hello-world program reloads `systemTable` from the stack immediately after
spilling it there, for instance -- but it is easy to verify correct, which matters far more at this
stage than performance.

## Stack Frame Layout

```text
frame_size = shadow_space (32) + 8 * (number of named values)
```

rounded up so that `frame_size % 16 == kEntryRspMod16 (8)` -- the same invariant
`docs/systems/calling-conventions.md` documents (`RSP % 16 == 8` at function entry, so
`sub rsp, frame_size` must leave `RSP` 16-byte aligned before any `CALL` this function makes).
Slots are assigned starting immediately after the 32-byte shadow space, in the same order values
were collected. For the hello-world's `Main(imageHandle, systemTable)`:

```text
shadow space        [rsp+0x00 .. 0x1F]  (32 bytes, reserved for calls this function makes)
imageHandle          rsp+0x20
systemTable          rsp+0x28
%t0 (string ptr)      rsp+0x30
%t1 (call result)     rsp+0x38
%t2 (return value)    rsp+0x40
                                          total: 0x48 (72) bytes
```

## Implicit `This` Argument

`docs/systems/uefi-bindings.md` flagged that `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString`'s real
C signature takes an implicit `This` pointer before the explicit `String` argument, while
ArcoBASIC's `systemTable.ConsoleOut.Write("...")` surface only shows one explicit argument. This
work package closes that gap: when lowering a `CALL_EXTERNAL` whose resolved field has
`implicit_this_argument = true`, the resolved receiver pointer (already in `RAX` from walking the
field chain) is moved into argument-location 0 (`RCX`) before the explicit arguments are loaded
into locations 1, 2, ... This is exactly why `assign_argument_locations` is called with
`explicit_arg_count + 1` rather than `explicit_arg_count` for such calls.

## Verification

Every encoder primitive in `include/arco/x86_64_encoder.hpp` is unit-tested in
`tests/runtime_tests.cpp` against byte sequences independently produced by `nasm -f bin` (not
derived from the same reasoning as the encoder -- an genuinely separate tool). The full hello-world
function's generated code was verified two ways beyond that:

1. **Against a hand-written `nasm` reference implementing the identical instruction sequence**
   (`sub rsp, 0x48` / spill params / `lea` the string / walk `ConsoleOut`/`OutputString` / indirect
   call / materialize and return `0` / epilogue), using `strict qword` to force the same
   full-width `MOV reg64, imm64` encoding the code generator uses (nasm's default optimizer
   would otherwise pick a shorter 5-byte form for the value `0`, which is not wrong but would not
   match what the "no optimization" code generator emits). Every byte matches exactly except the
   still-unpatched relocation placeholder (4 zero bytes where the generator leaves a `LEA`'s
   `disp32` field for a later linking step to fill in): the generator's relocation record
   (`disp_field_offset=0x11`, `instruction_end_offset=0x15`, `rdata_offset=0`) reproduces nasm's
   own computed displacement (`0x37`) exactly under the same contiguous text-then-rdata layout
   nasm used (`(instruction_end + text_size) - instruction_end == text_size == 0x4C`; nasm's
   computed value was `0x37 = 0x4C - 0x15`... i.e. `rdata_target(0x4C) - instruction_end(0x15) =
   0x37`, matching bit for bit).
2. **Against `objdump -D -b binary -m i386:x86-64 -M intel`** (a disassembler from a completely
   different toolchain than nasm, decoding the generator's raw output bytes rather than comparing
   against independently-assembled bytes) -- it decodes the generated `.text` bytes into exactly
   the intended instruction sequence with no unknown-opcode gaps or misalignment.

`tests/systems_x86_64_codegen_smoke.sh` golden-tests the full, exact hex dump of
`ArcoFission reveal .../hello.abas at X86_64`'s output against this already-verified baseline, so
it does not itself require nasm or objdump to be installed -- those were verification tools used
once while writing this work package, not a new dependency (consistent with
`docs/systems/uefi-target.md` section 10's "no new dependency" decision; nasm/objdump were already
noted there as available on the development host for diagnostic use, never as a build requirement).

## What This Work Package Does Not Do

- Does not patch relocations or lay out a final image -- the `LEA`'s `disp32` field is left as a
  documented placeholder (`Relocation{disp_field_offset, instruction_end_offset, rdata_offset}`)
  for WP-009 to resolve once it knows where `.text` and `.rdata` actually sit in a PE image's
  virtual address space.
- Does not produce a PE32+ file, ELF, or any other container format -- `render_x86_64` emits a
  deterministic disassembly-style text dump for diagnostics and golden testing (Packet section 3.2
  allows generated assembly for diagnostics), and the underlying raw bytes stay internal to
  `compiler/fission.cpp` until WP-009 needs them.
- Does not support any control flow (`IF`, loops, `TRY`), classes, arrays/objects, or any A-MIR
  instruction kind beyond the five listed in Scope. Extending coverage is future work, not silently
  assumed to already work.
