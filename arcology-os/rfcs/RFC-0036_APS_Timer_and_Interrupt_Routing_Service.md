# RFC-0036: APS Timer and Interrupt Routing Service

**RFC Number:** RFC-0036
**Title:** APS Timer and Interrupt Routing Service
**Status:** Implemented
**Category:** Substrate / Hardware Enablement
**Authors:** Arcology Project
**Created:** 2026-08-19
**Last Updated:** 2026-08-19
**Supersedes:** None
**Superseded By:** None
**Related RFCs:** RFC-0000, RFC-0005, RFC-0013, RFC-0017, RFC-0018, RFC-0019, RFC-0020, RFC-0037

------------------------------------------------------------------------

# 1. Executive Summary

The Arcology Polymorphic Substrate (APS) can install its own GDT, IDT, and TSS, recover from a
deliberate `#BP`, and switch onto a dedicated IST1 stack — all proven under real interrupt
delivery (`.agents/reports/aps-exception-entry-stub.md`, `aps-gdt-reload.md`,
`aps-emergency-stack.md`). None of that machinery has ever been driven by a real *device*. Every
proof to date ends in `CPU.HaltForever` — a terminal state by design. APS has no notion of time
passing and no periodic signal to build anything time-based on.

This RFC gives APS its first hardware device: a programmable interval timer, remapped and unmasked
safely alongside the legacy interrupt controller, delivering a periodic interrupt that increments a
monotonic tick counter. It is deliberately narrow: it does not schedule anything, run any
ArcoBASIC callback, or replace `CPU.HaltForever` as APS's terminal state. It produces one new,
observable fact — time is passing, and APS can prove it counted the ticks — and hands the *use* of
that fact to RFC-0037.

------------------------------------------------------------------------

# 2. Motivation

Every subsystem above this one needs a clock. A dispatch loop (RFC-0037) needs to wake up
periodically even with nothing else to do. A scheduler needs preemption. A filesystem or block
driver (RFC-0038) needs timeouts. Diagnostics need uptime. All of it is blocked on the same
missing primitive: a hardware interrupt APS can safely receive, acknowledge, and count.

x86-64 has no interrupt-free way to observe elapsed time with useful precision from ring 0 without
either polling a hardware counter in a tight loop (wasteful, and still needs *a* device to poll)
or receiving an interrupt. APS chose interrupts for exceptions already; time follows the same
shape.

This is also the first RFC in the project to route a *hardware* interrupt rather than a CPU
*exception*. The two share the IDT and, per this RFC's design, the same synthesized vector table
(`.agents/reports/aps-exception-entry-stub.md`), but they are not the same kind of event: an
exception is synchronous and attributable to the instruction that caused it; a hardware interrupt
is asynchronous, arrives from a device, and — critically — MUST be acknowledged
(end-of-interrupt) before another interrupt of equal or lower priority can be delivered again.
Getting that acknowledgment wrong does not fail loudly; it silently stops all future timer ticks,
which is exactly the kind of failure this RFC's testing strategy is built to catch immediately
rather than let masquerade as "the loop is just slow."

------------------------------------------------------------------------

# 3. Goals

- Remap the legacy 8259 PIC so its interrupt vectors cannot collide with the CPU exception vectors
  0-31 already owned by `.agents/reports/aps-exception-entry-stub.md`.
- Program the legacy PIT (8254) channel 0 for a configurable periodic rate.
- Extend the existing compiler-synthesized interrupt-vector table (previously scoped to
  exceptions only) to cover hardware interrupt vectors, with a shared IRQ handler that
  acknowledges the interrupt controller automatically — ArcoBASIC policy MUST NOT be responsible
  for remembering to send EOI.
- Expose a monotonic tick counter and elapsed-time queries to ArcoBASIC.
- Leave every other IRQ line masked. This RFC turns on exactly one device.

------------------------------------------------------------------------

# 4. Non-Goals

- The APIC/IOAPIC/LAPIC timer. The legacy 8259/8254 pair needs no ACPI table parsing, works
  identically under QEMU/OVMF and (per `physical-hardware-readiness-baseline.md`'s existing
  real-hardware discipline) real firmware, and is sufficient for a single logical processor.
  APIC support (needed for multi-core and higher-precision timing) is a Future Extension.
