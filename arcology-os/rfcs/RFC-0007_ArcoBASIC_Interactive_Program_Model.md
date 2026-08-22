
# RFC-0007: ArcoBASIC Interactive Program Model

**RFC Number:** RFC-0007
**Title:** ArcoBASIC Interactive Program Model
**Status:** Draft (first real increment implemented and QEMU-proven, see Section 14 — Immediate
Mode command dispatch for HELP/OT only; Program Mode, RPM, PEEK/POKE, and INSPECT remain
unimplemented)
**Category:** Language / User Experience

---

# 1. Executive Summary

This RFC defines the interactive programming experience presented by Arcology Seed.

The machine boots directly into a line-numbered ArcoBASIC environment and immediately reaches:

    READY.

The interactive prompt is the operating environment during the Seed era.

---

# 2. Goals

- Boot directly into programming.
- Support classic numbered BASIC editing.
- Preserve modern free-form `.abas` source files.
- Make experimentation immediate.
- Keep execution deterministic.

---

# 3. Non-Goals

Seed does not include:

- ArcFS
- Persistent storage
- Users
- Sessions
- Networking
- Windowing
- Package management

---

# 4. Programming Modes

## Immediate Mode

Commands without line numbers execute immediately.

Examples:

```basic
PRINT "HELLO"
HELP
LIST
RUN
NEW
CLEAR
OT
```

Immediate statements are never inserted into the resident program.

---

## Program Mode

Statements beginning with a line number become part of the Resident Program Memory.

```basic
10 PRINT "HELLO"
20 GOTO 10
```

---

# 5. Editing Rules

- New line number → insert.
- Existing line number → replace.
- Bare line number → delete.
- Programs remain sorted numerically.

---

# 6. Resident Program Memory (RPM)

## Purpose

Seed maintains one Resident Program Memory (RPM) object.

RPM represents the currently edited program.

It exists only in RAM.

Until persistent storage exists, powering off or issuing NEW destroys the program.

---

## Logical Layout

The physical representation is implementation-defined.

Logically RPM contains:

    Header
    Program Metadata
    Line Index
    Tokenized Program
    Runtime Workspace

Future implementations may optimize storage without changing observable behavior.

---

## Line Numbers

Valid range:

1 through 65535

Line 0 is reserved for Immediate Mode and is never stored.

---

## Program Ordering

Programs are always stored in ascending line-number order.

---

## Memory Reporting

The environment SHOULD expose remaining program memory.

Example:

    READY.
    32768 BYTES FREE.

The exact value depends on the target platform.

---

## Out of Memory

If a line cannot be inserted:

    ?OUT OF PROGRAM MEMORY

No partial modification shall occur.

---

# 7. Memory Inspection

Seed intentionally supports low-level memory inspection.

These facilities exist primarily for:

- firmware bring-up
- operating-system development
- debugging
- hardware experimentation

They are not intended to replace semantic hardware APIs.

---

## Traditional Access

```basic
X = PEEK($8000)

POKE $8000,42
```

---

## Typed Access

Future versions should support:

```basic
PEEK8()
PEEK16()
PEEK32()
PEEK64()

POKE8()
POKE16()
POKE32()
POKE64()
```

Typed access is preferred over implicit byte operations.

---

## Symbolic Access

When debugging information exists:

```basic
PRINT PEEK @Scheduler.RunLevel

POKE @Scheduler.RunLevel,2
```

The compiler or debugger resolves symbolic addresses.

This greatly improves readability while preserving the usefulness of direct memory access.

---

## Structured Inspection

Future runtime object inspection may expose:

```basic
INSPECT @Scheduler

INSPECT @CurrentTask

INSPECT @CPU
```

allowing rich inspection without raw pointer arithmetic.

---

## Security

Reading memory is generally unrestricted for development profiles.

Writing memory is capability controlled.

Normal user-space applications SHOULD require explicit permission before arbitrary memory modification.

---

# 8. Commands

Seed SHALL provide:

- LIST
- RUN
- NEW
- CLEAR
- HELP
- OT

---

# 9. READY State

After successful execution or recoverable errors, the system SHALL return to:

    READY.

This is the canonical idle state.

---

# 10. Modern Source

Interactive numbered programs exist for exploration.

Applications, libraries, compiler code, and operating-system components SHALL use modern free-form `.abas` source.

---

# 11. Whimsy

Seed's release feature is:

    OT

which displays a random Oregon Trail-inspired message compiled into the system image.

---

# 12. AI Implementation Guidance

