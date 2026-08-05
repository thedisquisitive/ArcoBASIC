# ArcoBASIC Repository Layout

This is the umbrella repository for ArcoBASIC, Arcology OS, the standalone Arcology Commons social
network, and the Lazarus recovery appliance. It separates public API, private implementation,
applications, tests, tooling, and generated output.
New files should follow these boundaries instead of creating new top-level source directories.

## Source and Library Boundaries

| Path | CMake target | Responsibility |
|---|---|---|
| `src/frontend/`, `src/runtime/`, `src/gui/` | `arco_runtime` / `ArcoBASIC::runtime` | Lexer, parser, canonical AST, hosted execution, runtime services, selected GUI backend |
| `src/compiler/` | `arco_compiler` / `ArcoBASIC::compiler` | Shared A-MIR, bytecode, native capsule, and Arcology integration pipeline |
| `arcology-os/src/`, `arcology-os/include/` | `arcology_os` / `ArcologyOS::backend` | Arcology-owned systems interfaces and UEFI PE32+ backend |
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
 arcology_os         arco_c_api
```

`ArcoBASIC::all` (legacy target name `arco`) aggregates those libraries for existing CMake
consumers. It is an interface compatibility target, not another copy of the implementation.

The frontend and runtime intentionally share one library. AST nodes implement hosted execution
against `Runtime`, while `Runtime` invokes the lexer/parser; splitting those files into separate
static libraries would introduce a misleading circular dependency.

## Applications

- `apps/arco/main.cpp` — basic file runner.
- `apps/arcosh/main.cpp` — interactive shell.
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
