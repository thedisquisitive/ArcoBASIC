# Rivet
## Arcology Build Orchestration System
### RFC + Coding Agent Packet

**Status:** Draft
**Project:** Arcology
**Component:** Rivet
**Primary Language:** ArcoBASIC
**Initial Platform:** Linux
**Intended Platforms:** Linux, Windows, Arcology OS, and any platform supported by an appropriate toolchain adapter

---

## 1. Summary

**Rivet** is an ArcoBASIC-programmable build orchestration system intended to replace Makefiles, CMake, Meson, Ninja-as-a-user-facing-tool, and similar build-management layers.

Rivet deliberately does **not** invent a new declarative build language.

The build description is ordinary executable ArcoBASIC.

Dependency tracking, incremental builds, caching, parallel scheduling, compiler invocation, artifact tracking, and reproducibility are provided by the Rivet runtime.

The core design rule is:

> **The build file is just ArcoBASIC.**

If a developer wants to enumerate a directory, test a filename extension, add matching files to an array, and build them, that should look like ordinary programming:

```basic
DIM arrFiles AS ARRAY OF STRING

FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension = ".cpp" THEN
        arrFiles.Add(FILE.Path)
    END IF
NEXT

myBuild.Build(arrFiles)
```

No special list language, glob syntax, generator expressions, configuration DSL, or generated intermediate project is required.

---

# 2. Goals

Rivet shall:

1. Make simple build logic genuinely simple.
2. Allow arbitrarily complex build logic without escaping into a second scripting language.
3. Expose why a file, option, dependency, or action exists in the build.
4. Provide deterministic incremental builds.
5. Support parallel execution safely.
6. Support heterogeneous projects and arbitrary toolchains.
7. Allow toolchain adapters, plugins, configuration, and extensions to be written in ArcoBASIC.
8. Avoid generated hidden build projects as part of normal operation.
9. Provide structured build state suitable for CLI, IDE, agent, and graphical inspection.
10. Remain useful for nontraditional build pipelines, including:
   - compilers
   - transpilers
   - game asset pipelines
   - shader compilers
   - ROM builders
   - FPGA tooling
   - VST builds
   - documentation generation
   - code generators
   - firmware builds
   - Arcology-specific package formats

---

# 3. Non-Goals

Rivet is not intended to:

- clone CMake syntax;
- clone Makefile syntax;
- become another declarative configuration language;
- require Ninja, Make, MSBuild, or another execution backend;
- hide compiler and linker behavior from the developer;
- impose one dependency manager;
- require a particular compiler;
- require all build logic to be expressible through predefined Rivet primitives.

Raw process execution must always remain available as an escape hatch.

---

# 4. The Five Problems Rivet Must Solve

## 4.1 Problem One: Build Languages Make Ordinary Programming Difficult

Many build systems expose a domain-specific language that becomes awkward as soon as the developer wants ordinary control flow.

This should not be difficult:

```basic
FOR FILE IN DIRECTORY("src")
    IF FILE.Extension = ".cpp" THEN
        Sources.Add(FILE)
    END IF
NEXT
```

Nor should platform-specific logic:

```basic
IF TARGET.OS = "windows" THEN
    Sources.Add("src/platform/windows.cpp")
ELSE IF TARGET.OS = "linux" THEN
    Sources.Add("src/platform/linux.cpp")
END IF
```

Rivet shall treat the following as ordinary ArcoBASIC concepts:

- arrays
- maps
- strings
- paths
- loops
- functions
- classes
- conditions
- environment variables
- subprocesses
- file operations
- user-defined abstractions

There shall be no separate "build programming" mental model.

---

## 4.2 Problem Two: Existing Build Systems Hide What Is Actually Happening

A common modern build chain is effectively:

```text
Build Configuration
        |
        v
Generator
        |
        v
Generated Build Project
        |
        v
Execution Backend
        |
        v
Compiler / Linker / Custom Tools
```

When something fails, the developer may be debugging a generated command that originated several abstraction layers earlier.

Rivet shall make build state inspectable.

Example:

```basic
myBuild = BUILD.New("Fission")

myBuild.Compiler = COMPILER.Clang()
myBuild.Linker = LINKER.LLD()

myBuild.Sources = arrFiles
myBuild.Include.Add("include")
myBuild.Define.Add("ARC_TARGET_LINUX")

myBuild.Output = "bin/fission"
```

The developer must be able to ask:

```basic
myBuild.Explain()
```

or from the CLI:

```text
rivet explain
```

Possible output:

```text
TARGET: Fission
TYPE: Executable

SOURCE DISCOVERY
  src/main.cpp
    Added by build.abas:14
    Rule: extension = ".cpp"

  src/parser.cpp
    Added by build.abas:14

COMPILER
  clang++ 19.1

FLAGS
  -std=c++23
    Added by compiler profile "cpp-modern"

  -O2
    Added by profile "Release"

LINKER
  lld

OUTPUT
  build/linux-x64/release/fission
```

