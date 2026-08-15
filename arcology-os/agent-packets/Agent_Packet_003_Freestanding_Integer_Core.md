# Arcology ArcoBASIC Systems Agent Packet 003
## Freestanding Integer Core

Status: ready for implementation
Target: ArcoBASIC / ArcoFission UEFI x86-64 systems path
Depends on:
- Systems Target RFC: UEFI x86-64
- Frontend to A-MIR Contract
- Calling Conventions
- x86-64 Code Generation
- current ArcoBASIC Systems-Level Reference

---

## 1. Mission

Implement the minimum fixed-width integer expression system required for useful freestanding ArcoBASIC hardware code.

The current UEFI x86-64 backend supports typed constants, loads, external calls, returns, and the existing CPU halt semantics, but it does not yet provide the integer operations needed to inspect status bits, construct register values, compare hardware state, calculate offsets, or drive control flow.

This packet adds those operations without changing the hosted Linux VM's behavior and without introducing a second parser, alternate systems-only expression grammar, or inline assembly.

---

## 2. Required Outcome

After this packet, a `#RUNTIME NONE` UEFI x86-64 program must be able to compile fixed-width integer expressions using:

### Arithmetic

```basic
left + right
left - right
left * right
left \ right
left MOD right
```

### Bitwise operations

```basic
left AND right
left OR right
left XOR right
NOT value
```

### Shifts

```basic
value SHL count
value SHR count
value SAR count
```

### Comparisons

```basic
left = right
left <> right
left < right
left <= right
left > right
left >= right
```

Comparison expressions produce `BOOL`.

The implementation must support these operations for the existing fixed-width systems types:

```text
U8 U16 U32 U64
I8 I16 I32 I64
BOOL
```

`PTR` is not treated as an ordinary arithmetic integer in this packet. Pointer and address arithmetic belong to the Address Semantics RFC.

---

## 3. Binding Language Decisions

### 3.1 No new expression syntax

Use the existing ArcoBASIC lexer, parser, precedence rules, canonical AST, and hosted-language operator spellings wherever they already exist.

This packet authorizes systems lowering for existing operations. It does not authorize a parallel systems expression language.

If one of the listed operator spellings is not currently recognized by the shared parser, stop and report the exact missing grammar surface before adding syntax.

### 3.2 Integer division

Backslash is the canonical integer-division operator:

```basic
result = dividend \ divisor
```

Slash is not added or redefined by this packet.

### 3.3 Right shifts

`SHR` is always a logical right shift and shifts zero bits into the high end.

`SAR` is always an arithmetic right shift and copies the sign bit.

The meaning of `SHR` must not silently change according to the signedness of the operand.

### 3.4 Fixed-width wrapping

Arithmetic and left shifts operate at the declared width and wrap modulo `2^width`.

Examples:

```basic
LET a AS U8 = 255
LET b AS U8 = a + 1       ' 0

LET c AS U8 = 128 SHL 1   ' 0
```

No overflow trap, saturation, or implicit widening is introduced.

Compile-time literal range checking remains in force for typed declarations.

### 3.5 Operand width and signedness

Binary integer operations initially require operands of the same declared fixed-width type.

```basic
LET small AS U8 = 1
LET large AS U16 = 2
LET bad = small + large   ' compile-time error
```

Do not invent implicit integer promotion rules.

`BOOL` may participate only in Boolean-compatible operations already accepted by the language. Do not silently treat `BOOL` as a general-purpose `U8` for arithmetic.

### 3.6 Comparison behavior

Equality and inequality compare the complete fixed-width value.

Relational operations use the operand type:

- `I8`, `I16`, `I32`, `I64`: signed comparison;
- `U8`, `U16`, `U32`, `U64`: unsigned comparison.

The result is a canonical systems `BOOL` value containing `0` or `1`.

### 3.7 Shift counts

The shift count is interpreted as an unsigned integer quantity.

For the first implementation, shifting by a count greater than or equal to the operand width must produce a deterministic result defined by the language, not accidental host or x86 behavior:

