# APS Timer and Interrupt Routing (RFC-0036)

## Scope delivered

APS can now receive, acknowledge, and count a real hardware device interrupt for the first time.
Implements RFC-0036 (`arcology-os/rfcs/RFC-0036_APS_Timer_and_Interrupt_Routing_Service.md`) in
full: PIC remap, PIT channel-0 programming, an IRQ dispatch branch in the compiler-synthesized
interrupt-entry table (previously exception-only, vectors 0-31; now covers vectors 0-47), a new
`CPU.InterruptPendingTableBase()` intrinsic, and the `PIC.*`/`Timer.*` ArcoBASIC surface.

Everything past `aps-exception-entry-stub.md`/`aps-gdt-reload.md`/`aps-emergency-stack.md`'s proven
GDT/IDT/IST mechanisms was terminal (`CPU.HaltForever`); this is the first proof in the chain where
APS keeps running and reacts to something asynchronous.

### Compiler changes (`src/compiler/fission.cpp`)

- `generate_exception_vector_table()`: `kVectorCount` 32 -> 48. Vectors 32-47 get the same
  fixed-16-byte-stride, placeholder-error-code-push stub shape every no-error-code exception vector
  already gets -- `x86_64_vector_has_error_code()` already defaults to `false` for anything not in
  its explicit exception list, so no change was needed there.
- The shared handler gained a third dispatch branch, `cmp rax,32; jae irq_dispatch`, inserted
  between the existing `#BP` (vector 3, resume) and `#DE` (vector 0, IST-probe) branches. For any
  vector >= 32: increments `pending_table[vector-32]` (RFC-0036 Requirement 6.4), sends EOI to the
  slave PIC first if the vector is in the remapped IRQ8-15 range (>= 40), then always to the
  master (Requirement 6.3), then resumes. This is structurally the ISR's entire payload -- it never
  calls into ArcoBASIC code, per RFC-0037 Requirement 6.2's top-half/bottom-half split, which this
  RFC's own design anticipates even though RFC-0037 itself is still unimplemented.
- No new x86-64 encoder primitive was needed for any of this (RFC-0036's own Testing Strategy
  correctly predicted this) -- the increment sequence and EOI port writes are built entirely from
  primitives already exercised by this table and by `aps-emergency-stack.md`'s probe:
  `mov_reg_reg`, `mov_reg_imm64`, `sub_reg_reg`, `shl_reg_imm8`, `add_reg_reg`, `mov_load64_rax`,
  `mov_store64_rax_from_rcx`, `out_dx_al`.
- New intrinsic `CPU.InterruptPendingTableBase() AS U64`, wired through both AMIR lowering sites
  (`lower_expression`'s `MemoryOperation` case and `lower_call`, matching `CPU.Interrupt`'s existing
  dual-path precedent) and the `Memory` codegen case. Unlike `CPU.ExceptionVectorTableBase()`, this
  does **not** resolve through the cross-symbol `internal_calls`/relocation mechanism -- it lowers
  to a single `mov rax, imm64` of a fixed constant. The reason is structural, not a shortcut: this
  project's PE writer (`arcology-os/src/compiler/pe_image.cpp`) marks `.text`
  `kSectionCode|kSectionMemExecute|kSectionMemRead` and `.rdata` `kSectionInitializedData|
  kSectionMemRead` -- neither section is writable at runtime, and this table must be *written* by
  the shared handler on every tick. `CPU.InterruptPendingTableBase()` returns a fixed low-memory
  scratch address (`0x2010000`, 16 entries x 8 bytes) instead, exactly the technique RFC-0036
  Requirement 6.4 calls out by name: "exactly as `aps-emergency-stack.md`'s scratch-address
  technique worked... except this is real, documented, versioned ABI surface."
- The exception table's golden byte count moved from 644 to 1045 bytes (documented, not silently
  drifted -- `systems_arco_basic_exception_entry_smoke.sh` and the new
  `systems_arco_basic_timer_tick_smoke.sh` both assert it).

### ArcoBASIC policy (`arcology-os/stdlib/timer_policy.abas`)

`PIC.Remap`/`PIC.Mask`/`PIC.Unmask`, `PITProgramChannel0`, `Timer.Ticks`/`Timer.FrequencyHz`/
`Timer.UptimeMilliseconds`/`Timer.Initialize`, all built only from `PORT.*` primitives, per RFC-0036
Requirement 6.5 -- no new compiler mechanism required for any of it beyond the two items above.