Rivet must preserve provenance metadata for meaningful build state wherever practical.

---

## 4.3 Problem Three: Configuration Explosion

Traditional build systems frequently grow combinations such as:

```text
Debug
Release
RelWithDebInfo
LinuxDebug
LinuxRelease
WindowsDebug
WindowsRelease
ARM64Debug
ARM64Release
SanitizedDebug
SanitizedLinuxDebug
...
```

Rivet shall model configuration as composable dimensions and traits.

Example:

```basic
myBuild.Configurations.Add("debug")
myBuild.Configurations.Add("release")

myBuild.Targets.Add("linux-x64")
myBuild.Targets.Add("windows-x64")
myBuild.Targets.Add("aos-x64")
```

Rules remain ordinary ArcoBASIC:

```basic
IF BUILD.Configuration = "debug" THEN
    myBuild.Compiler.Optimize = 0
    myBuild.Compiler.DebugSymbols = TRUE
END IF

IF BUILD.Target.OS = "aos" THEN
    myBuild.Define.Add("ARC_NATIVE")
    myBuild.Sources.AddTree("src/platform/aos")
END IF
```

Profiles should be composable:

```basic
PROFILE Release
    Optimization = "speed"
    DebugSymbols = FALSE
END PROFILE

PROFILE Sanitized
    Sanitizer = ["address", "undefined"]
END PROFILE
```

Invocation:

```text
rivet build --profile Release+Sanitized --target linux-x64
```

The goal is to prevent developers from creating a unique named configuration for every combination of independent properties.

---

## 4.4 Problem Four: Dependency Types Are Commonly Conflated

Rivet shall distinguish dependency semantics.

At minimum:

- source dependency
- header/include dependency
- build-order dependency
- generated-file dependency
- target dependency
- static library dependency
- shared library dependency
- package dependency
- tool dependency
- runtime packaging dependency
- explicit external file dependency

Example:

```basic
app.RequiresLibrary(core)
app.RequiresBuild(generator)
app.RequiresPackage("SDL3")
```

Generated artifact:

```basic
generated = GENERATE.File(
    Input := "resources/icons.svg",
    Output := "generated/icons.abas",
    Using := "tools/iconcompiler"
)

app.Sources.Add(generated)
```

Manual dependency declaration must remain easy:

```basic
app.DependsOn("assets/shaders/*.shader")
```

The graph should be queryable:

```text
rivet graph
rivet why generated/icons.abas
```

---

## 4.5 Problem Five: Generated Build Systems Create a Hidden Second Project

A CMake-style workflow can effectively contain:

```text
Project Written by Developer
            |
            v
Generated Build Project
            |
            v
Actual Build Execution
```

Rivet shall execute its build graph directly.

Primary architecture:

```text
build.abas
    |
    v
ArcoBASIC VM
    |
    v
Rivet Build Graph
    |
    v
Rivet Scheduler
    |
    +--> Compiler
    +--> Linker
    +--> Code Generator
    +--> Asset Tool
    +--> Arbitrary Process
```

Rivet may export compatibility formats:

```basic
BUILD.Export("ninja")
BUILD.Export("compile_commands.json")
BUILD.Export("visualstudio")
```

However, those exports are compatibility products.

They are not Rivet's internal architecture.

---

# 5. Additional Design Principle: Build Systems Should Be Inspectable Applications

Rivet should behave like an interactive developer tool rather than a dumb command launcher.

Example:

```text
> rivet build

Fission
──────────────────────────────────────
Target       linux-x64
Profile      debug
Compiler     clang++ 19.1
Sources      147
Cached       139
Rebuilding     8
──────────────────────────────────────
[███████████████████░░░░] 81%
```

Inspection:

```text
> rivet inspect src/parser.cpp

src/parser.cpp
  Source modified:     YES
  Last compiled:       23:52:14
  Compile fingerprint: CHANGED

Changed inputs:
  src/parser.cpp

Unchanged:
  compiler
  flags
  includes
  dependencies
```

This structured state should eventually be consumable by:

- ARCADE
- graphical build viewers
- Arcology agents
- CI systems
- test systems such as Fissure
- editor integrations
- remote Arcology tooling

---

# 6. ArcoBASIC-Native Object Model

The primary API shall feel like a normal ArcoBASIC library.

Example:

```basic
IMPORT Build

PROJECT App = BUILD.Project("MyApp")

App.Type = BUILD.EXECUTABLE
App.Language = "C++"

FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension = ".cpp" THEN
        App.Sources.Add(FILE)
    END IF
NEXT

App.Includes.Add("include")

App.Compiler.Standard = "c++23"
App.Compiler.Warnings = "strict"

IF TARGET.OS = "windows" THEN
    App.Libraries.Add("user32")
END IF

App.Build()
```

---

# 7. Convenience Syntax Must Not Become a Second Language

Rivet may expose concise ArcoBASIC helpers:

```basic
PROJECT "MyApp"

SOURCE "src/**/*.cpp"
INCLUDE "include"

CXX.Standard = 23

BUILD
```

These helpers must map onto the same Rivet object model.

The developer must never reach a point where the concise syntax becomes insufficient and the entire build must be rewritten in another language.

The progression should be:

```text
Convenience API
      |
      v
Normal ArcoBASIC API
      |
      v
Custom ArcoBASIC logic
      |
      v
Raw process execution
```

All layers remain inside ArcoBASIC.

---

# 8. File Discovery

File discovery shall be a first-class API.

Examples:

```basic
FOR FILE IN DIRECTORY("src")
    PRINT FILE.Path
NEXT
```

Recursive:

```basic
FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension = ".cpp" THEN
        App.Sources.Add(FILE)
    END IF
NEXT
```

Convenience:

```basic
App.Sources.AddTree("src", Extension := ".cpp")
```

Glob support may also exist:

```basic
App.Sources.AddGlob("src/**/*.cpp")
```

Glob syntax is optional convenience.

It must never be the only practical method of file selection.

---

# 9. Paths

Rivet shall use a structured path abstraction internally.

A path should expose properties such as:

```text
Path
Name
Stem
Extension
Directory
Absolute
RelativeTo()
Exists
IsFile
IsDirectory
ModifiedTime
Size
Hash
```

Example:

```basic
FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension IN [".cpp", ".c"] THEN
        Sources.Add(FILE.Path)
    END IF
NEXT
```

Rivet should normalize platform path differences internally.

---

# 10. Targets

A project may define multiple targets.

```basic
core = BUILD.StaticLibrary("core")
app = BUILD.Executable("arcade")
tests = BUILD.Executable("arcade-tests")
```

Relationships:

```basic
app.RequiresLibrary(core)
tests.RequiresLibrary(core)
```

Targets may include:

- executable
- static library
- shared library
- plugin
- test executable
- generated artifact
- asset bundle
- package
- firmware image
- ROM image
- Arcology Capsule
- custom target

Custom types may be added through adapters.

---

# 11. Build Actions

Internally, Rivet should normalize work into **Build Actions**.

Example conceptual structure:

```text
BuildAction
{
    ID
    Type
    Inputs[]
    Outputs[]
    Tool
    Arguments[]
    Environment
    WorkingDirectory
    Dependencies[]
    Fingerprint
    CachePolicy
    Provenance
}
```

Examples of build actions:

- CompileAction
- LinkAction
- GenerateAction
- CopyAction
- PackageAction
- RunToolAction
- TestAction
- CustomAction

This normalized action layer drives:

- dependency ordering
- incremental checks
- caching
- concurrency
- explanation
- graph visualization

---

# 12. Incremental Builds

Rivet should not rely solely on timestamp comparison.

Each build action should have a fingerprint derived from relevant state.

Conceptually:

```text
Action Type
+
Input Content Hashes
+
Tool Binary Identity or Hash
+
Arguments
+
Relevant Environment
+
Target
+
Profile
+
Adapter Version
+
Explicit Dependencies
```

If the fingerprint and required outputs match a cached successful action, the action may be skipped.

Example:

```text
CompileAction parser.cpp

Previous fingerprint:
  A91C...

Current fingerprint:
  A91C...

Result:
  CACHE HIT
```

Changed compiler options:

```text
Previous fingerprint:
  A91C...

Current fingerprint:
  F02D...

Changed:
  -O0 -> -O2

Result:
  REBUILD
```

---

# 13. Cache Design

Rivet should support:

- local action cache
- local artifact cache
- project cache
- shared cache
- optional remote cache in the future

A cached action must be invalidated when any fingerprinted dependency changes.

Suggested cache object:

```text
CacheEntry
{
    ActionFingerprint
    OutputArtifacts[]
    OutputHashes[]
    Timestamp
    ToolchainIdentity
    Metadata
}
```

Cache state must be inspectable:

```text
rivet cache status
rivet cache explain src/parser.cpp
rivet cache clear
```

---

# 14. Parallel Build Scheduling

Rivet should construct a directed acyclic graph where possible.

Independent actions may execute concurrently.

Example:

```text
              main.cpp compile ----\
                                    \
parser.cpp compile -------------------> link ---> executable
                                    /
lexer.cpp compile ------------------/
```

Scheduler requirements:

- bounded parallelism;
- dependency correctness;
- failure propagation;
- deterministic result graph;
- cancellation;
- structured logs;
- stdout/stderr capture;
- no concurrent writes to the same declared artifact.

CLI:

```text
rivet build --jobs 8
```

Default job count may be based on available logical CPU cores, system load, or configurable policy.

---

# 15. The `why` Command

`why` is a core Rivet capability, not a debugging afterthought.

The user must be able to ask why practically any meaningful build property exists.

Examples:

```text
rivet why parser.cpp
rivet why SDL3
rivet why -pthread
rivet why generated/icons.abas
rivet why rebuild src/parser.cpp
```

