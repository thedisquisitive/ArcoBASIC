# ARCADE Architecture

This is a working map of how the packet's architecture lands on this specific repository. It is
not a restatement of the packet — see the project owner's original agent packet (quoted in full in
the session transcript that started this work) for the actual contract. This document exists so
"the architecture" has one place to look that reflects real file paths, not abstract sections.

## The central model

```text
                      ┌───────────────┐
                      │ Application   │
                      │ Model         │
                      └───────┬───────┘
                              │
          ┌───────────────────┼───────────────────┐
          │                   │                   │
          ▼                   ▼                   ▼
      SURFACE               FLOW               SOURCE
   (ArcoUI runtime)   (new graph IR,       (RFC-0012 AST)
                        not yet designed)
          │                   │                   │
          └───────────────────┼───────────────────┘
                              ▼
                         RUN / TRACE
                    (real runtime path,
                  no external debug channel
                       exists yet)
```

Full findings behind each box: `docs/ARCADE_PROGRESS.md`, Session 1.

## What already exists vs. what ARCADE must build

| Layer | Status | Where |
|---|---|---|
| ArcoBASIC canonical AST (RFC-0012) | **Exists, authoritative** | `src/frontend/parser.hpp` (`CanonicalAstNode`) |
| ArcoUI runtime (semantic/presentation trees, intents, transactions, shaped surfaces) | **Exists, Milestones 0-4 delivered** | `include/arcoui/*.hpp`, `src/gui/arcoui/*.cpp`, `stdlib/arcoui.abas` |
| Immediate-mode GUI primitives (what ArcoUI itself is built on) | **Exists** | `stdlib/gui.abas`, `src/gui/glfw_backend.cpp` (Linux), `src/gui/canvas_backend.cpp` (web) |
| `.arcoproj` project file format | **Exists, real, already loadable** (`Project.Load`) | any `.arcoproj` file; loader in `src/runtime/runtime.cpp` |
| ARCADE Application Model (Surfaces/Regions/Controls/Components/States/Bindings above the AST) | **Does not exist** | to be built in `arcade/` |
| Flow graph IR | **Does not exist at all** — not even a prototype | to be designed, `arcade/` |
| Design-time widget/property reflection metadata | **Does not exist** in ArcoUI | real upstream gap, see ledger finding 2/3 |
| External runtime debug/introspection channel | **Does not exist** | real, unstarted, Phase H |
| ARCADE shell/Surface/Flow/Source/Run UI | **Does not exist** | to be built in `arcade/` |

## Layering decision

ARCADE's Application Model sits **above** the AST, not instead of it:

- **Source** is the AST directly (RFC-0012's `CanonicalAstNode`) — already authoritative, already
  carries real source positions, already the thing the compiler itself trusts.
- **Surface** is built on the ArcoUI runtime's semantic/presentation split and Intent/Transaction
  model, not on raw `gui.abas` calls and not on a parallel design-time renderer (packet Section
  10.3/39.4 forbid the latter explicitly).
- **Flow** needs a new graph IR. Because nothing existed before this session (ledger finding 4),
  its node vocabulary should be derived from `AstKind` (the same enum the AST already uses) rather
  than invented independently, so a Flow node and its Source counterpart stay in a known
  correspondence. This is real, unstarted design work.
- **Application Model** objects (Surface/Region/Control/Component/State/Binding, per packet Section
  18) get their own stable identity (not source line number, not screen position, not array
  index) and reference into the AST (for Source spans) and into ArcoUI handles (for the live
  Surface object), the same way the packet's Section 18 requires.

## Round-trip discipline

See `source-roundtrip.md` for the Level 1/2/3 strategy. In short: ARCADE must never assume it can
losslessly regenerate arbitrary existing Source, and must say so explicitly (packet Section 19)
rather than destructively rewrite what it doesn't fully understand.

## Bootstrap platform

ARCADE runs as an ArcoUI/ArcoBASIC application on the existing Linux desktop backend (GLFW +
Cairo/Pango), the same backend Arconaut and the ArcoUI examples already use. No separate
Qt/GTK/Win32 host framework — the packet is explicit about this (Section 43), and this repository
already has a working, actively-maintained native backend to build on rather than bootstrap a
second one. Arcology-OS-hosted ARCADE is a real future direction, not attempted now (ledger finding
10).
