
# RFC-0007: ArcoBASIC Interactive Program Model

**RFC Number:** RFC-0007
**Title:** ArcoBASIC Interactive Program Model
**Status:** Draft
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
