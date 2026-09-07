# ARCADE Flow Integration

**This is the least-formed part of ARCADE's architecture, and that fact is deliberate to state
plainly rather than paper over.**

Packet Section 16 says: "Flow must use the established ArcoFlow model rather than inventing an
ARCADE-specific graph language." Phase A reconnaissance (`docs/ARCADE_PROGRESS.md` Session 1,
findings 4-5) found that **no established ArcoFlow model exists** — no graph/node IR, no
serialization format, nothing beyond the project's own vision notes and the now-deleted
`arcoflow.abas` prototype, which was a plain text editor with explicitly no graph/Intent view.

This is exactly the kind of "material architectural ambiguity" packet Section 1 anticipates:
> "If a material architectural ambiguity blocks implementation, document it in the project
> progress record and choose the smallest reversible implementation that preserves the packet's
> architecture."

## Direction, not yet design

The constraint the packet DOES fix, even without a pre-existing format to integrate with:

- Flow must reach real parity with ArcoBASIC control flow (Section 16) — bidirectional Source ⇄
  Flow navigation, subcharts as first-class functions/classes/reusable behavioral units.
- Flow is not a generic node editor (Section 39.6 forbids a Surface-specific callback system
  separate from ArcoBASIC/ArcoFlow semantics; by extension, Flow itself must not become disconnected
  wiring notation with its own execution semantics unrelated to what the AST already describes).

Given that, the most defensible starting point is deriving Flow's own node vocabulary from
`AstKind` (`src/frontend/parser.hpp:17-71` — the same enum the canonical AST already uses for
`If`/`While`/`ForEach`/`Call`/`Assign`/etc.), so a Flow node and its Source counterpart for the
same construct stay in a known, deliberate correspondence rather than two independently-evolving
models of "what a program is." This is a starting hypothesis for the eventual design, not a
committed architecture — it has not been validated against a real Flow editor yet.

## What Flow needs to support once designed (from the packet)

- Subcharts for functions, classes, reusable behaviors, isolated subsystems (Section 16)
- Runtime trace animating the active Flow path (Section 16, Section 26)
- Navigation: Surface control → Flow behavior, Flow operation → Surface element, Flow function →
  Source, Source function → Flow (Section 16, Section 32)
- Debugger visibility "like current flowing through a simulated breadboard" (Section 16) —
  aspirational, not a near-term milestone

## Status

Undesigned. This is flagged in the progress ledger as a likely Phase G blocker. Do not attempt to
design the full graph IR speculatively far ahead of Phase G — packet Section 34's own milestone
ordering puts Flow integration (Phase G) after the Application Model (Phase C), round-trip proof
(Phase D), and intent layout (Phase E) are real. Revisit this document with an actual design once
that context exists, rather than guessing at a graph schema in the abstract now.
