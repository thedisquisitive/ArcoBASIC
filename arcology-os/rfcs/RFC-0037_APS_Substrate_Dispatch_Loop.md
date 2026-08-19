# RFC-0037: APS Substrate Dispatch Loop

**RFC Number:** RFC-0037
**Title:** APS Substrate Dispatch Loop
**Status:** Draft
**Category:** Substrate / Execution Model
**Authors:** Arcology Project
**Created:** 2026-08-19
**Last Updated:** 2026-08-19
**Supersedes:** None
**Superseded By:** None
**Related RFCs:** RFC-0000, RFC-0007, RFC-0017, RFC-0027, RFC-0036, RFC-0038

------------------------------------------------------------------------

# 1. Executive Summary

Every APS proof fixture built so far — CR3 cutover, GDT/CS/SS reload, IST1 emergency stack, and
(pending) the timer in RFC-0036 — ends in `CPU.HaltForever`: a deliberate, documented, *terminal*
state. That has been the right choice for each of those proofs individually. It is the wrong
shape for a running substrate. Nothing above this layer — a filesystem, input, a desktop — can
exist while APS's only steady state is "prove one thing, then stop forever."

This RFC defines the Loop: the single, non-returning body APS transitions into once bootstrap
completes, replacing `CPU.HaltForever` as the terminal call in `Main`. The Loop does not schedule
tasks, does not preempt anything, and does not decide what work means — FMAP-0001 lists
"execution contexts" as its own, later phase, and this RFC is deliberately not that phase. What it
does is turn "an interrupt happened" (RFC-0036's tick, and every future device interrupt) into "a
registered Provider's hook ran," on a single logical processor, cooperatively, deterministically,
and without ever needing to remember to re-enable interrupts or re-arm anything by hand.

------------------------------------------------------------------------

# 2. Motivation

RFC-0036 gives APS a way to *count* time passing. Nothing consumes that yet. Left as-is, a
`Timer.Initialize` call followed by `CPU.HaltForever` is exactly as useless as no timer at all —
the tick counter increments in memory that nothing ever reads.

More broadly: every future I/O device (storage in RFC-0038, eventually input, eventually
networking) delivers its completion the same way the timer does — an interrupt, a top-half ISR
that records "something happened" and gets out fast, and *something* that later notices and acts
on it. RFC-0036 specified the top half generically enough to support this (Requirement 6.4's
pending-interrupt table is not timer-specific). This RFC specifies the bottom half once, generally,
so RFC-0038 and everything after it are consumers of an existing contract rather than each
reinventing "how do we notice an interrupt happened and do something about it."

------------------------------------------------------------------------

# 3. Goals

- Define the Loop's structure: wait for an interrupt, check what happened, dispatch to whoever
  asked to be told, repeat.
- Define how a Provider registers interest in an event source (starting with RFC-0036's timer
  ticks) and what it receives when its hook runs.
- Make the Loop's execution deterministic and testable in bounded time, not just runnable forever
  — this project has already paid once for a test string that read as "it worked" when it hadn't
  (`project_arcology_os_exception_entry.md`'s QEMU boot-noise lesson) and separately for a proof
  that only "worked" because nothing observable distinguished success from silent failure
  (`aps-emergency-stack.md`'s zero-reads-as-zero scratch-address lesson). A Loop that can only be
  tested by "let it run forever and see if the machine seems fine" repeats both mistakes at a
  larger scale. It MUST be possible to run the Loop for a bounded number of iterations or a
  bounded simulated duration and assert on the outcome.
- State plainly what the Loop is not: not a scheduler, not preemptive, not multi-core aware.

------------------------------------------------------------------------

# 4. Non-Goals

- Scheduling, execution contexts, or any notion of a "task" with its own stack that the Loop
  switches into and out of. FMAP-0001's own phase list keeps execution contexts distinct from this
  work, and RFC-0007 explicitly excludes multitasking from even the eventual Seed release.
- Preemption. A hook that never returns hangs the Loop, and by extension the whole substrate. This
  RFC treats that as a discipline the hook author must uphold (Requirement 6.2), not something the
  Loop protects against — protecting against it is exactly what a scheduler with preemption would
  do, and building that is out of scope here.
- Multi-core dispatch, work-stealing, or any concurrency primitive beyond the single-producer
  (ISR) / single-consumer (Loop) pattern a single logical processor makes safe without locks.
- Prioritization between Providers beyond registration order (Requirement 6.3). A priority model
  is a Future Extension once there is more than one real Provider competing for attention.
- Power management (deeper sleep states than `HLT`, dynamic tick rates). Future Extension, and
  explicitly deferred by RFC-0036 too.

------------------------------------------------------------------------

# 5. Terminology

**The Loop:** This RFC's subject — the non-returning dispatch body APS runs after bootstrap.

**Provider:** As defined by RFC-0017 — an entity that owns and services a Resource. This RFC adds:
a Provider MAY register a **Loop Hook** to be notified when its associated event source has
pending work.

**Loop Hook:** A callable (RFC-0027's first-class callables) a Provider registers against an event
source. The Loop invokes it with no arguments once per wake when that source has pending work; the
hook is responsible for consuming/clearing whatever made it pending (typically via the relevant
`CPU.*InterruptPendingTableBase()`-style intrinsic from the owning device RFC).

**Event Source:** An opaque identifier (an `IRQ` line number for RFC-0036/RFC-0038-style hardware
sources; this RFC does not close off the identifier space to hardware only — a future software-
originated source, such as a deferred-work queue unrelated to any device, MAY register the same
way) a hook is registered against.

**Wake:** One return from `CPU.Halt` — either because an interrupt arrived, or (this RFC does not
assume `HLT` never spuriously returns on real hardware under all conditions; treating every return
as "check everything" rather than "trust which interrupt supposedly caused this" is intentionally
conservative).

**Idle:** A wake where no registered event source had pending work. Expected and unremarkable at
low tick rates with few Providers; not an error condition.

------------------------------------------------------------------------

# 6. Requirements

## 6.1 Loop structure

The Loop MUST have the shape:

```basic
CPU.EnableInterrupts
WHILE TRUE
    CPU.Halt
    LoopDispatchPendingWork()
WEND
```

`CPU.EnableInterrupts` MUST be called exactly once, immediately before the loop body begins, and
MUST NOT be called again inside the loop (interrupts remain enabled for the Loop's entire
lifetime; a hook that needs a critical section uses `CPU.SaveInterruptState`/
`CPU.DisableInterrupts`/`CPU.RestoreInterruptState` internally per `cpu-execution-semantics.md`,
scoped tightly, and MUST restore the enabled state before returning — the Loop does not do this on
a hook's behalf).

`CPU.Halt` (not `CPU.HaltForever`) is the correct primitive: per `hardware-semantics.md` it "may
resume according to the target architecture and falls through," which on x86-64 means it resumes
at the instruction after `HLT` once any unmasked interrupt is delivered and its handler returns.
This RFC introduces no new hardware semantic for waiting — the primitive already exists and is
already validated (`systems_hardware_semantics_smoke`).

## 6.2 Top half / bottom half discipline

Nothing a Loop Hook does runs inside interrupt context. The division is:

- **Top half** (inside an ISR, per RFC-0036 Requirement 6.3 and any future device RFC's equivalent
  requirement): record that an event source has pending work, acknowledge the device (EOI or
  device-specific equivalent), restore registers, `IRETQ`. Nothing else. In particular: **no ISR
  may call a Loop Hook, or any other ArcoBASIC function, directly.**
- **Bottom half** (`LoopDispatchPendingWork`, running as ordinary code after `CPU.Halt` returns):
  for each registered event source, in registration order, check whether it has pending work
  (via that source's own pending-check primitive — RFC-0036's `CPU.InterruptPendingTableBase()`-
  derived reads for hardware sources); if so, clear the pending indication *before* invoking the
  hook (so a re-arrival during the hook's execution is not lost, and is picked up on the Loop's
  next wake rather than missed), then invoke the hook.

This split exists because interrupt context on this substrate runs with interrupts implicitly
disabled for the duration (the IDT gates built by `BuildMinimalIDT` are interrupt gates, not trap
gates, per the descriptor-table policy) — any nontrivial work done there blocks every other
interrupt, including the next tick, for as long as it takes. A hook is ordinary code running with
interrupts enabled; it can be interrupted by the very next tick, which is the correct, expected
behavior for a responsive substrate.

## 6.3 Registration and dispatch order

```basic
Loop.RegisterHook(eventSource AS U32, hook AS CALLABLE) AS BOOL
Loop.UnregisterHook(eventSource AS U32) AS BOOL
```

Registration MUST be idempotent-safe to call before the Loop starts (typically during bootstrap,
after `Timer.Initialize` and before `CPU.EnableInterrupts`) and MAY be called from within a
hook's own execution (a Provider registering a second, related hook in response to the first
firing) — the dispatch table is only read at the start of `LoopDispatchPendingWork`'s enumeration,
so a hook mutating it takes effect on the *next* wake, not the one in progress, avoiding
iterator-invalidation-style hazards without needing a lock.

Multiple event sources' hooks, when more than one has pending work on the same wake, run in
ascending event-source-identifier order. This is a deliberate, simple, deterministic tie-break —
not a claim about real-world urgency — chosen so that test fixtures asserting on interleaving
order get a stable, documented answer rather than an implementation-defined one.

Exactly one hook per event source may be registered at a time; `RegisterHook` on an
already-registered source MUST replace the existing hook and return `TRUE`, not silently ignore
the call or throw. Fan-out to multiple interested parties for one event source is a Future
Extension (Section 18), not supported by this RFC.

## 6.4 Bounded/testable execution

```basic
Loop.Run() AS U64                                    ' production: never returns (U64 is unreachable)
Loop.RunUntil(maxWakes AS U64) AS U64                 ' test-facing: returns after maxWakes wakes
```

`Loop.RunUntil` MUST exist as a distinct, real code path (not a debug-only branch stripped from
production builds) so that QEMU/OVMF-executed tests can assert on Loop behavior in bounded time —
following exactly the precedent RFC-0036's own testing strategy and `aps-emergency-stack.md`
already established: prefer a deliberate, controllable trigger over waiting on an uncontrolled
real-world condition. `Loop.RunUntil` returns the number of wakes actually observed (which MAY
exceed `maxWakes` by at most one iteration's worth of slack, per straightforward "check after
dispatch, not before" loop construction) so tests can assert wake counts precisely rather than
inferring them from side effects alone.

## 6.5 Hook failure

If a hook does not return (an infinite loop, or a hardware fault it does not itself recover from),
the Loop hangs — indistinguishable, from outside, from `CPU.HaltForever`. This RFC does not
attempt to detect or recover from that (that would require preemption, explicitly out of scope).
A hook that raises a language-level runtime error (RFC-0022, once available on this profile) is
this RFC's only defined failure signal; until RFC-0022's freestanding-profile story exists, a
hook throwing is itself out of scope and MUST be treated as a programming error the hook author is
responsible for not committing, exactly as `IDTWriteInterruptGate`-style policy functions already
are today.

------------------------------------------------------------------------

# 7. Architecture

```text
Bootstrap (CR3 cutover, GDT/IDT/TSS activation, Timer.Initialize, Loop.RegisterHook(...) calls)
        |
        v
   CPU.EnableInterrupts
        |
        v
+-----------------------------------------------------------------+
|                      THE LOOP  (never returns)                  |
|                                                                   |
|   +------------+     +---------------------------------------+  |
|   | CPU.Halt   | --> | LoopDispatchPendingWork()               |  |
|   | (wakes on  |     |  for each registered source (ascending  |  |
|   |  any IRQ)  |     |  id order):                             |  |
|   +------------+     |    if pending: clear; invoke hook       |  |
|         ^             +---------------------------------------+  |
|         |                              |                          |
|         +------------------------------+                          |
+-----------------------------------------------------------------+
         ^
         | (top half only -- RFC-0036's shared IRQ handler,
         |  or any future device's equivalent)
         |
   Hardware interrupt (timer tick, future: storage completion, ...)
```

------------------------------------------------------------------------

# 8. User Experience

None directly — the Loop is invisible infrastructure. Its existence is what makes every later
user-facing subsystem (input responding to keypresses, a filesystem completing a read, a desktop
redrawing on a timer) possible at all; this RFC is a precondition, not a feature with its own UI.

------------------------------------------------------------------------

# 9. Developer Experience

```basic
FUNCTION OnTick() AS U64
    LET seconds AS U64 = Timer.Ticks() / Timer.FrequencyHz()
    ' ... periodic work ...
    RETURN 0
END FUNCTION

Timer.Initialize(100)
Loop.RegisterHook(0, OnTick)   ' event source 0 = IRQ0 = the timer, per RFC-0036's vector base
Loop.Run()
```

A Provider author never touches `CPU.Halt`, the pending table, or EOI directly — RFC-0036 and this
RFC jointly own that machinery. The only surface a Provider sees is `Loop.RegisterHook` and a plain
callable.

------------------------------------------------------------------------

# 10. Security Considerations

The Loop is single-threaded, ring-0, and trusts every registered hook completely — there is no
capability check on `Loop.RegisterHook` in this RFC (FMAP-0001's capability system does not exist
yet; see Open Questions). Until it does, any code able to call `Loop.RegisterHook` at all can
starve every other Provider simply by never returning from its hook. This is an accepted,
documented gap for this milestone, not an oversight — the same trust boundary every mechanism in
this project has had since `CPU.LoadGDT` was first exposed, unenforced pending a capability model
that does not yet have an RFC.

------------------------------------------------------------------------

# 11. Privacy Considerations

None at this layer.

------------------------------------------------------------------------

# 12. Accessibility Considerations

None directly. A live, responsive Loop is what eventually makes assistive-technology polling and
timing-sensitive accessible UI possible; this RFC has no UI surface of its own.

------------------------------------------------------------------------

# 13. Performance Considerations

Per wake: one `HLT`/resume, one linear scan of registered event sources checking pending state
(expected to be a handful of entries for the foreseeable future — Providers, not arbitrary
per-request callbacks, register hooks), and zero or more hook invocations. The scan cost is
`O(registered sources)` per wake, not per tick — at RFC-0036's 100 Hz default with, say, a dozen
future Providers registered, this is a negligible, bounded cost per second. If the registered-
source count grows enough for a linear scan to matter, a Future Extension (indexed/bitmap
dispatch) is the right fix, not a redesign of the dispatch contract itself.

------------------------------------------------------------------------

# 14. Compatibility

This RFC changes what `Main` does at the point every existing proof fixture currently calls
`CPU.HaltForever` as its final statement. It does not require changing those fixtures — they
remain valid, standalone proofs of the mechanisms they test, and are not retroactively required to
adopt the Loop. `CPU.HaltForever` itself is unchanged and remains the correct choice for a fixture
whose entire point is "prove one thing and stop," which is most of what exists today. The Loop is
what `Main` calls once APS is meant to *keep running*, which — per this project's own history —
is a distinct, later kind of program from a validation fixture.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
' Illustrative -- exact ArcoBASIC surface for a hook table depends on how far RFC-0027's
' callables have been wired into the freestanding profile at implementation time; if first-class
' callables are not yet available there, a fixed-size array of (eventSource, functionPointer)
' pairs indexed linearly is an acceptable interim shape that satisfies this RFC's requirements
' without blocking on RFC-0027's freestanding-profile completeness.

FUNCTION LoopDispatchPendingWork() AS U64
    LET index AS U32 = 0
    WHILE index < RegisteredHookCount()
        LET source AS U32 = HookEventSourceAt(index)
        IF InterruptPending(source) THEN
            ClearInterruptPending(source)
            InvokeHookAt(index)
        END IF
        index = index + 1
    WEND
    RETURN 0
END FUNCTION
```

------------------------------------------------------------------------

# 16. Testing Strategy

- Structural: `Loop.RegisterHook`/`UnregisterHook` behave correctly for register-replace,
  unregister-absent, and ordering, in a hosted or freestanding-but-not-QEMU-dependent test where
  possible (dispatch-order logic does not itself require real interrupt delivery to validate).
- **QEMU/OVMF-executed**: a fixture that initializes the timer (RFC-0036), registers a hook that
  increments a counter and writes it to serial each time it runs, calls `Loop.RunUntil(N)`, and
  confirms via the harness that exactly `N` hook invocations were observed — a direct, bounded,
  deterministic proof the whole chain (hardware tick -> top half -> bottom half -> hook) works
  end to end, matching this project's now-established standard of proving mechanisms by actually
  executing them under QEMU, not by compile-time shape checks alone.
- A regression test for Requirement 6.3's clear-before-invoke ordering: a hook that re-registers
  interest in its own source (simulating "more work arrived while I was running") and a
  confirmation that this is picked up on the *next* wake, not lost or double-counted.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

## 17.1 Implementation boundaries

In scope: the Loop's control structure, `Loop.RegisterHook`/`UnregisterHook`/`Run`/`RunUntil`, and
`LoopDispatchPendingWork`'s scan/clear/invoke sequence. Out of scope: anything in Non-Goals
(Section 4) — in particular, do not implement any form of preemption, priority, or multi-core
dispatch as part of this RFC even if it looks like a small addition; each is a separate design
decision this RFC deliberately does not make.

## 17.2 Prohibited implementation choices

- Invoking a hook from inside an ISR/interrupt context, under any circumstance.
- Any locking or atomic-instruction requirement to make the pending-table read/clear safe. On a
  single logical processor with interrupt gates (not trap gates) already disabling further
  interrupts for the ISR's own duration, the top-half/bottom-half split is safe by construction;
  adding synchronization primitives here is solving a multi-core problem this RFC does not have
  yet, and would need its own design once multi-core is in scope.
- Silently swallowing a hook that never returns. If a watchdog or diagnostic for this becomes
  necessary, it is a Future Extension, not something to improvise inline here.

## 17.3 Required deliverables

- The Loop's control structure and dispatch function.
- `Loop.RegisterHook`/`UnregisterHook`/`Run`/`RunUntil` ArcoBASIC surface.
- A QEMU/OVMF-executed fixture proving bounded, counted hook dispatch from a real timer tick, and
  a matching `systems_arco_basic_*_smoke.sh` test.
- An implementation report under `.agents/reports/` with an honest "remaining activation gate"
  section, following this project's established convention.

## 17.4 Acceptance criteria

- `ctest` suite passes in full.
- The QEMU/OVMF fixture demonstrates exact, deterministic hook-invocation counts across repeated
  runs (this project has twice now found that "looked correct" and "is correct" diverge exactly
  at the point determinism isn't actually checked — do not skip this).
- No existing fixture that still legitimately ends in `CPU.HaltForever` is required to change.

## 17.5 Stop conditions

Stop and request human review before: adding any form of hook prioritization beyond registration
order; adding any inter-hook communication or shared-state mechanism beyond what a Provider
already owns through its own Resource (RFC-0017); or discovering that RFC-0027's callables are not
usable on the freestanding profile at implementation time (fall back to the function-pointer-table
shape sketched in Section 15 and note the gap in the implementation report rather than expanding
this RFC's scope to fix RFC-0027's freestanding coverage inline).

## 17.6 Assumptions and dependencies

Hard dependency on RFC-0036 (the timer is this RFC's only available event source at
implementation time, and is required to prove the Loop actually dispatches anything). Soft
dependency on RFC-0027 (first-class callables) for the cleanest possible `Loop.RegisterHook`
signature; see 17.5 for the fallback if that dependency is not ready.

------------------------------------------------------------------------

# 18. Future Extensions

- Fan-out: multiple hooks per event source.
- Priority levels distinct from registration order.
- A software-only event source (deferred-work queue) not tied to any hardware interrupt, for
  Providers that want "run soon, but not from an ISR" without owning a device.
- Idle-time accounting / power management (deeper sleep states, dynamic tick rate).
- Multi-core dispatch, once execution contexts and per-core Loops (or a single shared one with
  real synchronization) are in scope.
- A watchdog for hooks that overrun a time budget, once there is a mechanism to enforce one
  without preemption.

------------------------------------------------------------------------

# 19. Open Questions

- Should `Loop.RegisterHook` be capability-checked once FMAP-0001's capability system exists, or
  is the Loop itself substrate-owned infrastructure exempt from that model? Same open question as
  RFC-0036 Section 19 raised for the Timer service; likely wants a single, consistent answer
  across both rather than being resolved independently per RFC.
- Is registration-order dispatch (Requirement 6.3) still the right default once there are enough
  real Providers that ordering starts to matter for correctness rather than just test determinism
  (for example: should a storage-completion hook always run before a UI-redraw hook on the same
  wake)? Left open until RFC-0038 and beyond give this a real second data point.

------------------------------------------------------------------------

# 20. References

- `.agents/reports/aps-exception-entry-stub.md`, `aps-gdt-reload.md`, `aps-emergency-stack.md`
- RFC-0036 (this RFC's hard dependency)
- RFC-0017 (Substrate Resource Model), RFC-0027 (First-Class Callables)
- `arcology-os/docs/systems/hardware-semantics.md`, `cpu-execution-semantics.md`

------------------------------------------------------------------------

# 21. Revision History

| Version | Date       | Summary       |
|---------|------------|---------------|
| 0.1     | 2026-08-19 | Initial draft |
