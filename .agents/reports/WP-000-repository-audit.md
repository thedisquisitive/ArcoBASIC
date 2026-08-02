# WP-000: Repository Audit

**Mission:** Arcology OS / ArcoBASIC Systems Language Milestone (UEFI x86-64 "Hello from ArcoBASIC")
**Packet reference:** `arcology-os/Arcology_ArcoBASIC_Systems_Agent_Packet.md`
**Repository root:** `/home/daedalus/projects/arcobasic` (git top-level)
**Date:** 2026-08-02

---

## 1. Repository Shape

`/home/daedalus/projects/arcobasic` is a single git repository containing **two unrelated projects**:

1. The **ArcoBASIC language/compiler/shell** (repo root: `compiler/`, `core/`, `runtime/`, `include/arco/`, `tools/`, `shell/`, `bindings/`, `stdlib/`, `examples/`, `tests/`, `docs/`, `CMakeLists.txt`). This is the project the Agent Packet targets.
2. **Lazarus / Lazarus OS** (`lazarus/`, `lazarus-os/`), a separate recovery-bench/appliance project with its own CMakeLists, docs, and tests. It has a large number of modified/untracked files in `git status` from prior work.

The design docs and Agent Packet live in `arcology-os/` (untracked, added this session): `Arcology_OS_Design_Plan_vNext.md`, `Arcology_OS_AI_First_Roadmap.md`, `Arcology_ArcoBASIC_Systems_Agent_Packet.md`.

