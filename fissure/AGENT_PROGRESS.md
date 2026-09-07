# Fissure — Agent Progress

This file is required by the Fissure RFC + Agent Coding Packet (section 22.4) and must be updated
continuously so work can move between agents without losing architectural context. It follows the
same convention `docs/ARCADE_PROGRESS.md` established for ARCADE in this same repository — one
dated session entry per work session, never rewritten, only appended to or corrected in place with
a visible note.

## Session 1 — 2026-09-07

**Agent/operator:** Direct instruction from the project owner: the full Fissure RFC + Agent Coding
Packet, delivered verbatim (see `docs/fissure-rfc.md` for the saved source), immediately after
finishing an ARCADE bug-fixing session ("Slight detour"). Following the packet's own First-Agent
Task Sequence (section 22.5) in order.

### Task 1 — Inspect the existing repository before creating structure

Done. Findings that shaped everything below:

- The embeddable ArcoBASIC VM is `include/arco/runtime.hpp`'s `arco::Runtime` class — already the
  proven embedding boundary for every native tool in this repo (`arco_cli`, `ArcoFission`,
  `arcfs-utils`'s FUSE daemon). Public surface used here: `run_string(code)`,
  `register_function(name, HostFunction)` where `HostFunction =
  std::function<Value(const std::vector<Value>&)>`, plus `set_global`/`get_global` for passing
  data across the boundary without a function call. This is the "cleanest embedding boundary" the
  packet's task 2 asks to determine — it already exists and is exactly what every other native
  ArcoBASIC host in this repo uses, so Fissure follows the same shape rather than inventing a
  second one.
- Build integration precedent: `arcfs-utils/CMakeLists.txt` (a subdirectory added via the root
  `CMakeLists.txt`'s `add_subdirectory(arcfs-utils)`, its own static library linked against
  `ArcoBASIC::runtime`, `arco_configure_library()` for include paths). Fissure's own
  `fissure/CMakeLists.txt` follows this same shape.
- `cmake/Dependencies.cmake` establishes the project's own `pkg_check_modules(... QUIET
  IMPORTED_TARGET ...)` convention for optional external libraries (GLFW3/PANGOCAIRO/GTK3/FUSE3).
  SQLite3 (this packet's own recommended persistence layer, section 16) is available on this
  development machine (`libsqlite3-dev` installed, `pkg-config --exists sqlite3` succeeds) and is
  wired the same way, gated the same conservative way FUSE3 is (build without it, degraded, rather
  than hard-failing the whole project configure).
- No existing SQLite usage anywhere in this repo — this is a genuinely new dependency, not a reuse
  of something already wired in.
- `arco::Value` (the language's own dynamic value type — `Value::Object`, `Value::Array`, string/
  number/bool/null variants) is the same type every host function in this codebase already
  constructs and returns; Fissure's `FISSURE.*` host contracts use it identically, no new value
  bridge needed.

### Design decisions made (packet section 22.8's ordering: generic core first, ArcoBASIC for
### extensible behavior, explainability, conservative correctness, cross-platform later)

1. **Tri-state classification, concretely for M1**: a probe is `UNAFFECTED` only when it has an
   explicit, non-empty *declared* file-dependency set (section 5.3's "Declared" evidence source)
   that does not intersect the current disturbance's changed-file set. A probe with **no** declared
   or discovered dependency information at all is `UNKNOWN` and always runs — never silently
   downgraded to `UNAFFECTED` from mere absence of a graph edge. This is the literal safety
   invariant from section 6.1 ("UNKNOWN must never be silently converted to UNAFFECTED"), made
   concrete: absence of evidence is treated as absence of *proof*, not as proof of independence.
2. **M1's Declared evidence comes from a project-local ArcoBASIC config script**
   (`fissure.ab` at the target repository's root, loaded if present), calling
   `FISSURE.Probe.Register(id, command, tags[])` and `FISSURE.Probe.DependsOn(id, path_or_glob)`
   per probe — mirroring section 7.7's own `ProjectImpact` example shape. No real static/import
   analysis is attempted at M1 (that is Tier 1+, section 7.3, explicitly later milestone work) —
   the M1 generic adapter (`adapters/language/generic.ab`) only registers file nodes for every
   tracked file in the repository, Tier 0's own stated ceiling ("Files, configured probe
   associations, build links, runtime observations").
3. **VCS disturbance detection is git-only for M1** (`adapters/vcs/git.ab`, shelling out to `git
   diff --name-only` via a new `FISSURE.Process.Exec` host contract), but the adapter boundary is
   the actual abstraction — nothing in the native core calls git directly, satisfying section 15's
   "must not depend on Git conceptually" at the architecture level even though only one VCS adapter
   ships yet.
4. **Persistence**: SQLite (section 16), one `.fissure/fissure.db` file under the target
   repository's own directory (not this source tree), schema-versioned via a `schema_meta` table
   (`key TEXT PRIMARY KEY, value TEXT`) storing a `schema_version` row — `GraphStore::open()`
   refuses to operate against a newer schema version than it knows and rebuilds/migrates from a
   older one it recognizes, rather than silently misreading rows in an unexpected shape.
5. **Structured events, not string-parsed output** (section 14): a plain in-process `EventBus`
   (`core/events/event_bus.hpp`) — synchronous callback subscription, no threading, no queue — the
   CLI's own renderer is just one subscriber. The event types implemented at M1: exactly the
   packet's own list (`DisturbanceDetected`, `GraphUpdated`, `ImpactComputed`, `ProbeSelected`,
   `ProbeSkipped`, `ProbeStarted`, `ProbePassed`, `ProbeFailed`, `RunCompleted`); `ConfidenceChanged`
   and `CalibrationRequired` are declared in the event type enum now (so M5's later work doesn't
   need to touch the enum) but not yet emitted anywhere real.
6. **Confidence (section 6.2) is tracked but not yet load-bearing at M1**: every edge/probe carries
   a `confidence` field (0.0-1.0), populated conservatively (1.0 for declared associations, since a
   human wrote them; there is no other evidence source yet at M1 to produce a different number).
   The invariant that confidence must never override the hard AFFECTED/UNKNOWN-always-run rule
   (section 6.2's own text) is enforced structurally: classification never reads the confidence
   field, only the tri-state result.

### Files changed

Tasks 4-13 of the First-Agent Task Sequence (section 22.5) all landed this session — the whole of
M0 and M1 (sections 20/22.6), not just the skeleton. Concretely:

- **`fissure/include/fissure/`** — `types.hpp` (Node/Edge/NodeKind/EdgeKind/Evidence/Probe/
  Classification/ProbeVerdict/ProbeResult, RFC sections 4.1/5/6.1/9 verbatim), `graph.hpp` (the
  in-memory Fault Graph, `Graph::propagate` with an explicit `Direction` — see its own doc comment
  for why impact analysis needs `Incoming`, not the more obvious-looking `Outgoing`), `events.hpp`
  (the structured `EventBus`, RFC section 14's own event list), `store.hpp` (SQLite persistence,
  schema-versioned), `impact.hpp` (`ImpactEngine::classify`, RFC 6.1's tri-state rule), `vm.hpp`
  (the embedded ArcoBASIC VM wrapper), `manifest.hpp` (RFC section 8 capability manifests),
  `platform.hpp` (RFC section 18's native platform boundary, process execution).
- **`fissure/core/`** — one `.cpp` per header above, plus `core/scheduler/scheduler.cpp` (RFC
  section 12, sequential for M1 — real scope, not deferred without saying so) and
  `core/platform/posix.cpp` (the one platform implementation that exists so far — Linux is the
  only target, section 18's cross-platform boundary is real but has exactly one implementation
  behind it today).
- **`fissure/adapters/`** — `language/generic.ab` (Tier 0, task 8), `vcs/git.ab` (task 10),
  `test/command.ab` (task 9). All three carry real `#FISSURE-PLUGIN` manifests that the capability
  system actually enforces (see vm.hpp's own comment for what "actually enforces" means at M1's
  scope), not decorative comments.
