# CPU Execution and Interrupt-State Semantics

Status: proposed
Depends on: existing `CPU.Halt` and `CPU.HaltForever`, x86-64 Freestanding Control Flow

## 1. Purpose

This RFC expands the existing CPU hardware semantic family with a spin-wait hint and safe manipulation of the current processor's maskable-interrupt state.

These operations are language semantics, not inline assembly and not ordinary hosted calls.

## 2. Existing operations retained

```basic
CPU.Halt
CPU.HaltForever
```

Their existing semantics remain unchanged.

## 3. New execution semantic

```basic
CPU.Pause
```

`CPU.Pause` is a nonterminal execution hint intended for spin loops and short hardware waits.

On x86-64 it lowers to `PAUSE`.

It:

- does not halt until an interrupt;
- does not call a scheduler;
- does not guarantee another thread or CPU will run;
- falls through to the next statement;
- may lower to a target-specific no-op when a future architecture has no equivalent hint, provided that target documents the behavior.

Example:

```basic
WHILE PORT.ReadByte(statusPort) = 0
    CPU.Pause
WEND
```

## 4. Interrupt-state type

Add an opaque type:

```basic
CPU.InterruptState
```

It represents the current processor's restorable maskable-interrupt state.

It is not an integer and cannot be compared, serialized, arithmetically modified, or constructed from a literal.

## 5. Interrupt-state operations

```basic
CPU.SaveInterruptState() AS CPU.InterruptState
CPU.DisableInterrupts
CPU.EnableInterrupts
CPU.RestoreInterruptState(state AS CPU.InterruptState)
```

### `SaveInterruptState`

Captures enough target state to restore whether maskable interrupts were enabled.

On x86-64 the implementation may capture `RFLAGS.IF` or an opaque representation containing it.

### `DisableInterrupts`

Disables maskable interrupts on the current logical processor.

On x86-64 it lowers to `CLI`.

It does not disable non-maskable interrupts, system-management interrupts, or machine checks.

### `EnableInterrupts`

Enables maskable interrupts on the current logical processor.

On x86-64 it lowers to `STI`.

The x86 delayed-recognition behavior following `STI` is architecture behavior and must be documented in backend notes rather than disguised.

### `RestoreInterruptState`

Restores the maskable-interrupt state represented by the opaque token.

It is the preferred operation after a critical section because blindly calling `CPU.EnableInterrupts` would be incorrect when interrupts were already disabled before entry.

Example:

```basic
LET prior AS CPU.InterruptState = CPU.SaveInterruptState()
CPU.DisableInterrupts

' critical section

CPU.RestoreInterruptState(prior)
```

## 6. Token restrictions

An interrupt-state token:

- belongs to the logical processor on which it was captured;
- must not be stored for long-term use;
- must not cross an interrupt, task, or CPU boundary when such facilities exist;
- is valid only for restoration in the dynamic operation that captured it.

The first implementation does not need a borrow checker. These are semantic rules enforced where statically practical and documented elsewhere.

## 7. Canonical AST and A-MIR

```text
CPU.PAUSE
CPU.SAVE_INTERRUPT_STATE
CPU.DISABLE_INTERRUPTS
CPU.ENABLE_INTERRUPTS
CPU.RESTORE_INTERRUPT_STATE
```

`CPU.Pause`, `CPU.DisableInterrupts`, and `CPU.EnableInterrupts` use bare statement syntax consistent with existing halt semantics.

The save and restore operations use call syntax because they produce or consume a value.

## 8. Hosted behavior

Hosted targets reject all operations in this RFC with explicit diagnostics. `CPU.Pause` is not reinterpreted as a host thread yield because that would change its semantics.

## 9. Aliases

No aliases are introduced initially.

In particular:

- `CPU.Sleep` is not an alias of `CPU.Halt`;
- `CPU.Yield` is not an alias of `CPU.Pause`;
- `MaskInterrupts` is not introduced until the project decides whether “masking” should refer to CPU state, interrupt-controller state, or both.

## 10. Validation

Tests must verify:

- exact x86-64 encodings for `PAUSE`, `CLI`, and `STI`;
- terminal behavior of existing `CPU.HaltForever` remains unchanged;
- a save-disable-restore sequence restores the prior IF state rather than always enabling interrupts;
- hosted rejection;
- correct use inside newly supported control flow.

A QEMU fixture should execute both paths:

1. interrupts initially enabled, then restored enabled;
2. interrupts initially disabled, then restored disabled.

Observable validation may use flags inspection within the backend test harness; no public general-purpose `CPU.ReadFlags` semantic is required by this RFC.

## 11. Non-goals

This RFC does not define:

- interrupt handlers;
- IDT entries;
- interrupt-controller masking;
- critical-section objects;
- locks or atomics;
- scheduler yielding;
- multicore synchronization;
- general `RFLAGS` access.

## 12. Acceptance

The implementation is complete when:

1. all five new A-MIR operations are explicit and typed;
2. x86-64 lowering is independently verified;
3. save/restore preserves prior state;
4. hosted targets reject the operations;
5. `CPU.Pause` works inside a generated polling loop;
6. existing halt semantics and tests remain intact.