- A scheduler, preemption, or any decision about what runs when a tick arrives. That is RFC-0037's
  entire subject.
- Any IRQ line other than IRQ0 (the timer). Keyboard (IRQ1), the cascade line (IRQ2, reserved by
  the PIC wiring itself and never unmasked directly), and everything else stay masked until a
  driver RFC for that specific device exists.
- Wall-clock / calendar time (RTC, CMOS). This RFC produces elapsed ticks since APS took ownership
  of the timer, not a date.
- High-resolution timing (`RDTSC`, deadline timers, one-shot alarms). Future Extensions.

------------------------------------------------------------------------

# 5. Terminology

**PIC (Programmable Interrupt Controller):** The 8259A pair (master at ports `0x20`/`0x21`, slave
at `0xA0`/`0xA1`, cascaded through the master's IRQ2 line) that routes 16 hardware interrupt lines
(IRQ0-IRQ15) to the CPU. Its default vector mapping (IRQ0-7 -> vectors 8-15, IRQ8-15 -> vectors
112-119 on most firmware, though the mapping left by any given firmware MUST NOT be assumed) is
unsafe to use as-is because vectors 8-15 collide with real CPU exceptions (`#DF` is vector 8).

**PIT (Programmable Interval Timer):** The 8254, whose channel 0 output is wired to the PIC's IRQ0
line on the standard PC platform. Its input clock runs at 1193182 Hz (the "PIT frequency"),
divided down by a 16-bit reload value to produce the desired output frequency.

**Remap:** Reprogramming the PIC's interrupt-vector-base registers (via its initialization command
word sequence) so IRQ0-7 land at a chosen, exception-safe vector range and IRQ8-15 land
immediately after it.

**EOI (End Of Interrupt):** A command byte (`0x20`) written to the PIC's command port after
servicing an interrupt, without which the PIC will not deliver another interrupt at that priority
level or lower. Sent to the slave PIC first when the source IRQ is 8-15, then always to the master.

**Tick:** One timer interrupt. **Tick Rate:** Ticks per second, chosen at `Timer.Initialize`.

------------------------------------------------------------------------

# 6. Requirements

## 6.1 PIC remapping

APS MUST remap the PIC before unmasking any IRQ line. The vector base MUST be chosen so that
IRQ0-15 occupy vectors entirely outside 0-31 (the CPU exception range) and entirely inside the
range the interrupt-vector table from `.agents/reports/aps-exception-entry-stub.md` is extended to
cover (Requirement 6.3). Vector base 32 (IRQ0-7 -> 32-39, IRQ8-15 -> 40-47) is the reference
choice and SHOULD be used unless a future RFC needs the space between 32 and that base for
something else.

Immediately after remapping, APS MUST mask every IRQ line except IRQ0. Masking MUST happen before
`CPU.EnableInterrupts` is ever reached with the new IDT active — an unmasked, unhandled line
(every one of them, until their own driver RFCs land) firing into a vector whose gate parks the
processor (the existing "unrecognized vector" behavior) is a silent hang, not a crash, and MUST be
prevented rather than diagnosed after the fact.

## 6.2 PIT programming

Channel 0 MUST be programmed in Mode 3 (square wave, effectively periodic for interrupt purposes)
with a 16-bit reload value computed as `1193182 / tickRateHz`, rounded to the nearest integer.
`tickRateHz` MUST be in the range 19-1193182 (below 19 Hz the 16-bit reload value overflows; above
roughly a few kHz the interrupt overhead becomes the dominant cost on real firmware, per no
current profiling — treat the practical ceiling as a Future Extension concern, not a hard limit
enforced here). `Timer.Initialize`'s default, when no rate is supplied, MUST be 100 Hz (10 ms
ticks), matching the smallest well-precedented general-purpose default from prior art without
claiming any specific real-time guarantee.

## 6.3 Interrupt-vector table extension

