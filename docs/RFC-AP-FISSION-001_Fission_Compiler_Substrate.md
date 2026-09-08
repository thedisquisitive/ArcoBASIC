# RFC-AP-FISSION-001: Fission Compiler Substrate

**Status:** Draft / Implementation Authority
**Project:** Arcology
**Component:** Fission
**Legacy Bootstrap:** C++ ArcoFission
**Implementation Language:** ArcoBASIC
**Primary Host:** Linux
**Secondary Platform:** Web
**Later Compatibility Platform:** Windows
**macOS:** Unsupported / Out of Scope

## 1. Executive Summary

Fission shall be rebuilt from the current C++ ArcoFission compiler into the
Fission Compiler Substrate, written in ArcoBASIC and compiled initially by the
legacy C++ implementation.

This is not a language-porting exercise. This is an architectural conversion
from one compiler with several built-in targets into a compiler substrate
assembled from swappable ArcoBASIC components.

Fission shall provide:

- pluggable language frontends;
- common intermediate representations;
- optimization passes;
- architecture modules;
- ABI modules;
- runtime modules;
- output/artifact modules;
- target recipes;
- bytecode engines;
- JIT engines;
- inspection and diagnostics;
- compiler-construction APIs.

The primary design requirement is:

> A new language author should only need to teach Fission how to understand that
> language.

If Linux native output already exists, a Brainfuck language extension shall
inherit Linux native compilation automatically. If Web output already exists,
that same Brainfuck extension shall inherit Web compilation automatically. If
Arcology OS output becomes available later, the same unchanged Brainfuck frontend
shall gain access to that target.

A language author shall not need to understand or implement ELF, PE, AEX,
WebAssembly packaging, machine code, calling conventions, runtime startup,
relocation, linking, executable layout, or operating-system process setup. Those
are Fission's responsibility. The language author's responsibility is the input
side.

## 2. Project Identity

The new compiler is **Fission**.

The architectural system is **Fission Compiler Substrate**.

The name **ArcoFission** refers only to the legacy C++ bootstrap/reference
implementation during migration.

New implementation code, documentation, namespaces, binaries, libraries,
packages, and commands should use `Fission`.

The `Arco` prefix shall not be added automatically to new Arcology components.

## 3. Core Design Principle

Fission is not merely an ArcoBASIC compiler. Fission is an
ArcoBASIC-programmable compiler construction substrate.

ArcoBASIC remains the primary Arcology language. However, languages, targets,
optimizers, output writers, runtimes, architecture support, and compiler behavior
are components assembled from the substrate.

The substrate itself must remain larger than any individual language or target.

## 4. Absolute Architectural Invariants

The following requirements are mandatory. Any implementation violating one of
these invariants shall be considered architecturally incorrect even if it
compiles and passes limited tests.

### 4.1 Everything is ArcoBASIC

First-party Fission functionality shall be implemented in ArcoBASIC wherever
technically possible.

This includes Fission Core, language packages, optimization passes, target
recipes, ABI definitions, runtime adapters, artifact writers, architecture
support, analysis tools, and compiler extensions.

A temporary C/C++ bridge is allowed only when necessary during bootstrap. Such
bridges must be explicitly marked as temporary.

There shall be no new compiler-definition DSL, separate target-definition
language, special optimizer scripting language, or language-description DSL.

Fission extensibility is ordinary ArcoBASIC programming through Fission
libraries.

### 4.2 We do not make a new language to make new languages

Language authors shall use the normal ArcoBASIC language and the Fission
library.

Conceptual entry point:

```basic
Language = Fission.Language("Brainfuck")
```

Not:

```text
LANGUAGE Brainfuck
TOKEN ...
RULE ...
```

unless such syntax is itself simply ordinary ArcoBASIC library syntax supported
by the language.

Compiler construction must remain programming, not configuration through another
custom language.

### 4.3 Language authors own only the input side

The default language-development path shall terminate at Fission's common
semantic/compiler representation.

```text
LANGUAGE AUTHOR DOMAIN

Source
  |
  v
Language Frontend
  |
  v
Fission Common Representation

================================

FISSION DOMAIN

Common Representation
  |
  v
Optimization
  |
  v
Target Lowering
  |
  v
Architecture
  |
  v
ABI
  |
  v
Runtime
  |
  v
Artifact
```

A normal language package shall not contain target-specific backend logic.

### 4.4 Output support is inherited

