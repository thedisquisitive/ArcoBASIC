# ARCADE Application Model

The shared representation Surface, Flow, and Source all edit. Not yet implemented — this document
records the design direction established during Phase A reconnaissance, per packet Section 18.

## Why a model above the AST is needed

The AST (`CanonicalAstNode`, `src/frontend/parser.hpp`) already answers "what does this ArcoBASIC
program say," precisely and authoritatively (RFC-0012). It has no concept of a Surface, a Region, a
Control, a Component, a named State, or a data Binding — those are ARCADE-level ideas that a
`FUNCTION`/`CLASS`/expression tree alone cannot represent. Packet Section 18 requires exactly this
model: Application, Modules, Symbols, Functions, Classes, Surfaces, Regions, Controls, Components,
Properties, Layout relationships, States, Bindings, Intent/event relationships, Flow relationships,
Source ownership/spans, Runtime identity.

## Stable identity

Packet Section 18 is explicit: do not use array index or visual order as durable identity. This
matters concretely here because ArcoUI already solved exactly this problem one layer down —
`arco::RuntimeHandle` (`include/arco/runtime_handles.hpp`) is an opaque, stable handle used
throughout `arcoui::Runtime` for every semantic node, presentation node, intent, and surface.
ARCADE's own Application Model objects should carry a comparable stable identity (not necessarily
the same handle type/table — the Application Model exists at design time, when nothing may be
running yet, while `RuntimeHandle`s are minted by a live `arcoui::Runtime`) that:

- survives Source reformatting (so a diff-only source edit never re-numbers every object),
- survives Surface rearrangement (moving a control does not change its identity),
- can be resolved to a live ArcoUI handle once the application is actually running (Phase H),
- can be resolved to an AST span for "go to Source" navigation.

## What an Application Model object needs to carry

For each Surface/Region/Control/Component:

- stable ID (see above)
- AST ownership: which `CanonicalAstNode`(s) express it in Source, so edits can be written back
  precisely (see `source-roundtrip.md`)
- ArcoUI binding: which semantic/presentation handle it corresponds to once instantiated (nullable
  — a design-time-only object has none until Run)
- property values, each tagged with its source per packet Section 11: base/default, inherited,
  state override, runtime-bound, responsive override, or computed — never presented as a plain
  editable constant when it's actually derived
- layout relationships (packet Section 8) — not raw coordinates unless the underlying ArcoUI
  layout model genuinely requires that representation
- state deltas (packet Section 10) — a base structure plus explicit per-state overrides, never a
  duplicated whole structure per state

## Relationship to Flow

Flow objects (once designed — see `flow-integration.md`) reference into this same model: a Flow
node representing a Surface control's event handler must resolve to the same stable Control ID
Surface uses, not a separate parallel naming scheme.

## Status

Not implemented. This document will be updated with the actual class/struct design the moment
Phase C ("Minimal Shared Application Model," packet Section 34) produces one. Do not treat anything
above as already-built — it is the target shape, derived from reading the existing AST and ArcoUI
code, not a description of code that exists yet.
