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
  - class declarations
  - constructors
  - interfaces
  - access/abstract/shared modifiers
  - class `EXTENDS` and `IMPLEMENTS` metadata
  - call expressions with positional and named arguments
  - method-style/postfix call expressions
  - arithmetic/comparison/bitwise/shift/membership binary expressions
  - `AND`, `ANDALSO`, `OR`, `ORELSE`, `%`, `MOD`, `BITAND`, `BITOR`,
    `BITXOR`, `SHL`, `SHR`, `SAR`, `IN`, `HAS`, and `CONTAINS`
  - unary `-`, `NOT`, `ADDRESSOF`, and `COPY`
  - boolean/null/number literals
  - parenthesized expressions
  - simple `IF`/`ELSE`/`END IF`
  - single-line nested `IF ... THEN ... ELSE IF ...`
  - simple `WHILE`/`WEND` and `END WHILE`
  - `DO WHILE`/`DO UNTIL` and post-condition `LOOP WHILE`/`LOOP UNTIL`
  - simple numeric `FOR` and collection `FOR IN`
  - loop `EXIT`/`CONTINUE`
  - `TRY`/`CATCH`/`END TRY`
  - `THROW`
  - member reads and index reads
  - array literals, array comprehensions, and object literals
- AST-to-SIR lowering added for the current ArcoBASIC parser subset.
- Initial ArcoBASIC semantic report pass added. It walks SIR, records imports,
  variables, constants, functions, parameters, classes, interfaces, constructor
  scopes, inferred assignment-created locals, return types, and parameter
  defaults, and reports duplicate symbols through normal diagnostics.
  It also resolves reads and direct calls against enclosing scopes, records
  resolved/external/unresolved references, and enforces unresolved-reference
  diagnostics when `#STRICT` is active.
- `language.arcobasic` now attaches `Semantics.Render` and
  `SemanticSymbolCount`/`SemanticReferenceCount` metadata to produced SIR
  artifacts and propagates semantic diagnostics into the compile result.
- Top-level `FissionCompiler.Compile()` now propagates artifact-carried
  diagnostics, allowing ArcoBASIC parser/SIR diagnostics to fail compilation.
- `FissionCompiler.Reveal(request, "AST"|"SIR"|"PIPELINE")` added for the
  initial reveal workflow.

## In-Progress Components

- WP-001 needs expansion beyond skeleton route resolution:
  - version negotiation;
  - richer CLI/API surface.
- WP-002 needs broader parser/semantic integration for ArcoBASIC; the current
  parser subset is enough for early regression scaffolding but not yet full
  ArcoBASIC.
- WP-003 SIR exists as a minimal model/builder/validator with an initial
  ArcoBASIC semantic report sidecar. It still needs typed values, canonical
  symbol references, resolved scopes, function signatures, control-flow
  structure, and canonical serialization.

## Legacy Bridges

- C++ ArcoFission remains the Generation 0 bootstrap compiler and regression
  oracle.
- Current Rivet `ArcoCapsuleTarget` invokes legacy `ArcoFission native`.
- Windows and Web capsule support remain legacy C++ ArcoFission bridge paths.
- `fission/tests/core_smoke.abas` is run by legacy C++ ArcoFission during G0
  bootstrap testing.
- `fission/tests/arcobasic_language_smoke.abas` is run by legacy C++
  ArcoFission during G0 bootstrap testing.

## Known Failures

- SIR is currently structural and renderable, but not yet typed, canonicalized,
  serialized, or lowered to real A-MIR.
- ArcoBASIC `Source.arcobasic -> SIR` supports a growing structural subset, but
  not yet real import expansion, full directive metadata semantics, lambdas,
  complete generic type syntax, full host interop, resolved symbols/types, or
  complete legacy-compatible syntax/semantics.
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

## Regression Status

