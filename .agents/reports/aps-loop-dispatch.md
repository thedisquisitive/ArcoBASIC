# APS Substrate Dispatch Loop (RFC-0037)

## Scope delivered

APS can now run indefinitely and react to hardware, rather than proving one thing and halting.
Implements RFC-0037 (`arcology-os/rfcs/RFC-0037_APS_Substrate_Dispatch_Loop.md`) in full: the
Loop's control structure (`CPU.EnableInterrupts` once, then `CPU.Halt`/dispatch forever),
`Loop.RegisterHook`/`UnregisterHook`/`Run`/`RunUntil`, and `LoopDispatchPendingWork`'s
scan/clear-before-invoke/dispatch sequence, consuming RFC-0036's timer as the first (and, at
implementation time, only) real event source.

No compiler changes were needed for this RFC -- everything is ordinary ArcoBASIC policy
(`arcology-os/stdlib/loop_policy.abas`), matching RFC-0037's own Testing Strategy expectation and
this project's now-repeated pattern (RFC-0036's `PIC.*`/`Timer.*` needed none either).

### One real, load-bearing deviation, flagged up front per RFC-0037 Section 17.5's own instruction

RFC-0037 Section 6.3 specifies `Loop.RegisterHook(eventSource AS U32, hook AS CALLABLE) AS BOOL`.
This implementation uses `Loop.RegisterHook(eventSource AS U32, hookSlot AS U32) AS BOOL` instead.

Confirmed directly, not assumed: `ADDRESSOF Name` (RFC-0027) parses and lowers to a real A-MIR
`AddressOf` instruction -- the frontend/A-MIR layers are RFC-0027-complete -- but
`generate_x86_64_function` (`src/compiler/fission.cpp`) has no codegen case for
`AmirInstruction::Kind::AddressOf` at all, and `CallValue`'s freestanding codegen unconditionally
resolves its call target as a compile-time-known function-name *string* looked up against the
module's declared functions; it has no path for calling through a value loaded from a variable or
register. `reveal ... at X86_64` on a minimal `ADDRESSOF`/`AS CALLABLE` repro fails cleanly with
"this milestone's code generator does not support this A-MIR instruction kind" -- exactly the
gap RFC-0037 Section 17.5 names as a stop condition, with an explicit instruction: fall back to
"the function-pointer-table shape sketched in Section 15" rather than building indirect-invocation
codegen inline as part of this RFC.

The fallback: `LoopInvokeSlot` is Section 15's own `InvokeHookAt(index)` sketch, made concrete as a
fixed-size (`LoopMaxHooks() = 4`) `IF`-chain dispatching to one of a small, fixed set of
well-known function names (`LoopHookSlot0`..`LoopHookSlot3`) that the *consuming program* defines.
Registration -- which event source maps to which slot, and whether that mapping is currently
active -- remains fully dynamic and runtime-driven, exactly as Requirement 6.3 specifies; only the
set of possible hook *bodies* is fixed per compiled program rather than arbitrary at runtime. Every
other requirement (Loop structure, top-half/bottom-half discipline, dispatch ordering,
replace-on-reregister, `Run`/`RunUntil`) is implemented with no further deviation.

### A second, smaller, load-bearing design decision: the pending indication is not the shared table

