# ArcoFission Compiler Capability Reference

This document describes the capabilities exposed by the ArcoFission compiler in
the current Arcology/ArcoBASIC tree. It is a capability inventory, not a tutorial:
use it to decide what should be wired into Fissure regression runs, Rivet build
targets, installers, and cross-target smoke tests.

## Command Surface

`ArcoFission` is the compiler entry point. It accepts source files, textual
`.arcof` bytecode files, and target-specific output paths.

| Command | Capability |
| --- | --- |
| `ArcoFission --help` | Prints the supported command shapes. |
| `ArcoFission --version` | Prints the alpha compiler version. |
| `ArcoFission reveal FILE at AST` | Preprocesses, parses, and renders the parsed AST. |
| `ArcoFission reveal FILE at A-MIR` | Lowers source to A-MIR and renders it. |
| `ArcoFission reveal FILE at BYTECODE` | Lowers source to hosted bytecode and renders textual `.arcof`. |
| `ArcoFission reveal FILE at CALLCONV` | Computes and renders calling-convention information. |
| `ArcoFission reveal FILE at X86_64 [--entry NAME]` | Lowers through A-MIR and renders generated x86-64 code. |
| `ArcoFission reveal FILE at PRETTY` | Prints canonical regenerated ArcoBASIC source. |
| `ArcoFission reveal FILE --stage STAGE` | Alternate spelling for `reveal FILE at STAGE`. |
| `ArcoFission bytecode FILE -o OUT.arcof` | Writes textual hosted bytecode. |
| `ArcoFission build FILE -o OUT.arcof` | Writes textual hosted bytecode based on the output extension. |
| `ArcoFission run FILE.arcof` | Runs textual hosted bytecode. |
| `ArcoFission compile-run FILE` | Compiles source to bytecode in memory and runs it. |
| `ArcoFission native FILE -o OUT` | Builds a Linux VM-backed native capsule by default. |
| `ArcoFission build FILE -o OUT` | Same default native capsule path unless a target or `.arcof` extension changes the output kind. |
| `ArcoFission native FILE -o OUT.exe --target windows-x86_64` | Cross-builds a Windows x86-64 PE32+ VM-backed capsule from a Linux host. |
| `ArcoFission native FILE -o OUT.html --target web` | Cross-builds an Emscripten browser VM-backed capsule from a Linux host. |
| `ArcoFission build FILE -o OUT.efi --target uefi-x86_64 [--entry NAME]` | Builds a freestanding UEFI x86-64 PE32+ image. |
| `ArcoFission build FILE -o OUT --target linux-x86_64 [--entry NAME] [--debug] [--sanitize]` | Builds an experimental direct Linux ELF64 executable with no embedded bytecode VM. |

Stage aliases:

| Canonical stage | Accepted aliases |
| --- | --- |
| `AST` | `ast`, `parsed`, `parsed-source` |
| `A-MIR` | `a-mir`, `amir` |
| `BYTECODE` | `bytecode`, `a-bc`, `abc`, `arcof` |
| `CALLCONV` | `callconv`, `calling-convention` |
| `X86_64` | `x86-64`, `x86_64`, `native-asm` |
| `PRETTY` | `pretty`, `source` |

Hosted run and hosted build commands accept
`--instruction-limit COUNT|unlimited`. Hosted execution defaults to unlimited
when invoked through ArcoFission.

## Source Intake

ArcoFission starts by using the ArcoBASIC runtime preprocessor, lexer, and parser.
That gives compiler outputs the same front-end behavior as `arco_cli` for hosted
programs.

Supported preprocessor and metadata capabilities include:

