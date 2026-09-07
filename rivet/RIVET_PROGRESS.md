# Rivet — Agent Progress

This file is required to be maintained continuously (not just at the end) so work can move between
agents without losing context. Follows the same convention `fissure/AGENT_PROGRESS.md` established
in this repo — one dated session entry, appended to or corrected in place, never rewritten.

## Session 1 — 2026-09-07

**Scope:** M1 + just enough of M3 to prove real incremental caching, per the RFC's own section 61
three-step acceptance bar (`rivet build` twice, then `rivet why rebuild <file>`). Followed a full
plan-mode design pass (Explore agent verifying real ArcoBASIC language capabilities, Plan agent
designing the concrete architecture) before writing any code — see the approved plan at
`/home/daedalus/.claude/plans/cozy-weaving-crown.md` for the full design rationale; this file
tracks implementation progress against it.

### Task 1 — Inspect the existing repository before creating structure

Done (via the plan-mode Explore agent, direct source verification, not guesswork). Key findings
that shaped the design:

- `include/arco/runtime.hpp`'s `arco::Runtime` is the proven embedding boundary (same one
  `arco_cli`/`ArcoFission`/`fissure` all use). Repeated `run_string()` calls on ONE instance share
  global/class state across calls — already proven by `fissure/core/vm/vm.cpp`'s own working
  `VM::load_script`, not a fresh assumption.
- `cmake/Dependencies.cmake` already has `ARCO_SQLITE3_FOUND`/`PkgConfig::SQLITE3` wired (used by
  Fissure); Rivet reuses it as-is, gated the same conservative way.
- `TARGET`/`PROJECT`/`PROFILE`/`BUILD`/`COMPILER`/`GENERATE` are NOT reserved keywords (grepped
  `src/frontend/lexer.cpp` directly) — safe as plain global variable names.
- No hashing, no file-stat, no `:=` named-args, no `DIM AS ARRAY OF` exist in real ArcoBASIC. All
  translated to real syntax in `rivet/stdlib/rivet.abas`.
- **Bare `AND`/`OR`, not just `NOT`, are bitwise and non-short-circuiting** (`lexer.cpp:75/77/81`)
  — confirmed independently this session, a wider instance of the already-known `NOT`-is-bitwise
  footgun. Every condition in Rivet's own shipped ArcoBASIC code uses `!`/`&&`/`ANDALSO`/`||`/
  `ORELSE`, never bare `NOT`/`AND`/`OR`.
- Two known tree-walking-interpreter bugs (top-level-variable-write-doesn't-persist-across-
  function-calls; `obj["stringkey"]` bracket-indexing throws) are designed around from the start:
  all Rivet stdlib mutable state lives on `SELF` inside real classes, never a module-level `.abas`
  variable mutated from a plain `FUNCTION`; `Object.Get/Set/Has` used exclusively, never bracket
  string-indexing.

### Design decisions made (with rationale)

1. **Two-phase build lifecycle**: ArcoBASIC only *declares* actions (`RIVET.Graph.Action`); the
   native CLI schedules/executes once, after `run_string()` returns. Matches RFC section 52's own
   numbered lifecycle (graph construction is a distinct step from scheduling/running); lets
   multi-target scripts share one graph/one scheduler pass.
2. **Fingerprint = 128-bit "wide FNV-1a"** (two 64-bit FNV-1a passes, different offset/prime
   pairs, concatenated), not a crypto hash. No crypto library is linked anywhere in this repo;
   this isn't a security boundary, it's a content-addressed cache key. Zero new dependencies.
3. **`rivet why` is 100% native/DB-driven, no VM re-execution.** Explains the LAST real build's
   causality off persisted SQLite state; faster and semantically cleaner than re-deriving from a
   freshly-run `build.abas`.
4. **`rivet/adapters/toolchain/` not `rivet/adapters/build/`** — avoids the exact `.gitignore`
   bare-`build/` collision `fissure/adapters/build/` already needed a workaround for.
5. **Cache is native-only, no `RIVET.Cache.*` ArcoBASIC contract** — nothing in `build.abas` ever
   needs to query the cache directly; only the native scheduler does. Avoids speculative surface
   area with no real consumer.
6. **Rivet's own platform process-execution is a separate implementation from Fissure's**
   (`rivet::platform::run_process`, argv-based, not shared with `fissure::platform::run_process`,
   which is shell/`popen`-string-based) — avoids shell-quoting hazards on compiler argument lists
   and lets a thread pool run genuinely N-wide without serializing through one shell.
