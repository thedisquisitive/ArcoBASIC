# Fissure

**Adaptive Regression Impact Analysis and Selective Test Execution**

- **Document Type:** RFC + Agent Coding Packet
- **Initial Target:** Linux CLI
- **Architecture:** Cross-platform core + embedded ArcoBASIC VM

## 1. Executive Summary

**Fissure** is a regression-testing utility that determines which tests are meaningfully affected by a code change and runs only those tests, while preserving a conservative safety model when dependency knowledge is incomplete.

The motivating problem is simple: a full regression suite may contain hundreds or thousands of tests, and executing the entire suite after every compilation becomes increasingly expensive. Fissure replaces indiscriminate full-suite execution with a language-agnostic **Fault Graph** that models code, build artifacts, runtime relationships, and regression probes. A change becomes a **Disturbance**; Fissure propagates that disturbance through the graph, determines the **Impact Field**, and activates the relevant **Probes**.

> Core design rule: Fissure must never assume that "unknown" means "unaffected." Unknown regions are treated conservatively and cause tests to run until sufficient evidence exists to classify them safely.

Fissure is language-agnostic from day one. Programming-language support, build integrations, test discovery, hooks, policies, reporters, VCS integration, and user extensions are all implemented through an embedded ArcoBASIC runtime. The native core exposes stable host contracts; ArcoBASIC extensions compose application behavior around those contracts.

## 2. Naming and Domain Vocabulary

| Term | Meaning |
| --- | --- |
| Fissure | The utility and platform. |
| Fault Graph | The unified dependency and behavioral relationship graph. |
| Disturbance | A detected source, artifact, configuration, or dependency change. |
| Propagation | Traversal of downstream or otherwise affected graph relationships. |
| Impact Field | The graph region potentially affected by a disturbance. |
| Probe | A regression test or test case. |
| Fracture | An observed regression failure. |
| Unknown Region | A graph area where available analysis cannot safely prove impact or non-impact. |
| Calibration | A full or expanded regression used to validate and improve Fissure's model. |

## 3. Goals

- Reduce regression execution time by running only probes plausibly affected by recent changes.
- Remain safe under uncertainty; do not silently skip tests merely because analysis is incomplete.
- Support arbitrary programming languages through ArcoBASIC adapters from the beginning.
- Support polyglot repositories and cross-language dependency relationships.
- Use static, build-system, runtime-observed, and historical dependency evidence together.
- Explain why every probe was selected or omitted.
- Learn from full-suite calibration runs and unexpected failures.
- Provide a distinct Arcology CLI presentation while keeping output frontend-neutral internally.
- Start on Linux while keeping core design cross-platform.
- Permit later TUI, GUI, ARCADE, CI, and other frontends without redesigning the core.

### Non-Goals

- Replacing compilers, interpreters, or language servers.
- Requiring every language to reach deep semantic-analysis fidelity before being usable.
- Assuming Git, CMake, pytest, or any other mainstream tool is mandatory.
- Embedding language-specific logic directly into the native Fissure core.
- Guaranteeing that selective testing can replace periodic full regression runs forever.

## 4. Architectural Principles

### 4.1 Language-Agnostic Native Core

The native core must understand generic concepts only:

```
Node
Edge
Artifact
Change
Probe
Observation
Diagnostic
Capability
Confidence
ExecutionResult
```

It must not contain privileged knowledge of C++, Python, ArcoBASIC, Rust, Java, or any other language.

### 4.2 Embedded ArcoBASIC Extension Runtime

Anything a project author, third party, or future Fissure maintainer may reasonably want to modify without rebuilding the native executable should live on the ArcoBASIC side of the VM boundary.

```
Fissure Native Core
    │
    ├── Graph engine
    ├── Persistence
    ├── Scheduler
    ├── Process isolation
    ├── Filesystem primitives
    ├── Platform abstraction
    ├── Native parser/tooling bridges
    └── Embedded ArcoBASIC VM
            │
            ├── Language adapters
            ├── Build adapters
            ├── Test adapters
            ├── VCS adapters
            ├── Observers
            ├── Policies
            ├── Hooks
            ├── Reporters
            └── User/project scripts
```

