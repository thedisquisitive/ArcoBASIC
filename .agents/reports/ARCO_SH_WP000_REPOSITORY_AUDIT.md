# WP-000: Repository Audit — ArcoSH Clean-Room Reimplementation

**Mission:** ArcoSH Clean-Room Reimplementation and `arco:` Semantic Terminal Protocol
**Packet:** (pasted directly into session, not yet saved as a file — see Open Question OQ-0 below)
**Repository root:** `/home/daedalus/projects/arcobasic` (git top-level)
**Branch:** `agent/arcfs-admin-suite`
**Date:** 2026-09-05

---

## 0. Scope note: this audit already changes the packet's own premise

The packet's own "Repository Baseline" section (§3) lists `rfcs/`, `docs/Arcology_OS_Design_Plan_vNext.md`,
`agent-packets/`, `src/compiler/`, `include/arco/`, `tests/` as if they were repo-root paths. **They are not.**
Every one of those paths exists only under `arcology-os/` — the freestanding UEFI/QEMU kernel subproject — not
at the repository root, where the just-deleted Linux `arcosh` (`apps/arcosh/main.cpp`,
`src/shell/arcosh.cpp`) used to live. This was surfaced to the project owner before WP-000 began; their
resolution: **ArcoSH should be a cross-platform base (AOS + Linux + Windows), authored in ArcoBASIC itself,
and — for this component specifically — compiled to genuinely native binaries, not the bytecode-VM-embedding
"arcocapsule" format the rest of this repository's native builds currently produce.** That decision reframes
several of this report's findings from "here's what exists" into "here's the real gap between what exists and
what the owner now wants," most importantly §5 below.

---

## 1. Repository shape (relevant to this mission)

This is one git repository containing several largely-independent projects. The two relevant to ArcoSH:

- **Repo root** (`src/frontend/`, `src/runtime/`, `src/compiler/fission.cpp`, `include/arco/`, `stdlib/`,
  `examples/`, `tests/`): the shared ArcoBASIC lexer/parser/AST/hosted-runtime/bytecode-compiler, and the
  Linux/Windows capsule toolchain (`ArcoFission`, `arco_cli`). This is where the deleted Linux `arcosh` lived
  and where the general-purpose bytecode VM (`execute_function` in `fission.cpp`) and this session's earlier
  loop-JIT (`include/arco/jit_x86_64.hpp`, Linux-only, narrow hot-loop scope) live.
- **`arcology-os/`** (its own `src/`, `include/arco/`, `rfcs/`, `docs/`, `tests/`, `agent-packets/`,
  `cmake/`): the freestanding UEFI/QEMU kernel subproject. Its own `CMakeLists.txt` links against the
  repo-root `arco_runtime`/`arco_compiler` (`arcology_os` is itself a dependency of `arco_compiler` — see
  root `CMakeLists.txt:101`), so language semantics are shared, but it adds an **entirely separate native
  x86-64 code generator** (`arcology-os/include/arco/x86_64_encoder.hpp`,
  `arcology-os/include/arco/pe_image.hpp`, `calling_convention.hpp`, `uefi_bindings.hpp`) targeting a
  restricted **freestanding profile** of the language, used to compile real UEFI PE binaries with no OS
  underneath them.

Nothing under `lazarus/` or `arcology-commons/` is relevant to this mission.

## 2. ArcoBASIC frontend/parser/AST/semantic execution

- Lexer: `src/frontend/lexer.cpp` / `src/frontend/lexer.hpp` (repo root, shared by every target).
- Parser/AST: `src/frontend/parser.cpp` / `parser.hpp`.
- Tree-walking interpreter + host-function registry (`Runtime::register_function`, the `Value` variant, all
  the universal built-ins made target-agnostic this session — `Directory.*`, `File.*`, `Path.*`, `Input`/
  `ReadLine`, `ArcoSH.AssetsDir`, etc.): `src/runtime/runtime.cpp`, `include/arco/runtime.hpp`,
  `include/arco/value.hpp`.
- Bytecode compiler + VM (`ArcoFission`'s A-MIR → bytecode → `execute_function`, this session's loop-JIT):
  `src/compiler/fission.cpp` (~7700 lines), `include/arco/jit_x86_64.hpp`.