The compiler-synthesized table this RFC builds on (`generate_exception_vector_table` in
`fission.cpp`, exposed as `CPU.ExceptionVectorTableBase()`) currently covers exactly the 32
architectural exception vectors. This RFC requires it to be extended to cover at least vectors
0-47 (32 exceptions + 16 remapped IRQ lines), following the same fixed-stride micro-stub design
already established: each new stub pushes its own vector number (IRQs never carry a hardware error
code, so every IRQ stub follows the placeholder-push shape already used for exception vectors
without one) and jumps to a shared handler.

The shared handler MUST distinguish an IRQ vector (>= 32 under the reference vector base) from an
exception vector (< 32) and, for an IRQ:

1. record that this specific IRQ line has a pending tick (a per-line pending count or flag,
   Requirement 6.4 — this is the ISR's *entire* payload; it MUST NOT call into arbitrary
   ArcoBASIC code, per RFC-0037 Requirement 6.2's top-half/bottom-half split);
2. send EOI: write `0x20` to the slave PIC's command port (`0xA0`) if the vector is IRQ8-15's
   remapped range, then always write `0x20` to the master's command port (`0x20`);
3. restore registers and `IRETQ`, exactly as the existing exception-recovery path does. A hardware
   interrupt is trap-class for `RIP` purposes in the same sense `INT3`/`INT n` are — the CPU was
   already between instructions when it was delivered, so no `RIP` adjustment applies, consistent
   with the finding recorded in `aps-exception-entry-stub.md`.

This RFC does not require renaming `CPU.ExceptionVectorTableBase()`; the symbol's *scope* is
widened, not its identity. Documentation SHOULD refer to it as "the interrupt-vector table" going
forward since it is no longer exception-only, but existing callers are unaffected.

## 6.4 Pending-interrupt visibility

APS MUST expose whether IRQ0 has a pending, unconsumed tick and MUST provide a way to consume it,
through a new zero-argument intrinsic mirroring the existing pattern:

```basic
CPU.InterruptPendingTableBase() AS U64
```

returning the address of a fixed-layout, compiler-synthesized table (one entry per IRQ line, same
"always emitted once, referenced by policy" discipline as the exception-vector table) that ArcoBASIC
policy reads and clears using ordinary `MEMORY.Read64`/`MEMORY.Write64`, exactly as
`aps-emergency-stack.md`'s scratch-address technique worked, except this is real, documented,
versioned ABI surface rather than a test-only hack. Layout and exact semantics (counter vs. flag,
overflow behavior) are an implementation decision left to the AI Implementation Guidance section,
not fixed here, provided the table's base is available through this single stable intrinsic and
its per-line stride and count are documented alongside `CPU.ExceptionVectorTableBase()`'s own
documentation once implemented.

## 6.5 ArcoBASIC-facing API

```basic
PIC.Remap(masterVectorBase AS U8, slaveVectorBase AS U8) AS BOOL
PIC.Mask(irq AS U8) AS BOOL
PIC.Unmask(irq AS U8) AS BOOL

Timer.Initialize(tickRateHz AS U32) AS BOOL
Timer.Ticks() AS U64
Timer.FrequencyHz() AS U32
Timer.UptimeMilliseconds() AS U64
```

`PIC.*` are ArcoBASIC policy (`stdlib/`, using only the existing `PORT.Address`/`PORT.Offset`/
`PORT.ReadByte`/`PORT.WriteByte` primitives — no new compiler mechanism is needed for PIC or PIT
programming, only for the interrupt-vector table extension and the pending-table intrinsic above).
`Timer.Initialize` composes `PIC.Remap`, PIT programming, `IDTWriteInterruptGate`-style
installation of the IRQ0 gate (reusing `BuildMinimalIDT`'s existing per-gate construction, adding
one gate rather than a new mechanism), and `PIC.Unmask(0)`, in that order, and MUST leave
interrupts disabled on return — enabling them is the caller's decision (RFC-0037's loop is the
first caller expected to do so). `Timer.Ticks()` reads and does not clear the pending table;
`Timer.UptimeMilliseconds()` is `Ticks() * 1000 / FrequencyHz()`, computed with the systems
integer-division primitive already validated by `systems_arco_basic_shift_correctness_smoke.sh`'s
sibling correctness work.

------------------------------------------------------------------------

# 7. Architecture

