# ARCADE Runtime Inspection

Packet Phase H requires attaching ARCADE to a running application and reading its object identity,
current visible/enabled state, selected properties, and active application state. Phase I builds on
that to allow narrowly-scoped live-safe edits.

## What exists today

**Nothing, for external attachment.** `arcoui::Runtime` (`include/arcoui/core.hpp`) is a real,
structured, in-process runtime with everything Surface needs conceptually (see `surface.md`) — but
it is in-process. Nothing in this repository exposes a running application's `arcoui::Runtime`
state to a *separate* process the way a debugger/inspector needs. This is confirmed by Phase A
reconnaissance (`docs/ARCADE_PROGRESS.md` Session 1, finding 6) — a real, unstarted piece of work,
not a small integration gap.

## What Phase H will need to build

At minimum, some channel (in-process hook if ARCADE ends up hosting the application directly for
debug purposes, or a real IPC protocol if it attaches to an independently-launched process) that
can:

- enumerate live `arcoui::Runtime` objects and their stable handles
- read current property values (position, size, visible, focused, intent state, `blocked_reason`)
- map a live handle back to the Application Model object that declared it (see
  `application-model.md`'s "ArcoUI binding" field) — this is what makes "select a live control →
  see its design object" (packet Section 15) possible at all
- eventually accept safe writes for Phase I's live-safe edit category, classified per packet
  Section 15's taxonomy (`LIVE-SAFE` / `REINSTANTIATE-CONTROL` / `REINSTANTIATE-SURFACE` /
  `RESTART-APPLICATION` / `RECOMPILE-REQUIRED` / `UNSUPPORTED-LIVE`)

## Explicit non-goal for now

Packet Section 15 is direct: "Do not fake hot reload by silently restarting the whole application
and pretending continuity." Until a real inspection channel exists, ARCADE's Run view is exactly
that — running the real application through the real runtime path, with no inspection or live edit
claimed. Do not build a placeholder that pretends otherwise.

## Status

Undesigned, unimplemented. Not a near-term milestone (Phase H comes after Flow integration in the
packet's own sequencing). Revisit once Phases B-G are real.