Once a language can produce a compatible Fission intermediate representation, it
automatically gains access to compatible installed targets.

```text
brainfuck.abas
      |
      v
A-MIR
```

Existing:

```text
A-MIR
  -> x86-64
  -> SysV
  -> Linux Runtime
  -> ELF64
```

therefore enables:

```text
Brainfuck -> Linux native executable
```

without changes to `brainfuck.abas`.

Later:

```text
A-MIR
  -> Web target
```

automatically enables:

```text
Brainfuck -> Web
```

Again without changing the language frontend.

### 4.5 No target conditionals in language packages

Normal language extensions must not require code such as:

```basic
IF Target = "linux-x86_64" THEN
    ...
ELSE IF Target = "web" THEN
    ...
END IF
```

Target-aware language behavior is permitted only as an advanced escape hatch for
languages that are intrinsically target-aware. It must not be required for
ordinary compiled languages.

### 4.6 No compiler degree required

The default language-construction API must be usable by competent programmers who
do not have formal compiler-engineering backgrounds.

The benchmark is not whether a compiler engineer can implement a new language.
The benchmark is whether a hobbyist who knows ArcoBASIC can implement a small
compiled language without learning compiler theory first.

## 5. Language Construction Philosophy

A simple language author should think in terms of tokens, symbols, statements,
expressions, variables, functions, loops, arrays, calls, values, types, and
operations.

They should not need to think in terms of SSA, phi nodes, register allocation,
calling conventions, relocations, machine blocks, ELF sections, PE sections,
object formats, or ABI register classes.

Fission shall provide friendly semantic construction APIs that hide those
details.

## 6. Progressive Abstraction Levels

Fission shall provide multiple levels of access. A user may descend into lower
layers only when required.

### Level 1 - Language Construction API

Target audience:

- hobbyists;
- students;
- esolang authors;
- game developers;
- DSL creators.

Example:

```basic
Language = Fission.Language("Tiny")

Language.Statement("Print", ...)
Language.Expression("Add", ...)
Language.Implement("Print", ...)
```

No IR knowledge required.

### Level 2 - Semantic Construction API

Users manipulate compiler-neutral concepts:

```text
CreateVariable
Assign
Call
Return
Loop
Branch
Add
Subtract
Index
Load
Store
```

Still no A-MIR knowledge required.

### Level 3 - SIR

Fission should provide a friendly semantic IR layer.

Tentative name: **SIR - Semantic Intermediate Representation**.

SIR exists specifically to avoid exposing language authors directly to A-MIR.

```text
Language Syntax
     |
     v
Friendly Semantic API
     |
     v
SIR
     |
     v
A-MIR
```

SIR represents programming concepts in a human-oriented way.

### Level 4 - A-MIR

Advanced compiler authors may directly emit or transform A-MIR.

### Level 5 - Raw Fission Components

Experts may replace parsers, semantic analyzers, IR layers, architecture
components, optimizers, artifact writers, JIT systems, and runtime components.

## 7. Brainfuck Acceptance Requirement

Brainfuck shall be used as the primary beginner-language acceptance test.

A Brainfuck implementation should resemble an ordinary ArcoBASIC library
extension.

```basic
Language = Fission.Language("Brainfuck")

Language.Extension(".bf")

Language.Symbol(">", "MoveRight")
Language.Symbol("<", "MoveLeft")
Language.Symbol("+", "Increment")
Language.Symbol("-", "Decrement")
Language.Symbol(".", "Output")
Language.Symbol(",", "Input")
Language.Pair("[", "]", "Loop")
```

Semantics might resemble:

```basic
Language.On("Increment", FUNCTION(Node, Context)

    Cell = Context.Index(
        Context.Var("Memory"),
        Context.Var("Pointer")
    )

    Context.Assign(
        Cell,
        Context.Add(Cell, 1)
    )

END FUNCTION)
```

The language package must not contain x86-64 code, ELF generation, System V ABI
knowledge, or Linux process startup code.

If the installed Linux pipeline is complete:

```sh
fission build hello.bf --target linux-x86_64
```

must be sufficient.

## 8. Language Simplicity Acceptance Test

Fission Language Kit is successful only if all of the following can be
implemented without directly using A-MIR.

### Test A - Brainfuck

Operations:

```text
>
<
+
-
.
,
[
]
```

### Test B - Tiny BASIC-like language

Features:

