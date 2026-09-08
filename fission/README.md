# Fission

Fission is the new Arcology compiler substrate described by
`docs/RFC-AP-FISSION-001_Fission_Compiler_Substrate.md`.

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