- `build-rivet/ArcoFission compile-run fission/tests/core_smoke.abas` passed.
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
  fission/tests/arcobasic_preprocess_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_preprocess_compile_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_preprocess_error_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_include_metadata_smoke.abas` passed.
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
- `(cd fission && ../build-rivet/fissure run)` passed.
- `ctest --test-dir build -R '^fission_substrate_core_smoke$'
  --output-on-failure` passed.
- `git diff --check` passed.
- Resolver smoke now covers accumulated capabilities, unmet requirements, and
  ambiguous shortest-route diagnostics.
- Core smoke now executes a callback pipeline and validates final artifact type
  and value for `Source.brainfuck -> SIR -> AMIR -> Executable.Linux.X86_64`.
- Full CTest/Fissure suite not run for this initial substrate slice.

## Bootstrap Generation

- Current generation: G0 legacy C++ ArcoFission.
- Target next generation: G1 ArcoBASIC Fission compiled by legacy C++
  ArcoFission.

## Files Changed

- `docs/RFC-AP-FISSION-001_Fission_Compiler_Substrate.md`
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
- `fission/language/api/language.abas`
- `fission/language/arcobasic/preprocess.abas`
- `fission/language/arcobasic/lexer.abas`
- `fission/language/arcobasic/parser.abas`
- `fission/language/arcobasic/lower_sir.abas`
- `fission/language/arcobasic/semantic.abas`
- `fission/language/arcobasic/register.abas`
- `fission/sir/model.abas`
- `fission/sir/builder.abas`
- `fission/sir/validate.abas`
- `fission/cli/fission.abas`
- `fission/tests/core_smoke.abas`
- `fission/tests/arcobasic_language_smoke.abas`
- `fission/tests/arcobasic_access_interface_smoke.abas`
- `fission/tests/arcobasic_class_smoke.abas`
- `fission/tests/arcobasic_call_statement_smoke.abas`
- `fission/tests/arcobasic_callable_comprehension_smoke.abas`
- `fission/tests/arcobasic_control_smoke.abas`
- `fission/tests/arcobasic_diagnostics_smoke.abas`
- `fission/tests/arcobasic_directive_decl_smoke.abas`
- `fission/tests/arcobasic_do_bitwise_smoke.abas`
- `fission/tests/arcobasic_expression_smoke.abas`
- `fission/tests/arcobasic_function_smoke.abas`
- `fission/tests/arcobasic_include_metadata_smoke.abas`
- `fission/tests/arcobasic_let_smoke.abas`
- `fission/tests/arcobasic_lexer_smoke.abas`
- `fission/tests/arcobasic_literal_smoke.abas`
- `fission/tests/arcobasic_logic_smoke.abas`
- `fission/tests/arcobasic_named_defaults_smoke.abas`
- `fission/tests/arcobasic_preprocess_smoke.abas`
- `fission/tests/arcobasic_preprocess_compile_smoke.abas`
- `fission/tests/arcobasic_preprocess_error_smoke.abas`
- `fission/tests/sir_builder_smoke.abas`
- `fission/tests/arcobasic_parser_smoke.abas`
- `fission/tests/arcobasic_postfix_smoke.abas`
- `fission/tests/arcobasic_reveal_smoke.abas`
- `fission/tests/arcobasic_semantic_smoke.abas`
- `fission/tests/arcobasic_semantic_diagnostics_smoke.abas`
- `fission/tests/arcobasic_semantic_reference_smoke.abas`
- `fission/tests/arcobasic_shared_smoke.abas`
- `fission/tests/arcobasic_single_line_if_smoke.abas`
- `fission/tests/arcobasic_strict_semantic_smoke.abas`
- `fission/tests/arcobasic_string_smoke.abas`
- `fission/tests/arcobasic_type_compound_smoke.abas`
- `tests/integration/fission_substrate_core_smoke.sh`
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

## Next Recommended Work

1. Add real import expansion once package/source discovery rules are available.
2. Add lambdas, richer type syntax, and more complete access/inheritance
   semantic validation.
3. Promote semantic report data into typed SIR scopes, symbol references, and
   function signatures.
4. Replace metadata-carried reveal payloads with typed reveal artifacts.
5. Start the real SIR-to-A-MIR lowering contract using legacy ArcoFission reveal
   output as the oracle.
6. Add route ambiguity policy controls and richer `explain pipeline` output.
7. Add version negotiation for component contracts.
8. Add component package discovery/loading conventions under `fission/`.
9. Keep all legacy compiler calls behind explicitly named bootstrap bridge
   components.