```text
variables
numeric expressions
PRINT
IF
WHILE
functions
```

### Test C - Tiny expression DSL

Example:

```text
damage = strength * 2 + weaponBonus
```

The author must not need to write lexer infrastructure, parser engine
infrastructure, machine code, binary writers, or runtime startup.

## 9. Fission Core

Fission Core shall deliberately remain small.

Its responsibilities are:

```text
Component Registry
Component Discovery
Pipeline Resolver
Artifact Transport
Capability Negotiation
Version Negotiation
Diagnostics
Compilation Requests
Pipeline Execution
Inspection
CLI/API
```

Fission Core must not intrinsically know ArcoBASIC syntax, Brainfuck syntax,
x86-64 encoding, System V ABI rules, Linux ELF layout, UEFI PE layout, or
Arcology AEX layout. Those belong to components.

## 10. Component Model

Every major compiler stage shall be replaceable.

Component classes include:

```text
Language
Preprocessor
Lexer
Parser
Semantic Analyzer
Language Lowerer
IR
IR Validator
IR Transformer
Optimizer
Target Lowerer
Architecture
Instruction Selector
Register Allocator
ABI
Runtime
Linker
Artifact Writer
Bytecode Engine
VM
JIT
Inspector
Diagnostic Provider
```

Not every implementation must expose each microscopic sub-stage independently on
day one. The architecture must permit future separation.

## 11. ArcoBASIC Library Model

Compiler extensions shall be created using normal ArcoBASIC libraries.

Examples:

```basic
Language = Fission.Language("Brainfuck")
Pass = Fission.Pass("ConstantFold")
Target = Fission.Target("linux-x86_64")
ABI = Fission.ABI("sysv-x86_64")
Runtime = Fission.Runtime("linux")
Writer = Fission.ArtifactWriter("elf64")
```

Registration:

```basic
Fission.Register(Language)
```

or equivalent package-based registration.

## 12. Language Package Example

Conceptual package:

```text
languages/
    brainfuck/
        brainfuck.abas
        tests/
        examples/
```

Inside:

```basic
PACKAGE Brainfuck

FUNCTION Register(FissionHost)

    Language = FissionHost.Language("Brainfuck")

    Language.Extension(".bf")

    Language.Symbol(">", "MoveRight")
    Language.Symbol("<", "MoveLeft")
    Language.Symbol("+", "Increment")
    Language.Symbol("-", "Decrement")

    ...

    FissionHost.Register(Language)

END FUNCTION

END PACKAGE
```

No separate language manifest is required beyond normal Arcology package
metadata unless useful.

## 13. Language Frontend Responsibilities

A language package may define file extensions, source identification, lexical
rules, tokens, grammar, syntax, semantic behavior, language-specific validation,
language-specific diagnostics, pretty printing, formatting metadata, editor
metadata, and syntax highlighting metadata.

It does not define normal output targets.

## 14. Automatic Tooling Generation

Fission should eventually derive tooling metadata from language packages.

Possible generated support:

```text
syntax highlighting
formatter support
AST inspection
language documentation skeleton
ARCADE editor metadata
debugger metadata
language server metadata
completion metadata
syntax error diagnostics
```

Goal:

```text
one language definition
        |
        +--> compiler
        +--> editor support
        +--> formatting
        +--> syntax metadata
        +--> debugger hooks
```

## 15. Intermediate Representation Strategy

The pipeline shall provide at least two conceptual IR layers.

### 15.1 SIR

SIR is beginner-friendly and semantic.

Examples:

```text
VariableCreate
VariableRead
VariableWrite
Call
Return
Add
Subtract
Multiply
Loop
Branch
ArrayRead
ArrayWrite
```

Language authors normally stop here.

### 15.2 A-MIR

A-MIR remains the canonical lower compiler representation.

The current compiler already exposes A-MIR as a formal reveal stage and uses it
across hosted bytecode and native paths.

A-MIR shall become versioned, documented, serializable, validated,
language-neutral, and stable enough for independent components.

## 16. Pipeline Architecture

Canonical pipeline:

```text
Source
  |
  v
Language Extension
  |
  v
Syntax Model
  |
  v
Semantic Construction
  |
  v
SIR
  |
  v
SIR -> A-MIR
  |
  v
A-MIR
  |
  +------ optimization passes
  |
  v
Target Lowering
  |
  v
Architecture
  |
  v
ABI
  |
  v
Runtime Binding
  |
  v
Artifact Writer
  |
  v
Executable / Firmware / Web / Bytecode
```