| Capability | Directives and behavior |
| --- | --- |
| Shebang stripping | Lines starting with `#!` are ignored during preprocessing. |
| Attributes | Lines beginning with `@` are recorded as compile metadata and removed from parsed source. |
| Conditional source selection | `#IFDEF`, `#IFNDEF`, `#IF`, `#ELSEIF`, `#ELSE`, `#ENDIF`. |
| Defines | `#DEFINE NAME [VALUE]` defines a preprocessor symbol and can expose a global value; `#UNDEF NAME` removes it. |
| Human metadata | `#VERSION`, `#AUTHOR`, `#DESCRIPTION`, `#NOTE`, `#TODO`, `#WARNING`. |
| Build metadata | `#ENTRY`, `#TARGET`, `#PROFILE`, `#RUNTIME`, `#CALLCONV`, `#EXPORT`, `#PACK`, `#ALIGN`, `#ENDIAN`. |
| Capability metadata | `#REQUIRE`, `#FEATURE`, `#STRICT`, `#EXPERIMENTAL`, `#DEPRECATED`. |
| Hard errors | Active `#ERROR` stops preprocessing with the directive text as the error. |
| Region markers | `#REGION` and `#ENDREGION` are accepted as structural no-ops. |
| File inclusion | `#INCLUDE "path"` inlines preprocessed source. |
| Imports | `#IMPORT "path"` inlines imported source; `#IMPORT "path" AS Alias` also creates case-insensitive alias wrappers. |
| Hosted instruction budgets | `#INSTRUCTION_LIMIT N`, where `N` is `1` through `9007199254740991`. |

`#PROFILE UEFI`, `#RUNTIME NONE`, and `#CALLCONV UEFI` are the currently accepted
systems-profile values. Under `#RUNTIME NONE`, hosted-only constructs are rejected
by the parser or lowerer, and `#INSTRUCTION_LIMIT` is rejected because there is no
hosted VM instruction counter in that mode.

## Inspection Pipeline

The `reveal` command family exposes the compiler pipeline without writing an
executable:

- AST reveal: preprocesses, lexes, parses, and emits the parsed structure.
- Pretty reveal: preprocesses and parses, then emits canonical regenerated source.
- A-MIR reveal: lowers parsed statements to the Arcology middle IR, including
  compiler metadata such as source instruction limits.
- Bytecode reveal: lowers A-MIR to hosted bytecode and renders textual `.arcof`.
- Calling-convention reveal: computes call signatures for systems/native codegen.
- x86-64 reveal: lowers through A-MIR and emits generated x86-64 assembly/program
  representation for an entry function.

Successful reveal/build paths emit status banners such as `SOURCE ACCEPTED`,
`STRUCTURE ASSEMBLED`, `A-MIR GENERATED`, `BYTECODE PREPARED`,
`CALLING CONVENTION COMPUTED`, `X86_64 GENERATED`, `BYTECODE WRITTEN`,
`ELF64 WRITTEN`, `PE32+ WRITTEN`, or `WEB CAPSULE WRITTEN`, depending on the path.
Pretty reveal intentionally prints only source text.

## Hosted Bytecode

ArcoFission can compile ArcoBASIC source to hosted bytecode, run textual bytecode,
and embed binary bytecode inside native capsules.

Capabilities:

- Textual bytecode output through `bytecode FILE -o OUT.arcof`,
  `build FILE -o OUT.arcof`, or `reveal FILE at BYTECODE`.
- In-memory compile-and-run through `compile-run FILE`.
- Textual bytecode execution through `run FILE.arcof`.
- Binary bytecode serialization for native capsules.
- Bytecode module preparation before execution, including constants, typed slots,
  function tables, and fused numeric bytecode paths used by common loop arithmetic.
- Registration of non-`Main` bytecode functions as callable runtime functions.
- Hosted instruction-limit enforcement from either source metadata or an operator
  CLI override.
- `Exit()` / `ExitTheProgram()` propagation as a real process exit code rather
  than a synthetic compiler failure.

Native capsules that execute embedded bytecode also populate a global `Args` array
from process arguments and register `ArcoFission.CompileRunSource(source)`. That
host function compiles and runs ArcoBASIC source in-process and returns an object
with `{Ok, Output, ExitCode, Error}`.

