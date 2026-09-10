# Fission Substrate Progress

This ledger is required by
`docs/RFC-AP-FISSION-001_Fission_Compiler_Substrate.md`. Every agent working on
the Fission Compiler Substrate must read this file before code changes and update
it before completion.

## Current Milestone

WP-002 - Language Construction API, with ArcoBASIC as the primary first-party
frontend.

## Current Work Package

WP-002 is active on top of the initial WP-001 core, with WP-003 SIR scaffolding
started where needed by ArcoBASIC. The current slice adds the public ArcoBASIC
language-definition surface, a first SIR model/builder/validator, and an initial
`language.arcobasic` registration that preprocesses/lexes/parses a growing
ArcoBASIC subset and lowers it into structured SIR.

## Completed Components

- Legacy ArcoFission capability reference documented.
- Current ArcoCapsule executable performance opportunities documented.
- RFC-AP-FISSION-001 recorded as the implementation authority.
- Initial Fission substrate progress ledger created.
- New `fission/` subproject created.
- WP-001 skeletons implemented in ArcoBASIC:
  - Component
  - Registry
  - Artifact
  - Pipeline
  - Resolver
  - CompileRequest
  - CompileResult
  - Diagnostic
  - Capabilities
- Initial generic pipeline route resolution works for `Source.brainfuck -> SIR
  -> AMIR -> Executable.Linux.X86_64`.
- Resolver now carries accumulated capabilities through routes, skips components
  whose requirements are unmet, and reports ambiguity when multiple shortest
  routes are available.
- Components can now attach ArcoBASIC transform callbacks with
  `TransformWith(ADDRESSOF Fn)`.
- `FissionCompiler.Compile()` now transports typed `FissionArtifact` values
  through the resolved pipeline and validates each produced artifact type.
- Fission substrate core smoke fixture added.
- Initial WP-002 language construction API added in ArcoBASIC:
  - `FissionLanguageDefinition`
  - file extensions
  - symbols
  - paired symbols
  - statements
  - expressions
  - operations
  - semantic callbacks
  - component export through `AsComponent()`
- Initial language authoring kit added in ArcoBASIC:
  - `Fission.LanguageKit()`
  - `BeginLanguage`
  - `SourceLanguage`
  - `BeginnerCompiledLanguage`
  - `SymbolLanguage`
  - language validation for name, extension, input, output, and missing
    semantic handlers
  - registration through the normal Fission host/component registry
- `FissionHost.Language(name)` now returns a language definition object, and
  `Fission.Register(Language)` registers its underlying component.
- Initial `language.arcobasic` package registration added under
  `fission/language/arcobasic/`.
- ArcoBASIC frontend smoke proves `Source.arcobasic -> SIR -> AMIR ->
  Executable.Linux.X86_64` through normal substrate route resolution.
- Initial WP-003 SIR model added:
  - `FissionSirNode`
  - `FissionSirModule`
  - stable text rendering
  - builder APIs for literals, reads, assignment, print, calls, returns,
    binary operations, loops, conditionals, and function declarations
  - validator for module names and known node kinds
- Initial ArcoBASIC lexer added under `fission/language/arcobasic/lexer.abas`.
  It recognizes identifiers, keywords, numbers, comments, newlines, EOF, and
  basic one/two-character symbols.
- Initial ArcoBASIC preprocessor added under
  `fission/language/arcobasic/preprocess.abas`.
  It supports `#DEFINE`, `#UNDEF`, `#IF`, `#IFDEF`, `#IFNDEF`, `#ELSE`,
  `#ELSEIF`, `#ENDIF`, active `#ERROR`, inactive-source filtering, and
  line-preserving output.
- The preprocessor now supports `#INCLUDE` expansion and captures compile
  metadata for `#VERSION`, `#AUTHOR`, `#DESCRIPTION`, `#ENTRY`, `#TARGET`,
  `#REQUIRE`, `#FEATURE`, `#STRICT`, `#EXPERIMENTAL`, `#DEPRECATED`,
  `#WARNING`, `#TODO`, `#NOTE`, `#PACK`, `#ALIGN`, `#ENDIAN`, and `#IMPORT`.
- ArcoBASIC Source-to-SIR now emits a structured `FissionSirModule` for the
  initial `PRINT value` slice and records token count/SIR render metadata.
