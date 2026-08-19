# APS Exception-Entry Stub

## Scope delivered

Closed the "remaining entry-ABI gate" from `aps-owned-idt.md`: a common exception-entry stub now
exists, so the 256-entry IDT it built no longer has to share one meaningless handler address
across all 32 architectural vectors -- **and it is now validated end to end on a real CPU under
QEMU/OVMF**, not just in isolated compile-time checks.

The compiler synthesizes a fixed exception-vector table for the x86-64 systems target: 32
fixed-stride (16 byte) per-vector micro-stubs followed by one shared handler, hand-assembled
machine code rather than lowered from A-MIR (no ordinary ArcoBASIC `FUNCTION` uses an interrupt
ABI). Each micro-stub first normalizes the CPU's inconsistent error-code push into a uniform
frame, then pushes its own vector number and jumps to the shared handler. The shared handler saves
all 15 general-purpose registers, recovers from vector 3 (`#BP`, breakpoint/`INT3`) by resuming
immediately via `IRETQ` (see "bug fixed" below), and parks the processor (`CLI; HLT` loop) for
every other vector rather than resuming into undefined state.

Policy code addresses the table through `CPU.ExceptionVectorTableBase() AS U64`, lowered to a
RIP-relative `LEA` resolved through the same `internal_calls` fixup mechanism ordinary calls use.
The table is only appended to a compiled program's `.text` when actually referenced.
`stdlib/descriptor_table_policy.abas`'s `BuildMinimalIDT` installs a distinct, vector-aware handler
per gate (`CPU.ExceptionVectorTableBase() + vector*16`); `IDTWriteInterruptGate`'s code-segment
selector is caller-supplied (via the new `CPU.ReadCS()` intrinsic) rather than a hardcoded
constant, since a hardcoded value is only valid when the caller has *just replaced the GDT* --
reusing the firmware's own GDT (this proof's approach, since a safe GDT replacement still needs a
CS-reload/far-jump mechanism this backend doesn't have) requires reading back whichever selector
is already loaded.

New encoder primitives (`arco/x86_64_encoder.hpp`): `push_reg`/`pop_reg` (r64, both legacy and
REX.B-extended forms), `push_imm8`, `iretq`, `inc_rax`, `nop`, `mov_rax_cs`.

## Three real bugs found and fixed getting here

Getting from "the table's bytes look right in isolation" to "a deliberate breakpoint actually
recovers under QEMU" surfaced three separate, previously-undiscovered, genuinely impactful bugs --
none of them specific to this feature. All three are now covered by executed (not just
compile-time) regression tests, because that is exactly the kind of check that would have caught
each one and didn't exist before.

1. **`INT.SHR`/`INT.SHL` with a non-constant shift count silently discarded the shift and returned
   the original, unshifted value.** The x86-64 lowering computed the shift into `RAX`, then
   immediately reused `RAX` as scratch for the out-of-range-shift-count safety check, clobbering
   the real result before it was ever stored -- so `x SHR n` silently became `x`. This affected
   *every* dynamic-shift-count `SHR`/`SHL` in the freestanding backend, not something narrow.
   Fixed by shifting into `R8` instead (matching the already-correct `SAR` lowering right next to
   it), leaving `RAX` free for the safety-check scratch work it actually needs.
   (`arcology-os/tests/fixtures/integer-core/shift-correctness.abas`,
   `systems_arco_basic_shift_correctness_smoke.sh`.)

2. **`sub`/`add rsp, imm8` used the sign-extending one-byte-immediate opcode (`0x83`) for any
   frame_size up to 255, but that encoding is only correct for 0-127.** A frame_size of, say, 200
   encoded as that raw byte is read by the CPU as -56, turning "allocate a 200-byte frame" into
   "grow the stack upward by 56 bytes into the caller's own frame" -- silent stack corruption on
   entry to *any* function whose frame landed in [128, 255] bytes, an extremely ordinary size.
   Fixed by lowering the threshold to 127. (`arcology-os/tests/fixtures/integer-core/
   large-frame-call.abas`, `systems_arco_basic_large_frame_call_smoke.sh` -- also fixed an
   existing test, `systems_uefi_bindings_smoke.sh`, whose hex-dump line-wrap-sensitive grep this
   correctness fix's byte-count change incidentally broke.)

3. **This table's own `#BP` recovery incremented the saved return `RIP` by one, on the assumption
   that `INT3` behaves like a fault** (CPU points at the faulting instruction, handler must skip
   past it to retry). `#BP` is a *trap*, not a fault: the CPU already pushes the RIP of the
   instruction *after* the one-byte `0xCC` opcode. The extra `+1` resumed one byte late --
   harmless by chance when what followed happened to decode tolerably from a shifted offset, and
   silently catastrophic (a wild jump, `#UD`, eventual QEMU shutdown from `-no-reboot`) whenever a
   `CALL` immediately followed `CPU.Breakpoint`, since resuming into `CALL+1` executes the
   instruction's own displacement bytes as an opcode. Removed the adjustment entirely; the
   recovery path is now just "confirm vector 3, restore registers, IRETQ."

## Validation

- exact encoder unit tests for every new primitive, byte-for-byte against the x86-64 architecture
  encoding, including `mov_rax_cs`;
- A-MIR/x86-64 reveal smoke (`systems_arco_basic_exception_entry_smoke.sh`): the internal-call
  fixup resolves to `$CPU.ExceptionVectorTable`; a golden 612-byte `TEXT` size for the fixture
  pins the table's shape; vector 3 and vector 14 (`#PF`) are confirmed to differ in whether they
  push a placeholder error code; the shared handler is confirmed to end in the full
  register-restore sequence and `IRETQ`, never `RET`;
- `systems_arco_basic_shift_correctness_smoke.sh` and `systems_arco_basic_large_frame_call_smoke.sh`:
  QEMU/OVMF-executed regression tests for bugs 1 and 2 above, checked against the actual numeric
  result / actual absence of stack corruption, not just compiled shape;
- **`systems_arco_basic_breakpoint_recovery_smoke.sh`: the real end-to-end proof.** Builds the full
  APS CR3-cutover identity-mapped hierarchy, exits boot services, installs the IDT, executes
  `CPU.Breakpoint`, and confirms "APS BP RECOVERED" -- printed by a *called function*, deliberately,
  so an off-by-one resume address would corrupt the call and never reach it -- appears in the real
  QEMU/OVMF serial output. Verified deterministic across repeated runs, not flaky;
- `stdlib/descriptor_table_policy.abas` still compiles cleanly with the new `BuildMinimalIDT`
  signature (`systems_arco_basic_descriptor_policy_smoke`);
- full existing test suite (42/42) passes.

## Remaining activation gate

The three bugs above are fixed and the mechanism is proven correct on a real CPU. What's still
open, matching `aps-owned-gdt.md`'s own note: this proof deliberately does **not** replace the
firmware's GDT or load a TSS (an IDT gate with IST=0, used throughout, never consults the TSS, and
a *replacement* GDT still needs a CS-reload/far-jump mechanism this backend doesn't have yet). Only
vector 3 has a real recovery policy; every other vector still just parks the processor. Wiring this
into `aps-cr3-cutover.abas` itself (the canonical, documentation-referenced fixture, left untouched
by this work) rather than a dedicated proof fixture is the natural next integration step.
