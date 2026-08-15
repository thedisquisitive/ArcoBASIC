# Arcology ArcoBASIC Systems Agent Packet 004
## Freestanding Control Flow

Status: ready after Agent Packet 003
Target: ArcoBASIC / ArcoFission UEFI x86-64 systems path
Depends on:
- Agent Packet 003: Freestanding Integer Core
- x86-64 Freestanding Control Flow RFC
- Frontend to A-MIR Contract
- x86-64 Code Generation
- CPU hardware semantics
- current ArcoBASIC Systems-Level Reference

---

## 1. Mission

Extend the UEFI x86-64 systems backend from one straight-line A-MIR block to the minimum deterministic multi-block control flow needed for hardware initialization, polling, bounded waits, branching diagnostics, and later driver work.

This packet enables existing ArcoBASIC control-flow constructs under `#RUNTIME NONE`. It is not permission to invent new loop syntax, a systems-only parser, exceptions, coroutines, or optimizer machinery.

---

## 2. Required Outcome

After this packet, freestanding UEFI x86-64 programs must support:

```basic
IF condition THEN
    ...
ELSEIF otherCondition THEN
    ...
ELSE
    ...
END IF
```

```basic
WHILE condition
    ...
WEND
```

`IF` and `WHILE` are mandatory.

`ELSEIF` and `ELSE` are mandatory when already represented by the shared parser and canonical AST.

`FOR ... NEXT` is explicitly deferred unless section 13's optional extension is authorized after the mandatory work is complete.

Conditions may use operations implemented by Packet 003.

---

## 3. Binding Language Decisions

### 3.1 Existing syntax only

Use the exact control-flow syntax already accepted by ArcoBASIC's shared frontend.

Do not add alternate forms such as:

```basic
ENDIF
LOOP WHILE
DO UNTIL
```

unless they already exist and are already canonical language syntax.

### 3.2 Boolean conditions

A control-flow condition must have type `BOOL`.

Do not introduce C-style integer truthiness for systems code.

```basic
LET count AS U32 = 1
IF count THEN             ' compile-time error
END IF
```

The developer must write an explicit comparison:

```basic
IF count <> 0 THEN
END IF
```

### 3.3 Evaluation order

Conditions are evaluated exactly once each time control reaches them.

`ELSEIF` conditions are evaluated in source order and only after all preceding conditions in the chain evaluate false.

### 3.4 Loop behavior

A `WHILE` condition is evaluated before each iteration.

A false initial condition executes the body zero times.

No implicit iteration limit, watchdog service, scheduler yield, or `CPU.Pause` is inserted.

### 3.5 Terminal semantics

`CPU.HaltForever` remains terminal wherever it appears.

A block ending in `CPU.HaltForever` has no fallthrough edge, no implicit return, and no synthetic jump to a merge block.

`CPU.Halt` remains resumable and may fall through.

---

## 4. Canonical AST Contract

The parser remains the only owner of source structure.

Every accepted branch or loop must reach A-MIR through the canonical AST. The backend must not:

- scan tokens;
- reinterpret source text;
- rebuild an `IF` or loop tree;
- infer branch nesting from indentation;
- attach systems-only meaning to hosted parser artifacts.

If the current canonical AST cannot represent a required existing construct without ambiguity, stop and report the exact structural gap.

---

## 5. A-MIR Control-Flow Contract

A-MIR must represent a function as stable, ordered basic blocks with explicit terminators.

Required terminator semantics:

```text
BRANCH target
BRANCH_IF condition, trueTarget, falseTarget
RETURN value
RETURN_VOID
CPU.HALT_FOREVER
```

Equivalent internal names are acceptable.

Rules:

1. Every reachable block ends with exactly one terminator.
2. A block may not contain instructions after a terminal operation.
3. `CPU.HALT_FOREVER` has no successor.
4. An `IF` merge block exists only when at least one branch can reach it.
5. A `WHILE` loop has an explicit condition block, body block, and exit block.
6. Back edges are explicit.
7. Block labels are deterministic for identical source.
8. A-MIR reveal output shows blocks and successors clearly.

The backend consumes this graph. It must not reconstruct it.

---

## 6. Required A-MIR Shapes

### 6.1 Simple `IF`

```text
entry:
    ... condition ...
    BRANCH_IF %cond, if.then, if.end

if.then:
    ... body ...
    BRANCH if.end

if.end:
    ...
```

### 6.2 `IF` / `ELSE`

```text
entry:
    BRANCH_IF %cond, if.then, if.else

if.then:
    ...
    BRANCH if.end

if.else:
    ...
    BRANCH if.end

if.end:
    ...
```