- **`fissure/apps/fissure/main.cpp`** — the CLI (task 12): `fissure run [--full] [--since REF]`,
  `fissure explain <probe>`, `fissure status`.
- **`fissure/examples/tiny-project/`** — a synthetic example project (`fissure.ab` + two source
  files) exercising all three classification outcomes plus a real Fracture, used by the smoke test
  below.
- **`fissure/CMakeLists.txt`**, **`CMakeLists.txt`** (root, `add_subdirectory(fissure)`),
  **`cmake/Dependencies.cmake`** (SQLite3 detection, gated the same conservative way FUSE3 is).
- **`tests/unit/fissure_tests.cpp`**, **`tests/integration/fissure_smoke.sh`**, both registered in
  **`cmake/Testing.cmake`** — not under `fissure/tests/` as the RFC's own section 19 layout
  suggests; see `fissure/CMakeLists.txt`'s own comment for why this repo's actual established
  convention (confirmed by reading `cmake/Testing.cmake` and `arcfs-utils/CMakeLists.txt`) was
  followed instead, per section 22.8's own "record any deviation" instruction.

### Tests executed and results

- `./build/fissure_tests` (12 plain-assert unit tests): **all pass**. Covers graph traversal
  (including that `Direction::Outgoing` and `Direction::Incoming` actually behave differently, not
  just that traversal runs at all), every tri-state classification path (most importantly: a probe
  with zero evidence of any kind is `UNKNOWN`, never silently `UNAFFECTED` — the single invariant
  the whole packet is built around), manifest parsing (including that a directive after real code
  has started is correctly NOT picked up), and that an undeclared capability is genuinely denied at
  the VM boundary while a declared one genuinely crosses it (RFC section 22.7's own required test:
  "unit tests proving UNKNOWN cannot become skipped without explicit evidence" — literally present
  as `test_probe_with_no_evidence_at_all_is_unknown_never_unaffected`).
