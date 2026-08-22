# RFC-0007: First Real Increment — READY.

**Status:** Delivered and QEMU-proven. Real physical hardware validation (WP-032) prepared but not
yet executed, matching this project's own standing rule.

## Why this increment, now

Every fixture in this project's history, across every RFC, has followed the same shape: boot,
prove exactly one real capability, `CPU.HaltForever`. That shape was right for validating hardware
bring-up incrementally (RFC-0006, RFC-0038, RFC-0039, RFC-0044, etc.) but it is not what an
operating system is. The user asked directly whether it was time to "start Arcology OS dev proper."
RFC-0007 (ArcoBASIC Interactive Program Model) already existed in this repository, in Draft, never
implemented, defining exactly this: boot directly into a live `READY.` prompt as the actual
operating environment. This increment closes the three real prerequisites that vision needed and
had never been built.

## What was closed

1. **Real keyboard input.** `EFI_SIMPLE_TEXT_INPUT_PROTOCOL` had no binding anywhere in this
   project. New `SystemTable.ConsoleIn` field (offset `0x30`) and
   `UEFI.SimpleTextInputProtocol.ReadKeyStroke` (offset `0x08`), both added as ordinary
   `UefiField` table entries resolving through the existing generic `CallExternal` codegen path —
   no new hand-assembled machine code needed, confirmed with a minimal probe before committing to
   the design (the same "probe cheaply first" discipline this project has used repeatedly).

2. **Real dynamic console output — a genuinely new finding.** Every `ConsoleOut.Write` call in
   this entire project before this increment passed a compile-time string literal. A standalone
   probe confirmed `ConsoleOut.Write` actually accepts a raw `MMIOPTR` to a runtime-constructed,
   null-terminated `CHAR16` buffer, with zero dependency on ArcoBASIC's own `STRING` literal
   machinery — the real UEFI ABI underneath just needs "a pointer to a null-terminated buffer,"
   and the compiler's generic argument-passing path does not enforce a stricter type than that at
   the call site. This is what makes echoing an arbitrary just-typed keystroke possible without
   inventing a new compiler feature (a `CHR$`-equivalent) first.

3. **A genuinely persistent loop.** `arcology-seed-ready.abas` is the first fixture in this
   project's entire history that does not `CPU.HaltForever` after doing one thing. It boots,
   prints the banner and `READY.`, and busy-polls `ReadKeyStroke` forever, echoing typed characters
   live and dispatching recognized commands on Enter.

## Real command surface delivered

- `HELP` — prints the real command list.
- `OT` — prints one fixed whimsical message (RFC-0007 Section 11 describes a RANDOM selection
  among several; this shows exactly one, an explicit, named scope reduction, not a silent gap).
- Anything else — a real `?SYNTAX ERROR`, matching classic BASIC error convention.
- Backspace — real line editing, removing the last typed character both from the internal buffer
  and visually on screen.
- Command matching is case-insensitive (a real usability choice beyond the RFC's own letter,
  reasonable given only three short command words to cover and standard in real BASIC systems).
- A hidden, TEST-ONLY `EXIT` command, explicitly documented as NOT part of RFC-0007's own real
  spec — included purely so automated QEMU testing terminates deterministically instead of running
  to a fixed timeout on every single test.

## Explicitly NOT in scope for this increment

Program Mode (numbered lines, Resident Program Memory, `LIST`/`RUN`/`NEW`/`CLEAR`), `PEEK`/`POKE`,
`INSPECT`, memory-free reporting. Each of these is a real, separate, substantial undertaking on its
own — Program Mode alone needs a real line-numbered program store plus an execution engine for
stored lines, neither of which exists anywhere in this project yet. This increment is scoped
tightly to what RFC-0007 Section 1's own `READY.` vision literally needed: a live prompt that
reads real input and responds, nothing more.

## A real testing-infrastructure problem found and solved

Proving a genuinely interactive fixture under QEMU needed something this project had never done
before: injecting REAL keystrokes into a running QEMU guest, not just capturing output. Found and
used QEMU's own human-monitor `sendkey` command over a UNIX socket (`-monitor unix:PATH,server,
nowait`, scripted with `socat`) — the same scancode-delivery mechanism a real keyboard driver
relies on, not an ArcoBASIC-specific shortcut.

**A real, empirically-found timing bug caught during harness development**: sending a keystroke
immediately after the monitor socket becomes connectable silently drops it — 0/1 delivery in a
direct test. The monitor socket exists and accepts connections well before OVMF's own PS/2
keyboard driver is ready to receive/queue scancodes. Fixed by waiting for the GUEST's own captured
output to contain a real readiness marker (proof the application is actively polling) before
injecting, plus a fixed settle delay — confirmed 100% reliable across repeated manual tests and
3x automated determinism runs afterward.

New reusable harness: `scripts/run/run-uefi-hello-with-keyboard.sh`.

## Validation

- Fixture compiles cleanly.
- Real, full interactive sequence proven under QEMU on the first attempt after the harness itself
  was debugged: type `HELP` → Enter → real command list; type `OT` → Enter → real whimsical
  message; type an unrecognized command → Enter → real `?SYNTAX ERROR`; type `EXIT` → Enter → real
  controlled halt. Full trace confirms every step, each response followed by a fresh `READY.`.
- Backspace line editing proven separately: typing `held`, backspacing twice, typing `lp` correctly
  reconstructs and recognizes `help`.
- 3x determinism confirmed on the `HELP` path.
- New smoke test `systems_arcology_seed_ready_smoke` (structural compile check, real HELP/OT
  dispatch, real negative control, real backspace editing, 3x determinism), registered in
  `Testing.cmake`.
- Full regression suite: 92 tests total after this increment (see commit for pass/fail count).

## What this does not close

Real physical keyboard input has never been confirmed on real hardware — this is a genuinely new
validation surface beyond every prior package's own GOP/ArcFS/USB-boot proofs. `WP-032-
arcology-seed-ready-hardware-validation.md` is prepared, status `READY FOR HUMAN EXECUTION — NOT
YET VALIDATED`, matching every other package in this project. This is also the FIRST package that
is fundamentally hands-on rather than watch-and-photograph — no QEMU proof substitutes for a real
person typing on a real keyboard and confirming the real, live response.

The busy-poll `ReadKeyStroke` loop runs with zero delay between polls — real, intentional, matches
this project's own "no blocking primitives" precedent, but has never been observed running for more
than a few seconds at a time (QEMU test durations). WP-032's own checklist asks the human tester to
run a real multi-minute interactive session specifically to surface anything that only shows up
over a longer real run.