- Freestanding lowering (the *other* native backend, used only by `arcology-os`'s own UEFI target): still
  inside `src/compiler/fission.cpp` (the same file — freestanding lowering is a mode of the same compiler,
  not a separate one), gated by `#RUNTIME NONE` / the freestanding-profile checks documented in
  `arcology-os/docs/systems/uefi-target.md`.

**`Value` representation** (`include/arco/value.hpp`): a `std::variant<monostate, bool, double, string,
shared_ptr<Array>, shared_ptr<Object>, shared_ptr<RuntimeHandle>, shared_ptr<BitVector>, shared_ptr<Array>
(tuple), shared_ptr<RangeValue>>`. This is the dynamic value model every hosted/capsule ArcoBASIC program
uses today. **It is not available in the freestanding profile at all** — see §5.

## 3. RFC-0007 / Immediate Mode / Resident Program Memory — already exists, but not as a reusable component

`arcology-os/rfcs/RFC-0007_ArcoBASIC_Interactive_Program_Model.md` (536 lines) specifies exactly the
immediate/numbered-line semantics this packet's §5.2/§5.3/§8 restate. **It is already implemented** — but as
one enormous, monolithic, hand-written ArcoBASIC *program*
(`arcology-os/tests/fixtures/aps-arcology-seed-substrate/aps-arcology-seed-substrate.abas`, 8,532 lines),
compiled through the freestanding backend into the actual Arcology OS boot console, using raw
`MEMORY.Read64`/`Write64`-style primitives to implement its own line-number table, keyboard input, and GOP
text rendering by hand (no dynamic `Value`/array/object types — see §5). It is real and QEMU-proven (per
prior-session project memory: RFC-0007 Program Mode, hardware-tested through multiple rounds), but it is:

- **not a reusable RPM abstraction** — no `ResidentProgram` type, no `InsertOrReplace`/`Delete`/`List`
  interface the packet's §8 describes; the line-table logic is inlined, ad hoc, and specific to this one
  8,500-line program;
- **not connected to any session/capability/presentation model** — there is no `ArcoSHSession`, no
  capability context, no presentation abstraction; it *is* the whole boot console, undifferentiated;
- built for a language subset (freestanding profile) that cannot host the typed dynamic values (§6.1),
  structured errors (§6.3), or object/collection dataflow (§7) this packet requires.

Per packet §2, RFC-0007's *concepts* (immediate mode, numbered-line RPM semantics) are an accepted legacy
input. This fixture is the closest existing reference for "what a native, self-hosted ArcoBASIC interactive
console looks like on this compiler," and is worth reading for that reason — but it is not something WP-001
can import or extend directly; the packet's actual `ResidentProgram` abstraction (§8) does not exist yet
anywhere in this repository, on any target.

## 4. Object/capability/session contracts

No `ArcoSHSession`-shaped abstraction exists anywhere in the repository today (grep for `ArcoSHSession`,
`CapabilityContext`, `SessionContext` returns nothing outside this audit and the packet itself).

**Capability enforcement**, specifically: `RFC-0046_Arcology_Executable_Assembly_AEX.md` defines the
capability-request model this packet's §5.5/§18 assume ("declarations, not grants"). Per this session's own
project memory, **AEX implementation is at Phases 1–4 of 15** — manifest/component-graph parsing is done;
**capability evaluation (the actual enforcement mechanism) is Phase 5+, not started.** There is currently no
kernel-level capability-context object an ArcoSH session could hold a reference to and be denied against.
`tests/systems/systems_aex_capability_requests_smoke.sh` exercises the *declaration* parsing only, not
enforcement.

**Implication:** packet §5.5/§18/§22.9 ("capability denial") cannot be implemented against a real enforcement
backend on the Arcology OS target today. On the Linux target there is no capability model at all — the
Linux runtime's host functions execute with the calling process's own OS-level permissions, full stop.

## 5. The central open question: dynamic values vs. "native, no arcocapsule"

`arcology-os/docs/systems/uefi-target.md:194`, verbatim: **"No heap allocator, no garbage collector, no
dynamic `Value` runtime"** for the freestanding profile — confirmed also by
`arcology-os/docs/systems/README.md:142` ("the freestanding profile, rejecting hosted-runtime constructs").

The packet's core dataflow requirement (§6.1: `p = PROCESS.LIST(); PRINT p` — a dynamic collection of typed
objects, filtered/sorted/reassigned at runtime) is a direct, structural use of the dynamic `Value` model
(`shared_ptr<Array>`/`shared_ptr<Object>`, heap-allocated, reference-counted). **The freestanding/native
compilation path that produces real UEFI PE binaries today cannot lower this** — it was deliberately scoped
out (per `uefi-target.md`) because a freestanding kernel target has no allocator yet.

On the Linux/Windows side, the dynamic `Value` model **is** fully available — but every existing way to
produce a binary that uses it (`ArcoFission build`/`native`) embeds the same bytecode VM
(`execute_function` in `fission.cpp`) this session spent its first half optimizing. There is no existing
"compile general ArcoBASIC, dynamic values included, straight to native machine code with zero interpreter
loop" backend anywhere in this repository. The one native-machine-code generator that exists for arbitrary
(non-loop) control flow (`arcology-os/include/arco/x86_64_encoder.hpp`) is explicitly scoped to the
freestanding profile specifically *because* it has no dynamic-value runtime to lower against. This session's
own loop-JIT (`include/arco/jit_x86_64.hpp`) is narrower still — pure numeric arithmetic, no strings/objects/
arrays at all, and Linux-only (`mmap`/`mprotect`).