```text
                         +-----------------------------+
                         |  PIT channel 0 (port 0x40)   |
                         |  reload = 1193182 / rateHz   |
                         +---------------+---------------+
                                         | IRQ0 (hardware line)
                                         v
+----------------------+      +-----------------------------+
|  PIC master (0x20)   |<-----| Remapped vector base (=32)  |
|  remapped, IRQ0 live |      | IRQ0 -> vector 32            |
|  everything else     |      +---------------+---------------+
|  masked               |                     |
+----------------------+                     v
                          +---------------------------------------------+
                          | Interrupt-vector table (extended, .../48)   |
                          | vector 32 stub: push 32; jmp shared handler |
                          +---------------------+-------------------------+
                                                |
                                                v
                          +---------------------------------------------+
                          | Shared handler, IRQ branch (NEW):            |
                          |  1. mark IRQ0 pending (interrupt pending tbl)|
                          |  2. EOI -> PIC master                        |
                          |  3. restore registers, IRETQ                 |
                          +---------------------------------------------+
                                                |
                                                v (observed later, not from the ISR)
                          +---------------------------------------------+
                          | Timer.Ticks() / UptimeMilliseconds()         |
                          | -- read by RFC-0037's loop each wake         |
                          +---------------------------------------------+
```

The vertical line at the bottom is deliberate: nothing downstream of "mark pending, EOI, IRETQ"
runs inside interrupt context. RFC-0037 owns everything below that line.

------------------------------------------------------------------------

# 8. User Experience

