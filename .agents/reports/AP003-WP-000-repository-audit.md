# Agent Packet 003 WP-000 Repository Audit

## Existing surfaces

- The shared lexer/parser already represented arithmetic, bitwise, shift, and comparison
  expressions as `Unary`, `Binary`, and `Logical` canonical AST nodes.
- The hosted evaluator in `src/frontend/parser.cpp` remains the source of hosted behavior.
- `src/compiler/fission.cpp` already lowered canonical AST expressions to generic A-MIR `Unary` and
  `Binary` instructions, but discarded fixed-width type information and the x86-64 backend rejected
  those instructions.
- `arcology-os/include/arco/fixed_width_types.hpp` provides exact fixed-width metadata and range
  bounds. Typed declaration metadata is retained in canonical assignment nodes; untyped hosted
  expressions remain untyped.
- `arcology-os/include/arco/x86_64_encoder.hpp` originally only encoded moves, calls, returns, and
  halt instructions. It required arithmetic, shifts, division, comparisons, and Boolean materialization.

## Syntax gap

The required backslash integer-division spelling was not present in the shared lexer/token model,
and `SAR` was not a keyword. Packet 003 requires both spellings, so the shared frontend was extended
with `TokenType::Backslash` and `TokenType::ShiftArithmeticRightWord`; no parallel systems parser was
introduced. Existing hosted `/` behavior was left unchanged.

## Implementation decision

Typed result and operand metadata was added to A-MIR instructions. The canonical AST remains the
only structural input to A-MIR. Reveal output maps the existing operator representation to the
canonical `INT.*` operation names while preserving the hosted A-MIR output for untyped programs.