## 17. Language Independence Requirement

The architecture must guarantee:

```text
ArcoBASIC ---\
Brainfuck ----\
TinyBasic -----+--> SIR/A-MIR --> targets
OtherLang ----/
```

Adding a new frontend must not require modifying x86-64, Linux runtime, ELF
writer, Web output, Arcology runtime, or AEX writer.

## 18. Target Independence Requirement

Likewise:

```text
              /--> Linux ELF
Language --> IR --> Web
              \--> Arcology AEX
```

Adding a new target must not require modifying every language frontend.

## 19. Target Recipes

A target shall be composition, not a monolithic backend.

Example conceptual ArcoBASIC:

```basic
Target = Fission.Target("linux-x86_64")

Target.Use("architecture.x86_64")
Target.Use("abi.sysv-x86_64")
Target.Use("runtime.linux")
Target.Use("artifact.elf64")

Target.Pass("validate.amir")
Target.Pass("optimize.constants")

Fission.Register(Target)
```

No target-specific switch statement belongs in Fission Core.

## 20. Linux Priority

Linux is the canonical development and native target.

The existing Fission implementation already supports direct Linux x86-64 output
as an experimental native path and also provides hosted Linux capsule execution.

The substrate rewrite shall promote Linux native support into a first-class
composed target:

```text
A-MIR
  |
  v
architecture.x86_64
  |
  v
abi.sysv-x86_64
  |
  v
runtime.linux
  |
  v
artifact.elf64
```

## 21. Web Priority

Web is the next priority after Linux.

The current compiler already supports web capsules through Emscripten and
browser-hosted runtime behavior.

Initially:

```text
runtime.web
artifact.web.emscripten
```

may wrap the existing implementation.

Later:

```text
architecture.wasm32
abi.wasm
artifact.wasm
```

may replace or supplement it.

Language frontends shall not change when this migration occurs.

## 22. Windows Priority

Windows support is compatibility work. It shall not dictate architecture.

The current Windows cross-build implementation may remain temporarily behind a
bridge component.

Windows parity shall not block Linux self-hosting, Fission substrate architecture,
Web support, or Arcology native support.

## 23. macOS

macOS is unsupported.

No agent shall add Apple-specific CI, Apple packaging, Mach-O support, Apple
framework integration, or macOS abstractions unless this RFC is explicitly
amended.

## 24. Arcology OS

Arcology OS shall be supported compositionally.

Arcology OS is not an architecture.

Correct:

```text
architecture.x86_64
abi.arcology-x86_64
runtime.arcology
artifact.aex
```

Future:

```text
architecture.arm64
abi.arcology-arm64
runtime.arcology
artifact.aex
```

Incorrect:

```text
backend.arcology
```

if that backend permanently entangles OS, architecture, ABI, and artifact format.

## 25. Architecture Components

Initial:

```text
architecture.x86_64
```

Future:

```text
architecture.arm64
architecture.riscv64
architecture.68000
architecture.6502
architecture.z80
```

Architecture modules own instruction encoding, registers, machine semantics,
architecture-specific lowering, relocation kinds, and CPU features.

They do not own Linux or Arcology OS.

## 26. ABI Components

Examples:

```text
abi.sysv-x86_64
abi.uefi-x86_64
abi.microsoft-x64
abi.arcology-x86_64
```

ABI owns argument placement, return placement, stack alignment, preserved
registers, volatile registers, external call rules, and calling convention
details.

## 27. Runtime Components

Examples:

```text
runtime.hosted
runtime.linux
runtime.web
runtime.arcology
runtime.none
```

Runtime capabilities may include:

```text
FILES
DIRECTORIES
PROCESS
NETWORK
THREADS
TIMERS
GUI
GRAPHICS
HOST_CALLS
COMPILE_RUN
```

Capability negotiation must occur before code generation.

## 28. Artifact Writers

Examples:

```text
artifact.arcof
artifact.elf64
artifact.pe32plus
artifact.web
artifact.aex
artifact.raw
```

Artifact format must remain separate from architecture.

## 29. Bytecode

Hosted bytecode remains first-class.

The current compiler supports textual `.arcof`, binary bytecode, execution,
native embedded capsules, bytecode preparation, optimization, and runtime
function calls.

These responsibilities shall be separated into components:

