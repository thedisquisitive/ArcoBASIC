
# RFC-0007: ArcoBASIC Interactive Program Model

**RFC Number:** RFC-0007
**Title:** ArcoBASIC Interactive Program Model
**Status:** Draft (two real increments implemented and QEMU-proven, see Section 14/15 — Immediate
Mode HELP/OT, and now Program Mode's own first real increment: RPM storage plus PRINT and GOTO
only, a deliberate scope reduction. LET/variables, IF, FOR/NEXT, PEEK/POKE, and INSPECT remain
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

---

# 15. Implementation Status — Program Mode, First Real Increment (2026-08-23)

**Real Program Mode, scoped to PRINT and GOTO only** — a deliberate, named reduction from Section
4's full vision (LET/variables, IF, FOR/NEXT are real, separate follow-on increments), implemented
directly in `aps-arcology-seed-substrate.abas`. This is a genuine embedded interpreter running
under APS's own CR3/GDT/IDT after `ExitBootServices`, not a host-compiled program — no ArcoFission
compiler involved once booted, matching classic BASIC's own immediacy.

## What's real

- **Resident Program Memory (RPM)**: a fixed-size (64 lines), always-sorted, always-compacted array
  in a dedicated scratch region — no dynamic allocator exists on this backend, the same "fixed
  scratch buffer, not a heap" idiom the fixture's line buffer, font table, and dashboard label
  table already established. Insert/replace/delete (Section 5's own edit rules) do a real in-place
  shift, not tombstoning, so RUN/LIST stay simple straight-line scans.
- **`RUN`**: walks RPM in real sorted order; `GOTO` re-seeks by real line number (the table is
  sparse, not by array index); `PRINT` echoes its own stored literal. A GOTO to a line number that
  doesn't exist produces a real `?UNDEFINED STATEMENT IN <N>` error, not a silent no-op or a hang.
- **`LIST`**: reconstructs real stored source (`10 PRINT "..."`, `20 GOTO 30`) from the binary
  record, not a cached copy of what was typed.
- **`NEW`**: real clear. **`CLEAR`**: Section 8 lists it as a required, separate command, but with
  no variables yet (a real, separate future increment) it has nothing distinct from `NEW` to
  actually do — aliases `NEW` verbatim rather than being left unrecognized (a real RFC compliance
  gap) or silently wrong, a named, honest scope reduction.
- **Immediate Mode `PRINT`**: Section 4's own explicit example (`PRINT "HELLO"`, no line number)
  executes right away and is never stored, sharing its parser with Program Mode's own `PRINT`.
- **A real, necessary answer to "how do you stop a running program"**: choosing to support `GOTO`
  at all means a `10 PRINT "X" / 20 GOTO 10` infinite loop is a real, reachable program state. This
  backend has no keyboard IRQ yet (only polling), so `RUN` does a cooperative ESC check once per
  statement — cheap, since a real loop iterates far faster than a human can react — and a real
  `BREAK IN <line>` message on interrupt, matching classic BASIC's own convention.
- **A real, blocking finding closed as part of this increment, not deferred**: RFC-0045's own PS/2
  and USB HID drivers are explicitly "unshifted only" (a named Non-Goal). A real double-quote is
  Shift+apostrophe on US QWERTY — with zero Shift support, `"` could never actually be typed on
  real hardware, which would have made a quoted-string `PRINT` design real but genuinely unusable.
  Rather than pick a different, unconventional string delimiter to dodge the gap, added real,
  narrowly-scoped Shift tracking to both drivers: PS/2 tracks Left/Right Shift make/break codes as
  persistent state (scan codes are discrete press/release events); USB HID reads the boot-report
  modifier byte fresh on every poll (already a live, level-triggered snapshot, no state needed). In
  both cases, only the apostrophe key's own translation is disambiguated by it (`'` unshifted, `"`
  shifted) — this is not a general modifier system, just enough to make quoted strings real.

## Validation

New scenarios added to the existing `systems_aps_arcology_seed_substrate_smoke` (not a separate
test): numbered-line `PRINT` storage plus `RUN` executing multiple stored lines in real order
(including a real injected `shift-apostrophe` keystroke producing a real `"`), `GOTO` storage
reconstructed correctly by `LIST`, a real undefined-statement `GOTO` error, and a real infinite
`GOTO` loop actually run and actually interrupted by a real injected `ESC`. All via real QEMU
`sendkey` injection over the same monitor-socket path this fixture's other scenarios already use.
96/96 full regression suite.

Also manually verified under QEMU (not yet promoted to the automated suite): malformed Program Mode
lines (garbage after the line number, an unterminated string) both produce a clean `?SYNTAX ERROR`
with no partial modification to RPM; `CLEAR` aliasing `NEW`; a 6-digit line number (exceeding
Section 6's own 65535 ceiling) correctly rejected rather than silently overflowing.

## Explicitly NOT in scope for this increment

`LET`/variables, `IF`, `FOR`/`NEXT`, `PEEK`/`POKE`, `INSPECT`, and RPM's own free-memory reporting
(Section 6). Each is a real, separate, substantial undertaking — variables alone need real typed
runtime storage and an expression evaluator, neither of which exists yet. Out-of-memory (RPM full)
is implemented per Section 6's own "no partial modification" requirement (capacity is checked
before any array shifting begins) but not yet exercised by an automated test — reaching it requires
65 real typed program lines, impractical via scripted keystroke injection; verified by code review
of `RpmCommit`'s own capacity-then-shift ordering instead.

## A real bug found on real hardware, Round 3 (2026-08-23)

The user's own first physical test of this increment: `10 PRINT "HELLO"` then `RUN` printed
nothing at all and never returned to `READY.` on its own — only a real injected `ESC` broke out of
it, reporting a nonsensical `BREAK IN 3206755423`. Never reproduced under QEMU.

**Root cause, the exact same class of gap as RFC-0045's own earlier CR3/identity-map finding**:
QEMU's own VM RAM starts fully zeroed on every boot, a well-known QEMU behavior this fixture was
silently relying on. `RpmCountAddress` (RPM's own entry count) and `KeyModifierStateAddress`
(Shift-held state) were both read before any code path was guaranteed to have written them, with
no explicit zero-init — invisible under QEMU, but real physical RAM makes no such guarantee at
all. On the user's real laptop, `RpmCountAddress` read back as real leftover garbage, so `RUN`
walked a garbage-length "program" of uninitialized entries — almost none of which coincidentally
matched a real statement type, so nothing ever printed — until the real injected `ESC` caught it
mid-scan and reported whatever garbage happened to sit in that entry's own line-number field.

**Confirmed by direct reproduction, not just code review**: built a throwaway variant that
deliberately poisons `RpmCountAddress` with the user's own reported garbage value right after boot
(simulating real uninitialized RAM under QEMU, which otherwise never exhibits this), which
reproduced the exact same symptom shape (no `PRINT` output, `ESC` required, a nonsense break line —
`BREAK IN 0` rather than the user's own `3206755423`, since QEMU's RAM backing the entry table
itself was still zeroed even though the count was deliberately poisoned; the mechanism is
identical). Re-adding the zero-init on top of that same poisoned build fixed it immediately.

**Fix**: explicit `MEMORY.Write32(RpmCountAddress(), 0)` and `MEMORY.Write32(
KeyModifierStateAddress(), 0)` at the top of `Main()`, matching `TermReadyAddress`'s own existing
same-spot precedent. An audit of every other stateful counter/cursor in this fixture
(`TermCursorCol/RowAddress`, `DashEventCursorAddress`, `DashAssemblyCountAddress`) found each
already explicitly zeroed before first use — this was a real, isolated gap in the two pieces of
state this specific increment introduced, not a systemic pattern across the whole fixture. 96/96
regression suite unaffected (QEMU's own zeroed RAM meant this was always a no-op there).

## A second real bug found on real hardware, same Round 3 session

After the zero-init fix above, the user retested: the freeze/blank-output bug was gone and `RUN`
worked correctly, including `GOTO`, but real typed output was still hard to read — specifically,
`PRINT "hi"` rendered indistinguishably from "ni". Not a scale problem (RFC-0045 revision 0.9
already fixed that); a real font-quality problem the scale fix couldn't touch.

**Root cause**: the terminal shared its font with the boot dashboard — an 8x8 bitmap rasterized
offline from FreeMonoBold.ttf. Decoding the actual stored bitmap confirmed it directly: lowercase
`h`'s own glyph had only ONE pixel distinguishing it from `n` (a single dot in the top row for the
ascender), because 8 total rows isn't enough to give ascenders (b/d/h/k/l), x-height letters, AND
descenders (g/j/p/q/y) each real headroom — the offline rasterizer had silently squeezed the
ascender down to almost nothing to fit everything into 8 rows.

**Fix**: gave the terminal its own dedicated 8x14 font (`TermFontTableAddress`/
`TermFontGlyphRowByte`/`WriteTermFontData`), re-rasterized from the same FreeMonoBold.ttf but at
the classic CGA/EGA "8x14" text-mode cell size, with real ascender/x-height/descender headroom.
Confirmed by decoding and visually inspecting the new bitmaps for h/n/i/b/d/g/p/y/m before
committing, then by a real QEMU screendump of `PRINT "hi there night owl"` after — clearly
legible, `h`'s ascender and `g`'s descender both visibly distinct from x-height letters.
Deliberately scoped to the terminal ONLY: the dashboard keeps its own original 8x8 font and
untouched pixel layout (the user had already confirmed it "looks nice and unique"), verified by a
diff review confirming zero lines in the dashboard's own drawing functions changed. 95/96
regression suite (the one failure is the same pre-existing, unrelated PS/2 flake noted in RFC-0045
revision 0.3).

## Round 4: the font fix wasn't enough, plus a real Shift feature request

The user retested the 8x14 font fix: overall clarity improved, but "h still kinda looks more like
an n" — a real, fair continued complaint. Decoding the actual bitmap confirmed why: the ascender
was only 1 row taller than x-height before the arch even started, an easy-to-miss difference at
real viewing distance. Re-rasterized at size 13pt (same 14-row cell budget, confirmed no clipping
across all 95 glyphs), giving `h` a real 2-row ascender lead over `n`. Confirmed by decoding the
bitmap before committing, then by a real QEMU screendump of `PRINT "hi there night owl"` —
unmistakable this time.

Same session, the user also asked directly: "Can we get shift key support? I keep wanting to
type." The previous fix (RFC-0007 Round 3, part 1 above) only disambiguated the apostrophe key —
just enough to make quoted `PRINT` strings typable — leaving every other shifted character
(uppercase letters, `!@#$%^&*()`, etc.) unavailable. A completely reasonable thing to want from any
real keyboard-driven prompt, and the honest scope limit of the earlier fix.

**Fix**: replaced the narrow apostrophe-only override with a real, general `ApplyShift(ch)`
function — letters via the classic ASCII case flip (`-32`), plus a real US QWERTY shifted mapping
for the digit row and common punctuation keys, applied uniformly by both `PollAnyKey` paths (one
shared function instead of duplicating shift logic per-driver). Verified under QEMU:
`PRINT "Big Test 1!"` typed via real `shift-b`/`shift-t`/`shift-1` keystrokes produces exactly that
string. New automated scenario added to `systems_aps_arcology_seed_substrate_smoke` locks in
coverage. 96/96 full regression suite (two transient CPU-contention flakes under `-j4` — the known
PS/2 one plus an unrelated ArcFS test that passed clean in isolation).

## What this does not close

Real hardware validation of Program Mode after any of the fixes above has not happened yet —
Round 3 and Round 4 found and fixed the RPM zero-init bug, the font legibility bug (twice), and the
narrow-vs-general Shift gap, but none of the fixes have been re-confirmed on real silicon since
Round 4's own build was written. WP-033 (RFC-0045 Phase 5) predates this whole increment and only
exercises Immediate Mode (`HELP`/`OT`). A future hardware package would need a human to actually
type real uppercase/shifted-symbol text (proving general Shift genuinely works on real silicon,
not just the apostrophe), read the new 8x14 font back clearly, and run a `GOTO` loop (proving ESC
genuinely interrupts it) on the real keyboard(s) WP-033 already covers.