A terminal branch does not receive a synthetic branch to `if.end`.

### 6.3 `ELSEIF`

Represent as an ordered chain of condition blocks, not as duplicated source parsing in the backend.

### 6.4 `WHILE`

```text
entry:
    BRANCH while.cond

while.cond:
    ... condition ...
    BRANCH_IF %cond, while.body, while.end

while.body:
    ...
    BRANCH while.cond

while.end:
    ...
```

---

## 7. x86-64 Lowering Requirements

Extend the instruction encoder and function generator with:

- unconditional relative jumps;
- required conditional relative jumps;
- block-label definition;
- forward fixups;
- backward fixups;
- deterministic relative-displacement patching;
- comparison-result consumption from Packet 003;
- whole-function stack-slot planning across all blocks.

Branch fixups are internal `.text` layout fixups. They are not PE base relocations and must be fully resolved before the PE writer receives machine code.

Short or near jump selection is an implementation decision, but it must be deterministic. A correctness-first implementation may use one fixed displacement width for all branches.

No external assembler or linker may become a required dependency.

---

## 8. Stack and Value Lifetime

The current spill-based strategy remains authoritative for this packet.

Before code emission, compute one frame layout for the entire function:

- parameters retain stable slots;
- temporaries used across blocks retain stable slots;
- values must not be assigned conflicting offsets in different blocks;
- the frame remains valid at every call site;
- Microsoft x64 shadow-space and alignment rules remain satisfied.

A general SSA allocator and phi-node register allocation are not required.

Where control-flow merging requires a source variable to hold a value assigned in multiple branches, use the variable's stable storage location rather than inventing an optimizer-grade phi system.

---

## 9. Reachability and Function Completion

The compiler must perform enough reachability analysis to avoid invalid implicit returns and dead fallthrough edges.

Required rules:

- If every reachable path is terminal or explicitly returns, do not append an implicit return.
- If a reachable non-void path reaches function end without a return, preserve the existing language diagnostic or add a deterministic systems diagnostic.
- Unreachable source after `CPU.HaltForever` must not be lowered within the same block.
- Unreachable blocks may remain in diagnostic A-MIR only if clearly marked and never emitted; preferably omit them.
- Do not treat an unsupported block as unreachable merely to make compilation succeed.

---

## 10. Diagnostics

Add deterministic diagnostics for:

- non-`BOOL` conditions;
- unsupported control-flow AST or A-MIR shapes;
- a reachable non-void path without return;
- instructions after a terminal operation in one block;
- malformed block graph;
- unresolved or duplicate labels;
- branch displacement overflow, should the selected encoding impose a limit;
- any control-flow construct still unsupported by the x86-64 systems target.

Example:

```text
WHILE condition must be BOOL under #RUNTIME NONE; received U32. Compare the value explicitly.
```

Never silently omit a branch, flatten a loop, or route the function through hosted bytecode.

---

## 11. Required Work Packages

### WP-000 Repository audit

Before editing:

1. locate shared parser and canonical AST forms for `IF`, `ELSEIF`, `ELSE`, and `WHILE`;
2. inspect current hosted A-MIR control-flow representation;
3. determine whether canonical blocks and terminators already exist;
4. identify current implicit-return behavior;
5. map terminal hardware semantics through parser, AST, and A-MIR;
6. identify x86-64 encoder gaps;
7. record any conflict with this packet.

Deliverable:

```text
.agents/reports/AP004-WP-000-repository-audit.md
```

### WP-001 Binding block-graph specification

Check in a concise authoritative document describing:

- block identity;
- ordering;
- terminators;
- successor rules;
- terminal semantics;
- reveal formatting.

Do not leave graph shape to emerge accidentally from implementation.

### WP-002 Canonical AST to A-MIR lowering

Implement or complete multi-block lowering using only canonical AST input.

Add unit tests for all required graph shapes.

### WP-003 A-MIR validation

Add validation that each reachable block has one valid terminator and all successor labels resolve.

### WP-004 x86-64 branch encoder

Implement branch and fixup primitives. Independently verify exact bytes and displacement calculations with a separate assembler or disassembler.

### WP-005 Whole-function frame planning

Expand stack-slot collection from one block to the complete reachable function graph.

### WP-006 x86-64 multi-block lowering

Emit blocks, patch branches, preserve calls and stack alignment, and respect terminal blocks.

### WP-007 `IF` family validation

Compile and boot simple, nested, chained, and terminal-branch cases.

### WP-008 `WHILE` validation

Compile and boot zero-iteration, finite forward-progress, nested-branch, and terminal-body cases.

### WP-009 Negative diagnostics

Add tests for each section 10 diagnostic category.