### 4.3 ArcoBASIC as Extension Language, Not Reimplementation Mandate

Extensions may call native host contracts for expensive or specialized operations. For example, a C++ adapter may orchestrate Clang-based parsing through an exposed host API rather than implementing a complete C++ parser in ArcoBASIC.

### 4.4 Polyglot by Construction

Multiple language adapters may operate in the same repository. Their output merges into one Fault Graph, allowing relationships such as:

```
Python generator
    ↓
generated C++ header
    ↓
native library
    ↓
ArcoBASIC runtime binding
    ↓
integration probe
```

## 5. Fault Graph Model

### 5.1 Node Types

Minimum supported node kinds:

- file
- module
- package
- namespace
- class/type
- function/procedure/method
- symbol
- build target
- binary/library
- generated artifact
- resource/configuration
- probe
- external dependency

### 5.2 Edge Types

Minimum supported edge kinds:

- imports/includes
- calls
- inherits/implements
- links-to
- generates
- consumes
- loads-at-runtime
- reads
- writes
- executes
- observed-during-probe
- build-dependency
- declared-test-association

### 5.3 Evidence Sources

| Evidence | Description |
| --- | --- |
| Static | Imports, symbols, references, call relationships, type relationships, AST-derived structure. |
| Build | Compilation units, targets, generated outputs, linker relationships, dependency files. |
| Observed | Files, modules, processes, libraries, or symbols actually touched during probe execution. |
| Historical | Previous change-to-failure and change-to-observation correlations. |
| Declared | Explicit project policy or adapter-provided relationships. |

## 6. Impact Analysis

For each execution, Fissure determines a baseline and current state, detects disturbances, maps them to graph nodes, then propagates impact through relevant edges.

```
State A
  ↓
Change Detection
  ↓
Disturbances
  ↓
Fault Graph Mapping
  ↓
Propagation
  ↓
Impact Field
  ↓
Probe Intersection
  ↓
Execution Plan
```

### 6.1 Tri-State Probe Classification

Every probe must be classified as:

| State | Meaning | Default Action |
| --- | --- | --- |
| AFFECTED | Evidence indicates the probe may observe the disturbance. | RUN |
| UNAFFECTED | Evidence is sufficiently strong to prove the probe cannot meaningfully observe the disturbance. | SKIP |
| UNKNOWN | Analysis is insufficient to prove either state. | RUN |

> Invariant: UNKNOWN must never be silently converted to UNAFFECTED.

### 6.2 Confidence

Confidence is computed from adapter fidelity, edge provenance, observation quality, graph freshness, and calibration history. Confidence influences policy but must not override hard safety invariants.

## 7. Adapter and Extension System

### 7.1 Extension Categories

All extension types are implemented in ArcoBASIC:

```
LANGUAGE
BUILD
TEST
VCS
OBSERVER
POLICY
HOOK
REPORTER
COMMAND
```

A single extension may implement multiple contracts.

### 7.2 Language Adapter Responsibilities

A language adapter may provide any subset of:

```
identify()
discover_project()
scan_source()
extract_nodes()
extract_edges()
extract_exports()
extract_imports()
map_artifacts()
discover_tests()
analyze_change()
observe_runtime()
```

### 7.3 Adapter Fidelity Tiers

| Tier | Capabilities |
| --- | --- |
| 0 — Generic | Files, configured probe associations, build links, runtime observations. |
| 1 — Structural | Modules, packages, imports/includes. |
| 2 — Symbolic | Functions, methods, classes, types, symbol references. |
| 3 — Semantic | Calls, inheritance, generics/templates, compile-time relationships, richer dispatch models. |
| 4 — Instrumented | Actual runtime relationships between probes and executed/touched components. |

