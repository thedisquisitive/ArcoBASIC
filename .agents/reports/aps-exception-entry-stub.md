# APS Exception-Entry Stub

## Scope delivered

Closed the "remaining entry-ABI gate" from `aps-owned-idt.md`: a common exception-entry stub now
exists, so the 256-entry IDT it built no longer has to share one meaningless handler address
across all 32 architectural vectors.

The compiler synthesizes a fixed exception-vector table for the x86-64 systems target: 32
fixed-stride (16 byte) per-vector micro-stubs followed by one shared handler, hand-assembled
machine code rather than lowered from A-MIR (no ordinary ArcoBASIC `FUNCTION` uses an interrupt
ABI). Each micro-stub first normalizes the CPU's inconsistent error-code push -- pushing a
placeholder 0 when the vector has none -- then pushes its own vector number and jumps to the
shared handler, so every vector reaches it with an identical stack shape regardless of whether
hardware pushed a real error code. The shared handler saves all 15 general-purpose registers
(RSP is restored from the CPU-pushed frame, never saved separately), and implements the first
concrete recovery path: vector 3 (`#BP`, breakpoint/`INT3`) steps the saved return `RIP` past the
one-byte `INT3` opcode and resumes via `IRETQ`. Every other vector is treated as an unexpected
fault and parks the processor (`CLI; HLT` loop) rather than resuming into undefined state.

Policy code addresses the table through a new intrinsic, `CPU.ExceptionVectorTableBase() AS U64`,
returning vector 0's entry point; vector *N*'s entry is `base + N*16`. It lowers to a RIP-relative
`LEA`, resolved through the same `internal_calls` fixup mechanism ordinary function-to-function
calls already use (the "next-instruction-relative disp32" math is identical for `CALL rel32` and
`LEA reg, [rip+disp32]`, so no new relocation kind was needed). The table itself is only appended
to a compiled program's `.text` when something actually calls the intrinsic -- most programs never
will, and it would otherwise silently add ~600 bytes to every UEFI binary.

`stdlib/descriptor_table_policy.abas`'s `BuildMinimalIDT` now installs a distinct, vector-aware
handler for every gate (`CPU.ExceptionVectorTableBase() + vector*16`) instead of one shared
address for all 32; its `handler` parameter is gone since it no longer needs one supplied.

New encoder primitives (`arco/x86_64_encoder.hpp`): `push_reg`/`pop_reg` (r64, both legacy and
REX.B-extended forms), `push_imm8`, `iretq`, `inc_rax`, `nop`.

## Validation

- exact encoder unit tests for every new primitive (`push_reg`/`pop_reg`/`push_imm8`/`iretq`/
  `inc_rax`/`nop`), byte-for-byte against the x86-64 architecture encoding;
- A-MIR reveal smoke: `CPU.ExceptionVectorTableBase()` lowers to `MEMORY.EXCEPTIONVECTORTABLEBASE`;
- x86-64 reveal smoke (`systems_arco_basic_exception_entry_smoke.sh`): the internal-call fixup
  resolves to `$CPU.ExceptionVectorTable`; a golden 636-byte `TEXT` size for the fixture pins the
  table's shape; vector 3's stub is confirmed to push a placeholder error code (`6a 00 6a 03 e9`)
  and vector 14's (`#PF`) is confirmed not to (`6a 0e e9`); the shared handler is confirmed to end
  in the full register-restore sequence and `IRETQ`, never `RET`;
- every stub and handler offset was additionally hand-verified against a real compiled fixture's
  hex dump (jump/branch targets, the JNE-to-fault-park and JMP-to-restore displacements, the
  15-register push/pop symmetry) -- this is genuinely new machine-code territory (no prior
  ArcoBASIC-generated code uses an interrupt-return ABI), so it was checked byte-by-byte rather
  than trusted from the encoder primitives alone;
- `stdlib/descriptor_table_policy.abas` still compiles cleanly with the new `BuildMinimalIDT`
  (`systems_arco_basic_descriptor_policy_smoke`);
- full existing test suite (39/39) still passes, including `systems_x86_64_codegen_smoke`'s golden
  byte count for a program that does *not* reference the table -- confirming the table is
  reference-gated, not unconditionally appended.

## Remaining activation gate

The live CR3-cutover fixture still does not load this IDT (same gate `aps-owned-gdt.md` and
`aps-owned-idt.md` already recorded for the GDT and the IDT construction itself -- none of the
three descriptor-table policies are wired into the live QEMU/OVMF proof yet).

The shared handler recovers from exactly one vector (3, breakpoint) as a proof that the whole
round trip works end to end; every other vector still just parks the processor. A real fault
policy per vector -- and a way for the recovered path to report state back out (the "recoverable
breakpoint proof" `aps-owned-idt.md` flagged this work as being for) rather than just resuming --
is still future work. `CPU.Breakpoint` deliberately executing `INT3` and observing a live resume
via this table, under QEMU, is the natural next validation step once the IDT is actually loaded.
