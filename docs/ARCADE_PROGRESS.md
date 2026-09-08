# ARCADE Progress Ledger

**ARCADE** — Arco Runtime Construction & Application Development Environment. The Arcology
official IDE: four synchronized views (Surface / Flow / Source / Run) over one application model.
Full architecture contract: the agent packet quoted in Session 1 below (not yet copied into
`docs/arcade/` verbatim — see "Exact next action").

This file is not a changelog. Its job is to let another agent (human or AI) enter this repository
cold and understand the current engineering reality without reconstructing it from commit history.
Never delete a historical decision because a later one supersedes it — mark it superseded and link
forward.

---

## Session 1 — 2026-09-06

**Agent/operator:** Direct instruction from the project owner: "Time to start, for real, the
ArcoFlow Arcology IDE. We had a prototype, we're deleting that and starting over with it... start
implementing this agent packet" (the full ARCADE packet, Sections 0-50), with a same-message
correction that the working directory is `arcobasic/arcade/`, not `arcobasic/arcoflow/`.

**Current milestone:** Phase B (ARCADE Shell) complete and verified live against a real display.
Phase A (Repository Reconnaissance) is complete — see the findings below, unchanged from the first
pass. Phase C (Minimal Shared Application Model) has not started.

### Files changed

- **Removed** `arcoflow/` in its entirety (`git rm -r`, 9 tracked files: `arcoflow.abas` (443
  lines, the old prototype — a plain text editor with Run via `Process.Run` → `ArcoFission
  compile-run`, a project explorer, no graph/Intent view), `README.md`, `build.sh`, `serve.sh`,
  `concept_render.png`, and `example-project/` (a `.arcoproj` + three `.abas` files)). This was the
  prototype the project owner explicitly asked to delete and start over from. It was **not** wired
  into the top-level `CMakeLists.txt` or `cmake/*.cmake` (it had its own self-contained
  `arcoflow/build.sh`), so removing it needed no build-system changes. A few READMEs elsewhere
  (`arco3d/README.md`, `games/README.md`, `docs/arcofission.md`) mention `arcoflow/` as an
  illustrative build-pattern example; those references are now stale pointers to removed example
  code. Low-priority cleanup, not fixed this session — noted under "Known defects" below.
- **Created** `docs/arcade/` — the full Section 45 documentation set (`README.md`,
  `architecture.md`, `application-model.md`, `surface.md`, `flow-integration.md`,
  `source-roundtrip.md`, `runtime-inspection.md`, `testing.md`), each grounded in the Phase A
  findings below rather than restating the packet abstractly.