**Scope boundary:** Per Packet §5 ("do not perform a broad rewrite," don't touch unrelated subsystems) and §15 (stop if a task would require modifying unrelated subsystems), all `lazarus/` and `lazarus-os/` content is **out of scope** and must not be touched by this mission.

An empty `.agents/` directory already existed at repo root; this report and future work-package reports are placed under `.agents/reports/` (mapping the Packet's conceptual `agent-reports:` prefix).

---

## 2. Build System

- CMake ≥ 3.16, C++17, `LANGUAGES CXX C`.
- One static library target `arco` built from `core/lexer.cpp`, `core/parser.cpp`, `runtime/runtime.cpp`, `shell/arcosh.cpp`, `compiler/fission.cpp`, `bindings/c/arco_c_api.cpp`.
- Four executables link against `arco`: `arco_cli`, `arcosh`, `ArcoFission`, `arco_tests`.
- Optional GUI (GLFW/Pango/Cairo/GTK3/OpenGL) and networking (libcurl) backends, both auto-detected and stubbed out if unavailable — irrelevant to this mission.
- `ctest` wires 3 tests: `arco_runtime_tests` (C++ binary, custom `require()` assertions — no gtest/catch2), `arcosh_alpha_smoke` and `arcofission_alpha_smoke` (bash driver scripts that run the built binaries and grep their output for golden strings).
- An existing configured build tree is present at `build/` (in-tree, not `.gitignore`d out — already has `CMakeCache.txt`, `libarco.a`, etc.).

**Baseline build command:**
```sh
cmake --build build -j"$(nproc)"
```
Result: clean, all 5 targets already up to date, no warnings/errors surfaced.

**Baseline test command:**
```sh
ctest --test-dir build --output-on-failure
```
Result: **3/3 tests passed** (`arco_runtime_tests` 1.76s, `arcosh_alpha_smoke` 1.12s, `arcofission_alpha_smoke` 1.83s). No pre-existing failures to distinguish from future regressions — the baseline is fully green.

---

## 3. Compiler Phase Map

| Phase | File(s) | Notes |
|---|---|---|
| Lexer | `core/lexer.hpp/.cpp` (39 + 405 lines), `core/token.hpp` (115 lines) | Hand-written. `TokenType` enum has no hardware/systems tokens. `#` is only recognized at column 1 as a `#!` shebang; any other `#...` line reaching the lexer throws `"unexpected character"`. |
| Preprocessor / directives | `Runtime::preprocess_source` in `runtime/runtime.cpp` (~line 2892) | **Shared** by both the interpreter and the ArcoFission pipeline — every ArcoFission entry point (`reveal_ast`, `reveal_amir`, `reveal_bytecode`, `compile_run`, `build_native_file`) constructs a `Runtime` and calls `runtime.preprocess_source(source)` before lexing. This is the correct hook point for new directives. |
| Parser / AST | `core/parser.hpp/.cpp` (98 + 2314 lines) | Recursive-descent, produces `std::vector<std::unique_ptr<Stmt>>`. |
| A-MIR | `compiler/fission.cpp` — `AmirInstruction/AmirBlock/AmirFunction/AmirModule`, `AmirBuilder` (lines ~189–2089) | Structured, textually rendered (`render_amir`) IR already exists (`reveal FILE at A-MIR`). Covers expressions, calls, arrays/objects, indexing, control flow, functions, classes, try/catch. **Correction from the original WP-000 pass (discovered during WP-002): A-MIR is NOT lowered from the `core/parser.cpp` AST.** `AmirBuilder::lower_*` functions (e.g. `lower_assignment`, `lower_expression`) re-scan the raw `Token` stream directly with their own from-scratch statement/expression pattern matcher, entirely independent of `Parser`'s `Stmt`/`Expr` classes. `reveal_amir`/`reveal_bytecode`/`compile_run`/`build_native_file` do call `Parser::parse()` first, but only to validate the source (the parsed AST is discarded — see `(void)parser.parse();` in `compiler/fission.cpp`); the actual A-MIR/bytecode structure comes from `build_amir(tokens, ...)` re-deriving it from tokens. Practical consequence: **any new grammar accepted by `core/parser.cpp` must be separately taught to `AmirBuilder`'s token matcher, or it silently falls back to an `UNSUPPORTED` instruction** rather than a compile error — this bit WP-002 (see `.agents/reports/WP-002-fixed-width-types.md`) and will bite WP-004/WP-005/WP-008 too if not kept in mind. |
| Bytecode | `compiler/fission.cpp` — `BytecodeInstruction/Block/Function/Module` (lines ~2150–2446), `render_bytecode`, `.arcof-text` format | A second, lower IR lowered from A-MIR; this is what `bytecode FILE -o OUT.arcof` emits and what the VM executes. |
| VM / interpreter | `compiler/fission.cpp` (`run_bytecode`/`run_bytecode_file`, frame execution ~2685–3002), `runtime/runtime.cpp`, `include/arco/value.hpp` | Tree-walking bytecode interpreter over `arco::Value`. |
| "Native" build | `compiler/fission.cpp::build_native_bytecode` (lines 3109–3202), driven by `build_native_file` | See §4 — **this is not a machine-code backend.** |
| Public API | `include/arco/fission.hpp` (25 lines) | `reveal_amir[_file]`, `reveal_bytecode[_file]`, `run_bytecode[_file]`, `compile_run[_file]`, `build_native_file`, `reveal_ast[_file]`. |
| CLI | `tools/arcofission_main.cpp` (166 lines) | `reveal FILE at {AST\|A-MIR\|BYTECODE}`, `build FILE -o OUT`, `bytecode FILE -o OUT.arcof`, `native FILE -o OUT`, `run FILE.arcof`, `compile-run FILE`. **No `--target` flag exists.** |
| Value/type system | `include/arco/value.hpp` (177 lines) | See §4. |

Out-of-scope-but-present: `tools/arco_cli.cpp`, `tools/arcosh_main.cpp`, `shell/arcosh.cpp` (ArcoSH), `bindings/c` + `bindings/cpp` (embedding API), `gui/*` (desktop GUI backend).

---

## 4. Critical Findings vs. Packet Assumptions

These are the gaps most likely to matter for WP-001 onward. None of them block WP-000 itself (repo is available, builds, has an identifiable compiler and a green test baseline), but they are exactly the kind of thing the Packet's stop-condition language (§15, and per-package "Stop conditions") is designed to surface before implementation begins.

### 4.1 No fixed-width integer type system (blocks WP-002 as currently scoped)

`arco::Value` (`include/arco/value.hpp`) is:
```cpp
using Storage = std::variant<std::monostate, bool, double, std::string, ArrayPtr, ObjectPtr>;
```
Every number in the language — interpreter, A-MIR, and bytecode VM alike — is a `double`. There is no `U8/U16/U32/U64/I8/I16/I32/I64` distinction, no fixed size/alignment, no signedness, and no overflow diagnostics. Packet §8 and WP-002 require all of this with tests for valid/invalid literal ranges (e.g. `DIM a AS U8 = 256` must be rejected). This is a real, additive type-system feature that does not exist today in any form — it is not a matter of extending an existing narrow-int system, it is new construction.

### 4.2 "Native" build is a bytecode-VM-in-an-ELF-wrapper, not a machine-code backend (blocks WP-005/WP-008/WP-009 as currently scoped)

`build_native_bytecode` (fission.cpp:3109) does the following, Linux-only:
1. Requires `libarco.a` to sit next to the `ArcoFission` binary (i.e. requires a CMake build tree).
2. Emits a tiny **C++** launcher source that embeds the compiled bytecode as a string literal and calls `arco::fission::run_bytecode(...)` — the interpreter — at runtime.
3. Shells out to the **host C++ compiler** (`$CXX` or CMake's cached `CMAKE_CXX_COMPILER`, else `c++`) to compile and link that launcher against `libarco.a` and its transitive link deps (pulled from `CMakeFiles/ArcoFission.dir/link.txt`).
4. Verifies the result is an ELF64 file and chmods it executable.

There is **no instruction encoder, no register allocator, no calling-convention modeling, and no PE/COFF writer anywhere in the codebase.** "Native" today means "wrap the VM interpreter in a real ELF binary," not "compile to machine code." This matches Packet §3.3's caution about bypassing A-MIR — today's default `build`/`native` path bypasses A-MIR *and* the bytecode-to-machine-code step entirely, going straight from embedded bytecode text to invoking a host C++ toolchain.

This is not a conflict with existing behavior that needs a redesign — WP-008 (x86-64 codegen) and WP-009 (PE32+ image) are explicitly scoped in the Packet as net-new work, consistent with this audit. It should be treated as confirmation that these packages require building an **entirely new backend stage** from A-MIR (or a subset of it) to real x86-64 instructions and a PE32+ writer, added as an alternative to — not a replacement of — the existing bytecode/VM path, per §3.3's "isolated behind a backend interface" guidance.

### 4.3 `#TARGET` directive name collision (relevant to WP-001, WP-003)

The shared preprocessor (`runtime.cpp:preprocess_source`) already recognizes a `#TARGET` directive:
```cpp
} else if (directive == "TARGET") {
    metadata_.targets = split_words(args);
```
This populates `CompileMetadata.targets` (`include/arco/runtime.hpp:31`), a `std::vector<std::string>` of package-metadata target-platform names — evidently intended for something like declaring supported OS targets. It is **not used by any example, stdlib module, or test in the repository today** (zero hits for `#TARGET` outside the implementation), so real-world impact of touching it is low, but the *name* is already an accepted part of the language surface.

The Packet's illustrative systems syntax (§7, §3.3) also uses `#TARGET`:
```basic
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT "efi_main"
```
with different semantics (single CPU architecture selection for codegen, not a list of supported platforms). `#PROFILE`, `#RUNTIME`, `#CALLCONV`, and `#EXPORT` do not exist at all today — those are clean additions. `#TARGET` needs a decision in WP-001 (Systems Target RFC): reuse/extend the existing directive's semantics for the systems profile, or pick a non-colliding name. Packet §7 explicitly forbids "multiple competing spellings for the same concept," which cuts toward extending the existing directive rather than introducing a second, differently-scoped `#TARGET`.

### 4.4 No `--target` CLI flag

Packet §10's preferred developer workflow (`arcofission build hello.abas --target uefi-x86_64`) has no existing counterpart. `tools/arcofission_main.cpp`'s `build FILE -o OUT` today chooses between "write `.arcof` bytecode text" and "build ELF64 VM wrapper" purely by sniffing the `-o` file extension. Adding a `--target` flag is additive and should not break the existing two paths (Assumption Policy §14: "a breaking change to existing programs" must be flagged, but adding a new flag with a new target value is not one).

---

## 5. Test Framework Summary

- `arco_tests` (`tests/runtime_tests.cpp`): a single C++ executable using a hand-rolled `require(bool, message)` helper (not a test framework like GoogleTest/Catch2) that exercises the `Runtime`/`Shell` C++ API and the C API directly, exits nonzero on first failure.
- `tests/arcosh_alpha_smoke.sh`, `tests/arcofission_alpha_smoke.sh`: bash scripts invoked via `add_test(... COMMAND bash ...)`, each running the built binary against inline heredoc `.abas` source and grepping stdout for expected golden substrings (e.g. `grep -q "A-MIR GENERATED" ...`). This is the established pattern for CLI-level golden testing and is the natural template for the Packet's required A-MIR/backend/PE-COFF/QEMU golden tests (WP-004, WP-005, WP-009, WP-010).
- No existing QEMU/OVMF integration, no PE/COFF inspection tooling, no disassembler integration.

---

## 6. Conceptual → Actual Path Mapping (Packet §6)

| Packet conceptual path | Actual repository path |
|---|---|
| `agent-reports:*` | `.agents/reports/*` (new; `.agents/` existed empty) |
| `docs:systems:*.md` | `docs/systems/*.md` (new subdirectory; `docs/` is currently flat) |
| `tests:systems:uefi-hello:{hello.abas,expected-output.txt}` | `tests/systems/uefi-hello/{hello.abas,expected-output.txt}` (new; existing `tests/` is flat, driver scripts live directly in `tests/`, so the *driver* for this should likely be a flat `tests/systems_uefi_hello_smoke.sh` wired into `CMakeLists.txt` the same way the two existing smoke tests are, with the subdirectory holding only fixture data) |
| `examples:systems:uefi_hello.abas` | `examples/uefi_hello.abas` (existing `examples/` is flat with no subdirectories; recommend keeping it flat rather than introducing `examples/systems/`) |

Final placement (especially the `tests/` layout) should be confirmed in WP-001 rather than assumed here.

---

## 7. Acceptance (Packet WP-000 criteria)

- [x] Repository map exists (§3 above).
- [x] Baseline build command documented (§2).
- [x] Baseline test command documented (§2).
- [x] Existing failures distinguished from new failures — **there are no existing failures; baseline is 3/3 green.**

## 7a. Additional Finding (discovered during WP-004)

The bytecode VM's user-defined-function call mechanism (`compiler/fission.cpp`, around the frame
setup in `execute_bytecode`) binds argument `i` to whichever declared local's name equals
`function.params[i]` verbatim. `function.params[i]` is the **whole joined parameter text**
(e.g. `"name AS String"`), not just the bare parameter name, so this match never succeeds for any
parameter that has an `AS Type` annotation — the argument silently fails to bind, and the function
body then fails with `undefined bytecode local: <name>` the moment it references the parameter.
This is a pre-existing bug (confirmed unrelated to WP-002/WP-003/WP-004's changes: it reproduces on
a plain `FUNCTION Greet(name AS String)` with no fixed-width types, no `#RUNTIME NONE`, and no
external calls involved). It does not block WP-004's acceptance (A-MIR generation for the
hello-world source is valid and deterministic regardless), but it does mean `compile-run`/`run`
cannot currently execute *any* function with a typed parameter, including the eventual
`Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable)` entry point — moot for the
mission's actual deliverable since that will run through WP-008's native codegen rather than this
bytecode VM, but worth knowing before assuming `compile-run` is a reliable way to exercise typed
systems functions end to end.

## 8. Stop Conditions Check

None of WP-000's own stop conditions are active: the repository is available, it builds without undocumented prerequisites, and the compiler source is clearly identifiable. **WP-000 is COMPLETE.**

The findings in §4 (no fixed-width types, no real machine-code backend, `#TARGET` name collision) are flagged here because they bear directly on WP-001's stop condition ("stop if required behavior conflicts with existing accepted ArcoBASIC syntax or compiler architecture") and are exactly the kind of ambiguity the Packet asks to be surfaced rather than silently resolved. §4.1 and §4.2 are consistent with the Packet's own scoping (it expects these to be built), so they are context, not blockers. §4.3 is the one item that most looks like it needs an explicit human/RFC decision before WP-001's document can be considered final.

---

## Completion Report

```text
STATUS
Complete

OBJECTIVE
Understand the existing ArcoBASIC codebase and establish a passing baseline (Packet WP-000).

SUMMARY
Audited the ArcoBASIC compiler repository at /home/daedalus/projects/arcobasic. Mapped all
compiler phases (lexer, shared preprocessor, parser/AST, A-MIR, bytecode, VM, "native" build,
public API, CLI). Rebuilt the project and ran the full ctest suite: 3/3 passing, no pre-existing
failures. Identified three architecture facts material to later work packages: (1) the numeric
type system is double-only with no fixed-width integer types; (2) "native" builds today wrap the
bytecode VM interpreter in a host-compiled ELF64 launcher rather than generating machine code —
there is no x86-64 encoder, calling-convention model, or PE/COFF writer anywhere in the codebase;
(3) a `#TARGET` directive already exists with different semantics (package-metadata target-platform
list) than the Packet's illustrative `#TARGET X86_64` (architecture selection), though it is unused
by any shipped example/stdlib/test today.

