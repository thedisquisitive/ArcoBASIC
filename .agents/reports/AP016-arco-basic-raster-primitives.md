# Agent Packet 016 — ArcoBASIC raster primitive slice

## Implemented

Added source-level `PutPixel` and `FillRectClipped` routines under
`arcology-os/stdlib/graphics_primitives.abas`. They perform bounds checks, rectangle clipping,
stride/address arithmetic, volatile 32-bit writes, and a completion barrier entirely in ArcoBASIC.

## Validation

- AST and A-MIR reveals prove explicit control flow and `MEMORY.WRITE32` operations.
- The nine-parameter clipped rectangle function compiles through freestanding x86-64 lowering,
  exercising stack-passed parameter homing.
- `systems_arco_basic_primitives_smoke` passes.

## Boundary

The functions currently operate on validated `MMIOPTR` surfaces. Opaque `SURFACE` construction and
runtime ownership remain separate work; this slice deliberately keeps the raster policy in source
language code.
