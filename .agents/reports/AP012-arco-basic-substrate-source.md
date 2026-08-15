# Agent Packet 012 — ArcoBASIC substrate source slice

## Result

The first freestanding substrate algorithm is now checked in as ArcoBASIC at
`arcology-os/stdlib/graphics_substrate.abas`. `FillMappedSurface` performs the complete pixel
address calculation, MMIO pointer offset, volatile `MEMORY.Write32`, nested scan loops, and final
`CPU.MemoryBarrier` in source language code.

The hardware fixture at `arcology-os/tests/fixtures/graphics-substrate/graphics-substrate.abas`
performs GOP discovery, maps the firmware framebuffer, and executes the same nested ArcoBASIC loops.
It contains no C++ renderer call and no direct framebuffer write hidden behind a library function.

## Evidence

- AST reveal contains `MMIOPTR`, `ADDRESS.Offset`, `MEMORY.Write32`, and `CPU.MemoryBarrier`.
- A-MIR reveal contains `MEMORY.MAPDEVICE`, `MEMORY.WRITE32`, and `CPU.MEMORYBARRIER`.
- `ArcoFission build` emits a PE32+ UEFI image for the fixture.
- `systems_arco_basic_substrate_smoke` passes.
- A QEMU/OVMF boot was launched; the image remained running until the fixture's intentional
  `CPU.HaltForever` endpoint, so the outer timeout terminated it (`124`).

## Current boundary

The C++ implementation remains compiler/runtime machinery: parser, A-MIR, x86-64 emission, UEFI
protocol bindings, MMIO primitives, and hosted test support. It does not own the scan conversion
algorithm in this slice.

The freestanding lowering now supports incoming stack-passed parameters. The source module accepts
an explicit fifth `color AS U32`, and generated code loads it from the Microsoft x64 shadow-space /
stack area before homing it into the spill frame. Internal freestanding function calls and reusable
`SURFACE` ABI composition remain separate follow-up work; no unsupported call path was invented.