```text
bytecode.lower
bytecode.optimize
bytecode.serialize.text
bytecode.serialize.binary
bytecode.vm
bytecode.jit.x86_64
```

## 30. Optimization

Optimization shall consist of composable passes.

Examples:

```text
validate.amir
optimize.constant-fold
optimize.dead-code
optimize.branch
optimize.local-fusion
optimize.loop
```

The existing implementation already contains bytecode fusion and numeric
hot-loop optimization logic that should be extracted rather than transliterated
into one subsystem.

## 31. Experimental Optimization

The substrate should make compiler research easy.

Example:

```basic
Optimizer = Fission.Pass("MendelOptimizer")

Optimizer.Input("IR.AMIR")
Optimizer.Output("IR.AMIR")

Optimizer.Run(FUNCTION(Module)
    ...
    RETURN Module
END FUNCTION)

Fission.Register(Optimizer)
```

Genetic/evolutionary optimization is explicitly within the intended design
space.

## 32. Component Contract

Each component shall expose metadata equivalent to:

```text
Name
Version
Class
Consumes
Produces
Capabilities
Requirements
Compatibility
Configuration
```

Example:

```text
Name:
    language.brainfuck

Class:
    Language

Consumes:
    SourceText

Produces:
    SIR

Requires:
    Fission.LanguageAPI >= 1
```

## 33. Typed Artifacts

Components communicate using explicit artifact types.

Examples:

```text
SourceText
TokenStream
LanguageAST
SIR
AMIR
Bytecode
MachineIR
MachineCode
Symbols
Relocations
RuntimeRequirements
ExecutableImage
FirmwareImage
WebArtifact
Diagnostics
```

Global hidden compiler state should be minimized.

## 34. Pipeline Resolution

The pipeline resolver shall find a valid transformation route.

Example request:

```text
Input:
    Source.Brainfuck

Output:
    Executable.Linux.X86_64
```

Resolver:

```text
language.brainfuck
        |
       SIR
        |
    sir.to.amir
        |
      A-MIR
        |
architecture.x86_64
        |
abi.sysv-x86_64
        |
runtime.linux
        |
artifact.elf64
```

The language package does not choose these output components.

## 35. Capability Negotiation

Pipeline components shall declare capabilities.

Example failure:

```text
TARGET CAPABILITY FAILURE

Language:
    ExampleSystemsLanguage

Program requires:
    PORT_IO

Target:
    web

Available:
    GUI
    NETWORK_WEB
    FILES_VIRTUAL

Missing:
    PORT_IO
```

Failure should occur before invalid machine generation.

## 36. Introspection

The existing `reveal` philosophy must survive. The current compiler already
exposes AST, A-MIR, bytecode, calling convention, x86-64, and pretty-source
inspection stages.

New forms should include:

```text
fission reveal FILE at AST
fission reveal FILE at SIR
fission reveal FILE at AMIR
fission reveal FILE at BYTECODE
fission reveal FILE at MACHINE_IR
fission reveal FILE at MACHINE_CODE
fission reveal FILE at PIPELINE
fission reveal FILE at COMPONENTS
```

## 37. Library First

The CLI is not the compiler. The compiler is an ArcoBASIC library.

Conceptual:

```basic
Compiler = Fission.Compiler()

Request = Fission.Request()

Request.Source = Source
Request.Language = "brainfuck"
Request.Target = "linux-x86_64"

Result = Compiler.Compile(Request)
```

ARCADE, Fissure, Rivet, ArcoFlow, and future tools should use this API directly
where appropriate.

## 38. Fission Self-Assembly

Fission itself should use the same component APIs offered to external developers.

Conceptually:

```basic
Fission.Register(ArcoBasicLanguage())
Fission.Register(SIR())
Fission.Register(AMIR())
Fission.Register(X86_64())
Fission.Register(SysV())
Fission.Register(LinuxRuntime())
Fission.Register(ELF64())
```

First-party components shall not receive hidden privileged integration mechanisms
merely because they ship with Fission.

## 39. Bootstrap

Migration generations:

```text
Generation 0:
    ArcoFission C++

Generation 1:
    Legacy C++ ArcoFission compiles Fission in ArcoBASIC.
    Legacy components may temporarily remain bridged.

Generation 2:
    Generation 1 compiles the Fission source tree.

Generation 3:
    Generation 2 recompiles the same source tree.
    Generation 2 and Generation 3 must behave equivalently.
```