7. **No manifest/capability system this slice** (unlike Fissure's `#FISSURE-PLUGIN`/`#REQUIRES`) —
   `stdlib/rivet.abas` and `adapters/toolchain/cxx.ab` are Rivet's own first-party trusted code,
   not third-party plugins; the capability model exists to gate untrusted extensions (a real,
   later M5 concern once a plugin registry exists), not the tool's own bundled implementation.
8. **No mtime/size pre-check before hashing** — every input's content is hashed fresh on every
   `rivet build`, by design, specifically to prove real content-based caching rather than a
   shortcut that could silently paper over a staleness bug in the very slice meant to demonstrate
   correctness.

### Files changed

All landed this session, matching the approved plan's directory layout exactly:
- `rivet/include/rivet/{action,graph,fingerprint,store,cache,scheduler,platform,toolchain,vm}.hpp`
  + one `.cpp` each under `rivet/core/*/`.
- `rivet/stdlib/rivet.abas` (the rich ArcoBASIC object model), `rivet/adapters/toolchain/cxx.ab`
  (the clang/gcc argument-construction adapter).
- `rivet/apps/rivet/main.cpp` (CLI: `build`, `why rebuild`, `clean`).
- `rivet/examples/tiny-project/` (`build.abas`, `include/math.hpp`, `src/{main,math}.cpp`).
- `rivet/CMakeLists.txt`, `rivet/docs/rivet-rfc.md` (verbatim saved RFC).
- Root `CMakeLists.txt` (`add_subdirectory(rivet)`), `.gitignore` (`.rivet/` addition).
- `tests/unit/rivet_tests.cpp`, `tests/integration/rivet_smoke.sh`, `cmake/Testing.cmake`
  (registration, gated `if(TARGET rivet_core)`).

### Real bugs found and fixed while building and testing this (not just written and assumed
### correct -- each one caught by actually running the thing)

1. **`ADD` is a reserved keyword in ArcoBASIC** (`src/frontend/lexer.cpp:85`, `TokenType::Add`) --
   `FUNCTION Add(path)` inside `CLASS FileList` failed to parse ("expected method name"). A real,
   previously-undocumented reserved-word collision, distinct from the NOT/AND/OR bitwise footgun.
   Renamed to `FileList.Append` throughout.
2. **`RIVET.Filesystem.WalkFiles` returned absolute paths while `BuildTarget.Build()`'s own
   outputs (object files, link output) are always constructed relative** -- the resulting
   `BuildAction.inputs`/`outputs` disagreed on path form, so `rivet why rebuild <file>`'s own
   lookup (which normalizes its argument to project-root-relative) silently never matched anything
   recorded. Fixed by making the native walker return project-root-relative paths, matching every
   other path this VM hands back to ArcoBASIC.
3. **`rivet build` never `chdir`ed into the located project root** -- RFC section 31 explicitly
   wants "invoking rivet build from a child directory" to work, but every relative path this VM
   produces is relative to the project root, and the scheduler passes those straight to the
   compiler with no explicit `cwd`. Fixed by `fs::current_path(project_root)` right after locating
   it.
4. **A comment-only source change can produce a byte-identical `.o` file** -- confirmed directly
   (`cmp` on two `clang++` outputs of the same file with only a trailing `// comment` added showed
   IDENTICAL object files). This is real, CORRECT, more-precise-than-naive content-based caching
   correctly skipping an unnecessary relink -- not a bug in Rivet, but it broke an early version of
   the smoke test's own assumption that any source edit forces a relink. Fixed the test to make a
   real semantic change instead of a comment.
5. **`rivet why rebuild <file>`, as first written, re-derived its answer by comparing current disk
   state against the store -- but by the time it's asked (after the very build it's meant to
   explain), that comparison always shows "unchanged", because the build already updated the
   record.** This is a real design gap, not a transient bug: `why` needs to explain what the LAST
   REAL BUILD decided and why, not re-answer a question that's already been overtaken by its own
   side effects. Fixed by persisting the cache decision's own reason string at record time (new
   `last_reason` column) and having `why rebuild` read it back directly, plus a separate "has it
   changed again since then" live check as an additional note -- covering both "explain what just
   happened" and "predict what would happen next" without conflating them.
6. **The Scheduler's worker threads all share one `StateStore`/SQLite connection with no
   synchronization** -- the scheduler's own concurrency unit test reliably reproduced "cannot
   start a transaction within a transaction" the first time two worker threads called
   `CacheManager::record()` at once. A real concurrency bug the test caught as designed. Fixed by
   adding an internal mutex to `StateStore`, held for every public method, so it's safe to share
   across the Scheduler's worker threads without every caller needing to remember to lock
   externally.