### 7.4 Generic Adapter

A built-in reference ArcoBASIC adapter must provide Tier 0 support for languages and DSLs with no dedicated adapter.

This is critical: Fissure must degrade to conservative generic analysis, not declare a language unsupported.

### 7.5 Host Contracts

The embedded VM should expose stable host namespaces similar to:

```
ECHO.Project.*        (temporary namespace name; rename to FISSURE.* in implementation)
FISSURE.Graph.*
FISSURE.Change.*
FISSURE.Probe.*
FISSURE.Exec.*
FISSURE.Filesystem.*
FISSURE.Process.*
FISSURE.VCS.*
FISSURE.Parse.*
FISSURE.Build.*
FISSURE.History.*
FISSURE.Log.*
FISSURE.Policy.*
```

The accidental legacy `ECHO.Project.*` placeholder above must not ship; all public contracts use the `FISSURE.*` namespace.

### 7.6 Example ArcoBASIC Adapter

```
SUB AnalyzeFile(Path)

    IF NOT EndsWith(Path, ".py") THEN RETURN

    LET Tree = FISSURE.Parse.Python(Path)

    FOR EACH Import IN Tree.Imports
        FISSURE.Graph.Edge Path, Import.Target, "imports"
    NEXT

    FOR EACH Fn IN Tree.Functions
        FISSURE.Graph.Node Fn.ID, "function"
    NEXT

END SUB
```

### 7.7 Project-Specific Policy

```
SUB ProjectImpact(Change)

    IF Change.Path = ":compiler:opcode-table.ab" THEN
        FISSURE.Graph.Affect ":vm:decoder"
        FISSURE.Graph.Affect ":vm:jit"
        FISSURE.Probe.RequireTag "bytecode"
    END IF

END SUB
```

## 8. Extension Permissions and Safety

Third-party and project-local ArcoBASIC extensions must declare capabilities. Fissure should deny undeclared privileged operations.

```
#FISSURE-PLUGIN 1
#NAME "Python Language Adapter"
#TYPE LANGUAGE
#REQUIRES FILE.READ
#REQUIRES PROCESS.EXEC
#REQUIRES PARSER.PYTHON
```

Candidate permissions:

`FILE.READ`, `FILE.WRITE`, `PROCESS.EXEC`, `NETWORK`, `VCS.READ`, `PARSER.*`, `BUILD.READ`, `GRAPH.WRITE`, `HISTORY.READ`

Project-local trusted extensions may have a simplified approval model, but capability declarations should still exist for auditability.

## 9. Probe Model

Tests are first-class graph entities, not merely commands.

Each probe should support:

- stable ID
- display name
- command or adapter-backed execution method
- tags
- historical duration
- historical failures
- static dependencies
- observed dependencies
- confidence metadata
- last successful run
- last full-calibration result

Example:

```
probe:
  id: compiler.parser.line-numbers
  runner: ctest
  tags:
    - parser
    - compiler
    - compatibility
```

## 10. Runtime Observation and Learning

When probes execute, Fissure should optionally observe what they actually touch. Platform-specific mechanisms may include process tracing, filesystem observation, dynamic-loader observation, coverage tooling, compiler instrumentation, or adapter-provided hooks.

Observed relationships are added to the Fault Graph with provenance and freshness metadata.

```
probe.parser.array-declarations
    → compiler/parser.cpp
    → compiler/types.cpp
    → runtime/array.cpp
    → runtime/memory.cpp
    → stdlib/collections.ab
```

### 10.1 Unexpected Fractures

If a probe classified as UNAFFECTED fails during a calibration run, Fissure must treat this as evidence of a missing or incorrect graph relationship.

```
ANOMALOUS FRACTURE

Probe:
    runtime.memory.reallocate

Predicted:
    UNAFFECTED

Observed:
    FAIL

Action:
    mark model discrepancy
    increase uncertainty in related region
    preserve evidence
    request/perform recalibration according to policy
```