## 40. Legacy Compiler Role

Legacy ArcoFission is the bootstrap compiler, reference implementation,
regression oracle, and temporary compatibility provider.

It is not the architectural model.

Do not transliterate its monolithic structure.

The current C++ source contains frontend handling, A-MIR lowering, bytecode
construction, optimization, execution, JIT work, ABI logic, machine code
generation, and target-specific build behavior in one large implementation
surface. That structure shall be decomposed.

## 41. Equivalence Testing

Each migrated component shall be compared against legacy behavior where
equivalent behavior exists.

Examples:

```text
Parser:
    canonical AST equivalence

A-MIR:
    canonical A-MIR equivalence

Bytecode:
    bytecode equivalence
    execution equivalence

Native:
    stdout
    stderr
    exit code
    side effects
    fault behavior

Artifact:
    format validity
    entrypoint
    sections
    relocations
    execution/boot
```

## 42. Fissure

Fissure is the migration regression authority.

Suggested test layout:

```text
tests/
    language/
        arcobasic/
        brainfuck-reference/
    sir/
    amir/
    optimizer/
    bytecode/
    architecture/
        x86_64/
    abi/
        sysv/
        uefi/
    runtime/
        linux/
        web/
    artifact/
        elf64/
        pe32plus/
        aex/
    bootstrap/
    equivalence/
```

## 43. Rivet

Rivet owns build orchestration.

Fission must not reinvent Rivet.

Rivet shall eventually perform:

```text
build legacy bootstrap
build Fission G1
run Fissure
build Fission G2
run Fissure
build Fission G3
compare
package components
```

## 44. Recommended Repository Structure

```text
fission/
    core/
        compiler.abas
        registry.abas
        component.abas
        artifact.abas
        pipeline.abas
        resolver.abas
        diagnostics.abas
        capability.abas
    language/
        api/
        arcobasic/
    sir/
        model.abas
        builder.abas
        validate.abas
        lower_amir.abas
    amir/
        model.abas
        validate.abas
        serialize.abas
        render.abas
    optimizer/
        common/
        amir/
        bytecode/
    bytecode/
        model.abas
        lower.abas
        vm.abas
        serializer.abas
        jit/
    architecture/
        x86_64/
    abi/
        sysv_x86_64/
        uefi_x86_64/
    runtime/
        hosted/
        linux/
        web/
        none/
        arcology/
    artifact/
        arcof/
        elf64/
        pe32plus/
        web/
        aex/
    target/
        linux_x86_64.abas
        uefi_x86_64.abas
        web.abas
        arcology_x86_64.abas
    bootstrap/
        legacy/
    cli/
```

## 45. Agent Progress Ledger

Required:

```text
.agents/FISSION_SUBSTRATE_PROGRESS.md
```

Every coding agent must read it before work. Every coding agent must update it
before completion.

It must contain:

```text
Current milestone
Current work package
Completed components
In-progress components
Legacy bridges
Known failures
Regression status
Bootstrap generation
Files changed
Architectural decisions
Next recommended work
```

## 46. Agent Start Procedure

Before modifying code:

```text
1. Read this RFC.
2. Read the progress ledger.
3. Run current relevant tests.
4. Identify the component boundary.
5. Identify legacy oracle behavior.
6. Confirm no existing component already provides the required function.
7. Make the smallest substrate-compatible change.
```

## 47. Agent Completion Procedure

Before declaring work complete:

```text
1. Run component tests.
2. Run regression/equivalence tests.
3. Run integration tests.
4. Update the progress ledger.
5. Record temporary bridges.
6. Record unresolved differences.
7. Verify no hard-coded target logic entered Fission Core.
8. Verify first-party code remains ArcoBASIC unless documented otherwise.
```

## 48. Forbidden Agent Behaviors

Agents must not:

```text
translate fission.cpp line-by-line;
build one giant fission.abas;
invent a compiler-definition DSL;
invent a target-definition DSL;
require language authors to write backend code;
put Linux conditionals inside ordinary language packages;
hard-code target ladders in Fission Core;
combine architecture and OS;
combine ABI and artifact format;
make new languages depend on x86-64;
add macOS support;
block Linux work for Windows parity;
silently change ArcoBASIC semantics;
remove legacy code before equivalence;
skip regression testing;
```

## 49. Migration Work Packages

### WP-000 - Inventory

Document all legacy compiler responsibilities.

### WP-001 - Core Component System