- `tests/integration/fissure_smoke.sh` (real end-to-end, the actual built `fissure` binary, a real
  `git init`-ed synthetic project): **passes**. Proves, against the real CLI, not a mock: a
  zero-disturbance baseline correctly makes every declared-dependency probe `UNAFFECTED`; changing
  one file correctly makes only the probes that declared a dependency on it `AFFECTED`, correctly
  leaves an unrelated declared probe `SKIP`ped, and correctly still runs the no-evidence probe;
  `fissure explain` gives real, matching reasoning for both an `AFFECTED` and an `UNAFFECTED`
  verdict; a genuinely failing probe (`probe.broken`) produces a nonzero `fissure run` exit code
  AND surfaces its captured diagnostic output inline (RFC 21 acceptance criterion 11); `--full`
  runs the otherwise-skipped `UNAFFECTED` probe too; `fissure status` reflects the persisted graph.
  Both registered in `cmake/Testing.cmake`, confirmed passing via `ctest --test-dir build -R
  fissure` (0.45s for both together).
- Found and fixed two real bugs *while building the smoke test itself*, both real findings, not
  busywork:
  1. The smoke test's own log-redirection (`tee some.log`) initially wrote its log file INSIDE the
     project directory being analyzed. `tee` creates that file (empty) before `fissure run`
     finishes, so Fissure's own git adapter correctly (this was Fissure working as designed, not a
     bug in it) detected it as a genuine new untracked file — a phantom disturbance on every
     "baseline, nothing changed" assertion. Fixed by moving all log output outside the project
     directory. Documented in the script itself so the next person doesn't lose time to the same
     thing.
  2. The CLI's renderer never printed a probe's captured stdout/stderr at all — only PASS/FAIL and
     timing. This silently failed RFC section 21 acceptance criterion 11 ("useful diagnostics").
     Fixed: `ProbeFailed` events now carry the captured output, and the renderer prints it (only on
     failure — matching the RFC's own terse example CLI output for passing probes, section 13.2;
     printing full output for every passing probe at scale would be pure noise, not diagnostics).