## 11. Calibration and Full Regression

Selective testing does not eliminate full regression. Fissure should periodically require or recommend calibration based on:

- commit count since last full run
- graph structural drift
- adapter changes
- large dependency changes
- low confidence
- unexpected failures
- explicit policy

Suggested modes:

| Mode | Behavior |
| --- | --- |
| minimal | Run strongly proven affected + unknown probes only under strict local policy. |
| normal | Default conservative selective execution. |
| expanded | Include weaker relationships and broader uncertain neighborhoods. |
| full | Execute every probe and recalibrate graph evidence. |

## 12. Scheduling

Once probes are selected, Fissure should optimize their execution using historical duration, criticality, failure likelihood, dependency ordering, and available workers.

First implementation may use simple parallel execution; scheduling intelligence can evolve later without changing the probe model.

```
TEST PLAN

Critical path       3 probes
High impact         7 probes
Normal             21 probes
Low-confidence      6 probes

Estimated runtime: 14m 03s with 4 workers
```

## 13. CLI Design

The CLI should feel like an Arcology subsystem console, not a reskinned generic test runner. Internally, however, presentation must consume structured events so alternate frontends can render the same execution model.

### 13.1 Initial Command Surface

```
fissure run
fissure run --full
fissure run --expanded
fissure impact <path|symbol|change>
fissure trace <path|symbol|probe>
fissure graph
fissure explain <probe>
fissure adapters
fissure calibrate
fissure history <component>
fissure status
```

### 13.2 Example Output

```
 FISSURE :: REGRESSION IMPACT ENGINE

 DISTURBANCE
 ───────────────────────────────────────────

  Δ compiler:parser.cpp
  Δ compiler:token.hpp
  · docs:operators.md

 PROPAGATION
 ───────────────────────────────────────────

  03 source nodes
       │
       ├──── 17 direct dependents
       ├──── 41 transitive dependents
       └──── 14 probes activated

 IMPACT FIELD  █████████░░░░░░░░░░░  41%

 172 PROBES
  14 ACTIVE
 158 DORMANT

 CONFIDENCE  97.3%

 EXECUTION
 ───────────────────────────────────────────

 [01] parser.line_numbers            PASS   0.42s
 [02] parser.expression              PASS   1.18s
 [03] compiler.basic                 PASS   4.81s
 [04] capsule.roundtrip              PASS   7.32s
 [05] repl.parser                    RUN    ▓▓▓▓▓░░░
```

### 13.3 Dumb Terminal Fallback

```
[FISSURE] IMPACT 41%
[FISSURE] PROBES 14/172
[PASS] parser.line_numbers 0.42s
```

## 14. Structured Event Model

All frontends consume events rather than parsing human CLI output.

```
DisturbanceDetected
GraphUpdated
ImpactComputed
ProbeSelected
ProbeSkipped
ProbeStarted
ProbePassed
ProbeFailed
ConfidenceChanged
CalibrationRequired
RunCompleted
```

This permits later:

- CLI
- TUI
- ARCADE integration
- CI output
- JSON output
- graphical dashboard
- ArcoSH integration

## 15. Repository and Change Detection

Git support should be first-class through an ArcoBASIC VCS adapter, but Fissure must not depend on Git conceptually. Filesystem snapshots or other VCS implementations should be possible.

```
fissure run
fissure run --since main
fissure run --since origin/main
fissure run --change a8bf129
```

Default behavior in a Git repository may compare the working tree to an adapter-defined baseline.

## 16. Persistence

Initial storage recommendation: SQLite, hidden within a project-local Fissure state directory.

```
.arcology/
    fissure/
        graph.db
        cache/
        logs/
```

Schema should preserve provenance and versioning so graph evidence can be invalidated when adapters, compiler versions, or analysis methods change.

## 17. Initial Reference Extensions

The initial repository should prove extensibility rather than hard-code mainstream tooling.

