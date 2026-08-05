# Arcology OS

This subtree owns the Arcology-specific layer built on top of ArcoBASIC. Its source, public systems
headers, standard-library module, examples, RFCs, agent packets, tests, hardware tooling, and
developer documentation live together here.

## Layout

- `src/` and `include/` — the Arcology UEFI/PE backend and systems interfaces.
- `stdlib/` and `examples/` — the Arcology Commons module and runnable applications.
- `scripts/` — hardware-image builders plus QEMU/OVMF and development launchers.
- `tests/` — systems integration tests and boot fixtures.
- `docs/` — implementation, architecture, roadmap, and bring-up documentation.
- `rfcs/` and `agent-packets/` — governing requirements and implementation packets.
- `cmake/` — Arcology-specific test and installation registration.

## Shared ArcoBASIC Integration Points

The generic ArcoBASIC frontend and ArcoFission pipeline remain under `src/` because hosted programs
and Arcology programs use the same parser, canonical AST, A-MIR, and bytecode implementation.
Arcology-specific directives and lowering hooks in those shared files are integration points; the
concrete UEFI interfaces, encoder, PE writer, tests, and tooling are owned by this subtree.

The top-level build exposes `ArcologyOS::headers` and `ArcologyOS::backend`. The latter produces
`libarcology_os.a` and is linked by `arco_compiler`.
