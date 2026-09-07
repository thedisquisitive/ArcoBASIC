# ARCADE Surface

Surface is the WYSIWYG / direct-manipulation view of application presentation, structure,
responsive behavior, visual states, and declarative relationships (packet Sections 4.1, 6-14).

## Foundation: ArcoUI, not a parallel renderer

Packet Section 39.4 ("Fake Runtime") forbids a lightweight design-only control library that
visually approximates the real GUI. Section 10.2 requires the real ArcoGUI layout/rendering
semantics wherever technically possible — "the thing seen in Surface should be the thing that
runs."

This repository already has the right foundation for that: `arcoui::Runtime`
(`include/arcoui/core.hpp`), an intent-oriented, retained runtime with:

- a **semantic tree** (what an object means) kept structurally separate from a **presentation
  tree** (what actually gets laid out/rendered/hit-tested) — `SemanticNode`/`PresentationNode`,
  deliberately split so an intent can belong to a semantic object whose visible control lives
  elsewhere
- opaque, stable `arco::RuntimeHandle`s for every object — see `application-model.md`
- **Intents** with real state (`Available`/`Blocked`/`Unavailable`/`Active`) and a
  `blocked_reason` string — this is the actual data source for packet Section 14's Explainability
  Overlay ("Why are you disabled? ← CanSearch ← ..."), not something ARCADE has to build a parallel
  causality tracker for
- **Transactions** with `Begin`/`Update`/`Commit`/`Cancel`/`Undo`/`Redo` — the substrate for packet
  Section 23's unified undo/redo, already shaped correctly (one transaction = one undoable unit,
  regardless of how many internal model changes it causes)
- a `Surface` object with a real `Shape` enum including `Polygon`/custom point lists, not just
  `Rectangle` — packet Section 22 (custom-shaped applications) is not blocked on future work here,
  the data model already supports it

Surface (the ARCADE view) renders by driving this same `arcoui::Runtime`, the same way any other
ArcoUI application would — not a copy of its logic, not an approximation.

## Known gap: design-time reflection metadata

Packet Section 21 wants ARCADE to consume ArcoGUI's own metadata (control type name, constructible
properties, property types/constraints, layout capabilities, binding capabilities) rather than
hard-code a per-widget switch statement. This does not exist in ArcoUI yet — see
`docs/ARCADE_PROGRESS.md` Session 1, finding 2/3. Per the packet's own instruction, this is
recorded as a required ArcoGUI-side capability, not something ARCADE fakes with a private
substitute. It does not block the earliest milestones (Phase C only needs one Label, one Button,
one Container — small enough to describe by hand), but it will block the control palette (packet
Section 30) if not resolved by then.

## Intent-first layout

Packet Sections 8-9 (semantic layout relationships, semantic snapping, responsive/adaptive
surfaces) are not yet implemented against anything. Whatever layout primitives ArcoUI's
presentation tree exposes (currently just `x`/`y`/`width`/`height` on `PresentationNode` —
`core.hpp:42-52`) are the starting point; if that's insufficient for real relational layout
(align/margin/fill/center/anchor, packet Section 8), that's a gap to record against ArcoUI the
same way the reflection-metadata gap is, not something to invent inside ARCADE alone.

## Status

Not implemented. This document records direction, not shipped behavior.