| Category | Reference Extension |
| --- | --- |
| Language | generic.ab |
| Language | cpp.ab |
| Language | arcobasic.ab |
| Language | python.ab |
| VCS | git.ab |
| Build | cmake.ab |
| Build | ninja.ab |
| Test | command.ab |
| Test | ctest.ab |

The architecture must permit additional adapters without native-core modification.

## 18. Cross-Platform Strategy

Linux is the first implementation target. Cross-platform viability must be preserved by putting operating-system-specific primitives behind native platform contracts.

### Native platform boundary examples

- process launch and control
- filesystem watching
- runtime tracing
- dynamic-loader observation
- terminal capability detection
- path normalization
- inter-process communication

No extension should need to know whether Fissure is running on Linux, Windows, or another supported platform unless it explicitly requests platform-specific capabilities.

## 19. Suggested Repository Layout

```
fissure/
├── core/
│   ├── graph/
│   ├── scheduler/
│   ├── history/
│   ├── execution/
│   ├── platform/
│   ├── events/
│   └── vm/
├── include/
├── adapters/
│   ├── language/
│   │   ├── generic.ab
│   │   ├── cpp.ab
│   │   ├── arcobasic.ab
│   │   └── python.ab
│   ├── build/
│   │   ├── cmake.ab
│   │   └── ninja.ab
│   ├── vcs/
│   │   └── git.ab
│   └── test/
│       ├── command.ab
│       └── ctest.ab
├── scripts/
├── tests/
├── docs/
└── examples/
```

## 20. Implementation Milestones

### M0 — Skeleton

- CLI executable starts.
- Embedded ArcoBASIC VM loads an extension.
- Host contract call crosses VM/native boundary.
- Structured event bus exists.
- SQLite state opens and versions correctly.

### M1 — Generic Selective Regression

- Generic language adapter.
- Git change adapter.
- Generic command probe runner.
- File-level Fault Graph.
- AFFECTED / UNAFFECTED / UNKNOWN classification.
- Conservative selective execution.
- `fissure explain` shows reasoning.

### M2 — Build Awareness

- CMake and Ninja adapters.
- Build artifacts and target edges.
- Generated-file relationships.
- Parallel scheduling.

### M3 — Language Reference Adapters

- C/C++ adapter using native parsing bridge as appropriate.
- ArcoBASIC adapter.
- Python adapter.
- Polyglot graph merge.

### M4 — Runtime Observation

- Linux observation backend.
- Observed dependency edges.
- Evidence provenance.
- Calibration discrepancy detection.

### M5 — Historical Intelligence

- Duration prediction.
- Failure sensitivity statistics.
- Graph-drift metrics.
- Automatic calibration policy.
- Coupling anomaly reports.

### M6 — Frontend Expansion

- TUI.
- ARCADE integration.
- GUI graph visualization.
- CI-native renderers.

## 21. Acceptance Criteria for Initial Usable Release

1. Fissure builds and runs on Linux.
2. All extension logic is loaded through the embedded ArcoBASIC VM.
3. The native core contains no C++-specific or Python-specific test-selection assumptions.
4. A repository with an unknown/custom language can operate through the generic adapter.
5. At least two language adapters can contribute nodes to the same graph.
6. A changed file causes only affected or unknown probes to execute.
7. Every skipped probe is explainable with recorded evidence.
8. Users can force full regression at any time.
9. A full calibration run can update graph evidence.
10. Structured events drive CLI output.
11. A failed probe produces a Fracture result with useful diagnostics and retained logs.

## 22. Agent Coding Packet

### 22.1 Mission

Implement Fissure as a Linux-first, cross-platform-ready regression impact analysis engine. The system must use an embedded ArcoBASIC VM for adapters, plugins, policies, hooks, configuration logic, and execution scripts.

### 22.2 Non-Negotiable Constraints