Example:

```text
> rivet why -pthread

Flag: -pthread

Introduced by:
  linux-native compiler profile

Reason:
  PROJECT.Threading = TRUE

Defined:
  build.abas:41
```

Example:

```text
> rivet why rebuild src/parser.cpp

src/parser.cpp was rebuilt because:

  include/parser.hpp changed
       |
       v
  dependency fingerprint changed
       |
       v
  compile action invalidated
```

Possible supported query classes:

```text
rivet why source <file>
rivet why flag <flag>
rivet why library <name>
rivet why package <name>
rivet why target <name>
rivet why rebuild <file>
rivet why action <id>
```

Rivet should also attempt convenient inference:

```text
rivet why -pthread
```

instead of always requiring the explicit category.

---

# 16. Explain

`explain` describes current build structure.

```text
rivet explain
rivet explain arcade
rivet explain src/parser.cpp
```

`why` answers causality.

`explain` answers structure and current state.

---

# 17. Graph

Rivet shall expose its dependency graph.

CLI:

```text
rivet graph
rivet graph arcade
rivet graph parser.cpp
```

Possible output targets:

```text
rivet graph --format text
rivet graph --format dot
rivet graph --format json
```

The graph representation should be suitable for later ARCADE visualization.

---

# 18. Dry Run

```text
rivet build --dry-run
```

The dry run shall show the actions that would execute without executing them.

Example:

```text
WOULD RUN
  [1] clang++ src/parser.cpp -> .rivet/obj/parser.o
      Reason: source changed

SKIP
  [2] clang++ src/lexer.cpp -> .rivet/obj/lexer.o
      Reason: cache hit

WOULD RUN
  [3] lld ...
      Reason: parser.o will change
```

---

# 19. Toolchain Adapters

Rivet itself must not hard-code a permanent list of supported programming languages.

Toolchains are adapters.

Initial likely adapters:

```text
C
C++
ArcoBASIC / Fission
Rust
Go
.NET / C#
Java
Assembly
Shader toolchains
```

However, the architecture must allow arbitrary additional adapters.

All Rivet adapters, plugins, add-ons, configuration modules, and execution extensions should be implementable in ArcoBASIC.

Example conceptual API:

```basic
CLASS MyCompiler EXTENDS Build.CompilerAdapter

    FUNCTION Detect()
        ...
    END FUNCTION

    FUNCTION Compile(Action)
        ...
    END FUNCTION

    FUNCTION Link(Action)
        ...
    END FUNCTION

END CLASS
```

A custom adapter might support:

- a retro compiler;
- a proprietary SDK;
- a console homebrew toolchain;
- a shader compiler;
- an FPGA synthesis tool;
- an embedded assembler;
- an asset conversion program.

Rivet should not need recompilation to support these.

---

# 20. Toolchain Detection

Adapters may expose discovery.

Example:

```text
rivet toolchains
```

Possible result:

```text
C++
  clang++ 19.1.4
  gcc 15.2.0

ArcoBASIC
  Fission 0.8-dev

Rust
  rustc 1.xx
```

A build script may request:

```basic
App.Compiler = COMPILER.Find("clang++")
```

Or:

```basic
App.Compiler = COMPILER.Require(
    Family := "clang",
    MinimumVersion := "19"
)
```

Rivet must fail clearly when requirements cannot be satisfied.

---

# 21. Raw Process Escape Hatch

No build must become impossible merely because Rivet lacks a native concept for a tool.

Example:

```basic
result = PROCESS.Run(
    "watcom.exe",
    args
)

IF result.ExitCode <> 0 THEN
    ERROR "Watcom compilation failed."
END IF
```

Preferably, raw processes may also be promoted into tracked actions:

```basic
action = BUILD.ProcessAction(
    Tool := "watcom.exe",
    Arguments := args,
    Inputs := Sources,
    Outputs := ["program.exe"]
)

BUILD.Add(action)
```

This allows arbitrary external tooling while preserving dependency tracking and caching.

---

# 22. Generated Files

Generated files must be ordinary graph artifacts.

Example:

```basic
generated = GENERATE.File(
    Input := "resources/icons.svg",
    Output := "generated/icons.abas",
    Using := "tools/iconcompiler"
)

App.Sources.Add(generated)
```

Rivet understands:

```text
icons.svg
   |
   v
iconcompiler
   |
   v
icons.abas
   |
   v
Fission compiler
   |
   v
Application
```

The developer should not manually implement ordering logic.

---

# 23. Dependency Discovery

Adapters may provide dependency discovery.

For C/C++ this may use:

- compiler dependency output;
- depfiles;
- include scanning;
- compiler-native mechanisms.

Rivet should not implement a brittle universal parser when the compiler already knows its dependencies.

The adapter should normalize discovered dependencies into Rivet's graph.

---

# 24. Reproducibility

Rivet should expose reproducibility information.

```text
rivet fingerprint
rivet fingerprint target arcade
```

Possible output:

```text
Target: arcade

Toolchain:
  clang++ 19.1.4

Target:
  linux-x64

Profile:
  release

Build Script:
  1cdb...

Dependency Graph:
  9af2...

Final Build Fingerprint:
  f719...
```

Future builds should be able to compare fingerprints between machines.

---

# 25. Environment Variables

Build scripts may access environment variables using normal ArcoBASIC APIs.

However, Rivet actions must explicitly track environment variables that influence deterministic actions.

Example:

```basic
SDK = ENV.Get("MYSDK")
App.Include.Add(SDK + "/include")
```

Rivet should ideally record that `MYSDK` affected graph construction.

For action-specific environment:

```basic
Action.Environment["SDKROOT"] = "/opt/sdk"
```

Relevant environment values become fingerprint inputs unless explicitly marked otherwise.

---

# 26. Build Script Provenance

Where practical, objects added to the build graph should remember where they were introduced.

Example metadata:

```text
Origin
{
    Script = "build.abas"
    Line = 41
    Function = "ConfigureLinux"
    Adapter = null
    ParentRule = "PROJECT.Threading"
}
```

This enables:

```text
rivet why
rivet explain
ARCADE graphical inspection
agent debugging
```

---

# 27. Logging

Rivet shall provide both human-readable and structured logs.

Human:

```text
[COMPILE] src/parser.cpp
[CACHE]   src/lexer.cpp
[LINK]    arcade
[DONE]    1.83s
```

Structured formats may include:

```text
JSON
JSONL
Arcology-native structured event stream
```

Build tools should not need to scrape terminal text to understand a build.

---

# 28. Error Handling

Errors should preserve relevant context.

Bad:

```text
Command failed with exit code 1.
```

Preferred:

```text
COMPILE FAILED

Target:
  arcade

Source:
  src/parser.cpp

Compiler:
  clang++ 19.1.4

Command:
  clang++ ...

Exit code:
  1

Compiler output:
  ...

Introduced by:
  build.abas:22

Build action:
  compile:arcade:src/parser.cpp
```

---

# 29. CLI Design

Base form:

```text
rivet <command> [target] [options]
```

Initial commands:

```text
rivet build
rivet clean
rivet rebuild
rivet explain
rivet why
rivet graph
rivet inspect
rivet targets
rivet profiles
rivet toolchains
rivet cache
rivet fingerprint
rivet doctor
```

Examples:

```text
rivet build

rivet build arcade

rivet build arcade --target linux-x64

rivet build arcade \
    --target linux-x64 \
    --profile Release+Sanitized \
    --jobs 8

rivet why rebuild src/parser.cpp

rivet explain arcade

rivet graph arcade

rivet doctor
```

---

# 30. `doctor`

`rivet doctor` should diagnose the local build environment.

Example:

```text
RIVET DOCTOR

ArcoBASIC VM
  OK

Build script
  build.abas
  OK

C++ toolchain
  clang++ 19.1.4
  OK

Linker
  lld 19.1.4
  OK

Required package
  SDL3
  NOT FOUND

Suggested resolution:
  Package adapter could not locate SDL3.
```

Adapters may contribute diagnostic checks.

---

# 31. Project Discovery

Default project discovery may search upward for:

```text
build.abas
```

Potential alternative names may later be supported, but one canonical filename should exist.

Example:

```text
project/
├── build.abas
├── src/
├── include/
└── assets/
```

Invoking:

```text
rivet build
```

from a child directory should locate the project root.

---

# 32. Rivet Metadata Directory

Suggested internal directory:

```text
.rivet/
```

Possible contents:

```text
.rivet/
├── cache/
├── obj/
├── graph/
├── logs/
├── state/
└── tmp/
```

No user-editable configuration should be required inside `.rivet`.

The source of truth remains ArcoBASIC.

---

# 33. Cleaning

Cleaning should be graph-aware.

```text
rivet clean
```

Should remove Rivet-owned build artifacts.

Optional targeted clean:

```text
rivet clean arcade
rivet clean --cache
rivet clean --all
```

Rivet should never casually delete arbitrary files merely because a wildcard happens to match.

Declared ownership of generated artifacts is important.

---

# 34. Package Integration

Rivet should not hard-code one package manager.

Package adapters may support:

- system packages
- vcpkg
- Conan
- Cargo
- NuGet
- npm/pnpm
- Arcology package management
- future systems

Example:

```basic
SDL = PACKAGE.Require("SDL3")
App.RequiresPackage(SDL)
```

The resulting adapter may provide:

```text
include paths
library paths
libraries
runtime assets
tool dependencies
version
package provenance
```

---

# 35. Cross Compilation

Cross-compilation should be modeled explicitly.

Example:

```basic
TARGET "aos-x64"

App.Compiler = TOOLCHAIN.Require("arcology-x86_64")
```

Target properties may include:

```text
OS
Architecture
ABI
Vendor
CPU
Features
Endianness
PointerWidth
Runtime
```

