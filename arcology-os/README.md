# Arcology OS

This subtree owns Arcology OS: the operating-system and freestanding systems layer built on
ArcoBASIC. Its source, public systems headers, RFCs, agent packets, tests, hardware tooling, and
developer documentation live together here. The separate Arcology Commons social network is under
`arcology-commons/`; it is an ArcoBASIC application, not part of Arcology OS.

## Layout

- `include/` — genuinely OS-specific public systems interfaces (currently just `uefi_bindings.hpp`,
  real UEFI struct/vtable layouts). Generic compiler/runtime infrastructure that doesn't actually
  concern Arcology OS as a product (the x86-64 encoder, calling-convention math, the PE32+ writer,
  the pixel/surface graphics library, UTF-16 encoding, fixed-width type metadata) lives at the
  project root's `include/arco/` and `src/` instead — see `docs/project-layout.md`'s "Shared
  infrastructure lives at the project root, not inside a component" note. Anything genuinely
  specific to booting/running Arcology OS belongs here; generic infrastructure a component happens
  to need does not.
- `examples/` — runnable Arcology OS and UEFI programs.
- `scripts/` — hardware-image builders plus QEMU/OVMF and development launchers.
- `tests/` — systems integration tests and boot fixtures.
- `docs/` — implementation, architecture, roadmap, and bring-up documentation.
- `rfcs/` and `agent-packets/` — governing requirements and implementation packets.
- `cmake/` — Arcology-specific test and installation registration.

## Shared ArcoBASIC Integration Points

The generic ArcoBASIC frontend and ArcoFission pipeline remain under the project root's `src/`
because hosted programs and Arcology programs use the same parser, canonical AST, A-MIR, and
bytecode implementation. Arcology-specific directives and lowering hooks in those shared files are
integration points; UEFI-specific interfaces (`uefi_bindings.hpp`), tests, and tooling are owned by
this subtree — the encoder, PE writer, and other genuinely generic codegen infrastructure those
same shared files use are not, and live at the project root instead (see above).

The top-level build exposes `ArcologyOS::headers` (header-only, `uefi_bindings.hpp`), linked by
`arco_runtime`/`arco_compiler` for their UEFI-target support.