Requirement 6.2 says to "clear the pending indication before invoking the hook." RFC-0036's
`CPU.InterruptPendingTableBase()` table is a monotonic counter, deliberately never cleared, because
`Timer.Ticks()` also reads it directly and is meant to keep counting forever (RFC-0036's own
Developer Experience example polls it in a plain `WHILE` loop). Mutating that shared table here
would make two independent consumers (direct `Timer.Ticks()` polling and the Loop's dispatch) race
each other for no benefit. Instead, `LoopDispatchPendingWork` keeps its own private "last observed
count per event source" cursor table (a second fixed scratch region) and treats "clear" as
"advance my own cursor to the count I just read" -- functionally identical to Requirement 6.2's
clear-before-invoke, re-arrival-not-lost semantics, without touching RFC-0036's contract at all.

### Two small pre-existing compiler diagnostics discovered along the way (not fixed -- worked around, in scope)

Both are narrow, freestanding-profile (`#RUNTIME NONE`) type-checking gaps, not correctness bugs in
generated code, and both have a one-line workaround the compiler's own diagnostic message names:

1. **A user-declared `FUNCTION ... AS BOOL`'s return type is not recognized when its call appears
   directly as an `IF`/`WHILE` condition.** `IF SomeBoolFunction() THEN` reports "IF condition
   must be BOOL under #RUNTIME NONE; received unknown," even though the callee is explicitly
   declared `AS BOOL`. Root cause (not chased further -- out of this RFC's scope): freestanding
   `type_of_expression` special-cases specific intrinsic call names (`CPU.*`, `GRAPHICS.*`, ...)
   but does not look up an ordinary user function's own declared return type for a bare
   `Call`/`MethodCall` node. Worked around with an explicit comparison at every call site this RFC
   added (`IF LoopFindHook(...) = 1 THEN`), which the diagnostic's own message suggests
   ("Compare the value explicitly").
2. **The `TRUE` literal has no freestanding x86-64 constant codegen.** `WHILE TRUE` parses and
   type-checks (A-MIR is clean) but `reveal ... at X86_64` fails with `numeric constant "true" is
   not an exact integer literal, which is all this milestone's code generator supports`. Worked
   around with `WHILE 1 = 1` (needs the same explicit-comparison treatment as item 1, since a bare
   `WHILE 1` reports "received U64", not BOOL).

Neither blocks correct behavior and neither was fixed in the compiler (unlike the trailing-comment
bug found implementing RFC-0036, these are two-line workarounds, not build-breaking, and fixing
them would mean touching freestanding type inference for arbitrary user functions -- real scope
creep for this RFC). Noted here so they are not rediscovered the hard way; a future
freestanding-profile completeness pass should look at both.

## Validation

- `ctest`, full suite: 46/46 passing (45 pre-existing + the new loop-dispatch test).
- **The real proof, executed under QEMU/OVMF** (`aps-loop-dispatch.abas`): builds the same minimal
  identity-mapped environment `aps-timer-tick.abas` established, initializes the timer, registers
  a decoy hook then the real hook against event source 0 (asserting Requirement 6.3's
  replace-not-duplicate semantics -- exactly one active row, with the second call's slot value --
  before proceeding), then calls `Loop.RunUntil(20)`. The fixture itself asserts, before printing
  any success marker, that both the observed wake count and the hook's own invocation counter equal
  exactly 20 -- a 1:1 tick-to-dispatch correspondence that only holds if top-half mark-pending, EOI,
  bottom-half clear-before-invoke, and hook invocation all genuinely happened once per real
  hardware tick. Passed; verified deterministic across three repeated runs.
- Requirement 6.3's clear-before-invoke re-arrival-not-lost race (a hook slow enough for a new tick
  to arrive *during* its own execution) is not directly QEMU-proven -- RFC-0037 Section 16 lists it
  as a testing-strategy item, but engineering a reliably slow hook under QEMU without an artificial
  delay primitive this milestone doesn't have would be a fragile, low-value test, the same judgment
  RFC-0036 already made about its own "unmask everything and see if it hangs" negative test.
  Requirement 6.2's ordering (clear the cursor using the just-read count, before the
  `LoopInvokeSlot` call) is correct by inspection and is the same construction Section 15's
  reference sketch itself uses.

## Remaining activation gate

- Fan-out (multiple hooks per event source) is unimplemented, per RFC-0037's own Future Extensions
  -- `Loop.RegisterHook` on an already-registered source replaces, exactly as Requirement 6.3
  specifies, not adds.
- The event-source space this implementation actually supports is 0-15 (RFC-0036's IRQ0-15 range),
  not the fully open `U32` RFC-0037's Terminology section leaves room for -- no real non-hardware
  event source exists yet to make that distinction matter (RFC-0037's own Future Extensions list a
  software-only deferred-work source as later work).
- The `hookSlot AS U32` deviation from `hook AS CALLABLE` is the real remaining gap this report
  exists to flag: once RFC-0027's callables gain freestanding-x86-64 codegen (a new AMIR
  `AddressOf` case plus a genuine indirect-call `CallValue` variant using the encoder's existing
  but currently UEFI-vtable-only `call_indirect_disp8`/`call_indirect_disp32` primitives),
  `Loop.RegisterHook`'s signature should be revisited to take a real callable and drop the
  fixed-slot indirection this report documents. That work belongs to RFC-0027's own
  freestanding-profile completion, not a follow-up patch to this RFC.
- RFC-0038 (Block Storage and Filesystem Provider Substrate) is the natural next consumer of this
  Loop -- a real, future block-device driver's completion interrupt is exactly the second event
  source (beyond the timer) this whole top-half/bottom-half/dispatch chain was generalized for.
