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
`language.arcobasic` registration that lexes/parses a growing ArcoBASIC subset
and lowers it into structured SIR.

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
- ArcoBASIC Source-to-SIR now emits a structured `FissionSirModule` for the
  initial `PRINT value` slice and records token count/SIR render metadata.
- Initial ArcoBASIC parser and AST model added:
  - string and interpolated-string literals with legacy-compatible basic escapes
  - `#IMPORT` directives with optional aliases
  - generic directive capture
  - constants
  - typed declarations
  - assignment statements
  - compound assignments
  - `PRINT`
  - `RETURN`
  - function declarations with positional arguments
  - typed function arguments and return annotations
  - class declarations
  - constructors
  - call expressions
  - method-style/postfix call expressions
  - arithmetic/comparison binary expressions
  - `AND`, `ANDALSO`, `OR`, `ORELSE`, `%`, and `MOD`
  - unary `-` and `NOT`
  - boolean/null/number literals
  - parenthesized expressions
  - simple `IF`/`ELSE`/`END IF`
  - simple `WHILE`/`WEND` and `END WHILE`
  - simple numeric `FOR` and collection `FOR IN`
  - loop `EXIT`/`CONTINUE`
  - `TRY`/`CATCH`/`END TRY`
  - `THROW`
  - member reads and index reads
  - array and object literals
- AST-to-SIR lowering added for the current ArcoBASIC parser subset.
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
- WP-003 SIR exists as a minimal model/builder/validator and needs typed values,
  scopes, symbols, function signatures, control-flow structure, and canonical
  serialization.

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
  not yet full preprocessor conditionals, access modifiers, inheritance,
  interfaces, lambdas, `ADDRESSOF`, `DO` loops, comprehensions, named/optional
  arguments, full call/member assignment semantics, host interop, or complete
  legacy-compatible syntax/semantics.
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
  fission/tests/arcobasic_class_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_control_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_directive_decl_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_postfix_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_literal_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_logic_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_string_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_type_compound_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_diagnostics_smoke.abas` passed.
- `build-rivet/ArcoFission compile-run
  fission/tests/arcobasic_reveal_smoke.abas` passed.
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
- `fission/language/arcobasic/lexer.abas`
- `fission/language/arcobasic/parser.abas`
- `fission/language/arcobasic/lower_sir.abas`
- `fission/language/arcobasic/register.abas`
- `fission/sir/model.abas`
- `fission/sir/builder.abas`
- `fission/sir/validate.abas`
- `fission/cli/fission.abas`
- `fission/tests/core_smoke.abas`
- `fission/tests/arcobasic_language_smoke.abas`
- `fission/tests/arcobasic_class_smoke.abas`
- `fission/tests/arcobasic_control_smoke.abas`
- `fission/tests/arcobasic_diagnostics_smoke.abas`
- `fission/tests/arcobasic_directive_decl_smoke.abas`
- `fission/tests/arcobasic_expression_smoke.abas`
- `fission/tests/arcobasic_function_smoke.abas`
- `fission/tests/arcobasic_lexer_smoke.abas`
- `fission/tests/arcobasic_literal_smoke.abas`
- `fission/tests/arcobasic_logic_smoke.abas`
- `fission/tests/sir_builder_smoke.abas`
- `fission/tests/arcobasic_parser_smoke.abas`
- `fission/tests/arcobasic_postfix_smoke.abas`
- `fission/tests/arcobasic_reveal_smoke.abas`
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

## Next Recommended Work

1. Add preprocessor conditional/directive handling (`#DEFINE`, `#IFDEF`,
   `#IFNDEF`, `#ELSE`, `#ENDIF`, `#ERROR`, etc.) in a frontend component.
2. Add access modifiers, inheritance, interfaces, abstract methods, lambdas,
   `ADDRESSOF`, `DO` loops, comprehensions, and named/optional arguments.
3. Add typed SIR values, scopes, symbols, and function signatures.
4. Replace metadata-carried reveal payloads with typed reveal artifacts.
5. Start the real SIR-to-A-MIR lowering contract using legacy ArcoFission reveal
   output as the oracle.
6. Add route ambiguity policy controls and richer `explain pipeline` output.
7. Add version negotiation for component contracts.
8. Add component package discovery/loading conventions under `fission/`.
9. Keep all legacy compiler calls behind explicitly named bootstrap bridge
   components.