Build scripts can inspect them normally.

---

# 36. Host vs Target

Rivet must distinguish:

```text
HOST
TARGET
```

This matters for code generators.

Example:

```text
Generator:
  built for HOST

Generated source:
  consumed by TARGET build
```

This should be natural:

```basic
generator = BUILD.Executable("schema-generator")
generator.Target = HOST

generated = generator.RunToFile(...)
app.Target = TARGET
```

---

# 37. Tests

Rivet should expose test actions but should not replace Fissure.

Rivet's responsibility:

```text
build test executable
invoke requested test action
track its dependency relation
```

Fissure's responsibility:

```text
regression testing
behavior verification
language/tool adapters
test orchestration
failure analysis
```

Integration example:

```text
rivet build tests
fissure run
```

Or:

```basic
BUILD.After("tests", Run := "fissure")
```

The tools should integrate without collapsing into one project.

---

# 38. Fission Integration

Fission should receive a first-party Rivet adapter.

Example:

```basic
App.Language = "ArcoBASIC"
App.Compiler = COMPILER.Fission()
```

Potential direct support:

```basic
CAPSULE app = BUILD.ArcoCapsule("MyApp")
```

Fission adapter responsibilities may include:

- ArcoBASIC source discovery;
- compilation;
- capsule construction;
- target-native executable generation;
- dependency extraction;
- debug metadata;
- toolchain fingerprinting.

---

# 39. ARCADE Integration

Rivet's graph and structured state should later allow ARCADE to present a visual build surface.

Possible views:

- project targets
- target graph
- source membership
- compiler profiles
- generated assets
- cache state
- dependency causality
- failed actions
- action timeline

The GUI is not authoritative.

It edits or manipulates the same ArcoBASIC-backed project model.

No opaque IDE-only project format should be required.

---

# 40. Agent Integration

Rivet should be unusually agent-friendly.

Commands should provide structured output:

```text
rivet explain --json
rivet graph --json
rivet why --json
rivet doctor --json
```

Agents should be able to answer:

- Why did this rebuild?
- Which compiler produced this artifact?
- Where was this flag added?
- Which target owns this generated file?
- What depends on this library?
- What changed between two builds?
- Which actions are not reproducible?
- Which targets failed?
- Which cache inputs changed?

This reduces the need for agents to reverse engineer terminal logs.

---

# 41. Security

Build files are executable code.

Rivet should state this clearly.

Potential future safety mechanisms:

```text
trusted project marker
restricted execution mode
process permission policy
network access policy
filesystem access policy
sandbox mode
```

However, Rivet should not pretend arbitrary build scripts are passive configuration files.

Opening and building untrusted projects may execute code.

---

# 42. Example: Small C++ Project

```basic
IMPORT Build

PROJECT App = BUILD.Executable("hello")

FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension = ".cpp" THEN
        App.Sources.Add(FILE)
    END IF
NEXT

App.Includes.Add("include")

App.Compiler = COMPILER.Require(
    Family := "clang"
)

App.Compiler.Standard = "c++23"
App.Compiler.Warnings = "strict"

App.Build()
```

---

# 43. Example: Conditional Platform Sources

```basic
IMPORT Build

PROJECT App = BUILD.Executable("example")

App.Sources.AddTree(
    "src/common",
    Extension := ".cpp"
)

SELECT CASE TARGET.OS

    CASE "linux"
        App.Sources.AddTree(
            "src/linux",
            Extension := ".cpp"
        )

    CASE "windows"
        App.Sources.AddTree(
            "src/windows",
            Extension := ".cpp"
        )

    CASE "aos"
        App.Sources.AddTree(
            "src/aos",
            Extension := ".cpp"
        )

END SELECT

App.Build()
```

---

# 44. Example: Multi-Language Project

```basic
IMPORT Build

core = BUILD.StaticLibrary("core")
core.Language = "C++"
core.Sources.AddTree("src/core")

tools = BUILD.Executable("asset-tool")
tools.Language = "ArcoBASIC"
tools.Compiler = COMPILER.Fission()
tools.Sources.AddTree("tools/asset-tool")

assets = BUILD.CustomTarget("assets")

FOR FILE IN DIRECTORY("assets/raw", RECURSIVE)
    action = tools.RunToFile(
        Input := FILE,
        Output := "assets/generated/" + FILE.Stem + ".arcasset"
    )

    assets.Actions.Add(action)
NEXT

game = BUILD.Executable("game")
game.RequiresLibrary(core)
game.RequiresBuild(assets)

game.Build()
```

---

# 45. Example: Custom Proprietary Tool

```basic
IMPORT Build

Action = BUILD.ProcessAction(
    Tool := "/opt/vendor/bin/widgetc",
    Arguments := [
        "--input", "assets/widget.src",
        "--output", "generated/widget.bin"
    ],
    Inputs := [
        "assets/widget.src"
    ],
    Outputs := [
        "generated/widget.bin"
    ]
)

BUILD.Add(Action)
BUILD.Run()
```