- **Created** this file, `docs/ARCADE_PROGRESS.md`.
- **Created** `arcade/` — Phase B's real, working shell:
  - `arcade/arcade.abas` — the ARCADE shell itself. `#IMPORT "arcoui"`; loads a `.arcoproj` via the
    existing `Project.Load`; opens one `App`/`Window` (ARCADE's own workspace, sized independently
    of whatever window size the opened project's own app declares — see the "Known defects" entry
    this replaced, below); a `ContentArea` class (`IMPLEMENTS Widget`) shows the real Source text
    (loaded via `File.ReadText` on the project's own `Entry` file) when the Source tab is active, an
    honest "not yet implemented" notice pointing at the matching `docs/arcade/*.md` file for
    Surface/Flow, and a real "press R to compile and run through `ArcoFission compile-run`" affordance
    for Run — four `Button` widgets (one per tab), each wired through a real `ArcoUI` intent
    (`win.DefineIntent("Show" + viewName)`), not a hand-rolled tab-bar hit-test.
  - `arcade/reference-project/` — the packet's own Section 35 "Customer Lookup" reference
    application (`main.abas` + `project.arcoproj`), used as the project ARCADE opens to prove all of
    the above. Deterministic in-memory sample data (three `CustomerRecord`s), a real `TextField` +
    `Button` + two `Label`s, and a real (if currently hand-rolled, not yet ARCADE-modeled) DEFAULT/
    ERROR/FOUND/NOT_FOUND state chain driven by the Search intent.
  - `arcade/README.md` — a short pointer into `docs/arcade/` and `docs/ARCADE_PROGRESS.md`.
  - `arcade/build.sh` — builds a standalone `arcade/build/arcade` capsule (a self-contained ELF64
    with the bytecode VM embedded, via `ArcoFission native`, the SAME command/convention the
    deleted `arcoflow/build.sh` prototype established — deliberately the more conservative,
    already-proven bytecode-capsule path, not the separate `--target linux-x86_64` no-VM native
    compiler backend this repository's own `fission.cpp` work, Phases 11-15, is about). Defaults
    to `build-release/ArcoFission` for a real `-O3` build (an unoptimized capsule embeds an
    unoptimized bytecode VM — a real, previously-measured ~26x slowdown); optionally also builds a
    web capsule if `ARCOFISSION_WEB_TOOLCHAIN_DIR` is set. Output lands in `arcade/build/`, already
    correctly gitignored by the existing bare `build/` pattern in the repository's own
    `.gitignore` (confirmed via `git check-ignore -v`, no new entry needed). Verified end to end:
    built the capsule, launched it standalone (bypassing `arco_cli`/interpreter mode entirely),
    screenshotted it showing the real four-tab shell with the reference project's Source content
    loaded correctly.

### Behavior added

- **ARCADE shell (Phase B, packet Section 34)**: opens a `.arcoproj` project, shows four tabs
  (Surface/Flow/Source/Run) sharing one project context, Source shows and can save real project
  file content (`Ctrl+S`), Run genuinely compiles and launches the real project through
  `ArcoFission compile-run` in a separate process/window, and returns control to ARCADE's own event
  loop cleanly once that window closes. All of this was driven live against a real X11 display with
  real `xdotool` clicks/keypresses and screenshotted at each step, not just read for plausibility —
  see "Tests added" below for exactly what was verified and how.
- **Reference application** (packet Section 35): a real, working "Customer Lookup" ArcoUI app,
  independently confirmed to render and respond to input correctly (Search with an empty ID
  produces the ERROR-state status text; the underlying `FindCustomer` logic for FOUND/NOT_FOUND
  could not be exercised live this session — see "Known defects" — but is the same straight-line
  code path already proven to compile and execute cleanly).

### Behavior removed or superseded

The `arcoflow/` prototype's own behavior (single-file text editing + Run, `.arcoproj` project
explorer, native + web capsule builds) is gone. Its Source-only-editing niche is now covered (more
narrowly, so far — no project explorer sidebar yet, no syntax highlighting) by `ContentArea`'s
Source tab inside the real ARCADE shell above; its `.arcoproj`-loading logic was the one piece
worth carrying forward as-is (see `LoadProject` in `arcade/arcade.abas`, adapted from the deleted
prototype's own proven version), everything else was rebuilt fresh rather than resurrected, per the
project owner's own "starting over" instruction.

### Tests added / currently passing / currently failing

No automated tests yet (packet Section 36's own categories — Model/Round-Trip/Layout/Renderer-
Parity/Runtime-Protocol/UI-Smoke — all need either the Application Model (Phase C) or a stable
enough shell to script against; `docs/arcade/testing.md` names the plan). What exists instead is
real, live, manual verification performed this session, screenshotted at every step:

- ARCADE shell opens `arcade/reference-project/project.arcoproj`, renders all four tabs at a
  correctly-sized 1200×760 workspace window (see "Known defects" for the bug this replaced), and
  the Source tab shows the real, current content of `arcade/reference-project/main.abas`.
- Clicking each of Surface/Flow/Source/Run correctly switches `ContentArea.ActiveTab` and what's
  drawn — confirmed with four separate screenshots, one per tab, each showing the expected content
  (Source: real file text; Surface/Flow: the tab-specific "not yet implemented" notice with the
  correct doc-file pointer; Run: the resolved entry-file path and the Press-R instruction).
- Pressing `R` on the Run tab genuinely shells out to `ArcoFission compile-run` against the
  reference project, which opens the real Customer Lookup application in its own window (confirmed
  by screenshotting that second window directly, showing the real "Customer ID / Search / Status:
  Ready" UI); closing that window returns control to ARCADE, whose own event loop resumes and
  redraws correctly (confirmed with a post-close screenshot).
- The reference application itself, launched directly (bypassing ARCADE, via `arco_cli` directly):
  clicking into the Customer ID field correctly gains focus (a visible caret/focus-ring change);
  clicking Search with an empty field correctly transitions to the ERROR state, updating the status
  label to "Status: enter a Customer ID" — confirmed only after finding and fixing a real bug (see
  "Known defects"/first bullet below) that silently no-opped the Search button entirely before the
  fix.

The full pre-existing project-wide test suite was **not** re-run this session (deferred, same
reasoning as before: no ARCADE-adjacent C++/compiler code changed this session that the existing
suite would exercise — only new ArcoBASIC application/documentation files were added, nothing in
`src/`, `include/`, or existing `tests/` was touched). Re-run it before/with the first ARCADE
change that touches shared C++ code (most likely Phase C, once an Application Model needs its own
C++ representation).

### Known defects

- **Fixed during this session, not left as a defect, but worth recording as a real bug found**: the
  reference application's `Search` button was silently inert — clicking it produced no visible
  effect (`Status: Ready` never changed) because `win.Attach(searchButton, "Search")` was called
  without first calling `win.DefineIntent("Search")`. `ArcoSurface.Invoke` resolves an intent name
  to a handle via a linear `IntentNames`/`IntentHandles` scan and silently returns `FALSE` (a
  no-op, not an error) when the name was never declared — so the button's own `HandleEvent`
  correctly returned `TRUE` (matching its own visual press feedback) while the actual
  `ArcoUI.InvokeIntent` call that would have queued a `PollEvent()`-visible event never happened.
  Found by actually clicking it and watching nothing happen, not by reading the code. Fixed by
  adding the missing `DefineIntent` call, with a comment on the exact failure mode for the next
  person who hits this same shape. This is a real, general ArcoUI usage trap (any app-defined
  intent needs a matching `DefineIntent` before any `Attach` references it by name) worth being
  aware of, not something specific to ARCADE.
- **Fixed during this session**: `LoadProject` originally read the opened project's own `Window`
  field (meant for the SIZE THAT PROJECT'S OWN APP WINDOW OPENS AT, e.g. the reference project's
  small 420×220) and used it for ARCADE's own shell window size too — opening ARCADE itself at
  420×220, clipping the fourth tab off-screen. Found by taking a screenshot immediately after first
  launch and seeing the cramped, clipped layout. Fixed by giving ARCADE's own shell a fixed,
  independent size (1200×760) and not reading the opened project's `Window` field for that purpose
  at all.
- **Real environment gotcha, not an ARCADE bug, documented so the next agent doesn't lose time to
  it again**: `Process.Env("ARCOFISSION_PATH")` returns empty in this development environment, and
  the resulting bare `"ArcoFission"` fallback resolves via `PATH` to `/usr/bin/ArcoFission` — a
  separate, apparently-stale system-wide install, NOT this repository's own actively-built
  `./build/ArcoFission`. Running Run without `ARCOFISSION_PATH` set produces a real but misleading
  failure (`unknown host function: GUI.SupportsShapedWindows`) that looks like a bytecode-VM/ArcoUI
  incompatibility but is not one — `./build/ArcoFission compile-run` against the exact same file,
  invoked directly, works correctly (confirmed: `GUI.Available()`, `GUI.SupportsShapedWindows()`,
  and a full `App`/`Window` construction all succeed standalone and inside a class constructor via
  `compile-run`). Always launch ARCADE (or run its own Run tab) with `ARCOFISSION_PATH` pointed at
  the repository's own build, e.g. `ARCOFISSION_PATH="$(pwd)/build/ArcoFission"`.
- Synthetic keyboard **text** input (`xdotool type`) did not register in this session's display
  environment when tested against the reference application's `TextField` (focus/click DID
  register correctly; typed characters did not appear). This matches an already-documented,
  pre-existing environment characteristic (see this project's own `[[project_arcoui]]` memory: "no
  Wayland input-injection tool" — GLFW's character-input callback path appears not to be reliably
  reachable via this specific X11/XTest setup). Not investigated further as an ARCADE-specific
  issue — the FOUND/NOT_FOUND Search states could not be exercised live this session as a direct
  result (only the ID-empty ERROR state, which needs no typed input, was confirmed). A real gap in
  test COVERAGE, not a known product defect.
- Stale `arcoflow/` mentions in `arco3d/README.md`, `games/README.md`, `docs/arcofission.md` (from
  the earlier deletion) — cosmetic, not functional, low priority, not fixed this session.

### Architectural decisions made (Phase A findings)

These answer the packet's own Section 42 "must remain open until repository inspection" list.
Every answer below comes from reading the actual current code, not from assumption — file paths
are given so a future agent can re-verify directly rather than trust this summary blindly.

1. **Is the compiler AST suitable as the persistent shared model, or is an application-level
   semantic model required above it?**
   The compiler already has a real, RFC-governed canonical AST: `CanonicalAstNode`
   (`src/frontend/parser.hpp:87`), part of **RFC-0012** ("ArcoFission Frontend → A-MIR Contract",
   `arcology-os/rfcs/RFC-0012_ArcoFission_Frontend_to_AMIR_Contract.md`), whose own stated purpose
   is exactly "one authoritative interpretation" of a program — no duplicated parsing, later stages
   never reinterpret source text. Every `Expr`/`Stmt` node exposes `canonical_ast()` returning this
   structure, carrying `source_line`/`source_column` (real position info, not synthesized) plus
   kind-tagged scalar/child/named-child/group fields general enough to represent every `AstKind`
   (69 variants — function/class/interface declarations, every statement and expression shape,
   including hardware-facing constructs the freestanding profile needs). `ArcoFission reveal FILE
   --stage AST` already renders it as a readable text tree (confirmed by running it directly on a
   throwaway file). **Finding: this AST is the right foundation for the Source-level shared model**
   — it is already authoritative, already stable (an RFC guards it against drift), and already
   carries real source positions. It is NOT by itself a full "Application Model" (Section 18) —
   Surface/Flow/state/binding/component concepts have no representation in it at all, and it has no
   machine-readable (e.g. JSON) serialization yet, only the human-readable `reveal ast` text dump.
   **Decision: build the Application Model as a layer ABOVE this AST** (own object identity, own
   Surface/Region/Control/Component/State/Binding graph, per Section 18), keeping the AST as the
   ground truth for anything ARCADE does NOT understand structurally (Section 19's "Level 3 —
   Source-Only" case) and as the target `CanonicalAstNode` shape Surface/Flow edits must ultimately
   resolve into when writing Source back out. A machine-readable AST dump (JSON or similar) is a
   real, disclosed gap ARCADE will need — recorded here, not invented ad hoc later.

2. **How does ArcoGUI currently declare surfaces and controls in ArcoBASIC?**
   There are **two real, currently-shipping layers**, not one:
   - `stdlib/gui.abas` (21,959 bytes) + the C++ `arco::gui` backend (`src/gui/glfw_backend.cpp` for
     Linux desktop, `src/gui/canvas_backend.cpp` for the web/WASM target, `include/arco/gui.hpp`
     for the shared interface) — an **immediate-mode** API: `GUI.Text`/`GUI.RoundedRectangle`/
     `GUI.WaitEvent`/etc., a hand-rolled `Widget` interface (`stdlib/gui.abas:28`), and concrete
     widgets like `Button` (`stdlib/gui.abas:294`) that a program draws explicitly every frame
     inside its own event loop. This is what Arconaut (`arcfs-utils/apps/arconaut/arconaut.abas`)
     uses directly, and what most of this session's own immediately-preceding work (the native
     compiler backend's GUI support, the layout-caching performance fix) touched.
   - `stdlib/arcoui.abas` (34,593 bytes) + a C++ **ArcoUI** core (`include/arcoui/{core,geometry,
     gesture,types}.hpp`, `src/gui/arcoui/*.cpp`, per this project's own memory: "RFC-ArcoUI",
     Milestones 0-4 delivered on this same branch) — a genuinely **intent-oriented, retained**
     model layered ON TOP of `gui.abas` (`stdlib/arcoui.abas:16` literally `#IMPORT "gui"`, and its
     own `Button` usage in `examples/arcoui_hello.abas` resolves to `gui.abas`'s class). Its C++
     `arcoui::Runtime` (`include/arcoui/core.hpp`) already has, independently of anything ARCADE
     asked for: a **semantic tree separate from a presentation tree** (`SemanticNode`/
     `PresentationNode`, explicitly split "so an intent be able to belong to a semantic object
     whose visible control is hosted elsewhere" — `core.hpp:38-41`), **opaque stable handles**
     (`arco::RuntimeHandle`, not array index or screen position) for every object, **Intents**
     with real state (`Available`/`Blocked`/`Unavailable`/`Active`) and a `blocked_reason` string
     (`core.hpp:74-81`), and **Transactions** with `Begin`/`Update`/`Commit`/`Cancel`/`Undo`/`Redo`
     (`core.hpp:127-134`). It also already models a `Surface` with a `Shape` enum including
     `Polygon`/custom point lists, not just `Rectangle` (`core.hpp:58-71`).
   **Finding: this is an unusually strong, load-bearing match for what the packet calls
   "ArcoGUI."** The semantic/presentation split is section 18's stable-identity requirement almost
   verbatim; Intent state + `blocked_reason` is section 14's Explainability Overlay's actual data
   source ("Enabled ← CanSearch ← ..." is directly answerable from `Intent::blocked_reason` once
   something populates it meaningfully); Transactions are section 23's unified undo/redo
   substrate; `Shape::Polygon` is section 22's custom-shaped-surface requirement, already real, not
   theoretical. **Decision: ARCADE's Surface is built on ArcoUI, not on raw `gui.abas`.** Raw
   `gui.abas` stays reachable underneath (ArcoUI itself depends on it for actual widgets/rendering,
   and Source-only ArcoBASIC programs using `gui.abas` directly must remain valid Source-only
   content per Section 19 Level 3), but ARCADE's own object model talks to ArcoUI's handles/
   intents/transactions, not to raw `GUI.*` calls.
   **Real, disclosed gap**: ArcoUI does not yet expose the design-time reflection metadata Section
   21 asks for (control type name, constructible properties, property types/constraints, layout
   capabilities, binding capabilities — a registry ARCADE's palette/inspector need to avoid a
   hardcoded per-widget switch statement). This does not exist anywhere in the current `arcoui`
   core or bindings. Per the packet's own Section 21 instruction ("record the requirement as an
   ArcoGUI interface dependency rather than embedding a private incompatible substitute"), this is
   recorded here as required upstream work, not something ARCADE fakes internally.

3. **Does ArcoGUI already expose property/control metadata?** No — see finding 2's gap above.
   `include/arcoui/bindings.hpp`/`bindings.cpp` bridge the C++ core to ArcoBASIC call sites, but
   there is no reflection/registration table of widget types and their own property shapes
   anywhere in the current tree.

4. **What is ArcoFlow's current serialization format?** **There is none. This is the single
   biggest finding of this session.** A repo-wide search for any graph/node/flow intermediate
   representation (`FlowGraph`, `FlowNode`, `class Flow`, `struct Flow`, any RFC with "Flow" in
   its own title) turned up nothing. The only things that have ever been called "ArcoFlow" in this
   repository are (a) this project's own memory/vision notes (not code), and (b) the just-deleted
   `arcoflow/arcoflow.abas` prototype itself, whose own README explicitly said "No Intent/graph
   view yet" — it was a plain text editor with a project explorer, nothing resembling a node/wire
   canvas or a persisted graph format ever existed on disk.

5. **Does ArcoFlow compile from a graph into ArcoBASIC AST, bytecode, or another IR?** Moot per
   finding 4 — there is no graph to compile from yet. **Decision, and the packet's own stated
   ambiguity-resolution rule applied here** (Section 1: "If a material architectural ambiguity
   blocks implementation... document it... choose the smallest reversible implementation that
   preserves the packet's architecture"): Flow's own graph IR does not yet exist and must be
   **designed**, not integrated with a pre-existing format. The design constraint the packet DOES
   already fix is that Flow must reach parity with real ArcoBASIC control flow (Section 16:
   bidirectional Source ⇄ Flow navigation, subcharts as first-class functions/classes/behaviors) —
   which means the new Flow IR's own node vocabulary should be derived from `AstKind` (finding 1)
   rather than invented independently, so a Flow node and an AST node for the same construct
   (`If`, `While`, `ForEach`, `Call`, `Assign`, ...) stay in a known, deliberate correspondence
   instead of drifting into two incompatible models of "what a program is." This is real,
   unstarted design work — not attempted this session, flagged as a likely Phase G blocker.

6. **What runtime debug/introspection channel already exists, if any?** None. `arcoui::Runtime`
   (finding 2) is a real, structured, in-process runtime, but nothing exposes it to an EXTERNAL
   process the way Phase H ("attach ARCADE to a running application... read object identity,
   current visible/enabled state...") requires. This is real, unstarted work, not a small gap.

7. **How are AEX/capsule application resources represented?** RFC-0046 (this project's own memory:
   "component-based, interface-resolved deps, capability declarations, ArcoBASIC Binding Table")
   is real but early — Section 61 Phases 1-4 of 15 are implemented (binary format, manifest/
   component-graph parsing), Phases 5-15 (capability evaluation, ArcFS-backed loading, native ABI,
   real hardware) are not started. **Decision: AEX is not mature enough to be ARCADE's project/
   packaging format today** — it is a real future integration point (an ARCADE project should
   presumably be able to build/ship as an AEX component eventually) but not a Phase A-D dependency.

8. **What existing project format should ARCADE open?** `.arcoproj` — a plain ArcoBASIC
   object-literal expression (`{Name: "...", Entry: "main.abas", Files: [...], Window: {...}}`,
   confirmed by reading the deleted prototype's own `example-project/project.arcoproj` before
   removal), loaded via a real, already-shipping host function `Project.Load`
   (`src/runtime/runtime.cpp`, registered alongside `ArcoSH.AssetsDir`/`Path.*` — see this
   project's own memory note on the `arcoflow`/`arcofission_pipeline` work). It evaluates the
   object-literal expression in a fresh, throwaway `Runtime` isolated from the caller's own globals
   specifically so a project's own field names never collide with anything else — already a
   deliberate, documented design choice, not incidental. **Decision: ARCADE keeps `.arcoproj`** as
   its project file format (Section 20 explicitly says to inspect existing conventions first
   rather than invent a new format), extending its schema as needed for Surface/Flow/designer-
   workspace-state fields (kept separate from semantic fields per Section 20's own instruction:
   "Designer-only state... must not create noisy source control diffs").

9. **Which UI toolkit currently hosts developer tools outside Arcology OS, if any?** None separate
   from ArcoGUI itself — and per the packet's own Section 43 ("Do not tie the application model to
   Win32, GTK, Qt, Cocoa, or browser DOM concepts... intended long-term UI/rendering substrate is
   ArcoGUI"), none should be introduced. The existing Linux desktop backend (GLFW window/input +
   Cairo/Pango rendering, `src/gui/glfw_backend.cpp`) is real, working, and was directly exercised
   and improved this same session (a genuine layout-caching performance fix landed in it earlier
   today, uncommitted as of this writing — see "Exact next action"). **Decision: ARCADE itself
   runs as an ArcoUI/ArcoBASIC application on this same backend**, eating its own dog food from day
   one rather than building a separate host-platform bootstrap tool, which the packet explicitly
   discourages (Section 43).

10. **What subset can run self-hosted on Arcology OS versus Linux/Windows during bootstrap?** Not
    resolved this session — Arcology OS's own native GUI story is still early (RFC-0047, Phase 0-1
    of a 4-phase native display driver, per this project's own memory — no native windowing/
    compositor yet). **Decision: ARCADE targets Linux first** (the same `linux-x86_64` bytecode-
    capsule and native compiler targets already proven this session for Arconaut), with an
    Arcology-OS-hosted ARCADE staying an explicit, out-of-scope-for-now future direction — matching
    the packet's own Section 43 "isolate host-platform bootstrap concerns" instruction and Section
    40's non-goals list (no requirement to solve this now).

### Temporary compromises

None yet — no code has been written.

### Questions requiring owner decision

1. **`.arcoproj` schema evolution**: extending it with Surface/Flow-relevant fields (Section 20)
   will change its shape. No decision needed to START (additive fields are safe, matching how
   `Window: {...}` was already added on top of the base `{Name, Entry, Files}` shape by whoever
   built the original prototype), but a real schema-versioning story may be worth a short RFC once
   the shape stabilizes past Phase D. Not blocking Phase A/B.
2. **ArcoUI's own missing design-time reflection metadata** (finding 2/3's gap): this is real,
   necessary upstream work, not something ARCADE can route around cleanly. Worth confirming whether
   this becomes its own ArcoUI milestone (extending the existing RFC-ArcoUI work already landed on
   this branch) versus something scoped as ARCADE-owned code that happens to live near ArcoUI. Not
   blocking Phase B/C (which only need one Label/one Button/one Container — small enough to
   hand-write metadata for without a registry yet), but will block Phase E (palette/component
   discovery, Section 30) if not resolved by then.
3. **Bootstrap platform** (finding 10): confirmed direction (Linux native, ArcoUI-hosted) needs no
   owner sign-off to proceed with, flagged here only so it's visible, not because it's contested.

### Exact next action

**Phase C — Minimal Shared Application Model** (packet Section 34). Concretely, in order:

1. Design and implement the smallest real Application Model that can represent the reference
   project's own shape: one surface, one container (or none, if the reference app's flat widget
   list doesn't need one yet — don't add a Region concept before something needs it), one label,
   one button, simple properties, **stable IDs** (packet Section 18 — see `docs/arcade/
   application-model.md` for the identity design direction already recorded). Decide concretely
   where this lives: a new C++ header/`.cpp` pair (most likely, given the AST and ArcoUI are both
   C++), or an ArcoBASIC-level structure inside `arcade/` itself if that proves sufficient for this
   small a model. Determine this by attempting it, not by assuming up front.
2. Prove **one direction** of round-trip first (packet Section 34's own Phase C acceptance: "model
   serializes deterministically, objects retain stable identity, Surface and Source can refer to
   same object") — this does not yet require Surface UI or two-way sync (that's Phase D). A model
   built from reading `arcade/reference-project/main.abas`'s own AST, referencing the same stable
   IDs a hypothetical Surface view would use, serialized deterministically, is Phase C's whole bar.
3. Add the first real automated test (packet Section 36.1, Model Tests) once there's a model to
   test — likely alongside `tests/unit/arcoui_core_tests.cpp`'s own plain-assert convention (see
   `docs/arcade/testing.md`). This is the FIRST ARCADE automated test; there are none yet.
4. Do NOT start on a control palette, property inspector, or Flow canvas before Phase C's model is
   real and tested, and Phase D's first true round trip (`Window / Label "Hello" / Button "Exit"`,
   Source ⇄ Surface, packet Section 34) works. The packet is explicit about this ordering (Section
   39.1 names "a canvas plus draggable buttons" as a failure mode to avoid skipping ahead into).
5. Revisit the "Questions requiring owner decision" list below, especially #2 (ArcoUI's missing
   design-time reflection metadata) — Phase C's own minimal scope (one label, one button) doesn't
   need it, but Phase E (palette) will, and it's worth flagging to ArcoUI's own maintainers/RFC
   process sooner rather than discovering the blocker mid-Phase-E.

## Session 2 — 2026-09-06

**Agent/operator:** Reactive bug-fixing pass driven directly by the project owner testing the
built `arcade/build/arcade` capsule and reporting three live symptoms verbatim: "It won't let me
close the application. It's also slow to update after clicking a button. And Surface stays
highlighted no matter what." No forward progress into Phase C this session — all of it went into
finding and fixing what those three symptoms actually were, which turned out to include one
significant, previously-undetected language-level bug (see below) well beyond ARCADE's own scope.
**Current milestone unchanged: still Phase B, now hardened.** Phase C (Application Model) is still
the next real forward step — see "Exact next action" above, still accurate.

### The headline finding: `NOT` is not logical negation in this language

This is the most important thing in this entry, and arguably in the ledger so far. While
debugging why a newly-added function (`PollRun`, below) never seemed to fire, direct measurement
showed:

```
NOT TRUE  = -2   (still truthy in an IF condition -- WRONG, should be FALSE)
NOT FALSE = -1   (truthy -- correct, but only by coincidence)
!TRUE     = FALSE  (correct)
!FALSE    = TRUE   (correct)
```

Root cause (`src/frontend/lexer.cpp`): the `NOT` keyword is lexed as `TokenType::BitNot` — a
genuine **bitwise** complement (`~`), the same token `~` itself produces, and `AND`/`OR` are
similarly aliased to `BitAnd`/`BitOr`. This is internally consistent with the classic BASIC
convention where `TRUE` is all-bits-set (`-1`): under that convention `~(-1) = 0` and `~0 = -1`,
so bitwise NOT and logical NOT coincide exactly. The bug is that this codebase's actual `TRUE`
(whether from a literal, a comparison, or a host function's returned C++ `bool`) is encoded as
plain `+1`, not `-1` — so `~1 = -2`, itself nonzero, so `IF NOT x THEN` is **truthy regardless of
x**. It never actually skips the guarded branch when `x` is `TRUE`. This was verified to be
long-standing, not a session regression: `git log -S'"NOT", TokenType::BitNot'` traces the mapping
back to the very first "Initial ArcoBASIC alpha snapshot" commit. `!` (the `Bang` token, a
genuinely separate operator) is the real, correct logical-not and was verified correct for both
`TRUE` and `FALSE` before it was used anywhere as the fix.

**Why this went undetected across a huge existing codebase**: `IF NOT x THEN` guards are
overwhelmingly written for the "bail out when x is FALSE" shape (`IF NOT File.Exists(...)`, `IF
NOT SELF.Ready`, etc.) — the common path where `x` really is `FALSE`, where the bug is invisible
(`-1` is still truthy, so the guard still fires when it should). It only misbehaves on the rarer
"proceed only while x stays TRUE" shape — a loop or guard that's supposed to stop once something
becomes `TRUE` — which is exactly the shape of `ShouldClose()`/`AnyOpen()` checks. This is a
genuine, scary blast-radius finding: **the fix in this entry only covers `stdlib/arcoui.abas` and
this session's own `arcade/` files** — `NOT` used the same broken way almost certainly exists
elsewhere across this large, multi-hundred-thousand-line ArcoBASIC codebase (Arcology OS, Arco3D,
games, ArcoFS tooling, etc.), unaudited. This deserves a real, deliberate decision from the project
owner — e.g. a project-wide grep-and-fix pass, a compiler warning/lint for `NOT <boolean-looking-
expression>`, or renaming/removing the `NOT`→bitwise alias entirely in favor of requiring `~` — not
a silent, partial fix buried in one feature's progress ledger. Flagged prominently here and reported
directly to the project owner in-conversation; not resolved project-wide this session.

**Update, same session, user-directed**: the project-wide audit was done immediately after this
was written, not left as a future decision. Grepped all 203 `.abas` files repo-wide for `NOT`
(740 raw hits, 715 were the English word "not" in comments -- real code hits were only 21). Found
and fixed 8 more real bugs beyond ARCADE/`stdlib/arcoui.abas`: `examples/arcoui_paint.abas`,
`arcoui_gadget.abas` (x2, including a genuine infinite-loop `WHILE NOT win.ShouldClose()` -- that
gadget could never close at all), `arcoui_notes.abas` (x3, including a real "every save after the
first always says Nothing to Save" bug), `gui_cube.abas`, `arcowrite.abas` (two `x = NOT x`
toggles that start from a `TRUE` literal and can never reach `FALSE` again). Empirically verified
(standalone repro scripts, not hand-reasoning about bit patterns) that some look-alike patterns
are actually fine and were left alone: a toggle starting `FALSE` self-corrects into a stable 0/-1
alternation; the compound `comparisonResult AND NOT boolFn()` shape coincidentally cancels out
correctly under this language's bitwise AND semantics (`1 AND -2 = 0`); `AS U32`/`AS U8`-typed
bitmask code in `arcology-os/` (`timer_policy.abas`, `aex_format_policy.abas`, `aps-*.abas`
fixtures) is genuine, correct, intentional bitwise NOT on hardware registers, not boolean logic.
Committed as `bb7eedc`. Not formally exhaustive of every conceivable future pattern, but covers
every real `.abas` NOT usage as of this date. The compiler-warning/rename option above remains
undecided/not pursued.

### The three reported symptoms, root-caused and fixed

1. **"It won't let me close the application."** Two real, independent bugs stacked on top of each
   other:
   - `RunProject()` called `Process.Run(...)`, which is a genuinely **blocking** `popen()`/
     `pclose()` (`src/runtime/runtime.cpp`'s `process_run`) — it does not return until the child
     process exits. Customer Lookup opens its own real GUI window and runs until a person closes
     it, so a synchronous `Process.Run` here froze ARCADE's entire event loop — no redraws, no
     click handling, no close handling — for as long as a run stayed open. Fixed by backgrounding
     the child (`(cmd; echo ARCADE_RUN_DONE) > outfile 2>&1 &`) so `Process.Run` itself returns in
     ~1ms instead of blocking for the run's full duration — **measured directly**: an isolated
     4-second inner `sleep` returned in 4.0s before the fix, ~0.001s after. The redirect had to be
     placed on the *outer* backgrounded subshell, not the inner command — an earlier attempt
     (`(cmd > file 2>&1; ...) &`) still measured ~4s, because a grandchild that inherits a copy of
     the pipe's write end before performing its own redirection keeps `popen`'s read loop blocked
     until *it* exits, a classic, easy-to-get-subtly-wrong shell/popen interaction.
   - Independently: `stdlib/arcoui.abas`'s `App.AnyOpen()` (`IF NOT surface.ShouldClose() THEN
     RETURN TRUE`) and the Draw loop's own `IF NOT surface.ShouldClose() THEN ignored =
     surface.Draw()` both hit the `NOT`-is-bitwise bug above — once `ShouldClose()` genuinely
     became `TRUE`, the guard stayed truthy anyway, so `AnyOpen()` could never actually return
     `FALSE` and the main loop could never exit on its own via a real close request. **This affects
     every ArcoUI app that uses `App.AnyOpen()`/`App.Start()`, not just ARCADE.** An earlier,
     apparently-successful live test ("ARCADE closed via a WM-level close request") turned out to
     be `xdotool windowclose` force-killing the process at the X level in this sandbox (confirmed
     via a captured `BadWindow` X error in the child's own output) rather than the app noticing
     `ShouldClose()` and exiting gracefully — the real bug was still live at that point. Fixed
     (`!surface.ShouldClose()` everywhere); re-verified via `Ctrl+Q` (ARCADE's own new in-app quit
     affordance, added this session for the same underlying "no close chrome" reason described
     below) closing the real standalone capsule (`arcade/build/arcade`) in ~30ms, process and
     window both confirmed gone.
   - Also added, while investigating: **`Ctrl+Q`** as an explicit in-application quit affordance.
     `xwininfo -tree` confirmed this environment's window manager adds **zero** title-bar/close-
     button chrome around an ordinary (non-shaped) ArcoUI window — there was no click target to
     close ARCADE with at all before this. `Escape` was deliberately not used (reserved as a
     plausible future "cancel current operation" key inside an editor).
2. **"Slow to update after clicking a button."** `Button.HandleEvent` only activates on
   `pointer-button` `release`, so a single real click spans at least two `GUI.WaitEvent` loop
   iterations (press, then release), each of which previously forced an unconditional full redraw
   (including the Source tab's real file-content render) — the same "redraws unconditionally"
   convention `App.Start()` itself documents as fine for small apps, uncritically copied into
   ARCADE's heavier-drawing shell. Fixed with a `needsRedraw` boolean, set `TRUE` only when a real
   event arrived or the active tab actually changed, plus a small periodic (~10-iteration, ~200ms)
   safety-net redraw as a hedge against any repaint need the tracking doesn't account for (e.g.
   window expose).
3. **"Surface stays highlighted no matter what."** Not a bug in `content.ActiveTab` (which mouse
   clicks correctly changed all along) — the persistent blue ring around "Surface" was ArcoUI's own
   **keyboard-focus** ring (`FocusIndex`, defaulting to whichever focusable widget was `Attach`ed
   first — always the Surface tab button), a completely different concept from click-driven tab
   selection; a click does not move keyboard focus in ArcoUI's model. Fixed by keeping references
   to all four tab `Button`s (`tabButtons` array) and setting the active one's `BackgroundR/G/B` to
   `win.Theme.AccentR/G/B` each redraw, all others to the dark default. Verified with exact
   `PIL.getpixel()` sampling, not just visual inspection — the first sample attempt was
   contaminated by leftover cursor-hover state at the sample coordinates (measured `(46,61,87)`,
   matching `Button.HoverR/G/B`, not the intended accent); moving the mouse away and re-sampling
   gave `(89,191,255)`, an exact match for `AccentR/G/B` (0.35, 0.75, 1.0) in 8-bit.

### PollRun() — the fourth piece, added to make fix #1 honest rather than a fire-and-forget stub

Backgrounding the Run child (fix #1) means `RunProject()` can no longer synchronously capture and
show the child's output the way it used to. Rather than silently drop that (which would make the
Run tab useless for seeing real compile errors — this app's other job), `ContentArea` gained
`RunInFlight`/`RunOutputPath` fields and a `PollRun()` method, called every main-loop iteration:
once the backgrounded shell's sentinel line (`ARCADE_RUN_DONE`, appended after the real command)
shows up in the output file, `PollRun()` reports the real captured output into `RunStatus` and
clears `RunInFlight`. Live-verified: typed `1002` into Customer Lookup's Customer ID field, clicked
Search, saw `Status: found Grace Hopper` (this ALSO re-verifies `TextField.HandleEvent`'s own
`NOT`-bug fix above — before it, typed keystrokes never reached a focused `TextField` at all, since
`IF NOT SELF.Focused THEN RETURN FALSE` was truthy even while genuinely focused); closed Customer
Lookup; confirmed ARCADE's Run tab picked up the real captured output asynchronously without ever
blocking.

### Known defects — correcting an earlier entry

Session 1's "Known defects" claimed synthetic keyboard text input "did not register in this
session's display environment," attributed to a `[[project_arcoui]]`-documented Wayland
input-injection gap. **That attribution was wrong** — this session typed `"1002"` into the exact
same `TextField` via `xdotool type` and it registered and displayed correctly, once
`TextField.HandleEvent`'s own `NOT`-is-bitwise bug (above) was fixed. The real cause was this
session's headline finding, not an environment limitation. Leaving Session 1's entry in place
un-deleted (ledgers should not rewrite history) but flagging the correction here.

### A testing-environment artifact worth naming (not a product bug)

`xdotool windowclose` in this sandbox does not deliver a graceful `WM_DELETE_WINDOW`-style request
the way it does on a normal desktop — it appears to force-kill the target process outright
(consistently reproduced a `BadWindow`/`X_GetWindowAttributes` or `X_SendEvent` X error in the
killed process's own output, and once via the *caller's* `xdotool` output too, ~30ms after a
`Ctrl+Q` had already closed the window on its own). All of this session's real close verification
was therefore done via `Ctrl+Q` (a path entirely internal to ARCADE's own ArcoBASIC code, not
dependent on WM-level `xdotool` tooling) rather than `windowclose`. Also found and worked around
twice: a stuck X11 button/key left over from this session's own earlier `xdotool` calls (once a
mouse button, this session again a `q` key, most likely from `xdotool key ctrl+q` racing a window
that closed mid-sequence) causing spurious clicks/keystrokes in later tests — `xdotool
mouseup`/`keyup` resolved both; neither was a real application bug. Also noted, not investigated:
ARCADE's own window title changes to `"ARCADE -- Customer Lookup"` after a Run, in this specific
window manager, for a reason not yet identified (no `GUI.SetTitle` call exists anywhere in
`arcade.abas`) — purely cosmetic, left as an open, low-priority curiosity.

### Files changed (Session 2)

- `arcade/arcade.abas` — `RunProject`/`PollRun` (async Run, described above), `Ctrl+Q` handling,
  `needsRedraw` gating, `tabButtons` active-tab highlighting, all in the main loop and
  `ContentArea` class; three `NOT` → `!` fixes.
- `stdlib/arcoui.abas` — five `NOT` → `!` fixes: `TextField.HandleEvent`'s `Focused` guard,
  `ArcoSurface.HandleEvent`'s `SetInputPassthrough` argument, `App.Pump`'s `RouteCloseEvents`
  guard, `App.AnyOpen`, `App.Start`'s own Draw loop. This is shared, foundational code — these
  fixes benefit every ArcoUI app, not just ARCADE.
- `arcade/reference-project/main.abas` — one `NOT` → `!` fix (its own hand-rolled Draw loop, mirrors
  `App.Start`'s pattern).
- `arcade/build/arcade` — rebuilt via `./arcade/build.sh` against `build-release/ArcoFission`, after
  confirming it was stale (built before this session's fixes existed). Re-verified standalone
  (bypassing `arco_cli`): `Ctrl+Q` closes the real shipped capsule cleanly.

### Build and run commands (packet Section 44)

```sh
# Build the ArcoFission compiler and the arco_cli interpreter ARCADE currently runs on top of
# (ARCADE has no C++ code of its own yet), from the repository root:
cmake --build build

# Launch ARCADE against the reference project DURING DEVELOPMENT, straight from source via the
# tree-walking interpreter -- fastest edit/test loop, no capsule build step. Needs a live X11/
# Wayland session (on this development box that means DISPLAY=:1 with WAYLAND_DISPLAY unset -- see
# this project's own [[project_arcoui]] memory for why). ARCOFISSION_PATH must point at this
# repo's own build, not a bare "ArcoFission" on PATH -- see "Known defects" above for the stale-
# system-install gotcha this avoids:
ARCOFISSION_PATH="$(pwd)/build/ArcoFission" \
WAYLAND_DISPLAY= XDG_SESSION_TYPE=x11 DISPLAY=:1 \
  ./build/arco_cli arcade/arcade.abas arcade/reference-project/project.arcoproj

# Open a bare .abas file instead of a project (no project explorer/context, matches how the
# deleted arcoflow.abas prototype's own no-project mode worked):
./build/arco_cli arcade/arcade.abas some/file.abas

# Build ARCADE as a real, standalone, double-clickable executable. As of Session 3 below, native
# builds go through the real project chain: `arcade/build.sh` -> `fissure run --full` ->
# `arcade/fissure.ab` -> `rivet build` -> `arcade/build.abas` -> ArcoFission's bytecode-capsule
# `native` command. Defaults to build-release/ArcoFission for the compiler and build/{fissure,
# rivet}/ for orchestration; set ARCOFISSION=/path, FISSURE=/path, or RIVET=/path to override.
# The optional web capsule still builds directly with ArcoFission when ARCOFISSION_WEB_TOOLCHAIN_DIR
# is set, because Rivet's current fission adapter only models native capsules.
./arcade/build.sh
ARCOFISSION_PATH="$(pwd)/build-release/ArcoFission" \
WAYLAND_DISPLAY= XDG_SESSION_TYPE=x11 DISPLAY=:1 \
  ./arcade/build/arcade arcade/reference-project/project.arcoproj

# Run the reference application directly (bypassing ARCADE, for isolated testing of the reference
# app itself):
./build/arco_cli arcade/reference-project/main.abas

# Compile-and-run the reference application through the bytecode VM instead of the tree-walking
# interpreter (this is what ARCADE's own Run tab shells out to):
./build/ArcoFission compile-run arcade/reference-project/main.abas

# ARCADE now has one buildchain smoke test:
ctest --test-dir build -R arcade_buildchain_smoke --output-on-failure
```

## Session 3 — 2026-09-07

**Agent/operator:** Direct instruction from the project owner: "Read the entire project. Then
finish converting the `arcade` subproject to the fission->fissure->rivet buildchain."

**Current milestone:** Phase B remains the product milestone; build orchestration is now converted.
Phase C (Minimal Shared Application Model) is still the next product feature.

### Files changed (Session 3)

- `arcade/build.abas` — new Rivet build description. Declares `BUILD.ArcoCapsule("arcade")`,
  `Entry = "arcade.abas"`, and `OutputDirectory = "build"`.
- `arcade/fissure.ab` — new Fissure project config. Registers `arcade.rivet-build`, a command
  probe that runs `rivet build --jobs 1`; dependencies include the shell, build file, and reference
  project files.
- `arcade/build.sh` — native build now runs `Fissure -> Rivet -> ArcoFission`; tool paths can be
  overridden via `FISSURE`, `RIVET`, and `ARCOFISSION`. `ARCOBASIC_STDLIB` is exported so copied or
  non-installed projects still resolve `#IMPORT "arcoui"`.
- `arcade/README.md` — updated to name `build.abas`, `fissure.ab`, and the new chain.
- `.gitignore` — added `.fissure/`, matching existing `.rivet/` state ignore.
- `tests/integration/arcade_buildchain_smoke.sh` and `cmake/Testing.cmake` — new automated smoke
  registered when `fissure`, `rivet`, and `ArcoFission` targets exist.
- Rivet fission support was completed by loading `rivet/adapters/toolchain/fission.ab` from
  `rivet/core/vm/vm.cpp`; the underlying fission target/API additions were already present in the
  worktree and are tracked in `rivet/RIVET_PROGRESS.md`.

### Tests executed and results (Session 3)

- `tests/integration/arcade_buildchain_smoke.sh build/fissure/fissure build/rivet/rivet
  build/ArcoFission .` — passes. Verifies `arcade/build.sh` reaches Fissure, Fissure runs the
  `arcade.rivet-build` probe, Rivet produces `build/arcade`, and a second direct `rivet build`
  reports `[CACHE] arcade.abas` with zero recompiles.
- `./arcade/build.sh` — passes in the real checkout, producing `arcade/build/arcade` through the
  converted chain.
- `ctest --test-dir build -R 'arcade_buildchain_smoke|rivet' --output-on-failure` — passes
  (`rivet_tests`, `rivet_smoke`, `arcade_buildchain_smoke`).

### Known limits after Session 3

- Rivet's fission adapter fingerprints only the entry file, not transitive `#IMPORT`s such as
  `stdlib/arcoui.abas`. This is already documented in `rivet/adapters/toolchain/fission.ab`; a
  future ArcoFission dependency-reporting mode or dedicated import scanner is needed for precise
  invalidation.
- Optional web capsule output remains outside Rivet for now because `ArcoCapsuleTarget` has no web
  target option yet.