## Default Native Capsules

Without a target, `native FILE -o OUT` and `build FILE -o OUT` create a Linux ELF64
launcher that embeds binary bytecode and links it to the hosted VM. This is the
stable native capsule path and remains separate from the experimental
`--target linux-x86_64` no-VM backend.

Capabilities:

- Produces an executable ELF64 file on Linux.
- Embeds compiled bytecode as data inside a small C++ launcher.
- Runs through `arco::fission::run_bytecode_binary`.
- Honors operator instruction-limit overrides passed at build time.
- Passes capsule command-line arguments to the ArcoBASIC global `Args`.
- Uses a lean runtime when the bytecode does not require full hosted runtime
  services.
- Links the full runtime when GUI or other full-runtime services are required.
- Validates that the output starts as an ELF64 executable and marks it executable.

The build looks for the compiler source root through `ARCO_SOURCE_ROOT` when that
macro is present, otherwise from the source checkout path baked into the compiler.
For link inputs it can use generated link metadata where available or flat static
archives such as `libarco_compiler.a`, `libarco_runtime.a`,
`libarco_compiler_core.a`, `libarco_runtime_core.a`, and `libarcology_os.a`.

## Windows Capsules

`native FILE -o OUT.exe --target windows-x86_64` cross-builds a Windows x86-64
PE32+ bytecode capsule from a Linux host.

Target aliases:

- `windows-x86_64`
- `windows-x86-64`

Capabilities:

- Uses the same embedded-bytecode launcher model as default native capsules.
- Links against Windows-targeted `libarco_compiler.a`, `libarco_runtime.a`, and
  `libarcology_os.a`.
- Uses `x86_64-w64-mingw32-g++` by default.
- Accepts `ARCOFISSION_WINDOWS_CXX` to override the cross compiler.
- Auto-discovers a sibling `build-rivet-windows/` tree or installed
  `windows-x86_64/` support directory when the required archives are present.
- Accepts `ARCOFISSION_WINDOWS_TOOLCHAIN_DIR` for a custom Windows-targeted
  support directory.
- Links with static libgcc/libstdc++ flags and `ws2_32`.
- Verifies that the output is PE32+ and reports x86-64 machine type.

Known boundaries:

- Windows capsule builds are only supported from a Linux host in this compiler.
- The Windows runtime support is core-oriented today; GUI is stubbed and browser-
  or POSIX-style process/network behavior should not be assumed complete.

## Web Capsules

`native FILE -o OUT.html --target web` cross-builds an Emscripten browser
bytecode capsule from a Linux host.

Target aliases:

- `web`
- `wasm`
- `web-wasm32`

Capabilities:

- Uses the same embedded-bytecode launcher model as default native capsules.
- Links against Emscripten-targeted `libarco_compiler.a`, `libarco_runtime.a`,
  and `libarcology_os.a`.
- Uses `em++` by default.
- Accepts `ARCOFISSION_WEB_CXX` to override the Emscripten compiler.
- Auto-discovers a sibling `build-rivet-web/` tree or installed `web-wasm32/`
  support directory when the required archives are present.
- Accepts `ARCOFISSION_WEB_TOOLCHAIN_DIR` for a custom Emscripten-targeted support
  directory.
- Emits `.html`, `.js`, or `.wasm` shapes according to the output extension.
- Adds `.html` when no recognized web extension is provided.
- Uses `-Os`, exceptions, `ASYNCIFY`, memory growth, and exported
  `ccall`/`cwrap` runtime methods.
- For `.html`, uses the Arco web shell and defaults to `-sSINGLE_FILE=1` so the
  result can be opened directly without an HTTP server.
- Accepts `ARCOFISSION_WEB_SINGLE_FILE=0` to produce a separate `.wasm` file for
  HTTP/CDN deployment.
- Verifies single-file HTML by file presence and plausible size; verifies
  separate wasm output by WebAssembly magic bytes.

Browser runtime capabilities:

- GUI capsules use the canvas backend.
- File dialogs use the File System Access API where available, with prompt/MEMFS
  fallback behavior.
- `ArcoFission.CompileRunSource(source)` gives browser-hosted programs an
  in-process compile-run capability.

Known boundaries:

- Web capsule builds are only supported from a Linux host in this compiler.
- Emscripten must be installed and active in the shell or named through
  `ARCOFISSION_WEB_CXX`.
- `Process.Run` cannot spawn native host processes inside the browser.
- `GUI.Image` is not implemented in the current canvas backend.

## UEFI Images

`build FILE -o OUT.efi --target uefi-x86_64 [--entry NAME]` creates a freestanding
UEFI x86-64 PE32+ image. This path does not embed the hosted bytecode VM.

Target aliases:

- `uefi-x86_64`
- `uefi-x86-64`

Capabilities:

- Preprocesses and parses source with freestanding restrictions when
  `#RUNTIME NONE` is active.
- Lowers source to A-MIR.
- Generates x86-64 code for the selected entry function.
- Emits PE32+ EFI image bytes through the Arcology systems image writer.
- Carries text, read-only data, entry symbol, and relocation information into the
  generated image.
- Reports `PE32+ WRITTEN OUT (N bytes)` on success.

Systems-profile source metadata currently accepted by the front end:

```basic
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT efi_main
```

`#TARGET` selects `X86_64` as the UEFI architecture under `#PROFILE UEFI`.
Other UEFI architectures, profiles, runtime modes, and calling conventions are
rejected in this milestone.

## Direct Linux Native Backend

`build FILE -o OUT --target linux-x86_64` is not a bytecode capsule. It compiles
source directly to real x86-64 machine code and links an ELF64 executable.

Target aliases:

- `linux-x86_64`
- `linux-x86-64`

Capabilities:

- Uses the same preprocessor, parser, and A-MIR lowerer as other compiler paths.
- Generates x86-64 code with the System V ABI.
- Renames the selected ArcoBASIC entry function to `main` for normal C runtime
  startup.
- Emits temporary assembly and invokes the host C++ compiler/assembler/linker.
- Links the native runtime ABI sources for printing and value handling.
- Links `-lm` for floating-point `MOD` semantics through `fmod`.
- Uses a generic host-function bridge when generated code calls hosted functions
  that do not have dedicated native codegen.
- Links a lean runtime for non-GUI host-function bridge calls.
- Links the full GUI-capable runtime when the program calls a `GUI.*` function.
- Validates that the result is ELF64 and marks it executable.
- `--debug` writes stable annotated assembly next to the output as `OUT.s`.
- `--sanitize` adds debug symbols and AddressSanitizer.

Regression-covered behavior includes numeric arithmetic, comparisons, `MOD`,
bitwise operators, Boolean logic, branches, loops, function calls, default
parameters, arrays, objects, tuples, classes, methods, shared fields, boxed values,
script globals, selected host-function bridging, throwing/catching, explicit exit,
runtime arguments, GUI host-call classification, and reference-lifetime checks.

Known boundaries:

- This backend is experimental and narrower than hosted bytecode execution.
- It is only supported on Linux.
- Some diagnostics that are fatal for freestanding systems profiles are not fatal
  for this hosted Linux backend, matching hosted dynamic ArcoBASIC behavior.
- Host-function calls require the relevant runtime support libraries to exist in
  the buildchain directory.

## Public C++ API

The public embedding API is declared in `include/arco/fission.hpp`.

