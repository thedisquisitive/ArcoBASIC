# ARCADE Source Round-Trip Strategy

Packet Section 19 requires ARCADE to not assume arbitrary user Source can always be losslessly
reformatted into an exact visual model and regenerated identically. This document adopts the
packet's own three-level strategy and grounds it in what actually exists in this repository.

## Level 1 — Fully Structured

Source constructs ARCADE understands completely and can round-trip semantically: the constructs
the Application Model has a real representation for (see `application-model.md`). Initially this
will be a small, deliberately narrow set — matching packet Phase C/D's own minimal target ("one
surface, one container, one label, one button" then "Window / Label / Button" round-trip).

## Level 2 — Structured with Preserved Source Regions

ARCADE understands the surrounding construct (e.g. "this is a Button declaration") but preserves a
custom expression or block verbatim/AST-backed rather than regenerating it — for anything inside
that construct the Application Model doesn't yet model precisely (a computed property expression,
an unusual argument shape). The canonical AST (`CanonicalAstNode`, RFC-0012) already carries real
source positions (`source_line`/`source_column`), which is the mechanism this level depends on:
ARCADE can know exactly which source span to leave untouched even while editing the surrounding
structure.

## Level 3 — Source-Only

Valid ArcoBASIC behavior with no Surface or Flow representation at all. Packet Section 19 is
explicit: ARCADE must allow this to exist, and the correct response to non-visualizable code is
**not** to reject it or destructively convert it. Given `stdlib/gui.abas`'s raw immediate-mode
`GUI.*` calls remain valid ArcoBASIC (Arconaut itself is built entirely this way, with no ArcoUI/
Surface concepts at all), Level 3 is not a hypothetical edge case — it is the *current* shape of a
large amount of real, working ArcoBASIC GUI code in this repository, and ARCADE must open, show,
and preserve it correctly even before Surface understands it.

## Coverage indicator

Packet Section 19's own example:

```text
Function SearchCustomer
SOURCE   ✓
FLOW     ✓
SURFACE  —
```

Any real ARCADE UI must show this per-symbol/per-object, not imply full coverage where none exists.

## Generated source discipline (packet Section 17.2)

Whenever Surface/Flow edits do regenerate Source text, that output must be deterministic, readable,
stable under no-op round trips, merge-friendly, and minimally noisy — never reformat unrelated
Source just because one control changed. This has a direct, testable consequence (see `testing.md`,
Round-Trip Tests): a no-op Surface→Model→Source pass over unmodified content must produce
byte-identical output, or the discipline has already failed.

## Line-numbered ArcoBASIC (packet Section 17.1)

ArcoBASIC supports a classic BASIC line-number style alongside structured modern style (confirmed
by `CanonicalAstNode::line_label`, `src/frontend/parser.hpp:91`, and `Stmt::line_label`,
`parser.hpp:124` — a real, already-parsed field, not something to add). ARCADE must not force line
numbers into structured Source, and must not strip them from Source that intentionally uses them. A
project may mix styles per compiler rules; ARCADE follows whatever the compiler already accepts,
not a stricter subset of its own invention.

## Status

Strategy adopted; no implementation yet. This document will gain concrete examples once Phase D's
first true round trip (packet Section 34) is working.
