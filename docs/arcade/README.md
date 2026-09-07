# ARCADE

**Arco Runtime Construction & Application Development Environment**

ARCADE is the official Arcology development environment for ArcoBASIC applications. It is not a
form painter, not a generic node editor, and not a code generator that produces disposable output.

An ARCADE application is understood through four synchronized views over **one application
model** — none of them is the "real" program while the others are derived artifacts:

```text
SURFACE — what it is and how it presents   (visual structure, ArcoUI-backed)
FLOW    — how it behaves                    (visual behavior, a graph IR that does not exist yet)
SOURCE  — how it is expressed               (ArcoBASIC text, RFC-0012's canonical AST)
RUN     — what it is doing                  (the real runtime path, no design-time substitute)
```

Editing any one view changes the shared model; the other views update from that same model. Where
an exact round trip is impossible, ARCADE says so rather than silently discarding intent — see
`source-roundtrip.md`.

## Where things live

```text
arcade/                 ARCADE's own ArcoBASIC/C++ source (created, not yet populated)
docs/arcade/             this documentation set
docs/ARCADE_PROGRESS.md  the mandatory, continuously-updated progress ledger — READ THIS FIRST
```

`docs/ARCADE_PROGRESS.md` is not a changelog. It records the actual current engineering reality —
what exists, what was decided and why, what's still open — so any agent (human or AI) picking this
project up can continue without reconstructing context from commit history. Every session that
touches ARCADE updates it.

## Starting point

The full architecture contract (Sections 0-50, product identity, object model, layout philosophy,
milestone sequencing, testing requirements, failure modes to avoid, acceptance criteria) was
supplied verbatim by the project owner and is treated as the implementation contract for this
whole effort. It is not reproduced in full here — `architecture.md` summarizes it with pointers to
where each concern is actually implemented as work lands; the ledger's Session 1 entry has the
real repository-inspection findings (existing AST, existing ArcoUI runtime, the fact that no Flow
graph format exists yet, the project file format, and more) that the contract itself required
before any large implementation began.

## Current state

As of the ledger's Session 1 entry: reconnaissance complete, `arcade/` and `docs/arcade/` created,
no ARCADE code written yet. See `docs/ARCADE_PROGRESS.md` for the exact next action.
