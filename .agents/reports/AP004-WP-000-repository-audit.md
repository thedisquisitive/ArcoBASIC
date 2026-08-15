# Agent Packet 004 WP-000 Repository Audit

Date: 2026-08-05
Scope: repository audit only; no Packet 004 implementation was performed.

## 1. Shared parser and canonical AST

The shared parser in `src/frontend/parser.cpp` already owns the required `IF` and `WHILE`
structures:

- `IfStmt::canonical_ast()` emits `AstKind::If`, one condition child, and `then`/`else`
  canonical groups.
- `WhileStmt::canonical_ast()` emits `AstKind::While`, one condition child, and a `body`
  canonical group.
- `IF ... ELSE ... END IF` and `WHILE ... WEND` are accepted by the shared grammar.
- Conditions are currently evaluated with hosted truthiness; the canonical nodes do not carry
  an explicit condition type annotation.

`ELSEIF` is not currently a shared-language token or parser form. The lexer recognizes `ELSE`
only, and an `ELSEIF` source line currently fails parsing as an ordinary assignment-like
statement (`expected '=' after variable name`). There is therefore no canonical `ELSEIF`
representation to lower. This is a structural gap against Packet 004's claim that `ELSEIF` is
mandatory when already represented; it must not be invented in the backend.

The canonical AST also represents `DO`, `FOR`, `FOR EACH`, `SELECT`, and `TRY`; these are
outside the mandatory first implementation scope and must remain explicitly gated in the systems
backend.

## 2. Existing hosted A-MIR control-flow representation

`src/compiler/fission.cpp` contains `AmirFunction::blocks`, `AmirBlock`, and explicit A-MIR
instruction kinds for `Jump`, `Branch`, `Return`, `CpuHalt`, and `CpuHaltForever`.
`AstAmirBuilder` consumes only parser-produced canonical AST nodes and already lowers:

- `IF` to entry `BRANCH`, then/else blocks, and an end block;
- `WHILE` to condition, body, and end blocks with a back-edge;
- additional hosted constructs (`DO`, `FOR`, `SELECT`, and `TRY`) using the same block container.

Block names are generated from a monotonically increasing counter (`IfThen0`, `IfElse1`,
`IfEnd2`, etc.) in source traversal order. A-MIR reveal prints `BLOCK` sections and textual
`JUMP`/`BRANCH` instructions, but does not print a separate successor list.

## 3. Terminator and validation state

`is_terminal_instruction()` treats `RETURN`, `JUMP`, `BRANCH`, and `CPU.HaltForever` as
terminators. `CPU.Halt` is intentionally resumable and receives fallthrough. The current lowering
adds an implicit `RETURN` only to the builder's current block via `ensure_terminated()`.

`validate_module()` checks that blocks are non-empty, end in one of those recognized terminators,
and that `JUMP`/`BRANCH`/`TRY` targets appear in a flat target-name collection. It does not yet
perform reachability analysis, detect duplicate block/label names, validate successor arity or
terminator placement after an earlier terminator, or distinguish block labels from source labels.

## 4. Implicit-return behavior

The synthetic top-level `Main` and generated functions currently call `ensure_terminated()` on
the current block after lowering. This preserves existing hosted behavior but is insufficient for
Packet 004's path-sensitive rule: a function with a reachable non-void path falling off the end
needs a deterministic diagnostic, while a function whose every reachable path returns or halts
must not receive a synthetic return. This requires a later graph/reachability work package.

## 5. Terminal hardware semantics

The parser produces `AstKind::HardwareSemantic` for `CPU.Halt` and `CPU.HaltForever`.
The A-MIR builder preserves these as distinct instructions. `CPU.HaltForever` is recognized as
terminal and does not receive a synthetic fallthrough jump. `CPU.Halt` emits a resumable `HLT`
and remains open for a subsequent edge. The hosted bytecode backend reports both hardware
semantics as unsupported rather than changing hosted execution behavior.

## 6. x86-64 encoder and code-generator gaps

`arcology-os/include/arco/x86_64_encoder.hpp` currently provides `jmp_rel8()` only; it has no
conditional branches, near jumps, labels, or fixup/patch tables. The generated UEFI function path
explicitly rejects any function with more than one A-MIR block (`control flow beyond a single
straight-line block`). Consequently no multi-block function is emitted to PE/COFF today.

The code generator already collects stack slots across all blocks, but that logic is unreachable
for multi-block code because of the early rejection. Branch-aware frame planning, block layout,
forward/backward displacement patching, and branch-condition materialization remain unimplemented.

## 7. Conflicts and stop conditions

- Packet 004 requires `ELSEIF` only where the shared parser already represents it; the current
  parser does not. Adding it would be a public grammar change and is outside WP-000.
- Existing hosted lowering supports more control-flow forms than the UEFI x86-64 backend; the
  systems implementation must not silently accept and flatten them.
- No external assembler/linker is required by the current architecture. Any future branch-byte
  verification must remain an independent development check, not a runtime/build dependency.

WP-000 is complete. The next authorized step is WP-001's binding block-graph specification;
control-flow lowering and encoder changes should wait for that specification.