- Do not bake language support into the native core.
- Do not treat C++ as a privileged first-class implementation path.
- Do not treat UNKNOWN as UNAFFECTED.
- Do not make Git, CMake, or any specific test framework mandatory.
- Do not make extension authors rebuild the native core.
- Do not parse human-readable CLI output internally; use structured events.
- Do not use YAML/JSON as the primary programmable configuration language when ArcoBASIC can express the policy directly.
- Do not sacrifice cross-platform boundaries merely because Linux is the first target.

### 22.3 Coding Priorities

1. Correctness and test-selection safety.
2. Stable extension contracts.
3. Graph provenance and explainability.
4. Performance.
5. Presentation polish.

### 22.4 Required Progress File

> The coding agent must maintain a project progress file continuously so work can move between multiple AI coding agents without losing architectural context.

Create:

```
AGENT_PROGRESS.md
```

Every meaningful change must append or update:

- current milestone
- completed work
- files changed
- design decisions
- known issues
- next recommended task
- tests executed and results
- temporary compromises or TODOs

### 22.5 First-Agent Task Sequence

1. Inspect the existing repository before creating structure.
2. Locate the embeddable ArcoBASIC VM API and determine the cleanest embedding boundary.
3. Create or update `AGENT_PROGRESS.md`.
4. Establish the native core skeleton and structured event bus.
5. Embed the ArcoBASIC VM and successfully load a minimal Fissure extension.
6. Expose the first host contracts: logging, graph node creation, graph edge creation, and project filesystem read.
7. Implement SQLite-backed graph persistence with schema versioning.
8. Create `generic.ab` and prove that an unsupported language can register source nodes.
9. Create a generic command probe adapter in ArcoBASIC.
10. Implement a basic filesystem or Git-based disturbance source through an adapter.
11. Implement conservative file-level impact propagation.
12. Implement `fissure run` and `fissure explain`.
13. Add unit and integration tests before adding deeper language intelligence.

### 22.6 Definition of Done for M1

```
Given:
  - a repository
  - a set of registered probes
  - a previous baseline
  - one or more changed files

Fissure must:
  1. identify the disturbance,
  2. build/update the Fault Graph,
  3. classify every probe,
  4. run AFFECTED probes,
  5. run UNKNOWN probes,
  6. skip only probes proven UNAFFECTED,
  7. explain every decision,
  8. persist results and graph evidence.
```

### 22.7 Testing Requirements

- Unit tests for graph traversal.
- Unit tests for classification invariants.
- Unit tests proving UNKNOWN cannot become skipped without explicit evidence.
- Integration tests for embedded ArcoBASIC adapters.
- Integration test with a synthetic polyglot project.
- Integration test proving a generic unknown language still functions.
- Calibration test in which a supposedly unaffected probe fails and causes graph uncertainty to increase.
- CLI snapshot/golden tests only after structured-event behavior is tested independently.

### 22.8 Agent Decision Policy

If implementation details are ambiguous, prefer the design that:

1. keeps the native core generic,
2. moves extensible behavior into ArcoBASIC,
3. preserves explainability,
4. preserves conservative correctness,
5. does not block future cross-platform operation.

Do not redesign major architecture silently. Record any proposed architectural deviation in `AGENT_PROGRESS.md` before implementing it.

## 23. Future Opportunities

The Fault Graph is reusable beyond regression testing. Once sufficiently mature, Fissure could answer:

- What needs recompiling?
- What documentation may now be stale?
- Which packages need rebuilding?
- Which downstream API consumers may break?
- Which components should increment versions?
- Which deployment targets are affected?
- Which architectural components show suspicious hidden coupling?

This allows Fissure to evolve from a selective regression tool into a broader **change-propagation intelligence layer** for the Arcology ecosystem without compromising the immediate regression-testing mission.

## 24. Initial Product Thesis

> Fissure must determine which regression probes could observe a behavioral difference between two project states, execute those probes safely, and explain precisely why each probe was run or skipped.

Everything else is subordinate to that requirement.

---

*Fissure RFC + Agent Coding Packet — Arcology Project*