7. **`rivet clean --all` left now-empty output directories (e.g. `build/`) behind** -- a real,
   if minor, incompleteness against a genuinely clean checkout. Fixed with a best-effort upward
   walk that removes a directory only while it's empty and stays inside the project root (RFC
   section 33: never delete more than what this exact clean pass just emptied).

### Known problems / disclosed gaps

- Provenance is Script+Function, not Script:Line (`arco::Runtime` has no live-line accessor for a
  host function mid-execution — a small, separate, real follow-up, not attempted here).
- Header-change detection (via `-MMD -MF` depfiles) is inert on a project's very first build —
  normal for depfile-based incremental systems (Ninja works identically), not a bug.
- No environment-variable fingerprinting yet (`BuildAction` has no `Environment` field) — no
  consumer in this slice; additive when a real adapter needs it.
- Adapter plugin registry (M5), `GENERATE.File`/`BUILD.ProcessAction` (M6), profiles/cross-
  compilation (M7), `explain`/`graph`/`inspect`/`doctor`/`--dry-run`/`--json` (M4/M8), packages,
  Fissure/ARCADE/agent integration beyond a future `--json` flag, security/sandboxing — all real,
  explicitly deferred, matching the RFC's own milestone list, not silently dropped.

### Tests executed and results

- `./build/rivet_tests` (12 plain-assert unit tests, all pass): fingerprint stability/content-
  sensitivity/order-sensitive `combine()`; graph independent/linear/fan-in/fan-out ordering, cycle
  rejection with provenance, duplicate-output rejection; cache miss→hit→miss-on-argument-change,
  output-tampering detection; scheduler real concurrency (two 0.3s dummy actions with `jobs=2`
  complete in under 0.5s, not the ~0.6s serial sum) and failure cascade (a failing action's
  dependent reports `SkippedDueToFailure` while an independent sibling still completes); store
  round-trip record/lookup and `inputs_referencing`-equivalent lookup.
- `tests/integration/rivet_smoke.sh` (the real built `rivet` binary against a real tiny C++
  project, real `clang++`/`g++`): **passes, proving the RFC's own section 57/61 acceptance test
  literally** -- cold build compiles both sources, links, and the produced binary actually runs
  and returns the right result; second build is 100% cache hits, zero compile/link actions (the
  literal "second run does nothing" bar); after a real semantic source change, only the changed
  file recompiles and the executable relinks, the unchanged file stays cached; `rivet why rebuild`
  correctly names the changed input as the cause and correctly cascades to the dependent Link
  action; `rivet clean --all` leaves the project directory byte-for-byte back to its source state.
- Both registered in `cmake/Testing.cmake`, confirmed passing via `ctest --test-dir build -R
  rivet` (0.93s for both together).
- Manual sanity pass (per the plan's own verification step 3): ran `rivet build`/`why rebuild`/
  `clean --all` by hand directly inside `rivet/examples/tiny-project/` (not just via the
  tmp-dir-copying smoke test) -- confirmed `.rivet/` output structure looks sane
  (`state/rivet.db`, `obj/tinyapp/*.o` + matching `.d` depfiles), confirmed `clean --all` leaves
  the directory clean including the now-empty `build/` (bug #7 above, found and fixed during this
  exact pass).

### Exact next action

M1 + real incremental caching (M3's own core, not the full milestone) is complete and tested end
to end against the real CLI, matching the approved plan's full scope. Real remaining work, per the
plan's own "Explicit Scope Boundaries" (all disclosed, not silently dropped): `ninja.ab` doesn't
exist (only the CMake-generator-agnostic angle isn't relevant here -- Rivet doesn't read CMake at
all, this note is inherited phrasing, disregard); adapter plugin registry (M5) -- `cxx.ab` is
hardcoded, the seam is `BuildTarget.Build()`'s two fixed function-name calls; `GENERATE.File`/
`BUILD.ProcessAction`/generated-artifact chains (M6); `BuildProfile` composition, cross-
compilation, real HOST/TARGET distinction beyond a read-only host-info object (M7); `explain`/
`graph`/`inspect`/`doctor`/`--dry-run`/`--json`/`fingerprint`/`cache status` (M4/M8); package
integration; Fissure/ARCADE/agent integration beyond a future `--json` flag; security/sandboxing;
environment-variable fingerprinting; real `Script:Line` provenance (only `Script:Function`); a
target-link-dependency model (a probe/target must currently list every transitive dependency by
hand, matching how `RegisterTargetProbe`'s own tiny-cmake-project example in Fissure needed the
same thing).