- Initial ArcoBASIC parser and AST model added:
  - string and interpolated-string literals with legacy-compatible basic escapes
  - `#IMPORT` directives with optional aliases
  - generic directive capture
  - constants
  - `LET` declarations with optional type and initializer
  - typed declarations
  - assignment statements
  - compound assignments
  - expression/call statements
  - `PRINT`
  - bare and value `RETURN`
  - function declarations with positional arguments
  - typed/default function arguments and return annotations
  - qualified function names and qualified type names
  - class declarations
  - constructors
  - interfaces
  - access/abstract/shared modifiers
  - class `EXTENDS` and `IMPLEMENTS` metadata
  - call expressions with positional and named arguments
  - method-style/postfix call expressions
  - arithmetic/comparison/bitwise/shift/membership binary expressions
  - `AND`, `ANDALSO`, `OR`, `ORELSE`, `%`, `MOD`, `BITAND`, `BITOR`,
    `BITXOR`, `SHL`, `SHR`, `SAR`, `IN`, `HAS`, `CONTAINS`, single-token
    `=` equality, and `\` integer division
  - unary `-`, `NOT`, `ADDRESSOF`, and `COPY`
  - boolean/null/integer/decimal number literals
  - parenthesized expressions
  - simple `IF`/`ELSE`/`END IF`
  - single-line nested `IF ... THEN ... ELSE IF ...`, with newline termination
    so following statements are not absorbed into the `Then` body
  - simple `WHILE`/`WEND` and `END WHILE`
  - `DO WHILE`/`DO UNTIL` and post-condition `LOOP WHILE`/`LOOP UNTIL`
  - simple numeric `FOR` and collection `FOR IN`
  - loop `EXIT`/`CONTINUE`
  - `TRY`/`CATCH`/`END TRY`
  - `THROW`
  - member reads and index reads
  - array literals, array comprehensions, and object literals
  - contextual built-in/keyword-like identifiers used by real project code,
    including `copy` variables and `STRING(...)` calls
  - token-kind-aware punctuation and keyword operator recognition so string
    literals such as `"addressof"`, `"or"`, and `"mod"` are not parsed as
    syntax
  - logical bang unary expressions (`!value`)
  - `STOP` statements as structural SIR
  - multi-line call, argument, array, and object literal lists
  - every current first-party Fission `.abas` source as an input fixture
- AST-to-SIR lowering added for the current ArcoBASIC parser subset.
- Initial ArcoBASIC semantic report pass added. It walks SIR, records imports,
  variables, constants, functions, parameters, classes, interfaces, constructor
  scopes, inferred assignment-created locals, return types, and parameter
  defaults, and reports duplicate symbols through normal diagnostics.
  It also resolves reads and direct calls against enclosing scopes, records
  resolved/external/unresolved references, and enforces unresolved-reference
  diagnostics when `#STRICT` is active.
- The semantic pass now records class-body declarations as fields, infers class
  fields from `SELF.Field = ...`, resolves `SELF.Field` references against the
  enclosing class scope, and resolves `SELF.Method(...)` calls against class
  methods.
- Loop variables from `FOR item IN values` and `FOR index = start TO end` are
  now recorded as semantic variables in the active scope before loop body
  resolution.
- Initial import-aware ArcoBASIC program semantic analysis added. It analyzes
  multiple source files as a program set, records import edges, validates that
  imported paths are either in the program set or present on disk, and reports
  missing imports through normal diagnostics.
- Import-aware program semantic analysis now builds a canonical program symbol
  index across modules. Program symbols retain module name, scope, kind, type,
  default value, and a stable canonical name for future cross-file reference and
  signature resolution.
- Import-aware program semantic analysis now records canonical program
  references and resolves imported module-level calls/reads through the program
  symbol index. Strict whole-program diagnostics are enforced after this
  cross-file resolution step so lowercase imported ArcoBASIC helpers can be
  proven before A-MIR lowering.
- ArcoBASIC function semantic symbols now carry normalized signature metadata,
  including parameter name/type/default descriptors and return type. Program
  symbols preserve those signatures for future typed SIR and A-MIR lowering.
- ArcoBASIC program semantics now builds a stable semantic binding sidecar for
  resolved references. Bindings map canonical read/call references to canonical
  target symbols and preserve target kind, type, and function signature data for
  future SIR/A-MIR lowering.
- `language.arcobasic` now attaches `Semantics.Render` and
  `SemanticSymbolCount`/`SemanticReferenceCount` metadata to produced SIR
  artifacts and propagates semantic diagnostics into the compile result.
- Top-level `FissionCompiler.Compile()` now propagates artifact-carried
  diagnostics, allowing ArcoBASIC parser/SIR diagnostics to fail compilation.
- `FissionCompiler.Reveal(request, "AST"|"SIR"|"PIPELINE")` added for the
  initial reveal workflow.
- Initial executable module substrate added:
  - `FissionExecutableModule`
  - `FissionModuleComponentDescriptor`
  - host-side module registration through `Fission.RegisterModule(module)`
  - stdio manifest protocol marker `fission-module-stdio-v0.1`
  - first ArcoBASIC frontend component capsule entrypoint under
    `fission/modules/arcobasic_frontend/main.abas`
- Rivet can now build Fission module capsules with an explicit dependency on
  the in-graph `ArcoFission` bootstrap compiler target.
- Executable module transform invocation added for the first component boundary:
  `language.arcobasic` can now run as a built module capsule and transform
  `Source.arcobasic -> SIR` through the normal `FissionCompiler.Compile()`
  pipeline.
- Initial SIR module serialization added. Executable modules now return
  structural SIR data through `FissionSir+ArcoCompy`, and the host reconstructs
  `FissionSirModule` / `FissionSirNode` class instances before continuing the
  pipeline.
- SIR module executable responses now support a metadata envelope while
  remaining backward-compatible with plain packed SIR modules. The ArcoBASIC
  frontend capsule now returns semantic metadata (`Semantics.Render`,
  `SemanticSymbolCount`, `SemanticReferenceCount`, reveal renders, compile
  metadata) through the module boundary.