Implement Component, Registry, Artifact, Pipeline, Resolver, CompileRequest,
CompileResult, Diagnostic, and Capabilities in ArcoBASIC.

### WP-002 - Language Construction API

Implement beginner-facing APIs:

```text
Fission.Language
symbols
tokens
grammar rules
statement definitions
expression definitions
semantic callbacks
```

This work package is critical. Do not begin language migration assuming every
frontend author writes raw parser classes.

### WP-003 - SIR

Implement semantic construction model.

The Brainfuck reference frontend must compile through this layer.

### WP-004 - A-MIR

Port and formalize A-MIR model, validation, serialization, rendering, and
versioning.

### WP-005 - ArcoBASIC Frontend

Port preprocessor, lexer, parser, canonical AST, and semantic handling.

Use the new language component model wherever practical.

### WP-006 - ArcoBASIC -> SIR/A-MIR

Port semantics and lowering.

### WP-007 - Bytecode

Port lowering, model, serialization, VM, and execution.

### WP-008 - Optimization Passes

Extract legacy optimizations into components.

### WP-009 - x86-64

Port architecture logic.

### WP-010 - SysV ABI

Implement Linux ABI component.

### WP-011 - Linux Runtime

Extract Linux runtime capabilities.

### WP-012 - ELF64

Implement artifact writer.

### WP-013 - Linux Native Target

Compose:

```text
A-MIR
x86-64
SysV
Linux Runtime
ELF64
```

### WP-014 - Brainfuck Reference Language

Implement Brainfuck using only public beginner-facing Fission language APIs.

This is a required architecture test. Brainfuck code must contain no
Linux/x86/ELF-specific implementation.

### WP-015 - Target Independence Test

Without modifying the Brainfuck frontend:

```text
compile Brainfuck -> Linux
```

Then when Web is available:

```text
compile same Brainfuck frontend -> Web
```

No language changes allowed.

### WP-016 - Self-Hosting

Compile Fission G1 -> G2 -> G3.

### WP-017 - UEFI

Extract UEFI ABI, `runtime.none`, and PE32+ using the shared x86-64 architecture
implementation.

### WP-018 - Web

Wrap current Web pipeline, then progressively replace legacy dependencies.

### WP-019 - Arcology OS

Implement:

```text
abi.arcology-x86_64
runtime.arcology
artifact.aex
target.arcology-x86_64
```

### WP-020 - Second Beginner Language Test

Implement a small BASIC-like or command language using only public
language-construction APIs.

### WP-021 - Legacy Retirement

Legacy C++ ArcoFission leaves the normal toolchain only after self-host and
required targets pass Fissure.

## 50. Language Author Experience Requirement

Desired workflow:

```text
Create language.abas
        |
        v
Use Fission.Language
        |
        v
Define syntax
        |
        v
Define semantics
        |
        v
Register language
        |
        v
Compile program
```

Example:

```sh
fission build program.mynewlanguage --target linux-x86_64
```

The language author should not need to create a compiler executable separately.

## 51. Target Inheritance Requirement

This shall be a formal conformance test.

### TARGET-INHERITANCE-001

Given:

```text
Language L -> A-MIR
A-MIR -> Target T
```

then:

```text
L -> T
```

must be available without language-specific target implementation.

Failure indicates improper coupling.

## 52. Frontend Isolation Requirement

### FRONTEND-ISOLATION-001

A new language frontend must be implementable without modifications to Fission
Core, x86-64 module, Linux runtime, ELF writer, Web module, AEX writer, or ABI
modules unless the language requires genuinely new common semantics.

## 53. Backend Isolation Requirement

### OUTPUT-ISOLATION-001

Adding a new target must not require modifications to every language frontend.

If a new architecture or output target requires edits to Brainfuck, Tiny BASIC,
and ArcoBASIC frontends merely to become visible, the design has failed.

## 54. Ease-of-Use Requirement

### LANGUAGE-EASE-001

A programmer comfortable with ArcoBASIC but unfamiliar with compiler
implementation must be able to create a small compiled language using public
Fission APIs.

### LANGUAGE-EASE-002

Brainfuck must be implementable without directly using A-MIR blocks, machine
registers, ABI definitions, machine code, artifact sections, or relocations.

### LANGUAGE-EASE-003

The reference Brainfuck implementation should remain compact enough to be
understandable as an instructional example.

Readability is more important than minimizing line count.