Agents SHALL preserve the distinction between Immediate Mode and Program Mode.

Agents SHALL implement RPM as a logical abstraction rather than relying on a fixed physical layout.

---

# 13. Definition of Done

Implemented when:

1. Immediate Mode executes.
2. Program Mode stores numbered lines.
3. Replacement and deletion work.
4. LIST works.
5. RUN works.
6. NEW works.
7. READY is consistently restored.
8. RPM reports available memory.
9. PEEK functions.
10. POKE functions with capability enforcement.
11. OT functions.

---

# 14. Implementation Status (2026-08-22)

**First real increment implemented and QEMU-proven**: `arcology-os/tests/fixtures/
arcology-seed-ready/arcology-seed-ready.abas`. This is the first fixture in the ENTIRE Arcology OS
project that does not `CPU.HaltForever` after doing one thing — it boots directly to `READY.` and
stays there in a genuine, real, persistent loop, exactly matching this RFC's own Section 1 vision.

## What's real

- Real keyboard input: `EFI_SIMPLE_TEXT_INPUT_PROTOCOL` had no binding anywhere in this project
  before this increment. New `SystemTable.ConsoleIn` field (offset `0x30`) and
  `UEFI.SimpleTextInputProtocol.ReadKeyStroke` (offset `0x08`) resolve through the existing generic
  `CallExternal` codegen path — no new hand-assembled machine code needed, the same story every
  UEFI binding in this project after the first few has had.
- Real DYNAMIC console output — a genuine capability finding, not previously used or documented
  anywhere in this project: `ConsoleOut.Write` accepts a raw `MMIOPTR` to a runtime-constructed,
  null-terminated `CHAR16` buffer, with no dependency on ArcoBASIC's own `STRING` literal
  machinery. Confirmed with a standalone probe before committing to the fixture design. This is
  what makes echoing an arbitrary just-typed keystroke possible at all.
- Real command dispatch: `HELP` and `OT` (Section 8's two simplest, storage-free commands; `OT`
  shows exactly one fixed message, not the random selection Section 11 describes — an explicit,
  named scope reduction), case-insensitive, plus a real `?SYNTAX ERROR` path for anything else —
  matching classic BASIC systems' own error convention, not specified verbatim by this RFC but a
  reasonable reading of "recoverable errors" in Section 9.
- Real line editing: Backspace genuinely removes the last typed character (both from the internal
  line buffer and visually, via a real backspace+space+backspace echo sequence).
- Real persistence: no `CPU.HaltForever` anywhere in the normal command loop. The only halt path is
  a TEST-ONLY hidden `EXIT` command this RFC does NOT define — included purely so automated QEMU
  testing can terminate deterministically instead of running to a fixed timeout on every test.

## Explicitly NOT in scope for this increment

Program Mode (numbered lines, Resident Program Memory, `LIST`/`RUN`/`NEW`), `PEEK`/`POKE`,
`INSPECT`, memory-free reporting, and OT's own random-message selection. These are each real,
separate, substantial undertakings — Program Mode alone needs a real line-numbered program store
and an execution engine for it, neither of which exists yet. This increment closes exactly the
three real prerequisites Section 1's own `READY.` vision needed and no further.

## Validation

New test harness `scripts/run/run-uefi-hello-with-keyboard.sh`: injects real keystrokes via QEMU's
own human-monitor `sendkey` command over a UNIX socket — the same scancode-delivery path a real
keyboard driver uses, not a shortcut specific to this fixture. A real, empirically-found timing
requirement: the monitor socket exists and accepts connections well before OVMF's own PS/2
keyboard driver is ready to receive scancodes — injecting immediately after the socket appears
silently drops the keystroke (confirmed directly: identical setup, only the injection-timing
changed, went from 0/3 to 3/3 delivery). The harness instead waits for the guest's own
`READY_MARKER` to appear in captured output first, then a fixed settle delay, before injecting.

New smoke test `systems_arcology_seed_ready_smoke`: real `HELP`/`OT` dispatch, a real negative
control (an unrecognized command genuinely produces `?SYNTAX ERROR`, not a false match), real
backspace line editing, and 3x determinism. Full regression suite passing (92 tests total after
this increment).

## What this does not close

Nothing in this project has ever been confirmed on real hardware for keyboard input specifically —
this is a new, additional real-hardware validation surface beyond WP-026/029/030/031's own GOP/
ArcFS/USB-boot proofs. See `.agents/reports/WP-032-arcology-seed-ready-hardware-validation.md`.
