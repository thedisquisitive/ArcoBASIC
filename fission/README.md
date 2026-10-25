# Fission

Fission is the Arcology compiler substrate described by
`docs/RFC-AP-FISSION-001_Fission_Compiler_Substrate.md`. It is written in
ArcoBASIC, compiles ArcoBASIC to standalone native x86-64 Linux executables
(no `as`, `ld` or C toolchain involved), and compiles itself.

## Install and use

```sh
scripts/install/install-fission.sh                       # /usr/local (uses sudo when needed)
PREFIX=$HOME/.local scripts/install/install-fission.sh   # no privileges
REBUILD=1 scripts/install/install-fission.sh             # recompile from this checkout first
BOOTSTRAP=1 scripts/install/install-fission.sh           # build everything from the legacy C++ compiler
```

The default install uses the checksum-verified seed compiler committed at
`packaging/fission/seed/` and takes about 30 seconds (most of it prebuilding
the native runtime cache). The seed is a verified fixpoint of the checked-in
sources; `scripts/build/refresh-fission-seed.sh` regenerates it, and
`BOOTSTRAP=1` rebuilds it from nothing: the legacy compiler ("G0") builds the
Fission driver, that builds the native compiler (stage 1), and stage 1
recompiles itself (stage 2), which must be byte-identical (about 15 minutes).
The installer smoke-tests the staged compiler before it installs anything.

```sh
fission build hello.abas -o hello && ./hello
fission build prog.abas -o prog --target bytecode      # also: sir, amir
fission build prog.abas --pipeline                     # show the resolved route
```

- `#IMPORT "name"` resolves like the legacy compiler: the name, then `.abas`/
  `.arc`/`.bas`, against the working directory, `stdlib/`, `../stdlib/`, the
  installed stdlib and `$ARCOBASIC_STDLIB`. `#IMPORT "x" AS Alias` is not
  supported yet.
- The launcher sets `FISSION_HOME` (compiler sources) and `ARCOBASIC_STDLIB`
  relative to itself, so an install can be moved. Both can be overridden.
- The runtime cache lives in the install (prebuilt), else in
  `$FISSION_CACHE` or `~/.cache/arcobasic/fission-runtime`.
- A hello world compiles in about 2 seconds. The compiler itself takes about
  7.5 minutes to recompile itself.

Everything below documents the substrate's components and tests.

This folder contains only the new implementation. The legacy C++ ArcoFission
compiler remains in place as the Generation 0 bootstrap/reference implementation.

Current scope:

- WP-001 core component skeletons in ArcoBASIC;
- component metadata;
- registry;
- typed artifacts;
- diagnostics;
- pipeline steps;
- generic pipeline resolver;
- capability-aware route resolution with unmet-requirement diagnostics;
- ambiguity diagnostics for multiple shortest routes;
- compile request/result facade;
- component transform callbacks with typed artifact transport;
- a smoke fixture proving a language component can inherit a downstream target
  route through common artifact types and execute that route without target
  conditionals in the language component.
- a project-local Fissure probe for the WP-001 substrate core smoke.

Run the current smoke fixture with:

```sh
build-rivet/ArcoFission compile-run fission/tests/core_smoke.abas
```

or from the repository root through the wrapper used by CTest:

```sh
tests/integration/fission_substrate_core_smoke.sh build-rivet/ArcoFission "$PWD"
```