One real, deliberate deviation from RFC-0036 Section 6.5's own illustrative signature
(`Timer.Initialize(tickRateHz AS U32) AS BOOL`): the implemented signature is
`Timer.Initialize(idtTable AS MMIOPTR, codeSelector AS U16, tickRateHz AS U32) AS BOOL`. Installing
a live IDT gate for vector 32 genuinely requires the table's address and a valid code selector --
exactly as `IDTWriteInterruptGate` already requires them everywhere else in this codebase -- and
RFC-0036's own Section 6.5 code block is a developer-experience illustration, not a byte-for-byte
contract (the same status Section 15's own `PICRemap` sketch explicitly claims for itself). Flagged
here rather than silently diverging.

`Timer.FrequencyHz()` stores the achieved frequency in a second, policy-private fixed scratch
address (`0x2018000`) rather than a language-level global variable -- the freestanding profile has
no general global-variable mechanism, and this is the same small-persistent-state pattern
`CPU.InterruptPendingTableBase()` itself formalizes, just not promoted to compiler ABI since
nothing outside ArcoBASIC policy needs to address it.

## One real, general compiler bug found and fixed, unrelated to timers specifically

**A trailing same-line comment after a function's final `RETURN <expression>` broke the build
outright** (`EFI BUILD FAILED`, not just a diagnostic) for *any* ArcoBASIC function, freestanding or
hosted, timer-related or not. Found because `stdlib/timer_policy.abas`'s natural writing style
(inline comments explaining a constant right after the line that uses it) happened to put one on a
function's last line.

Root cause, confirmed via `reveal ... at AST`: `RETURN <expr>   ' comment` parses as **two sibling
statements** in the function body -- a `Return` node followed by a separate `Comment` node -- not
one. `lower_statement` (`fission.cpp`) unconditionally pushed a source-position marker instruction
(`amir_source`) for *every* statement before dispatching on its kind, including `Comment`/`NoOp`,
which otherwise lower to nothing. When the trailing comment was the last statement in a block, its
marker instruction became the block's new last instruction -- non-terminal, sitting immediately
after the genuinely terminal `RETURN` -- which `validate_module`'s own correctness check flagged as
"instruction after terminal operation" and refused to build. Confirmed general (not timer- or
`MemoryOperation`-specific) with minimal repros: a bare literal `RETURN 42   ' x`, a binary
expression, and an ordinary user-function call all reproduced it identically; only the *position*
(last statement in a block) mattered.

Fixed by having `lower_statement` return immediately for `Comment`/`NoOp` nodes before pushing the
source marker (their existing line-label handling, needed for `GOTO` targets on blank/comment
lines, runs first and is unaffected). Verified against all ten variant repros plus a full,
real `EFI BUILD FAILED` -> succeeds round trip; the exception-vector table's own golden byte count
(1045) is unchanged by this fix, confirming it is purely a debug-marker bookkeeping change with no
codegen effect. Full existing suite (44/44 before this fix, all still passing after) plus the new
timer test (45/45) all pass.

## Validation

- `ctest`, full suite: 45/45 passing (44 pre-existing + the new timer test), including every prior
  exception-entry/GDT/IST proof unmodified in behavior.
- Reveal-level: `CPU.InterruptPendingTableBase()` lowers to `INTERNAL_CALLS 0` (no relocation) and
  the exact `mov rax, 0x2010000` byte sequence; the extended table's golden byte count (1045) is
  asserted in two independent test scripts.
- **The real proof, executed under QEMU/OVMF** (`aps-timer-tick.abas`): builds the same minimal
  identity-mapped environment `aps-breakpoint-recovery.abas` established (no GDT/TSS replacement --
  RFC-0036's own Requirement 6.5 note that the timer doesn't need one, validated against the
  firmware's own GDT via `CPU.ReadCS()`), installs the vectors-0-31 IDT via `BuildMinimalIDT`,
  calls `Timer.Initialize` (PIC remap to base 32/40, PIT programmed for 100 Hz, IRQ0 gate
  installed, IRQ0 unmasked), enables interrupts, and spins on `CPU.Halt` until `Timer.Ticks()`
  reaches 100 -- RFC-0036's own literal acceptance criterion ("at least 100 real, hardware-
  delivered ticks counted correctly at the default rate"). Passed; verified deterministic across
  three repeated runs. A missing or wrong EOI would manifest as exactly one tick ever arriving
  (RFC-0036 Section 2's own stated failure mode) -- the loop would spin forever instead of reaching
  100, caught by the test harness's own timeout rather than a graceful in-fixture check, exactly as
  the RFC's Testing Strategy anticipated.

## Remaining activation gate

- Only IRQ0 (the timer) is ever unmasked, per RFC-0036's own Non-Goals -- every other line stays
  masked until its own driver RFC exists. The "does an unmasked-but-unhandled line actually hang
  the system" failure mode is deliberately *not* tested (RFC-0036 Section 16 explicitly calls this
  out as not a productive regression test); Requirement 6.1's masking-before-enabling order is
  enforced by construction (`PIC.Remap` masks everything; `Timer.Initialize` unmasks exactly line
  0) and code review, not a negative test.
- `Timer.Ticks()` is a monotonic counter incremented per real IRQ0 delivery, never cleared -- RFC-
  0036's Open Question about 64-bit overflow is accepted as a non-concern (unreachable at any sane
  tick rate) but not re-litigated here.
- RFC-0037 (the actual "APS loop" -- `CPU.EnableInterrupts` once, then dispatch forever) is the
  natural next step and the reason this RFC deliberately stopped at "count ticks," not "do
  anything with them." This fixture's own terminal `CPU.HaltForever` after the proof is exactly the
  gap RFC-0037 exists to close.
- The interrupt-pending table's scratch address (`0x2010000`) and `Timer.FrequencyHz`'s private
  scratch address (`0x2018000`) are both fixed, hand-chosen low-memory constants, the same pattern
  `aps-emergency-stack.md`'s probe used before being promoted to real ABI -- a real page/heap
  allocator (out of scope for every RFC in this chain so far) would be the eventual replacement for
  this whole "pick an address by hand and hope nothing else uses it" discipline.