FILES CHANGED
.agents/reports/WP-000-repository-audit.md (created)

PUBLIC BEHAVIOR
None. No compiler, runtime, or language behavior was modified.

TESTS RUN
cmake --build build -j$(nproc)  -> all 5 targets up to date, no errors
ctest --test-dir build --output-on-failure -> 3/3 passed
  arco_runtime_tests: Passed (1.76s)
  arcosh_alpha_smoke: Passed (1.12s)
  arcofission_alpha_smoke: Passed (1.83s)

ACCEPTANCE CRITERIA
Repository map exists: PASS
Baseline build command documented: PASS
Baseline test command documented: PASS
Existing failures distinguished from new failures: PASS (no existing failures)

ASSUMPTIONS
- Placed agent reports under .agents/reports/ (the existing empty .agents/ directory) rather than
  a new top-level agent-reports/ directory, since .agents/ already existed as the apparent
  convention slot for this purpose.
- Proposed docs/systems/, tests/systems/<case>/, and a flat examples/ layout as the likely final
  artifact locations, pending confirmation in WP-001.

DEVIATIONS
None from WP-000 scope. Findings that may affect WP-001's architecture decisions are documented
in section 4 of the full report rather than acted upon.

REGRESSIONS
None.

RISKS
- lazarus/ and lazarus-os/ share this git repository and must not be touched by this mission.
- The `#TARGET` directive name collision (section 4.3) should be resolved explicitly in WP-001
  rather than assumed, per the Packet's rule against competing spellings for the same concept.
- Sections 4.1 and 4.2 mean WP-002, WP-004, WP-005, WP-008, and WP-009 are net-new subsystem
  construction, not extension of existing partial implementations. This matches the Packet's own
  scoping but is called out so the scale of remaining work is not underestimated.

NEXT SAFE WORK PACKAGE
WP-001: Systems Target RFC (docs/systems/uefi-target.md). No architectural decision has been made
that would block starting it; the #TARGET naming question in section 4.3 is precisely the kind of
open question WP-001 exists to resolve.
```