None directly — this RFC has no UI-facing surface. It is a precondition for every subsystem that
does (a responsive desktop needs a live clock; RFC-0037's loop needs one to exist at all).

------------------------------------------------------------------------

# 9. Developer Experience

```basic
Timer.Initialize(100)          ' 100 Hz, 10ms ticks
CPU.EnableInterrupts
LET start AS U64 = Timer.Ticks()
WHILE Timer.Ticks() < start + 500
    CPU.Halt
WEND
' ~5 seconds have elapsed
```

`Timer.Initialize`'s return value is `FALSE` if PIC remap or PIT programming detects an invalid
argument (out-of-range `tickRateHz`); it does not raise a language-level exception, consistent
with how other freestanding-profile mechanisms in this codebase report failure (`BuildMinimalIDT`,
`AllocatePages` wrappers) — callers check the boolean.

------------------------------------------------------------------------

# 10. Security Considerations

An unmasked, misrouted, or unacknowledged interrupt line is a denial-of-service against the entire
substrate (nothing else can run once the CPU is stuck servicing, or waiting to service, a
malformed interrupt stream) — this is why Requirement 6.1 makes masking-before-enabling and
Requirement 6.3 makes automatic EOI non-negotiable rather than caller discipline. There is no
untrusted input at this layer (the PIT and PIC are trusted platform devices, not attacker-
controlled), so this RFC's security surface is entirely about APS not shooting itself.

------------------------------------------------------------------------

# 11. Privacy Considerations

None. A tick counter carries no user data.

------------------------------------------------------------------------

# 12. Accessibility Considerations

None directly. A reliable timer is an eventual precondition for accessible timing-sensitive UI
(configurable animation/response timing, assistive-technology polling intervals) once RFC-0037 and
the desktop layers above it exist, but this RFC has no UI surface of its own.

------------------------------------------------------------------------

# 13. Performance Considerations

At the reference default of 100 Hz, the ISR (register save, one pending-table write, one or two
port writes for EOI, register restore, `IRETQ`) runs roughly a few hundred cycles, on the same
order as the already-measured exception-recovery path — a small, bounded, constant cost per
second regardless of what else APS is doing between ticks. Higher tick rates trade responsiveness
for that fixed per-tick overhead multiplied by rate; this RFC does not attempt to characterize
that tradeoff numerically (no profiling infrastructure exists yet) and leaves rate selection to
the caller.

------------------------------------------------------------------------

# 14. Compatibility

Additive. No existing IDT gate, hardware semantic, or ArcoBASIC surface changes meaning. The
interrupt-vector table's widened scope is backward compatible: every existing exception-vector
consumer (`aps-breakpoint-recovery.abas`, `aps-gdt-reload.abas`, `aps-emergency-stack.abas`)
continues to work unmodified, since vectors 0-31's stubs and the exception branch of the shared
handler are unchanged.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
' stdlib/pic_policy.abas (illustrative, not normative byte-for-byte)
FUNCTION PICRemap(masterBase AS U8, slaveBase AS U8) AS BOOL
    LET masterCmd AS IOPORT = PORT.Address(32)   ' 0x20
    LET masterData AS IOPORT = PORT.Address(33)  ' 0x21
    LET slaveCmd AS IOPORT = PORT.Address(160)   ' 0xA0
    LET slaveData AS IOPORT = PORT.Address(161)  ' 0xA1
    PORT.WriteByte(masterCmd, 17)   ' ICW1: begin init, cascade mode
    PORT.WriteByte(slaveCmd, 17)
    PORT.WriteByte(masterData, masterBase)  ' ICW2: vector base
    PORT.WriteByte(slaveData, slaveBase)
    PORT.WriteByte(masterData, 4)   ' ICW3: slave attached at IRQ2
    PORT.WriteByte(slaveData, 2)
    PORT.WriteByte(masterData, 1)   ' ICW4: 8086 mode
    PORT.WriteByte(slaveData, 1)
    PORT.WriteByte(masterData, 255) ' mask everything for now
    PORT.WriteByte(slaveData, 255)
    RETURN 1
END FUNCTION
```

Full PIT programming, gate installation, and the pending-table read/clear follow the same shape as
`stdlib/descriptor_table_policy.abas`'s existing conventions and are left to implementation, not
reproduced here in full.

------------------------------------------------------------------------

# 16. Testing Strategy

- Encoder-level: none new (this RFC introduces no new x86-64 encoder primitives; PIC/PIT
  programming is ordinary `PORT.*` calls already validated).
- Reveal-level: confirm the extended interrupt-vector table's byte layout for vector 32's stub
  (push-vector, jump-to-shared-handler, matching the existing golden-byte-count discipline from
  `systems_arco_basic_exception_entry_smoke.sh`) and that the shared handler's IRQ branch contains
  the expected EOI port write.
- **QEMU/OVMF-executed, matching this project's own established standard for this kind of claim**:
  a fixture that calls `Timer.Initialize(100)`, enables interrupts, spins on `CPU.Halt` until
  `Timer.Ticks()` reaches a threshold, and prints a marker through the serial port — proving ticks
  are real, counted, and that EOI is genuinely being sent (a missing or wrong EOI manifests as
  exactly one tick ever arriving, an easy, specific regression to catch).
- A negative test: deliberately do *not* mask other IRQ lines and confirm... this is intentionally
  **not** attempted as an automated test. Provoking an unmasked, unhandled IRQ on real or emulated
  hardware to prove the failure mode is not a productive regression test (it would need to
  reliably trigger a spurious/real interrupt on a line nothing drives); Requirement 6.1's masking
  order is enforced by code review and the reference implementation's own construction instead.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

## 17.1 Implementation boundaries

In scope: PIC remap policy, PIT programming policy, extending `generate_exception_vector_table`
(or an equivalent successor) to cover vectors 32-47 with an IRQ dispatch branch, the
`CPU.InterruptPendingTableBase()` intrinsic, `Timer.*`/`PIC.*` ArcoBASIC surface. Out of scope:
anything in Non-Goals (Section 4), and no change to the existing exception dispatch branches for
vectors 0-31.

## 17.2 Prohibited implementation choices

- Sending EOI from ArcoBASIC policy instead of the compiler-generated ISR. Forgetting it is a
  silent, hard-to-diagnose hang; it MUST be structurally impossible to forget, which means it
  MUST live in the always-emitted shared handler, not in something a policy author can omit.
- Unmasking any IRQ line other than 0 as part of this RFC's implementation.
- Reusing vector numbers already assigned to CPU exceptions (0-31) for any IRQ line, under any
  vector-base choice.

## 17.3 Required deliverables

- Extended interrupt-vector table generator with IRQ dispatch branch.
- `CPU.InterruptPendingTableBase()` intrinsic, wired through both AMIR lowering sites following
  the existing dual-path convention (`lower_expression`'s `MemoryOperation` case and `lower_call`).
- `stdlib/pic_policy.abas`, `stdlib/pit_policy.abas` (or a combined `timer_policy.abas`).
- Encoder/reveal unit tests for the extended table's byte layout.
- A QEMU/OVMF-executed fixture and matching `systems_arco_basic_*_smoke.sh` test, following this
  project's own established pattern (`systems_arco_basic_breakpoint_recovery_smoke.sh`,
  `systems_arco_basic_gdt_reload_smoke.sh`, `systems_arco_basic_emergency_stack_smoke.sh`).
- An implementation report under `.agents/reports/`, including an honest "remaining activation
  gate" section in the same style as the three reports this RFC is built on.

## 17.4 Acceptance criteria

- `ctest` suite passes in full, including the new timer test.
- A QEMU/OVMF run demonstrates at least 100 real, hardware-delivered ticks counted correctly at
  the default rate, deterministic across repeated runs (matching this project's own established
  bar — see `aps-emergency-stack.md`'s explicit determinism verification).
- No existing exception-recovery fixture regresses.

## 17.5 Stop conditions

Stop and request human review before: choosing a vector base other than 32 without a documented
reason; changing the shared handler's register save/restore set (established and proven correct
by three prior reports — do not re-derive it); or discovering the target QEMU/OVMF environment's
PIC is not in the state this RFC assumes (masked-by-firmware is expected and handled; anything
else is a signal the environment differs from what was tested here).

## 17.6 Assumptions and dependencies

Depends on RFC-0005 (hardware semantics), the exception-entry table and its reports (a hard
prerequisite — this RFC extends that exact mechanism), and the GDT/IDT/TSS activation chain from
`aps-gdt-reload.md`/`aps-emergency-stack.md` for a realistic environment to test against, though
the timer itself does not require a replaced GDT — it can be validated standalone against the
firmware's own GDT, exactly as the very first breakpoint-recovery proof was.

------------------------------------------------------------------------

# 18. Future Extensions

- LAPIC/IOAPIC timer support for multi-core and higher precision.
- One-shot deadline timers and a sleep/delay API built on them.
- RTC (CMOS) wall-clock time.
- `RDTSC`-based sub-tick timing for profiling.
- Dynamic tick-rate adjustment (tickless/dynamic-idle designs) once RFC-0037's loop has enough
  real workload to make fixed-rate ticking a measured cost worth avoiding.

------------------------------------------------------------------------

# 19. Open Questions

- Should `Timer.Ticks()` overflow (at a 64-bit counter, effectively never at any sane tick rate)
  be a documented non-concern, or should the pending-table format reserve room for explicit
  overflow signaling now to avoid an ABI break later? Leaning toward "non-concern" (a 64-bit
  counter at 1 MHz doesn't wrap for over half a million years) but recorded here rather than
  assumed silently.
- Does a future capability/rights model (FMAP-0001's still-undesigned capability system) need the
  Timer service itself to be a rights-checked Resource (RFC-0017), or is it substrate-owned
  infrastructure exempt from that model the way the exception-vector table itself is? Deferred
  until the capability system has an RFC of its own.

------------------------------------------------------------------------

# 20. References

- `.agents/reports/aps-exception-entry-stub.md`, `aps-gdt-reload.md`, `aps-emergency-stack.md`
- `arcology-os/docs/systems/hardware-semantics.md`, `port-io-semantics.md`
- RFC-0005 (ArcoBASIC Hardware Semantics), RFC-0017 (Substrate Resource Model)
- Intel 8259A and 8254 datasheets (primary sources for PIC/PIT programming, as this project's own
  practice has been to verify against primary hardware documentation rather than secondhand
  summaries — see `docs/systems/x86-64-codegen.md`'s stated verification discipline)

------------------------------------------------------------------------

# 21. Revision History

| Version | Date       | Summary       |
|---------|------------|---------------|
| 0.1     | 2026-08-19 | Initial draft |
| 1.0     | 2026-08-19 | Implemented and validated end-to-end under QEMU/OVMF (at least 100 real, hardware-delivered IRQ0 ticks, deterministic across repeated runs); see `.agents/reports/aps-timer-tick.md` |