### Known issues / disclosed gaps (not silent omissions)

- **A target project should `.gitignore` its own `.fissure/` state directory.** Observed directly
  while building the smoke test: without that, `fissure run`'s own `.fissure/fissure.db` write
  becomes a real untracked file that the NEXT run's git-based disturbance detection correctly (not
  a bug) reports as changed. Harmless for correctness (it doesn't change any probe's classification
  outcome, since nothing declares a dependency on it) but a real day-two rough edge worth fixing
  properly later — either recommending `.fissure/` in a project's `.gitignore` in real
  documentation once this ships more broadly, or having git.ab itself exclude the state directory
  by convention. Not fixed this session; recorded here rather than left for someone to rediscover.
- **Capability enforcement is real but not inter-extension-isolated** (see vm.hpp's own detailed
  comment) — every loaded script's declared capabilities union into one VM-wide granted set, not a
  per-script/per-call-stack sandbox. "Deny undeclared privileged operations" (RFC section 8) is
  genuinely true for a VM where NO loaded script ever declared a given capability; it is not yet
  true in isolation between two adapters that are both loaded and where one declared it.
- **Persistence is full-rewrite, not incremental** (`GraphStore::save` deletes and rewrites every
  node/edge each run) — correct and simple, and fine at M1's scale; revisit only if a real
  project's graph size makes this measurably slow.
- **`generic.ab`'s file walk has a small hardcoded ignore list** (`.git`, `.fissure`, `build`,
  `node_modules`), not real `.gitignore`-file awareness. A correctness floor, not full polish.
- **No parallel scheduling yet** — `Scheduler::run` is sequential. RFC section 20 places real
  scheduling under M2, not M1; this is on-schedule, not a shortfall.
- **CLI command surface is intentionally partial**: `run`, `explain`, `status` exist; `impact`,
  `trace`, `graph`, `adapters`, `calibrate`, `history` (RFC section 13.1) do not, and `fissure`
  with no args says so rather than pretending. Task 12 named exactly `run` and `explain` as M1's
  own scope; `status` was added because it was small and immediately useful for the smoke test's
  own final assertion, not because it was required.
- **Only one VCS adapter (`git.ab`) and one language adapter (`generic.ab`) exist.** RFC section
  21 acceptance criterion 5 ("at least two language adapters can contribute nodes to the same
  graph") is **not yet met** — `cpp.ab`/`arcobasic.ab`/`python.ab` (section 17's own reference
  extension table) are explicitly M3 work (section 20), not attempted this session. Recorded here
  so it isn't mistaken for an oversight later.
- **`ctest.ab`** (the more specific TEST adapter section 17 lists alongside `command.ab`) does not
  exist yet — auto-discovering an existing CTest suite's own tests is real, valuable, deferred
  work, not attempted this session.
- The full pre-existing project-wide test suite (`ctest --test-dir build` with no filter) was
  **not** re-run this session — deliberately: this work is entirely new, additive files (a new
  subdirectory, two new top-level test files, and small, targeted additions to
  `cmake/Dependencies.cmake`/`cmake/Testing.cmake`/the root `CMakeLists.txt`) that don't touch any
  existing target's own sources. `fissure_tests` and `fissure_smoke` were run directly and pass;
  re-running the entire suite adds several minutes for no additional signal about this change
  specifically (confirmed with the project owner directly this session, after an earlier
  unrelated full-suite run was flagged as taking too long for what it was actually verifying).

## Session 1, continued — M2 (build awareness), same day

**Agent/operator:** "Proceed" -- direct continuation of the "Exact next action" above, no new
instruction needed.

### What landed