- Initial generic SIR semantic artifact model added:
  - `FissionSirSemanticArtifact`
  - `FissionSirSemanticSymbol`
  - `FissionSirSemanticReference`
  - `FissionSirSemanticBinding`
  ArcoBASIC program semantics can now export canonical symbols, references,
  bindings, target types, and function signatures into this substrate-level
  model.

- Measured real-world ArcoBASIC frontend coverage against the full non-Fission
  corpus (207 `.abas` files across Arcology OS, Arco3D, ARCADE, examples,
  stdlib, games, arcfs-utils): 188/207 (90.8%) structurally clean (lex/parse/
  lower-to-SIR, zero diagnostics); the remaining 19 are all large (4800-7200
  line) Arcology OS files that were slow, not incorrect -- zero real parse/
  lowering failures found anywhere in the corpus.
- Fixed a real Rivet correctness bug: `ArcoCapsuleTarget.Build()` fingerprinted
  only the entry file (`Inputs: [SELF.Entry]`), completely ignoring ArcoBASIC's
  own `#IMPORT` closure -- editing an imported file (e.g. `parser.abas`) did
  not invalidate a cached capsule built from an entry that imports it. Fixed
  in `rivet/stdlib/rivet.abas` via `ArcoCapsule_ImportClosure` (a plain BFS
  line-scanner over `#IMPORT "path"` directives -- deliberately not the real
  ArcoBASIC lexer/preprocessor, since Rivet must not depend on Fission to
  build Fission). Verified both directions: unchanged inputs stay a full cache
  hit; editing an imported (non-entry) file now correctly triggers a rebuild.
- Every `fission/tests/*.abas` smoke fixture (50 files) is now built as its
  own cached Rivet `ArcoCapsule` target (`fission/build/rivet_test_capsules.abas`,
  auto-discovered via `DIRECTORY("fission/tests", FALSE)`, so new test files
  need no registration). `tests/integration/fission_substrate_core_smoke.sh`
  now runs the prebuilt capsules instead of `ArcoFission compile-run` per
  fixture, eliminating redundant from-source frontend recompilation on every
  run. Confirmed byte-identical output between a prebuilt capsule and
  `compile-run` for the same fixture. Full suite: 363s -> ~205s on this
  change alone, all 50 golden-output checks still passing.
- Fixed a real instruction-limit bug blocking the above: Rivet's own embedded
  `arco::Runtime` (in `rivet/core/vm/vm.cpp`, used to run `build.abas` itself)
  inherited the default 100000-instruction hosted-execution safety cap, which
  a build script walking dozens of `#IMPORT` closures for 50+ capsule targets
  legitimately exceeds. Fixed via `runtime_.set_instruction_limit_override(0)`
  in `VM::VM()`, matching `src/native/host_bridge.cpp`'s identical existing
  rationale for native-embedding contexts. Required a one-time manual
  bootstrap compile of the `rivet` binary (clang++ direct invocation linking
  against the already-built `libarco_runtime.a`/`libarcology_os.a`) to escape
  the chicken-and-egg problem of the old (uncapped-less) binary being unable
  to even register the action that rebuilds itself.