### WP-010 Regression validation

Run all existing systems tests and ensure straight-line programs still compile and boot.

### WP-011 Documentation

Update the systems-level reference to mark only implemented control-flow constructs as available.

### WP-012 Completion report

Create:

```text
.agents/reports/AP004-freestanding-control-flow.md
```

Include files changed, tests, exact acceptance results, deviations, and unresolved risks.

---

## 12. Required Validation Programs

### 12.1 Conditional output

```basic
LET enabled AS BOOL = TRUE

IF enabled THEN
    systemTable.ConsoleOut.Write("enabled\r\n")
ELSE
    systemTable.ConsoleOut.Write("disabled\r\n")
END IF

RETURN 0
```

Expected output:

```text
enabled
```

### 12.2 Chained condition

```basic
LET value AS U8 = 2

IF value = 1 THEN
    systemTable.ConsoleOut.Write("one\r\n")
ELSEIF value = 2 THEN
    systemTable.ConsoleOut.Write("two\r\n")
ELSE
    systemTable.ConsoleOut.Write("other\r\n")
END IF

RETURN 0
```

### 12.3 Zero-iteration loop

```basic
LET count AS U32 = 4

WHILE count < 4
    count = count + 1
WEND

RETURN count
```

The body must execute zero times.

### 12.4 Finite backward loop

```basic
LET count AS U32 = 0

WHILE count < 4
    count = count + 1
WEND

RETURN count
```

Prove the backward branch executes correctly under QEMU/OVMF.

### 12.5 Terminal branch

```basic
LET fatal AS BOOL = TRUE

IF fatal THEN
    CPU.HaltForever
END IF

RETURN 0
```

The true branch must have no edge to the return block.

### 12.6 Conditional terminal branch with live alternate path

```basic
LET fatal AS BOOL = FALSE

IF fatal THEN
    CPU.HaltForever
ELSE
    systemTable.ConsoleOut.Write("continuing\r\n")
END IF

RETURN 0
```

### 12.7 Nested control flow

A finite `WHILE` containing an `IF` must compile and execute correctly.

### 12.8 Existing milestone regression

The original UEFI hello-world and hardware halt artifact remain functional.

---

## 13. Optional Extension: `FOR ... NEXT`

`FOR ... NEXT` may be added only after all mandatory acceptance criteria pass and only when:

- the shared parser already supports the syntax;
- the canonical AST has a stable representation;
- its iteration semantics are already defined by ArcoBASIC;
- no new public behavior must be selected;
- it lowers naturally through the same block and integer machinery.

If any behavior is ambiguous, defer it. Do not let a pleasant loop become a semantic raccoon in the ventilation ducts.

Optional `FOR` work must not delay, destabilize, or dilute mandatory `IF` and `WHILE` completion.

---

## 14. Acceptance Criteria

Packet 004 is complete only when:

1. `IF`, `ELSEIF`, `ELSE`, and `WHILE` compile under the UEFI x86-64 target where supported by the shared syntax;
2. all conditions require `BOOL`;
3. A-MIR exposes deterministic basic blocks and explicit terminators;
4. forward and backward branches are patched correctly and independently verified;
5. loops execute under QEMU/OVMF;
6. whole-function stack planning works across blocks;
7. `CPU.HaltForever` remains terminal inside branches and loops;
8. invalid graphs and unsupported constructs fail clearly;
9. the hosted path remains unchanged;
10. existing straight-line systems programs remain functional;
11. documentation matches the implemented surface.

---

## 15. Non-Goals

This packet does not add:

- `TRY`, exceptions, or unwinding;
- `BREAK` or `CONTINUE` unless separately authorized and already semantically complete;
- `SELECT CASE` or jump tables;
- recursion guarantees;
- functions with fifth-and-later parameters;
- floating point or SIMD;
- optimizer passes;
- SSA construction or phi-node allocation;
- coroutines, tasks, threads, or scheduler integration;
- implicit `CPU.Pause` or timeout behavior;
- port I/O, MMIO, interrupts, or paging.

---

## 16. Stop Conditions

Stop and report before proceeding if implementation would require:

- adding or changing public control-flow syntax;
- choosing truthiness rules not already authorized here;
- changing hosted control-flow semantics;
- reparsing source outside the frontend;
- replacing canonical AST as A-MIR's structural input;
- introducing a required external assembler or linker;
- inventing exception, `BREAK`, `CONTINUE`, or `FOR` behavior;
- weakening `CPU.HaltForever` terminal semantics;
- silently dropping unsupported blocks or instructions.

Internal label names, branch encoding width, block container types, and fixup data structures may be selected without stopping when they preserve this packet's public and diagnostic contract.