- `SHL` and `SHR`: result is zero;
- `SAR`: result is all zeroes for non-negative values and all ones for negative values.

Constant out-of-range counts may be folded, but must follow the same rule.

---

## 4. Architecture Contract

The parser remains the authority on accepted source and types. The canonical AST remains the sole structural input to A-MIR generation.

No backend component may reparse tokens or infer signedness from source spelling.

Every integer operation reaching the x86-64 backend must carry enough type information to determine:

- width;
- signedness;
- result type;
- comparison mode;
- shift mode.

---

## 5. A-MIR Requirements

Add or complete canonical typed operations equivalent to:

```text
INT.ADD
INT.SUB
INT.MUL
INT.DIV_SIGNED
INT.DIV_UNSIGNED
INT.MOD_SIGNED
INT.MOD_UNSIGNED

INT.AND
INT.OR
INT.XOR
INT.NOT

INT.SHL
INT.SHR
INT.SAR

INT.CMP_EQ
INT.CMP_NE
INT.CMP_LT_SIGNED
INT.CMP_LT_UNSIGNED
INT.CMP_LE_SIGNED
INT.CMP_LE_UNSIGNED
INT.CMP_GT_SIGNED
INT.CMP_GT_UNSIGNED
INT.CMP_GE_SIGNED
INT.CMP_GE_UNSIGNED
```

Equivalent names are acceptable only if there is one unambiguous canonical operation for each semantic.

A-MIR values must retain their fixed-width type. Do not collapse every operation into an untyped 64-bit integer and hope the backend remembers what happened.

`ArcoFission reveal FILE at A-MIR` must expose operation, operand types, and result type deterministically.

---

## 6. x86-64 Backend Requirements

Extend the existing correctness-first spill-based backend. A general register allocator is not required.

Implement the minimum encoder and lowering support for:

- fixed-width add, subtract, multiply;
- signed and unsigned divide and modulo;
- AND, OR, XOR, NOT;
- logical left shift;
- logical right shift;
- arithmetic right shift;
- signed and unsigned comparisons;
- canonical Boolean materialization as `0` or `1`.

Narrow values must be truncated or sign/zero extended according to their declared type whenever loaded, operated on, stored, or returned.

Division must handle x86-64 dividend preparation correctly for signed and unsigned operations. Do not share one lowering path where the architecture requires different sign-extension behavior.

Division by a compile-time constant zero must be rejected at compile time.

Runtime division by zero behavior is not expanded into an exception model by this packet. The backend may produce the architecture-defined fault, and this fact must be documented.

---

## 7. Constant Folding

Constant folding is permitted but not required.

If implemented, folded results must be bit-identical to runtime fixed-width behavior, including:

- wrapping;
- signed versus unsigned comparisons;
- logical versus arithmetic shifts;
- defined oversized-shift behavior;
- signed division and modulo rules.

The compiler must not use host-language undefined behavior while calculating constant results.

---

## 8. Diagnostics

Diagnostics must identify:

- unsupported operand type;
- mixed-width operands;
- invalid Boolean arithmetic;
- division by constant zero;
- use of `PTR` in ordinary integer arithmetic;
- any operation represented by the parser but not supported by the systems A-MIR or x86-64 backend.

Examples:

```text
operator AND requires matching fixed-width integer operands; received U8 and U16.
```

```text
PTR values do not support ordinary arithmetic in the UEFI systems profile. Use the address operations defined by Address Semantics.
```

Do not silently route unsupported systems expressions into hosted bytecode or runtime helpers.

---

## 9. Required Work Packages

### WP-000 Repository audit

Before editing:

1. locate the hosted parser and AST forms for every required operator;
2. locate existing hosted VM semantics and precedence rules;
3. identify current A-MIR representation, if any;
4. identify where systems type information is attached and where it is lost;
5. identify encoder gaps;
6. record conflicts or missing shared syntax.

Deliverable:

```text
.agents/reports/AP003-WP-000-repository-audit.md
```

### WP-001 Binding semantic matrix

Create a checked-in matrix covering every required operation by:

