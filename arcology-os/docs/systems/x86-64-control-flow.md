# x86-64 Freestanding Control Flow

Status: proposed
Depends on: Frontend to A-MIR Contract, x86-64 Code Generation

## 1. Purpose

The x86-64 systems backend now emits the minimum multi-block control flow needed for useful boot and hardware initialization code. This document remains the binding semantic reference for that implementation.

This is a backend expansion, not new ArcoBASIC syntax. Existing language constructs become available under `#RUNTIME NONE` when their expressions and bodies use supported freestanding operations.

## 2. Initial supported constructs

The first implementation supports:

```basic
IF condition THEN
    ...
ELSEIF otherCondition THEN
    ...
ELSE
    ...
END IF

WHILE condition
    ...
WEND

FOR counter = start TO finish
    ...
NEXT counter
```

`IF` and `WHILE` are required. `FOR` may be implemented in the same work package if its existing A-MIR form can be lowered without inventing a second loop model.

## 3. Required expression support

Conditions require freestanding lowering for:

- `=`
- `<>`
- `<`
- `<=`
- `>`
- `>=`
- `AND`
- `OR`
- `NOT`

Integer arithmetic required for loops and address offsets:

- `+`
- `-`
- `*`
- integer division when already represented in A-MIR;
- modulo when already represented in A-MIR.

This RFC does not authorize floating-point operations.

## 4. A-MIR contract

The backend consumes the existing canonical multi-block A-MIR. It must not reconstruct control flow from source tokens or AST text.

Each block has a stable label. Terminators include:

```text
BRANCH target
BRANCH_IF condition, trueTarget, falseTarget
RETURN value
CPU.HALT_FOREVER
```

`CPU.HALT_FOREVER` remains terminal and emits no fallthrough edge.

## 5. x86-64 lowering

The backend adds the minimum encodings required for:

- `CMP` and `TEST`;
- conditional jumps;
- unconditional jumps;
- label resolution and relative displacement patching;
- integer arithmetic used by supported expressions.

Forward and backward branch fixups are internal machine-code relocations. They are resolved before the PE image writer receives `.text`; they are not PE base relocations.

## 6. Stack and value model

The existing correctness-first spill model remains valid. Values may continue to occupy fixed stack slots across blocks.

The implementation must compute one stack frame for the whole function before emission. A value used in multiple blocks retains one stable slot.

No SSA register allocator is required.

## 7. Safety and diagnostics

The backend must reject unsupported control-flow shapes with deterministic diagnostics naming the unsupported A-MIR instruction or construct.

It must not:

- silently drop blocks;
- assume an unrecognized block is unreachable;
- append an implicit return after a terminal semantic;
- emit an invalid fallthrough from one source branch into another.

## 8. Minimum validation programs

### Conditional output

```basic
IF enabled THEN
    systemTable.ConsoleOut.Write("enabled\r\n")
ELSE
    systemTable.ConsoleOut.Write("disabled\r\n")
END IF
RETURN 0
```

### Bounded loop

```basic
LET count AS U32 = 0
WHILE count < 4
    count = count + 1
WEND
RETURN count
```

### Terminal branch

```basic
IF fatal THEN
    CPU.HaltForever
END IF
RETURN 0
```

The final case must not generate a fake edge from `CPU.HaltForever` to the return block.

## 9. Non-goals

This RFC does not add:

- exceptions or `TRY` lowering;
- recursion guarantees;
- switch/jump tables;
- optimization;
- phi-node register allocation;
- coroutines;
- scheduler-aware yielding.

## 10. Acceptance

The implementation is complete when:

1. `IF` and `WHILE` compile for the UEFI x86-64 target;
2. branch displacements are independently verified;
3. backward loops execute correctly under QEMU/OVMF;
4. `CPU.HaltForever` remains terminal inside branches;
5. reveal output exposes labels and branch targets deterministically;
6. existing straight-line programs remain byte-stable unless encoder refactoring makes a documented change necessary.
