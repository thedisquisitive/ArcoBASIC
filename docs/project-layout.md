# ArcoBASIC Repository Layout

This is the umbrella repository for ArcoBASIC, Arcology OS, the standalone Arcology Commons social
network, the Lazarus recovery appliance, and the standalone Arcology Shell restart. It separates
public API, private implementation, applications, tests, tooling, and generated output.
New files should follow these boundaries instead of creating new top-level source directories.

## arcosh/

`arcosh/` is a fresh, Linux-focused restart of ArcoSH, developed as its own independent CMake
project (like `lazarus/`) with its own `CMakeLists.txt`, build tree, and tests — it is not
`add_subdirectory`'d into the root build. See `arcosh/README.md`.

The previous implementation (`src/shell/arcosh.cpp`, `apps/arcosh/main.cpp`) is retired as a
product — the `arcosh` executable and its install/packaging/smoke-test wiring were removed from the
root build — but the source is left in place. `arco_shell` still compiles internally only because
`tests/unit/runtime_tests.cpp` exercises `arco::shell::*` directly; it is not installed and no
executable links it.

## Source and Library Boundaries

| Path | CMake target | Responsibility |
|---|---|---|
| `src/frontend/`, `src/runtime/`, `src/gui/` | `arco_runtime` / `ArcoBASIC::runtime` | Lexer, parser, canonical AST, hosted execution, runtime services, selected GUI backend, generic pixel/surface graphics (`src/gui/graphics.cpp`) |
| `src/compiler/` | `arco_compiler` / `ArcoBASIC::compiler` | Shared A-MIR, bytecode, native capsule, PE32+ image writing (`src/compiler/pe_image.cpp`), and Arcology integration pipeline |
| `arcology-os/include/` | `arcology_os_headers` / `ArcologyOS::headers` | Genuinely OS-specific interfaces only (currently just `uefi_bindings.hpp`, real UEFI struct/vtable layouts) |
| `arcology-commons/stdlib/` | ArcoBASIC application modules | Standalone Arcology Commons social-network framework and domain code |
| `src/shell/` | `arco_shell` / `ArcoBASIC::shell` | ArcoSH commands, REPL, help, and host integration |
| `src/bindings/` | `arco_c_api` / `ArcoBASIC::c_api` | Language-binding implementations |
| `include/arco/` | public headers | Stable generic embedding interfaces |
| `apps/` | executable targets | Thin command-line entry points only |

The dependency direction is:

```text
arco_compiler ---> arco_runtime <--- arco_shell
      |                  ^
      v                  |
arcology_os_headers  arco_c_api
```

`ArcoBASIC::all` (legacy target name `arco`) aggregates those libraries for existing CMake
consumers. It is an interface compatibility target, not another copy of the implementation.

### Shared infrastructure lives at the project root, not inside a component

`calling_convention.hpp`, `fixed_width_types.hpp`, `graphics.hpp`/`.cpp`, `pe_image.hpp`/`.cpp`,
`utf16.hpp`, and `x86_64_encoder.hpp` all used to live under `arcology-os/include/` and
`arcology-os/src/`, even though none of them have anything conceptually to do with Arcology OS —
they're generic compiler/runtime infrastructure (a general x86-64 encoder, calling-convention math,
a PE32+ writer, a pixel/surface library, UTF-16 encoding, fixed-width type metadata) that
`src/compiler/fission.cpp` needs for both its UEFI and native Linux backends, and `src/runtime/
runtime.cpp` needs for GUI support. They now live in the generic `include/arco/` and `src/` like
everything else this table describes, and `arcology-os/` keeps only what's genuinely OS-specific
(`uefi_bindings.hpp`'s real EFI struct/vtable layouts). When adding new code, a component directory
should hold only content that's actually specific to that component — generic infrastructure a
component happens to need belongs at the project root, consumed the same way any other target
consumes `include/arco/`, not owned by whichever component used it first.

The frontend and runtime intentionally share one library. AST nodes implement hosted execution
against `Runtime`, while `Runtime` invokes the lexer/parser; splitting those files into separate
static libraries would introduce a misleading circular dependency.

## Applications

- `apps/arco/main.cpp` — basic file runner.
- `apps/arcosh/main.cpp` — retired; no longer built. See `arcosh/` for the active restart.
- `apps/arcofission/main.cpp` — compiler and stage-inspection CLI.

Application entry points should contain argument handling only. Reusable behavior belongs in one of
the libraries above.

## Tests

- `tests/unit/` — compiled C++ unit and component tests.
- `tests/integration/` — end-to-end hosted CLI/shell scripts.
- `arcology-os/tests/` — freestanding compiler, ABI, PE, QEMU/OVMF tests, and boot fixtures.
- `arcology-commons/tests/` — Arcology Commons application, persistence, and export tests.
- `tests/fixtures/` — stable input and expected-output data for generic ArcoBASIC tests.

CTest registration lives in `cmake/Testing.cmake` rather than the top-level build file.

## Scripts and Output

- `scripts/build/` — compilers, package builders, and artifact construction.
- `scripts/install/` — interactive or system installation helpers.
- `scripts/run/` — local service and VM launchers.
- `scripts/arcosh/` — ArcoSH-language scripts installed with the shell.
- `arcology-os/scripts/` — Arcology OS hardware builders, QEMU launchers, and entry points into the
  independent Lazarus subproject.
- `arcology-commons/scripts/` — Arcology Commons development and service launchers.
- `books/` — long-form technical references for Arcology components, organized by component.

Generated builds belong in `build/` or `build-*`; generic distributable output belongs in `dist/`.
Arcology OS artifacts belong in `arcology-os/dist/`. Arcology Commons generated sites and runtime
state belong in `arcology-commons/dist/` and `arcology-commons/var/`.
Databases, logs, package repositories, and generated images do not belong at repository root.

## Build-System Files

- `CMakeLists.txt` defines the project and library/application graph.
- `cmake/Dependencies.cmake` discovers optional GUI and networking dependencies.
- `cmake/Testing.cmake` registers tests.
- `cmake/Install.cmake` owns installation layout.
- `arcology-os/CMakeLists.txt` defines the Arcology library boundary; its `cmake/` directory owns
  Arcology-specific test and installation registration.
- `arcology-commons/cmake/` owns installation and test registration for the standalone social
  network.

The `lazarus/` directory is an independently buildable CMake component within the Arcology project.
It intentionally keeps its own source, tests, appliance assets, and release lifecycle.