`fissure/adapters/build/cmake.ab` (RFC section 17's own reference BUILD extension), tested
end-to-end against a real `cmake` configure, not a hand-crafted fixture:

- Reads `CMAKE_EXPORT_COMPILE_COMMANDS` output (`<project_root>/build/compile_commands.json`) --
  **never runs `cmake` itself**, deliberately: silently reconfiguring or triggering a build the
  first time Fissure touches a large, unfamiliar project would be a surprising, disproportionate
  side effect for a regression-impact tool to take uninvited. Absent file -> graceful no-op, the
  same pattern `git.ab` already established for "not every project uses this."
  This was chosen over generator-specific introspection (`ninja -t deps`, which this repository's
  own `build/` directory can't even exercise -- it's generated with Unix Makefiles, confirmed by
  the `gmake` prefix in its own build output) specifically because `compile_commands.json` is
  generator-agnostic: the same adapter works whether a project uses Makefiles or Ninja. `ninja.ab`
  itself (section 17's other listed reference) is not built this session -- deferred, not silently
  dropped.
- No JSON library, native or otherwise -- `ParseCompileCommands` scans for known field labels
  (`"file"`, `"output"`) rather than parsing general JSON, which is robust enough given CMake's own
  output shape is always a flat array of single-level objects (RFC section 4.3's own "call a host
  contract for expensive/specialized work" doesn't apply here; there's no host contract to call,
  and adding a native JSON dependency for a format this regular would be over-engineering for M2's
  actual need).
- One `GeneratedArtifact` node per compiled object, one `consumes` edge (object -> source,
  Evidence.Build) per translation unit -- the same dependent -> dependency direction every other
  edge in this project uses (see `graph.hpp`'s own comment).
- `TargetSources(targetName)` and `RegisterTargetProbe(id, command, tags, targetNames)`: lets a
  project's own `fissure.ab` declare "this probe covers target X" by CMake TARGET NAME, resolved
  to real source files through `compile_commands.json`'s own `CMakeFiles/<target>.dir/...` object-
  path convention -- no per-file hand-listing, no C++-specific understanding baked into the native
  core (RFC section 4.1's own constraint, upheld: the native core still knows nothing about
  targets, objects, or CMake at all -- every bit of that understanding lives in this one `.ab`
  file).

New synthetic example: `fissure/examples/tiny-cmake-project/` (one static lib, one executable,
`CMAKE_EXPORT_COMPILE_COMMANDS=ON` in its own `CMakeLists.txt`) -- deliberately NOT tested against
this repository's own huge CMake project, to keep the smoke test fast and avoid any risk of an
unexpected side effect against the real build tree. New test:
`tests/integration/fissure_build_smoke.sh`, registered in `cmake/Testing.cmake`, passing (real
`cmake -S . -B build` configure, then the real `fissure` binary, proving a target-name-declared
probe correctly becomes `AFFECTED` when a source file of that target changes, with matching
`explain` output).

### A second real interpreter finding, same shape as the NOT-footgun

While writing `cmake.ab`'s first draft (a version that cached parsed `compile_commands.json` in a
top-level `CompileCommandsCacheBuilt`/`CompileCommandsByTarget` pair, guarded by a `IF
CompileCommandsCacheBuilt THEN RETURN` early-exit), the cache never actually took effect --
`fissure status` showed every edge registered 3 times over (once per call into the caching
function: one explicit `DiscoverBuildTargets()` call from the CLI, plus two more from
`RegisterTargetProbe`'s own loop over `["app", "mathlib"]`). Isolated with a 9-line repro script
before concluding it wasn't a mistake in this file specifically: **a `FUNCTION` assigning to a
top-level script variable does not persist that write back to the script-level variable, on the
tree-walking interpreter** (`arco::Runtime::run_string` -- what `arco_cli` uses, and what
Fissure's own embedded VM is built on). The read side sees the correct current value; the write
silently creates a function-local shadow instead. Confirmed this does NOT reproduce through
`ArcoFission compile-run` (the separate bytecode-compiling path) or through a CLASS's own
`SELF.field = value` (used correctly and extensively elsewhere in this repository, e.g. all of
ARCADE's work) -- specific to bare top-level variables plus plain functions on the tree-walker.
Also found, in the same debugging pass: bracket indexing with a STRING key (`obj["key"]`, both
read and write) throws `"value is not a number"` on the tree-walker, while the identical syntax
works fine through the bytecode path -- `Object.Get`/`Object.Set`/`Object.Has` work correctly on
both and are what every adapter in this project uses instead.

Not fixed at the interpreter level (out of scope for this work) -- `cmake.ab` was redesigned
around it instead (see the file's own top comment): no caching at all, a pure re-parsing lookup
function plus exactly one graph-writing function called exactly once per run from the native CLI.
Saved as its own memory note
(`[[project_arcobasic_treewalker_global_mutation_bug]]`) alongside
`[[project_arcobasic_not_bitwise_footgun]]`, since it's the same shape of finding: a real,
previously-undiscovered language-level trap, found by directly testing rather than assuming,
worth remembering before writing any future ArcoBASIC script in this repository that tries to
cache state in a top-level variable across function calls.

### Tests executed and results (M2 addition)

- `tests/integration/fissure_build_smoke.sh`: **passes**. A real `cmake` configure of the tiny
  synthetic project, then the real `fissure` binary: baseline `UNAFFECTED` (declared deps via both
  targets, empty disturbance), exactly 4 edges in the persisted graph (2 `consumes` + 2
  `declared-test-association` -- explicitly asserted to be exactly 4, not more, as a direct
  regression check against the caching bug above recurring silently), `AFFECTED` + `PASS` after
  changing `src/mathlib.cpp` (a file belonging to the "mathlib" target, not "app" -- proving
  `TargetSources("mathlib")` really resolved through the target-name declaration, not by accident),
  matching `explain` output. Registered in `cmake/Testing.cmake`, confirmed passing via `ctest
  --test-dir build -R fissure` (all three fissure tests together: 0.86s).

### Known issues / disclosed gaps (M2 addition)

- **`ninja.ab` does not exist yet** (RFC section 17's other listed BUILD reference extension) --
  `compile_commands.json` covers the CMake-generator-agnostic case `cmake.ab` targets; real
  Ninja-specific introspection (`ninja -t deps`'s own compiler-depfile-derived HEADER dependency
  edges, a genuinely higher-fidelity Build evidence source than what `compile_commands.json` alone
  provides) is real, valuable, deferred work, not attempted this session.
- **No target-to-target (link) dependency edges yet** -- `cmake.ab` registers object -> source
  edges but not e.g. "app" depending on "mathlib" as a whole (CMake's own `target_link_libraries`
  relationship). `TargetSources` happens to make this mostly moot for THIS session's own testing
  (a project simply lists every target a probe cares about, transitively, by hand, in its own
  `fissure.ab` -- see `tiny-cmake-project/fissure.ab`'s own `["app", "mathlib"]`), but a real
  project with a deep target dependency chain would want this resolved automatically rather than
  requiring every transitive target to be listed by hand. Real follow-up work, not attempted here.

### Exact next action

M0, M1, and M2's own `cmake.ab` slice are all functionally complete and tested end-to-end against
the real CLI. Real remaining M2 scope (section 20): `ninja.ab`, real parallel scheduling (RFC
section 12 -- `Scheduler::run`'s current sequential implementation is a real, intentional seam for
this, not a rewrite), and the target-link-dependency gap just above. After that, M3 (RFC section
20: "Language Reference Adapters") is next -- revisit the acceptance-criterion-5 gap (two language
adapters contributing to the same graph) there; `arcobasic.ab` (this project's own language) is the
most natural second adapter to attempt first, since the compiler's own AST-reveal tooling
(`ArcoFission reveal FILE --stage AST`, already proven elsewhere in this repository) is a real,
existing Tier 2+ analysis primitive Fissure could call into via `FISSURE.Process.Exec`, without
needing new native parsing work.