**In short: no target in this repository today can compile a dynamic-value-using ArcoBASIC program
(which any real interactive shell — variables of unknown type at compile time, process/file/session objects,
etc. — structurally is) to a binary with no embedded interpreter.** Achieving the owner's stated goal ("pure
native binaries and no arcocapsules for this one") for a real ArcoSH is not a shell-implementation task; it
is a **new compiler backend** — either (a) a real ahead-of-time native-code generator for the dynamic-Value
subset of the language (general register allocation, heap-backed `Value` lowering, GC or refcounting in
native code, function calls, string/array/object operations — a materially larger project than this
session's loop-JIT), or (b) extending the freestanding profile with a real allocator and dynamic-Value
lowering (which changes a deliberate, documented design boundary of the freestanding target — a decision
`uefi-target.md`'s own authors evidently made on purpose, not an oversight). This is the single largest
feasibility question in the whole mission and is **not resolvable by this audit** — it is a decision for the
project owner, made with this finding in hand (see Open Questions, OQ-2).

## 6. Structured errors

No structured error type exists anywhere in this codebase today. `Runtime::run_string` and every host
function communicate failure as a **plain `std::string` message** (`RunResult{bool ok; std::string error;}`
in `include/arco/runtime.hpp`; every `throw std::runtime_error("...")` across `runtime.cpp`/`fission.cpp`).
There is no `domain`/`code`/`recoverability`/`object` structure of any kind. Packet §6.3/§17's "structured
errors" requirement is a genuinely new piece of language/runtime surface, not something to wire up to an
existing type.

## 7. Graphics/surface APIs — two unrelated systems, matching the split in §5

- **Linux desktop**: `include/arco/gui.hpp` / `src/gui/glfw_backend.cpp` / `src/gui/canvas_backend.cpp` /
  `src/gui/stub_backend.cpp` — a `Value`/`std::string`-typed window/surface API (GLFW+Cairo/Pango backed),
  plus the newer higher-level `arcoui` layer (`include/arcoui/`, `src/gui/arcoui/`) built earlier this
  session's project history. Fully dynamic-Value-based; only runs where the bytecode VM runs.
- **Freestanding (arcology-os)**: `arcology-os/include/arco/graphics.hpp` — a low-level, non-Value,
  fixed-width `PixelFormat`/`Color`/framebuffer API for GOP/UEFI bring-up
  (`arcology-os/docs/systems/graphics-foundation.md`, `graphics-substrate-in-arco-basic.md`). No windowing,
  no widgets, no dynamic types — this is closer to a hardware framebuffer driver's surface than a desktop
  GUI toolkit.

Packet §14's "audit existing surface/framebuffer/graphics contracts before creating new public names" needs
to reckon with the fact that these two APIs share almost no shape or vocabulary. A cross-platform
`PresentationSink`/`arco:` graphics capability set (packet §11/§13.7) will need to be a new abstraction that
can lower to *either* of these, not a thin wrapper around one of them.

## 8. Terminal/console abstractions

No `arco:` scheme, `ArcoTerminal`, or "semantic terminal protocol" work exists anywhere in the repository
(confirmed by grep across `.md`/`.hpp`/`.cpp`) — this part of the mission is a genuine clean slate, not a
refactor. The closest prior art is RFC-0045 (`Polymorphic_Substrate_Console_and_Real_Hardware_Input.md`),
which covers real keyboard/GOP-console input plumbing at the freestanding/hardware level (already
QEMU-and-hardware-proven per project memory) — relevant as a *transport-level* input source `arco:` could
sit on top of on the Arcology OS side, not as a protocol design itself.

On Linux, there is no existing PTY/terminal abstraction in this repository at all (the deleted `arcosh` wrote
directly to `std::cout`/read directly from `std::cin`/`termios`; nothing reusable survives that removal
worth resurrecting per the clean-room mandate anyway).

## 9. Build/test baseline

Full build (`cmake --build build -j$(nproc)`) is currently clean at repo root (last verified this session,
prior to this audit, while landing the loop-JIT leaf-call-inlining work). The fast test subset (13 tests:
`arco_runtime_tests`, `jit_x86_64_tests`, `jit_loop_smoke`, both ArcoFission smoke suites, etc.) plus
`arcology_commons_unit_tests` all pass. `arcology-os`'s own QEMU-based suite was not re-run for this audit
(expensive — minutes per run under parallel load, per this session's own project memory on QEMU harness
timing) and should be re-baselined before any WP-001 work that touches `arcology-os/` specifically, so any
new failure can be attributed correctly. One known, pre-existing, unrelated failure from earlier this
session: `arcosh_alpha_smoke` no longer exists (the test itself was deleted along with `arcosh`) — not
applicable here.

## 10. Archived / non-authoritative references

Per packet §2, the following are historical evidence only, not implementation specification, and were **not**
used as a basis for anything in this report or should be for future WP-001+ work:

- the deleted `src/shell/arcosh.cpp`/`apps/arcosh/main.cpp` (Linux shell, removed this session — git history
  only, `git show HEAD~N:...` if ever needed for *historical* comparison, never as a spec);
- this session's own prior conversation summary describing that shell's feature set (mods, sysadmin helpers,
  login-shell installer, prompt templating) — none of that is a requirement here.

---

## Open Questions (blocking WP-001 until resolved)

**OQ-0 — Packet not yet saved as a file.** The mission packet exists only as pasted chat text this session.
Recommend saving it verbatim under `arcology-os/agent-packets/` (matching that directory's existing
convention) as the first WP-001 action, so future agents don't depend on scrollback.

**OQ-1 — Which repository is "the" ArcoSH home?** Given the owner's cross-platform decision, ArcoSH is
neither purely a repo-root project nor purely an `arcology-os/` project. A location decision (new top-level
`arcosh/` directory with per-target backends? something else?) is needed before WP-001 creates any files, to
avoid rebasing directory structure mid-mission.

**OQ-2 — "No arcocapsules" feasibility and sequencing (the big one, §5 above).** No compiler backend in this
repository can today compile a dynamic-Value ArcoBASIC program to native code with no embedded interpreter,
on any target. Recommend the owner choose one of:
  (a) build ArcoSH first on the existing bytecode-VM capsule path (fully dynamic, fully cross-platform via
      Linux/Windows `ArcoFission` today, *not yet native-only*) to get the session/RPM/protocol architecture
      real and testable, with "compile ArcoSH itself with no embedded VM" as an explicit, later, separate
      compiler-backend work stream layered in once it exists — this is what the packet's own §9 ("smallest
      reusable execution bridge... do not fork language semantics") seems to assume was possible;
  (b) start with the compiler backend work (a real AOT native codegen for dynamic-Value ArcoBASIC, or a
      freestanding allocator + dynamic-Value lowering) as its own mission before ArcoSH's shell logic is
      written, accepting that no interactive shell exists until that lands;
  (c) some explicit hybrid/staging the owner specifies.
This is the one decision this audit cannot make on the owner's behalf, and blocks meaningfully starting
WP-001 (session core) in a way that won't need to be re-architected once a backend decision is made.

**OQ-2 resolved (2026-09-05), option (b), and it grew into its own RFC.** The project owner chose
compiler-backend-first, and pursuing it surfaced that this genuinely is a separate, general Arcology
platform capability — not an ArcoSH implementation detail — now governed by **RFC-0049**
(`arcology-os/rfcs/RFC-0049_Native_Hosted_ArcoBASIC_Compilation_and_System_Runtime.md`), with its own
progress ledger (`.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md`, renamed from `ARCO_SH_PROGRESS.md` — the
entries after this WP-000 audit are RFC-0049's history, not ArcoSH's). Phase 1 of RFC-0049 (Linux/System
V: PRINT, arithmetic, a real reference-counted `ArcoValue` runtime ABI) is implemented and tested. WP-001
(ArcoSH's own session core) has still not started, and should now be scoped as a consumer of RFC-0049
rather than something that builds a compiler backend itself.

**OQ-3 — Capability enforcement doesn't exist yet (§4).** Packet §5.5/§18's capability-context requirement
has no real enforcement backend to attach to on Arcology OS (AEX capability evaluation is unimplemented) and
no equivalent concept at all on Linux. Recommend an explicit, disclosed placeholder capability model for
early work packages (matching packet §21's WP-000→WP-001 sequencing, which does not gate session-core work on
this), clearly marked as provisional pending real AEX enforcement.

**OQ-4 — Graphics abstraction target.** Given §7's two-unrelated-APIs finding, should `arco:`'s graphics
capability set be designed against the freestanding `arco::graphics` shape, the Linux `arco::gui` shape, or a
genuinely new third abstraction both lower to? Affects packet WP-010 sequencing and naming.

---

## Next safe task

Resolve OQ-0 through OQ-2 with the project owner (OQ-3/OQ-4 can default to the recommendations above without
blocking). Do not begin WP-001 file creation until at least OQ-1 and OQ-2 are answered — both determine
where files live and what execution model session/RPM code is written against, and getting either wrong
means redoing WP-001 rather than extending it.