## 55. ARCADE Integration

Future ARCADE support should expose the exact same APIs visually.

Conceptually:

```text
ARCADE Language Designer
        |
        v
ArcoBASIC language script
        |
        v
Fission.Language
```

ARCADE must not create a competing language-definition format. The visual
designer edits or generates normal ArcoBASIC.

## 56. ArcoFlow Integration

A future visual compiler-construction surface may produce ArcoBASIC or interact
through the same underlying APIs.

No parallel compiler architecture shall be introduced for visual tooling.

## 57. Community Language Packages

Community developers should be able to distribute:

```text
languages/brainfuck
languages/lolcode
languages/mydsl
languages/retro-basic
```

as ordinary Arcology packages.

Installing a language package should make Fission aware of that language.
Installed compatible targets become available automatically.

## 58. Community Compiler Components

Advanced developers may distribute:

```text
optimizer.*
architecture.*
abi.*
runtime.*
artifact.*
target.*
```

through the same component system.

First-party and third-party components use the same public contracts.

## 59. Fission as a Compiler Laboratory

Fission should enable experimentation such as new syntax, new languages, genetic
optimization, experimental IR, new architecture, custom runtime, retro targets,
and special firmware outputs without requiring forks of Fission Core.

## 60. Self-Hosting Definition

Fission is self-hosting when:

```text
Fission source
    |
    v
Fission Generation N
    |
    v
Fission Generation N+1
```

and the resulting compiler passes the required Fissure suite.

One successful generation is insufficient. At minimum, G1 -> G2 -> G3 must
succeed.

## 61. Toolchain Independence

The stronger milestone is reached when a supported Linux environment can build
the normal Arcology development toolchain without requiring the legacy C++
ArcoFission compiler.

Foreign-language compilers may remain optional tools. They cease to be mandatory
ancestors of the normal Fission build.

## 62. First Alpha Acceptance

Fission Substrate Alpha requires:

```text
ArcoBASIC implementation
component registry
pipeline resolver
language construction API
SIR
A-MIR
hosted bytecode
Linux native
x86-64
SysV
Linux runtime
ELF64
Fissure integration
Brainfuck reference language
target inheritance
pipeline introspection
```

## 63. Self-Host Alpha Acceptance

Requires:

```text
Legacy -> G1
G1 -> G2
G2 -> G3

G2 and G3:
    pass Fissure
    compile ArcoBASIC
    compile Brainfuck
    produce Linux native output
```

## 64. Substrate Proof

The substrate is not considered proven merely because ArcoBASIC works.

Required proof:

```text
ArcoBASIC ----\
               \
Brainfuck ------> common IR -> Linux native
```

Both languages use the same downstream pipeline.

Later:

```text
same Brainfuck frontend
    -> Linux
    -> Web
    -> Arcology
```

must work without target-specific language changes.

## 65. Architectural Litmus Tests

Before approving a design:

- Can someone add Brainfuck without understanding ELF? If no, redesign.
- Can Brainfuck gain Web support without modifying Brainfuck? If no, redesign.
- Can a new architecture be added without changing the ArcoBASIC parser? If no,
  redesign.
- Can a new language use the existing Linux backend immediately? If no, redesign.
- Can targets be written as ArcoBASIC components? If no, redesign.
- Can an optimizer be written as ordinary ArcoBASIC? If no, redesign.
- Is there a new compiler-definition DSL? If yes, remove it.

## 66. Prime Developer Promise

The public promise of Fission shall be:

> Teach Fission your language once. Fission handles the machines.

Or more technically:

> Implement the frontend in ArcoBASIC. Installed compatible Fission output
> modules provide the rest of the compiler.

## 67. Prime Architectural Invariant

> Languages describe input semantics. Targets describe output mechanics. Neither
> shall be forced to know the other exists.

## 68. Final Directive to Agents

Do not build a better monolithic ArcoBASIC compiler.

Build a programmable compiler substrate.

Do not make language authors rebuild compiler infrastructure. Do not make new
languages understand Linux. Do not make new languages understand x86-64. Do not
invent another DSL. Do not privilege first-party components with hidden
interfaces.

Everything should be ArcoBASIC. Everything should compose. The legacy compiler is
the bootstrap. The new Fission is the substrate.

The long-term outcome is that creating a new compiled language becomes an
ordinary Arcology programming task rather than a compiler-engineering research
project.

That is the definition of success.