Rivet can therefore track and cache tools it knows nothing about.

---

# 46. Internal Architecture

Suggested major subsystems:

```text
Rivet CLI
    |
    v
Project Loader
    |
    v
Embedded ArcoBASIC VM
    |
    v
Build API / Object Model
    |
    v
Graph Builder
    |
    +--> Provenance Tracker
    +--> Adapter Registry
    +--> Dependency Resolver
    |
    v
Action Graph
    |
    +--> Fingerprint Engine
    +--> Cache Manager
    +--> Scheduler
    |
    v
Execution Engine
    |
    +--> Process Runner
    +--> Toolchain Adapters
    +--> Structured Logger
    |
    v
Artifact Store / Build State
```

---

# 47. Core Runtime Objects

Initial conceptual types:

```text
Project
Target
BuildProfile
BuildTargetPlatform
SourceSet
Path
Artifact
BuildAction
Dependency
Tool
Toolchain
CompilerAdapter
LinkerAdapter
PackageAdapter
Generator
ProcessAction
BuildGraph
CacheEntry
BuildResult
BuildDiagnostic
BuildProvenance
```

---

# 48. Adapter Registry

Adapters should register capabilities rather than merely names.

Example conceptual metadata:

```text
Adapter:
  Name: clang-cpp
  Language: C++
  Capabilities:
    compile
    link
    dependency-discovery
    preprocess
    version-detection
```

This enables Rivet to select tools by requirements.

---

# 49. Action Identity

Every action needs a stable logical identity separate from its fingerprint.

Example:

```text
compile:arcade:src/parser.cpp
```

Action identity answers:

> Which logical build operation is this?

Fingerprint answers:

> Are all relevant inputs identical to the previous execution?

---

# 50. Build State Database

Rivet will likely need a lightweight persistent state database.

Initial implementation may use:

```text
SQLite
```

or an Arcology-native store if one is already appropriate.

Suggested stored data:

```text
action identity
previous fingerprint
previous result
input hashes
output hashes
dependency list
timings
tool identity
diagnostics
provenance references
```

The storage layer should be abstracted.

---

# 51. Dependency Graph Rules

The graph engine must detect cycles.

Example:

```text
ERROR: BUILD DEPENDENCY CYCLE

core
  -> generator
  -> assets
  -> core
```

Preferably:

```text
Introduced by:
  core.RequiresBuild(generator)      build.abas:18
  generator.RequiresBuild(assets)   build.abas:21
  assets.RequiresBuild(core)        build.abas:28
```

---

# 52. Build Lifecycle

Conceptual lifecycle:

```text
1. Locate project
2. Start ArcoBASIC VM
3. Load Rivet API
4. Execute build.abas
5. Construct target/action graph
6. Resolve adapters/toolchains/packages
7. Validate graph
8. Calculate fingerprints
9. Determine cached / dirty actions
10. Schedule executable actions
11. Run actions
12. Verify declared outputs
13. Record resulting fingerprints
14. Produce final artifacts
15. Emit human + structured build report
```

---

# 53. First Implementation Scope

The first useful version should avoid attempting every feature immediately.

## Milestone 1: Executable ArcoBASIC Build Files

Implement:

- `rivet build`
- locate `build.abas`
- embed ABAS VM
- expose basic Rivet module
- file discovery
- executable target
- C/C++ source collection
- raw process execution

Goal:

```basic
FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension = ".cpp" THEN
        Sources.Add(FILE)
    END IF
NEXT

App.Build()
```

must work.

---

## Milestone 2: Action Graph

Implement:

- compile action
- link action
- dependency edges
- DAG validation
- parallel scheduling
- target selection

---

## Milestone 3: Incremental Build Engine

Implement:

- input hashing
- action fingerprints
- persistent state
- cache hit detection
- rebuild reasoning

At this stage:

```text
rivet why rebuild src/parser.cpp
```

should become functional.

---

## Milestone 4: Provenance and Diagnostics

Implement:

- source location provenance
- explain
- why
- inspect
- structured errors

---

## Milestone 5: Adapter Interface

Move compiler-specific logic behind ArcoBASIC adapters.

Initial first-party adapters:

```text
clang C/C++
gcc C/C++
Fission / ArcoBASIC
```

---

## Milestone 6: Generated Artifacts and Custom Actions

Implement:

- ProcessAction
- declared inputs
- declared outputs
- generated source dependency chains
- cacheable arbitrary tools

---

## Milestone 7: Profiles and Cross Compilation

Implement:

- host
- target
- profile composition
- toolchain requirements
- platform traits

---

## Milestone 8: ARCADE / Agent Interface

Implement:

```text
--json
graph export
structured event stream
stable introspection API
```

---

# 54. Coding Agent Requirements

Any coding agent implementing Rivet must follow these rules:

1. Do not introduce a new build DSL.
2. The canonical build language is ArcoBASIC.
3. Configuration logic must remain executable ArcoBASIC.
4. Toolchain adapters must be designed for ArcoBASIC implementation.
5. The runtime must preserve enough provenance to support `why`.
6. Execution must operate directly from Rivet's action graph.
7. Ninja/Make/MSBuild may be export targets but cannot become required internal engines.
8. Incremental decisions must be inspectable.
9. Avoid timestamp-only caching.
10. Raw external processes must remain possible.
11. Do not hard-code C++ assumptions into the core graph architecture.
12. Do not couple the graph engine to a single package manager.
13. Keep host and target distinct.
14. Build artifacts must have declared ownership.
15. Structured output is a first-class feature, not an afterthought.
16. The project must remain usable from a terminal without ARCADE.
17. ARCADE integration must consume the same model used by the CLI.
18. Prefer simple explicit object models over magical implicit behavior.
19. Every major automated decision should eventually be explainable.
20. Do not rename the project without explicit instruction.

---

# 55. Agent Continuity File

The coding agent must maintain a project progress file:

```text
RIVET_PROGRESS.md
```

Every meaningful implementation change must be recorded there.

The file should include:

```markdown
# Rivet Development Progress

## Current State

## Completed

## In Progress

## Next Tasks

## Architectural Decisions

## Known Problems

## Tests

## Files Changed

## Notes for Next Agent
```

This file exists specifically so development can move between multiple coding agents without losing design context.

Update it **during development**, not only at the end of a session.

---

# 56. Testing Requirements

Initial automated tests should cover:

### File discovery

- direct directory enumeration
- recursive enumeration
- extension filtering
- path normalization
- empty directories
- spaces in paths

### Graph

- independent actions
- linear dependencies
- fan-in
- fan-out
- cycle rejection

### Fingerprinting

- unchanged inputs
- source change
- flag change
- compiler change
- environment dependency change
- target change
- generated dependency change

### Cache

- cache hit
- cache miss
- missing output despite cache record
- corrupted output
- clean rebuild

### Scheduler

- correct dependency ordering
- parallel independent actions
- failure cancellation behavior
- output collision detection

### Provenance

- source added from build script
- flag added by profile
- library added by adapter
- dependency added indirectly

### CLI

- build
- clean
- explain
- why
- graph
- inspect
- doctor

---

# 57. Example Acceptance Test

Given:

```text
example/
├── build.abas
├── include/
│   └── math.hpp
└── src/
    ├── main.cpp
    └── math.cpp
```

And:

```basic
IMPORT Build

App = BUILD.Executable("example")

FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension = ".cpp" THEN
        App.Sources.Add(FILE)
    END IF
NEXT

App.Includes.Add("include")
App.Build()
```

Running:

```text
rivet build
```

must:

1. discover both `.cpp` files;
2. compile them;
3. link the executable;
4. persist build state.

Running:

```text
rivet build
```

again without changes must:

1. execute no compile actions;
2. execute no link action;
3. report cache/up-to-date state.

After modifying `src/math.cpp`:

```text
rivet build
```

must:

1. rebuild `math.cpp`;
2. avoid recompiling `main.cpp` unless dependency analysis requires it;
3. relink the executable.

Then:

```text
rivet why rebuild src/math.cpp
```

must identify the changed source as the cause.

---

# 58. Naming

**Project name:** Rivet

Naming rationale:

A rivet joins independent structural components into a larger engineered whole.

The name is:

- short;
- easy to type;
- industrial;
- appropriate for a CLI tool;
- compatible with the Arcology aesthetic;
- distinct from compiler terminology.

Example Arcology toolchain:

```text
Fission   compiler
Fissure   regression/fault testing
Rivet     build orchestration
```

CLI examples:

```text
rivet build
rivet clean
rivet graph
rivet why
rivet explain
```

---

# 59. Core Design Statement

> **Rivet is an ArcoBASIC-programmable build orchestration system in which the build description is ordinary executable code, while dependency tracking, parallelism, caching, reproducibility, diagnostics, and toolchain integration are services provided by the runtime rather than burdens placed on the developer.**

---

# 60. Design Litmus Test

When considering a Rivet feature, ask:

> Could a developer reasonably expect to solve this using an ordinary loop, condition, function, object, or array?

If yes, Rivet should let them do that in ArcoBASIC instead of inventing special syntax.

And:

> If Rivet made this decision automatically, can the developer ask why?

If not, the diagnostic model is incomplete.

---

# 61. Initial Development Directive

The first implementation should prioritize the shortest path to this working program:

```basic
IMPORT Build

App = BUILD.Executable("hello")

FOR FILE IN DIRECTORY("src", RECURSIVE)
    IF FILE.Extension = ".cpp" THEN
        Sources.Add(FILE)
    END IF
NEXT

App.Build()
```

followed immediately by:

```text
rivet build
rivet build
rivet why rebuild <file>
```

The first invocation proves orchestration.

The second proves incremental state.

The third proves Rivet's defining diagnostic philosophy.