| API | Capability |
| --- | --- |
| `reveal_ast(source, source_name)` / `reveal_ast_file(path)` | Render AST. |
| `reveal_pretty(source, source_name)` / `reveal_pretty_file(path)` | Render canonical source. |
| `reveal_amir(source, source_name)` / `reveal_amir_file(path)` | Render A-MIR. |
| `reveal_bytecode(source, source_name)` / `reveal_bytecode_file(path)` | Render textual bytecode. |
| `reveal_callconv(source, source_name)` / `reveal_callconv_file(path)` | Render calling-convention data. |
| `reveal_x86_64(source, source_name, entry)` / `reveal_x86_64_file(path, entry)` | Render x86-64 output. |
| `run_bytecode(bytecode, limit)` | Run textual bytecode. |
| `run_bytecode_binary(bytecode, limit, script_args)` | Run binary bytecode with script arguments. |
| `run_bytecode_file(path, limit)` | Load and run textual bytecode. |
| `compile_run(source, source_name, limit)` / `compile_run_file(path, limit)` | Compile and execute source in memory. |
| `build_native_file(path, output, limit, target)` | Build default, Windows, or web VM-backed native capsules. |
| `build_efi_image(source, source_name, entry, output)` / `build_efi_image_file(path, entry, output)` | Build UEFI PE32+ image. |
| `build_linux_native_image(source, source_name, entry, output, debug_options)` / `build_linux_native_image_file(path, entry, output, debug_options)` | Build direct Linux x86-64 ELF64 image. |

All API calls return `arco::fission::Result`, with `ok`, `output`, and `error`.
Direct Linux native debug options are represented by `NativeDebugOptions` with
`annotate` and `sanitize` booleans.

## Buildchain Integration Points

ArcoFission now needs these buildchain artifacts to exercise every target:

| Capability | Required local artifact or tool |
| --- | --- |
| Hosted bytecode/reveal/compile-run | Host ArcoFission binary and host compiler libraries. |
| Default Linux native capsules | Host static archives and host C++ compiler. |
| Direct Linux native backend | Host C++ compiler, native runtime ABI sources, and host runtime archives for bridged host calls. |
| Windows capsules | Windows-targeted static archives plus `x86_64-w64-mingw32-g++` or `ARCOFISSION_WINDOWS_CXX`. |
| Web capsules | Emscripten-targeted static archives plus `em++` or `ARCOFISSION_WEB_CXX`. |
| UEFI images | Arcology systems PE32+ image writer linked into ArcoFission. |

Rivet builds should provide the same archive names and support-directory shapes
that ArcoFission auto-discovers:

- host output beside the running `ArcoFission`;
- sibling `build-rivet-windows/` for Windows support archives;
- sibling `build-rivet-web/` for Emscripten support archives;
- installed `windows-x86_64/` and `web-wasm32/` support directories beside the
  installed ArcoFission binary.

Custom cross-support directories can be supplied with:

```sh
ARCOFISSION_WINDOWS_TOOLCHAIN_DIR=/path/to/windows/support
ARCOFISSION_WEB_TOOLCHAIN_DIR=/path/to/web/support
```

Custom cross compilers can be supplied with:

```sh
ARCOFISSION_WINDOWS_CXX=x86_64-w64-mingw32-g++
ARCOFISSION_WEB_CXX=em++
```

## Exit Codes and Error Behavior

The CLI uses:

- `0` for success.
- `1` for compilation, build, bytecode-run, or native-run failures.
- `2` for invalid command-line usage or invalid option values.

Failed compiler API calls return `Result{false, "", error}`. Successful calls set
`ok` and place rendered stage text, build banners, or program output in `output`.

## Current Capability Boundaries

These are intentional current boundaries, not necessarily permanent design limits:

- The direct Linux native backend is experimental and is not the default native
  output.
- Windows and web capsule builds are implemented only as Linux-hosted cross-builds.
- UEFI support is x86-64 only.
- `#PROFILE` currently accepts only `UEFI`.
- `#RUNTIME` currently accepts only `NONE` as a freestanding override; empty means
  normal hosted runtime.
- `#CALLCONV` currently accepts only `UEFI`.
- Source instruction limits are hosted-only.
- Browser capsules cannot spawn native host processes.
- Web `GUI.Image` support is not implemented yet.
- Windows runtime GUI and network behavior are limited compared with the Linux
  hosted runtime.