- Found and partially fixed a real O(n^2) performance bug in the ArcoBASIC
  frontend's hot lexing path. Root cause: native `String.Length`/`String.Slice`
  (`src/runtime/runtime.cpp`) are both, of necessity, O(current string length)
  per call (a full byte-0 rescan to answer "what UTF-8 codepoint is at byte
  N", with no state carried between calls) -- and
  `fission/language/arcobasic/lexer.abas` called `String.Length(source)` as a
  `WHILE` loop condition (recomputed every iteration) plus
  `String.Slice(source, index, 1)` per character, over the entire source
  file. A real ~4800-line file measured at 4m7s to lex+parse+lower, ~3.2x
  worse than linear extrapolation from a smaller file predicted. Added
  `String.ToChars` (new, purely additive native function, zero behavior change
  to existing `String.Length`/`String.Slice` callers) that explodes a string
  into an array of codepoint-substrings in one real O(n) pass, then rewrote
  `lexer.abas` to call it once per file and index the result (O(1) per
  character) instead. Cut the measured worst-case file from 4m7s to 2m42s
  (~34% faster) -- real, but confirms a second, distinct O(n)-per-lookup
  pattern remains elsewhere (see Known Failures) and is now the larger
  remaining cost for whole-program semantic analysis specifically.
  Caught and fixed a real regression while validating this: `WHILE index <
  sourceLength AND chars[index] != ...`-style guards used bare `AND`, which is
  bitwise/non-short-circuiting in this language (see
  [[project_arcobasic_not_bitwise_footgun]]) -- `chars[index]` was being
  evaluated even when `index` was out of range, throwing "array index out of
  range" where the old `String.Slice`-based code had silently degraded
  instead. Fixed by using `ANDALSO` for every such guard. Also updated one
  now-stale golden symbol count in the integration smoke script (2033 ->
  2044) -- an expected, correct change since the rewrite added a new function
  (`Fission_ArcoBasicCharsSlice`) and new parameters/locals to `lexer.abas`
  itself, one of the 75 files under whole-program self-semantic analysis.

## In-Progress Components

- WP-001 needs expansion beyond skeleton route resolution:
  - version negotiation;
  - richer CLI/API surface.
- WP-002 needs broader parser/semantic integration for ArcoBASIC; the current
  parser subset is enough for early regression scaffolding but not yet full
  ArcoBASIC.
- The language authoring kit is an early facade over `FissionLanguageDefinition`;
  it still needs reusable tokenizer/parser helpers, semantic construction
  recipes, fixture scaffolding, package metadata helpers, and generated editor
  metadata before it is enough for third-party language packs.
- WP-003 SIR exists as a minimal model/builder/validator with an initial
  ArcoBASIC semantic report sidecar. It still needs typed values, canonical
  symbol references, resolved scopes, function signatures, control-flow
  structure, and canonical serialization.

## Legacy Bridges

- C++ ArcoFission remains the Generation 0 bootstrap compiler and regression
  oracle.
- Current Rivet `ArcoCapsuleTarget` invokes legacy `ArcoFission native`.
- Windows and Web capsule support remain legacy C++ ArcoFission bridge paths.
- Fission executable modules are currently native ArcoCapsule binaries produced
  by legacy C++ ArcoFission. The initial transform protocol uses request and
  response files plus `Process.Run`; richer in-process/shared-library loading is
  deferred.
- `fission/tests/core_smoke.abas` is run by legacy C++ ArcoFission during G0
  bootstrap testing.
- `fission/tests/arcobasic_language_smoke.abas` is run by legacy C++
  ArcoFission during G0 bootstrap testing.

## Known Failures

- `FissionArcoBasicSemanticReport`/`FissionArcoBasicProgramReport` symbol
  lookup (`HasSymbol`, `FindSymbol`, `FindCanonicalSymbol`,
  `FindLocalTargetSymbol` in `fission/language/arcobasic/semantic.abas`) is a
  linear `FOR symbol IN SELF.Symbols` scan per call, called once per
  reference. With 2044 program symbols and hundreds of references (the
  current Fission self-semantic corpus), this is now the largest remaining
  performance cost for whole-program strict semantic analysis -- distinct
  from, and not fixed by, the lexer's O(n^2) fix above. Needs an actual index
  (an `Object`-keyed map from `scope::name` or canonical name to symbol)
  instead of a scan.
- SIR is currently structural and renderable, but not yet typed, semantically
  bound, canonicalized, or lowered to real A-MIR.
- ArcoBASIC `Source.arcobasic -> SIR` supports a growing structural subset, but
  not yet real import expansion, full directive metadata semantics, lambdas,
  complete generic type syntax, full host interop, complete resolved
  symbols/types, or complete legacy-compatible syntax/semantics.
- Non-strict ArcoBASIC currently records unresolved references without failing
  compilation so existing host/global-heavy code can keep flowing through the
  substrate. `#STRICT` converts unresolved references into diagnostics.
- Unsupported ArcoBASIC statements are now surfaced into the top-level compile
  result diagnostics.
- No Brainfuck reference language exists yet.
- No G1/G2/G3 self-host path exists yet.
- The WP-001 compiler facade executes simple transform callbacks, but there is no
  persistent component package discovery, advanced artifact storage, version
  negotiation, or real target backend yet.
- Executable module transforms currently support structural SIR exchange for the
  ArcoBASIC frontend boundary and SIR metadata exchange for semantic/reveal
  data. A-MIR and other artifact codecs are still needed.

## Regression Status

- `build-rivet/ArcoFission compile-run fission/tests/core_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/language_authoring_kit_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run fission/tests/module_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_language_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_lexer_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run fission/tests/sir_builder_smoke.abas`
  passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_parser_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_expression_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_function_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_access_interface_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_call_statement_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_callable_comprehension_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_class_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_control_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_decimal_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_directive_decl_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_do_bitwise_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_postfix_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_literal_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_logic_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_named_defaults_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_os_stdlib_parse_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_preprocess_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_preprocess_compile_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_preprocess_error_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_real_timer_parse_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_real_project_parse_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_expression_smoke.abas` passed with both `NOT` and
  `!` unary operators.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_real_project_parse_smoke.abas` passed with 10 real
  project files, including ArcoUI, ArcoWrite, Arconaut, and the Fission
  ArcoBASIC parser itself.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 65 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 66 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 67 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 67
  current first-party Fission `.abas` files and zero parse/lowering/semantic
  diagnostics.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 68 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 68
  current first-party Fission `.abas` files and zero strict
  parse/lowering/semantic diagnostics.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_loop_semantic_smoke.abas` passed, proving strict
  semantic resolution for `FOR IN` and numeric `FOR` loop variables.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_import_semantic_smoke.abas` passed, proving
  import-aware program analysis accepts known imports and reports missing
  imports.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_program_symbol_smoke.abas` passed, proving canonical
  program symbols for module-level constants, classes, class fields, and typed
  functions.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_cross_file_semantic_smoke.abas` passed, proving
  strict whole-program semantic resolution for imported lowercase functions and
  variables through the canonical program symbol index.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_signature_semantic_smoke.abas` passed, proving
  exported function signatures preserve parameter names, types, defaults, and
  return type in the program symbol index.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_binding_semantic_smoke.abas` passed, proving resolved
  references produce binding records with canonical targets, target kind/type,
  and function signatures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 69
  modules, 99 import edges, and zero diagnostics using strict import-aware
  program semantic analysis.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 70 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 71 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 71
  modules, 101 import edges, 1873 program symbols, and zero diagnostics using
  strict import-aware program semantic analysis with cross-file reference
  resolution.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 72 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 72
  modules, 102 import edges, 1903 program symbols, and zero diagnostics using
  strict import-aware program semantic analysis with function signature capture.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 73 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 73
  modules, 103 import edges, 1940 program symbols, and zero diagnostics using
  strict import-aware program semantic analysis with semantic binding records.
- `build-rivet/ArcoFission compile-run
  fission/tests/module_smoke.abas` passed after the executable ArcoBASIC
  frontend capsule began returning semantic metadata through the SIR response
  envelope.
- `build-rivet/ArcoFission compile-run
  fission/tests/sir_semantic_artifact_smoke.abas` passed, proving ArcoBASIC
  program semantic data exports into the generic SIR semantic artifact model.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 73 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 73
  modules, 105 import edges, 1957 program symbols, and zero diagnostics after
  module metadata envelope support was added.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_parse_smoke.abas` passed with 75 current
  first-party Fission `.abas` files and zero parse/lowering failures.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_fission_self_semantic_smoke.abas` passed with 75
  modules, 108 import edges, 2033 program symbols, and zero diagnostics after
  generic SIR semantic artifacts were added.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_self_semantic_smoke.abas` passed, proving strict
  `SELF` field/method resolution for a class-local fixture.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_include_metadata_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_legacy_operator_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_let_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_string_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_type_compound_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_diagnostics_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_reveal_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_semantic_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_semantic_diagnostics_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_semantic_reference_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_shared_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_single_line_if_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_strict_semantic_smoke.abas` passed.
- `tests/integration/fission_substrate_core_smoke.sh build-rivet/ArcoFission
  /home/daedalus/projects/arcobasic` passed.
- `tests/integration/fission_substrate_core_smoke.sh build-rivet/ArcoFission
  /home/daedalus/projects/arcobasic` passed with the Fission self-parse fixture
  included.
- `(cd fission && ../build-rivet/fissure run)` passed.
- `(cd fission && ../build-rivet/fissure run --full)` passed.
- `(cd fission && ../build-rivet/fissure run --full)` passed after
  cross-file ArcoBASIC semantic reference resolution was added.
- `(cd fission && ../build-rivet/fissure run --full)` passed after
  ArcoBASIC function signature capture was added.
- `(cd fission && ../build-rivet/fissure run --full)` passed after
  ArcoBASIC semantic binding records were added.
- `(cd fission && ../build-rivet/fissure run --full)` passed after SIR module
  metadata response envelopes were added.
- `(cd fission && ../build-rivet/fissure run --full)` passed after generic SIR
  semantic artifacts were added.
- `ctest --test-dir build -R '^fission_substrate_core_smoke$'
  --output-on-failure` passed.
- `build-rivet/rivet build` passed and produced
  `build-rivet/fission/modules/fission-arcobasic-frontend`.
- `build-rivet/fission/modules/fission-arcobasic-frontend` printed its module
  manifest successfully.
- `build-rivet/fission/modules/fission-arcobasic-frontend transform ...`
  produced SIR successfully.
- `FissionCompiler.Compile()` invoked `language.arcobasic` as an executable
  module and received a reconstructed structural SIR module successfully.
- `FissionCompiler.Compile()` invoked `language.arcobasic` as an executable
  module and received semantic metadata on the returned SIR artifact
  successfully.
- `git diff --check` passed.
- Resolver smoke now covers accumulated capabilities, unmet requirements, and
  ambiguous shortest-route diagnostics.
- Core smoke now executes a callback pipeline and validates final artifact type
  and value for `Source.brainfuck -> SIR -> AMIR -> Executable.Linux.X86_64`.
- Full CTest/Fissure suite not run for this initial substrate slice.
- `tests/integration/fission_substrate_core_smoke.sh build-rivet/ArcoFission
  /home/daedalus/projects/arcobasic` passed (all 50 fixtures, prebuilt Rivet
  capsules) after switching from `ArcoFission compile-run` per fixture to a
  cached `rivet build` + direct capsule execution: 363s -> ~205s.
  `arcobasic_lexer_smoke` initially failed ("array index out of range") after
  the `String.ToChars` lexer rewrite -- root cause was bare `AND` (bitwise,
  non-short-circuiting) guarding `chars[index]` array access, fixed with
  `ANDALSO`; full suite re-passed after the fix, including the updated
  program-symbol-count golden value (2033 -> 2044).
- Directly re-timed the worst-case large file
  (`arcology-os/tests/fixtures/aex-manifest-probe/aex-manifest-probe.abas`,
  4808 lines) alone, isolated from suite/system noise: 4m7s before the
  `String.ToChars` lexer fix, 2m42s after (~34% faster), confirming the fix
  is real but that whole-program semantic analysis's O(n) symbol-lookup scan
  (see Known Failures) is now the larger remaining cost.
- Verified the Rivet `#IMPORT`-closure fingerprint fix both directions:
  `rivet build` run twice with no source changes stays a full cache hit
  (0 compiled); appending a comment to an *imported* (non-entry) file
  (`fission/language/arcobasic/lexer.abas`, imported by the
  `fission-arcobasic-frontend` module) correctly triggers a rebuild where the
  unfixed `Inputs: [SELF.Entry]` behavior would have silently stayed cached.

## Bootstrap Generation

- Current generation: G0 legacy C++ ArcoFission.
- Target next generation: G1 ArcoBASIC Fission compiled by legacy C++
  ArcoFission.

## Files Changed

- `docs/RFC-AP-FISSION-001_Fission_Compiler_Substrate.md`
- `.gitignore`
- `.agents/FISSION_SUBSTRATE_PROGRESS.md`
- `fission/README.md`
- `fission/fissure.ab`
- `fission/core/capability.abas`
- `fission/core/diagnostics.abas`
- `fission/core/artifact.abas`
- `fission/core/component.abas`
- `fission/core/registry.abas`
- `fission/core/pipeline.abas`
- `fission/core/resolver.abas`
- `fission/core/compiler.abas`
- `fission/core/fission.abas`
- `fission/core/module.abas`
- `fission/build/rivet_modules.abas`
- `fission/language/api/language.abas`
- `fission/language/api/authoring_kit.abas`
- `fission/language/arcobasic/preprocess.abas`
- `fission/language/arcobasic/lexer.abas`
- `fission/language/arcobasic/parser.abas`
- `fission/language/arcobasic/lower_sir.abas`
- `fission/language/arcobasic/semantic.abas`
- `fission/language/arcobasic/register.abas`
- `fission/sir/model.abas`
- `fission/sir/builder.abas`
- `fission/sir/validate.abas`
- `fission/sir/semantics.abas`
- `fission/sir/serialize.abas`
- `fission/cli/fission.abas`
- `fission/modules/arcobasic_frontend/main.abas`
- `fission/tests/core_smoke.abas`
- `fission/tests/language_authoring_kit_smoke.abas`
- `fission/tests/module_smoke.abas`
- `fission/tests/sir_semantic_artifact_smoke.abas`
- `fission/tests/arcobasic_language_smoke.abas`
- `fission/tests/arcobasic_access_interface_smoke.abas`
- `fission/tests/arcobasic_binding_semantic_smoke.abas`
- `fission/tests/arcobasic_class_smoke.abas`
- `fission/tests/arcobasic_call_statement_smoke.abas`
- `fission/tests/arcobasic_callable_comprehension_smoke.abas`
- `fission/tests/arcobasic_control_smoke.abas`
- `fission/tests/arcobasic_cross_file_semantic_smoke.abas`
- `fission/tests/arcobasic_decimal_smoke.abas`
- `fission/tests/arcobasic_diagnostics_smoke.abas`
- `fission/tests/arcobasic_directive_decl_smoke.abas`
- `fission/tests/arcobasic_do_bitwise_smoke.abas`
- `fission/tests/arcobasic_expression_smoke.abas`
- `fission/tests/arcobasic_fission_self_parse_smoke.abas`
- `fission/tests/arcobasic_fission_self_semantic_smoke.abas`
- `fission/tests/arcobasic_function_smoke.abas`
- `fission/tests/arcobasic_include_metadata_smoke.abas`
- `fission/tests/arcobasic_import_semantic_smoke.abas`
- `fission/tests/arcobasic_legacy_operator_smoke.abas`
- `fission/tests/arcobasic_let_smoke.abas`
- `fission/tests/arcobasic_lexer_smoke.abas`
- `fission/tests/arcobasic_literal_smoke.abas`
- `fission/tests/arcobasic_logic_smoke.abas`
- `fission/tests/arcobasic_loop_semantic_smoke.abas`
- `fission/tests/arcobasic_named_defaults_smoke.abas`
- `fission/tests/arcobasic_os_stdlib_parse_smoke.abas`
- `fission/tests/arcobasic_preprocess_smoke.abas`
- `fission/tests/arcobasic_preprocess_compile_smoke.abas`
- `fission/tests/arcobasic_preprocess_error_smoke.abas`
- `fission/tests/arcobasic_program_symbol_smoke.abas`
- `fission/tests/arcobasic_real_timer_parse_smoke.abas`
- `fission/tests/arcobasic_real_project_parse_smoke.abas`
- `fission/tests/sir_builder_smoke.abas`
- `fission/tests/arcobasic_parser_smoke.abas`
- `fission/tests/arcobasic_postfix_smoke.abas`
- `fission/tests/arcobasic_reveal_smoke.abas`
- `fission/tests/arcobasic_self_semantic_smoke.abas`
- `fission/tests/arcobasic_semantic_smoke.abas`
- `fission/tests/arcobasic_semantic_diagnostics_smoke.abas`
- `fission/tests/arcobasic_semantic_reference_smoke.abas`
- `fission/tests/arcobasic_shared_smoke.abas`
- `fission/tests/arcobasic_signature_semantic_smoke.abas`
- `fission/tests/arcobasic_single_line_if_smoke.abas`
- `fission/tests/arcobasic_strict_semantic_smoke.abas`
- `fission/tests/arcobasic_string_smoke.abas`
- `fission/tests/arcobasic_type_compound_smoke.abas`
- `tests/integration/fission_substrate_core_smoke.sh`
- `rivet/stdlib/rivet.abas`
- `rivet/core/vm/vm.cpp`
- `src/runtime/runtime.cpp`
- `build.abas`
- `fission/build/rivet_test_capsules.abas`
- `cmake/Testing.cmake`
- `fissure.ab`

## Architectural Decisions

- Fission is the new compiler and compiler substrate name.
- ArcoFission is legacy/bootstrap/reference terminology only.
- First-party Fission implementation should be ArcoBASIC wherever technically
  possible.
- Fission Core must remain component-oriented and must not hard-code target
  ladders.
- Normal language packages own input semantics only and inherit installed output
  targets through common IR.
- SIR is the intended beginner-friendly semantic IR above A-MIR.
- Brainfuck is the required beginner-language architecture proof.
- Fissure is the regression authority.
- Rivet owns build orchestration.
- macOS is unsupported and out of scope.
- The first implementation slice uses plain ArcoBASIC classes and functions, not
  a compiler-definition DSL.
- Fission Core route resolution is based on artifact types (`Consumes` /
  `Produces`) rather than hard-coded target names.
- Reserved ArcoBASIC words cannot be used as method names; early core APIs avoid
  `Add`, `Has`, `Contains`, `Error`, and `Warning` in method position.
- Language construction is implemented as ordinary ArcoBASIC classes and methods
  layered on components, not as a new language-definition DSL.
- The ArcoBASIC frontend is treated as a normal language package. Its output path
  is inherited from registered SIR/AMIR/target components rather than wired into
  Fission Core.
- SIR builder method names avoid ArcoBASIC statement keywords in method position
  (`PrintValue`, `AssignValue`, `CallValue`, `Conditional`) while preserving
  compiler-facing node kinds like `Print`, `Assign`, `Call`, and `Branch`.
- The initial ArcoBASIC frontend lexer is a first-party ArcoBASIC module, not a
  C++ bridge and not Fission Core logic.
- AST parsing and AST-to-SIR lowering are first-party ArcoBASIC language package
  modules under `fission/language/arcobasic/`, preserving frontend isolation.
- Reveal output is exposed through the compiler facade, with AST/SIR data carried
  as artifact metadata until dedicated typed reveal artifacts are introduced.
- Calls are represented as callee-expression calls (`CallExpr`) so free
  functions, method calls, and future callable values can share one path.
- Member/index assignment lowers to `Set`; simple variable assignment remains
  `Assign`.
- Preprocessing is a first-party ArcoBASIC frontend phase before lexing, not
  Fission Core behavior. Its initial behavior intentionally preserves line count
  to keep future diagnostics/reveal output stable.
- `#INCLUDE` expansion is implemented in the preprocessor. `#IMPORT` is recorded
  as metadata and represented as an import node, but package/source expansion is
  deferred until package discovery rules exist.
- Fission now carries at least one real Arcology OS source file,
  `arcology-os/stdlib/timer_policy.abas`, through ArcoBASIC lex/parse/SIR
  lowering without diagnostics as a standing compatibility smoke.
- Fission now carries a broader real-project fixture through ArcoBASIC
  lex/parse/SIR lowering without diagnostics:
  - `arcology-os/stdlib/timer_policy.abas`
  - `arcology-os/stdlib/uefi_bootstrap.abas`
  - `arcology-os/stdlib/uefi_memory_manager.abas`
  - `arco3d/stdlib/arco3d_rig.abas`
  - `arco3d/stdlib/arco3d_scene.abas`
  - `examples/gui_window.abas`
  - `stdlib/arcoui.abas`
  - `examples/arcowrite.abas`
  - `arcfs-utils/apps/arconaut/arconaut.abas`
  - `fission/language/arcobasic/parser.abas`
- Fission now carries 25 smaller Arcology OS stdlib modules through ArcoBASIC
  lex/parse/SIR lowering without diagnostics, including graphics, descriptor
  table, block device, volume, timer, keyboard, FAT32, namespace, UEFI memory,
  and virtio-net policy sources.
- Fission now carries every current first-party Fission `.abas` source through
  ArcoBASIC lex/parse/SIR lowering without diagnostics, including the executable
  module entrypoint and all Fission smoke fixtures.
- Fission now carries every current first-party Fission `.abas` source through
  non-strict ArcoBASIC semantic analysis without diagnostics. This is not
  self-compilation yet, but it is the next rung above structural self-parse.
- Fission now carries every current first-party Fission `.abas` source through
  strict per-file ArcoBASIC semantic analysis without diagnostics. This still
  analyzes files independently; whole-program import-aware semantics is the next
  self-compilation step.
- Fission now carries every current first-party Fission `.abas` source through
  strict import-aware program semantic analysis without diagnostics. This
  validates the current import graph exists but does not yet merge exported
  symbols across modules.
- Fission now indexes 1873 canonical program symbols and resolves canonical
  program references across explicit imports for the current first-party Fission
  source set. This is still analysis-only until the bindings are promoted into
  typed SIR.
- Fission now indexes 1903 canonical program symbols across 72 current
  first-party Fission `.abas` files and preserves normalized function
  signatures for exported ABAS functions.
- Fission now indexes 1940 canonical program symbols across 73 current
  first-party Fission `.abas` files and produces stable semantic binding records
  for resolved references.
- Fission now indexes 2033 canonical program symbols across 75 current
  first-party Fission `.abas` files and can export resolved ABAS program
  semantics into a generic SIR semantic artifact.
- The executable ArcoBASIC frontend module must preserve the same public
  semantic metadata keys as the in-process frontend so reveal, diagnostics, and
  future SIR/A-MIR lowering do not depend on whether a component runs in-process
  or as an ArcoCapsule.
- SIR semantic data has a generic substrate-level representation. Language
  frontends may adapt their own semantic reports into it, but downstream passes
  should consume the generic model rather than frontend-private report classes.
- The first self-compilation semantic pressure point is covered: class fields and
  self-method calls are represented in the semantic report instead of falling
  through as unresolved or external `SELF` reads.
- The language authoring kit is a convenience facade over ordinary ArcoBASIC
  Fission APIs, not a compiler-definition DSL. It must remain layered on the same
  public component contracts used by first-party language packages.
- Fission modules should be buildable executable ArcoCapsule binaries during G0.
  They advertise normal component metadata through a stable stdio manifest
  protocol and are registered into the same host registry as in-process
  components, so the compiler still behaves as one composed substrate.
- Strict ArcoBASIC program semantics intentionally analyze each file non-strict
  first, build the full import/symbol/reference index, resolve imported symbols,
  and only then enforce unresolved-reference diagnostics at the program boundary.
- Fission smoke tests run as prebuilt, Rivet-cached `ArcoCapsule` targets, not
  `ArcoFission compile-run` from source -- a test's own logic is unchanged and
  its output is required to be byte-identical either way (verified); only the
  dispatch mechanism changed, specifically to stop paying a from-source
  recompile of the shared frontend on every single test invocation. Any new
  Rivet `ArcoCapsuleTarget`-based target must fingerprint the full `#IMPORT`
  closure (`ArcoCapsule_ImportClosure`), never just the entry file, or its
  cache goes silently stale on an edit to an imported file.
- Native runtime primitives that are correctly O(current string length) per
  call by necessity (`String.Length`, `String.Slice` -- UTF-8 codepoint
  indexing with no cross-call state) must not be called in a per-character
  loop over a whole file; that is an O(n^2) trap regardless of how fast any
  single call is. `String.ToChars` exists so hot per-character scanners
  explode a string once (real O(n)) and then index (O(1)), and is additive
  only -- it must never change the existing behavior of `String.Length`/
  `String.Slice` for their existing callers.

## Next Recommended Work

1. Continue expanding ArcoBASIC frontend compatibility against real Arcology
   source files until the Fission parser can ingest the primary ABAS corpus.
2. Keep advancing Fission self-compilation by serializing
   `FissionSirSemanticArtifact` through module/component boundaries and attaching
   semantic artifact references to SIR compile results.
3. Add real import expansion once package/source discovery rules are available.
4. Add lambdas, richer type syntax, and more complete access/inheritance
   semantic validation.
5. Promote semantic report data into typed SIR scopes, symbol references, and
   function signatures.
6. Start the real SIR-to-A-MIR lowering contract using legacy ArcoFission reveal
   output as the oracle.
7. Add A-MIR and diagnostics artifact codecs so executable modules can pass the
   next compiler representations structurally.
8. Replace metadata-carried reveal payloads with typed reveal artifacts.
9. Add route ambiguity policy controls and richer `explain pipeline` output.
10. Add version negotiation for component contracts.
11. Add component package discovery/loading conventions under `fission/`.
12. After ArcoBASIC frontend support is substantially complete, expand the
   language authoring kit with reusable tokenizer/parser helpers, semantic
   construction recipes, package metadata helpers, and generated tooling
   metadata.
13. Keep all legacy compiler calls behind explicitly named bootstrap bridge
   components.
14. Fix the O(n) linear symbol-lookup scan in
   `fission/language/arcobasic/semantic.abas` (`HasSymbol`/`FindSymbol`/
   `FindCanonicalSymbol`/`FindLocalTargetSymbol`) with a real index -- this is
   now the largest remaining performance cost for whole-program strict
   semantic analysis, confirmed by direct measurement after the lexer's
   O(n^2) fix landed. See Known Failures for detail.
15. Extend the same `#IMPORT`-closure-fingerprinted, cached-capsule pattern
   (`fission/build/rivet_test_capsules.abas`) to any other repeated
   `ArcoFission compile-run` call sites that recompile shared frontend source
   from scratch on every invocation.
16. Re-run the full 207-file real-corpus frontend scan (see Regression Status
   history) once the semantic-lookup and remaining large-file performance
   fixes land, to get a clean up-to-date completion number for the 19 files
   that previously could not be confirmed within a 240s cap.
