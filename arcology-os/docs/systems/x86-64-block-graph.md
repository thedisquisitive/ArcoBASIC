# Freestanding x86-64 A-MIR Block Graph

This document is the binding block-graph contract for the `#RUNTIME NONE` UEFI x86-64 path.

## Blocks and identity

- A function owns an ordered list of uniquely named basic blocks.
- The first block is the entry block; its name is `Entry`.
- Generated names use a stable prefix plus a monotonically increasing decimal suffix in source
  traversal order (`IfThen0`, `IfElse1`, `IfEnd2`, `WhileCond3`, and so on).
- A block label is the block name. Source line labels are separate A-MIR `LABEL` markers and are
  not block identities.

## Terminators and successors

Every reachable block ends in exactly one terminator:

| Terminator | Successors |
|---|---|
| `JUMP target` | `target` |
| `BRANCH condition, trueTarget, falseTarget` | `trueTarget`, `falseTarget` |
| `RETURN value` / `RETURN_VOID` | none |
| `CPU.HALT_FOREVER` | none |

`CPU.HALT` is resumable and is not a terminator by itself; a block containing it may continue to
the following instruction or explicit edge. Instructions after a terminal operation are invalid.

An `IF` has a branch block and only creates a merge block when at least one arm can reach it. A
`WHILE` uses a condition block, body block, explicit back edge, and exit block. Branches are
evaluated once on entry to their condition block; loop conditions are evaluated on every traversal
of the condition block.

## Reachability

Reachability starts at `Entry` and follows only terminator successors. Unreachable blocks are not
emitted for machine code. A reachable block with no valid terminator is a diagnostic. A reachable
non-void path may not fall off the function; terminal paths do not receive a synthetic return.

## Reveal format

The A-MIR reveal prints blocks in their deterministic ordered layout:

```text
BLOCK Entry
    ...
    BRANCH %cond, IfThen0, IfElse1
END BLOCK
```

Successors are the target operands of `JUMP` and `BRANCH`; no backend may reconstruct control flow
by reparsing source text or inferring edges from block names.