- operand type;
- result type;
- wrapping behavior;
- signedness;
- invalid combinations;
- expected A-MIR operation.

This matrix is authoritative for later work packages.

### WP-002 Canonical AST and semantic validation

Ensure each accepted systems expression has:

- a canonical AST representation;
- fixed-width operand types;
- deterministic semantic validation;
- source-located diagnostics.

Do not add a compiler-side parser.

### WP-003 A-MIR integer operations

Implement typed A-MIR lowering and deterministic reveal output.

Add unit and smoke tests proving signed and unsigned operations remain distinguishable.

### WP-004 x86-64 encoder expansion

Add encoder primitives required by WP-005.

Independently verify emitted bytes using a separate assembler or disassembler. Verification tools may be optional development aids, not build dependencies.

### WP-005 x86-64 lowering

Lower all required integer and comparison operations using the existing function-frame and spill model.

### WP-006 Diagnostics and unsupported cases

Add negative tests for every invalid category in section 8.

### WP-007 End-to-end validation

Build and boot representative UEFI programs under QEMU/OVMF. Verify returned values or printed result markers through existing bound console output.

### WP-008 Documentation

Update the systems-level reference with only the features actually implemented.

Do not describe Packet 004 control flow as implemented by this packet.

### WP-009 Completion report

Create:

```text
.agents/reports/AP003-freestanding-integer-core.md
```

Include:

- files changed;
- tests run;
- exact acceptance results;
- deviations;
- unresolved risks;
- any public syntax conflict encountered.

---

## 10. Required Validation Programs

### 10.1 Bit masks

```basic
LET status AS U8 = &H25
LET ready AS BOOL = (status AND &H20) <> 0
LET lowBits AS U8 = status AND &H0F
RETURN lowBits
```

### 10.2 Shifts

```basic
LET packed AS U16 = &HABCD
LET high AS U16 = packed SHR 8
LET signedValue AS I16 = -16
LET arithmetic AS I16 = signedValue SAR 2
RETURN high
```

### 10.3 Wrapping

```basic
LET value AS U8 = 255
LET wrapped AS U8 = value + 1
RETURN wrapped
```

### 10.4 Signed and unsigned comparisons

Use equal bit patterns represented as `I8` and `U8` and prove relational results differ where expected.

### 10.5 Division and modulo

Validate signed and unsigned quotient and remainder behavior with positive and negative operands where permitted by type.

### 10.6 Existing milestone regression

The existing UEFI hello-world and hardware halt images must still compile and boot.

---

## 11. Acceptance Criteria

Packet 003 is complete only when:

1. every operation in section 2 compiles for supported fixed-width systems types;
2. signed and unsigned behavior follows section 3;
3. comparisons materialize canonical `BOOL` values;
4. wrapping and oversized shifts follow the language rules exactly;
5. A-MIR retains type and signedness information;
6. generated machine code is independently verified;
7. invalid combinations fail with deterministic diagnostics;
8. hosted behavior remains unchanged;
9. existing systems programs remain functional;
10. the systems reference documents exactly the implemented surface.

---

## 12. Non-Goals

This packet does not add:

- `IF`, `WHILE`, `FOR`, or multi-block code generation;
- floating point or SIMD;
- pointer arithmetic;
- port I/O or MMIO;
- checked, saturating, or arbitrary-precision arithmetic;
- user-defined operator overloads;
- optimizer passes;
- a general register allocator;
- exception handling for division faults;
- binary literal syntax unless it already exists in the shared language.

---

## 13. Stop Conditions

Stop and report before proceeding if implementation would require any of the following:

- redefining an existing hosted operator's public meaning;
- adding a new public operator spelling not explicitly authorized here;
- inventing implicit numeric promotion rules;
- changing the size or meaning of an existing systems type;
- routing freestanding operations through the hosted VM or runtime;
- replacing the canonical AST to A-MIR contract;
- introducing a required external assembler, linker, or runtime dependency;
- selecting a pointer-conversion syntax on the project's behalf.

Small internal encoder and A-MIR naming decisions may be made without stopping when they preserve this packet's public semantics.
