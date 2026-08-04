
# RFC-0005: ArcoBASIC Hardware Semantics

**RFC Number:** RFC-0005  
**Title:** ArcoBASIC Hardware Semantics  
**Status:** Draft  
**Category:** Language / Systems  
**Authors:** Arcology Project  
**Created:** 2026-08-03  
**Related RFCs:** RFC-0000, RFC-0004

---

# 1. Executive Summary

This RFC defines how ArcoBASIC expresses low-level hardware operations without exposing assembly language to developers.

ArcoBASIC describes **intent**. The compiler lowers that intent through A-MIR into architecture-specific instructions.

---

# 2. Motivation

Modern systems programming generally requires developers to understand processor instruction sets, ABIs, and compiler intrinsics.

Arcology's goal is different:

A competent developer should be able to write firmware, bootloaders, drivers, kernels, and operating-system components without routinely reading an ISA manual.

Assembly remains an implementation detail owned by the compiler backend.

---

# 3. Goals

- Eliminate handwritten assembly for normal systems development.
- Keep hardware APIs architecture-independent.
- Express hardware operations as semantic intent.
- Allow multiple backend implementations.
- Produce deterministic machine code.

# 4. Non-Goals

This RFC does **not** define:

- MMIO
- Interrupt controllers
- Paging
- DMA
- PCI
- USB
- Networking
- ARM64 lowering
- Inline assembly syntax

---

# 5. Terminology

**Semantic Operation**

A language construct describing hardware intent.

**Backend**

Architecture-specific machine code generator.

**Lowering**

Translation from A-MIR semantics into target instructions.

---

# 6. Hardware Semantics Philosophy

Hardware semantics are **not wrappers around assembly instructions**.

They represent stable language concepts.

The backend is free to emit different instructions on different architectures while preserving identical observable behavior.

Programs should describe *what* the hardware should do, never *which instruction* should perform it.

---

# 7. Hardware Semantics Design Rules

## Rule 1: Express intent, not instructions

Good:

```basic
CPU.HaltForever
CACHE.Flush
MEMORY.Barrier
```

Poor:

```basic
HLT
MFENCE
WBINVD
```

---

## Rule 2: Architecture independence

Equivalent source code should compile for every supported architecture whenever equivalent semantics exist.

---

## Rule 3: Compiler-owned lowering

Instruction selection belongs exclusively to compiler backends.

Developers should not need inline assembly for ordinary systems programming.

---

## Rule 4: Semantic stability

Published semantic operations become part of the language contract.

Backend implementations may evolve without changing observable behavior.

---

## Rule 5: Consistent namespaces

Hardware semantics belong to logical namespaces.

Examples:

```basic
CPU.*
MEMORY.*
CACHE.*
ATOMIC.*
IO.*
PCI.*
POWER.*
INTERRUPT.*
```

---

## Rule 6: Safe by default

Capabilities and build profiles determine access to privileged hardware semantics.

---

## Rule 7: Explainability

Compiler diagnostics should reference semantic intent rather than raw machine instructions.

---

# 8. Namespace Architecture

Hardware semantics are organized by responsibility rather than processor vendor.

Future namespaces include:

- CPU
- MEMORY
- CACHE
- IO
- PCI
- DMA
- INTERRUPT
- POWER
- TIMER

---

# 9. Initial Semantic Operations

## CPU.Halt

Halts execution until resumed according to architecture rules.

## CPU.HaltForever

Terminal semantic.

Properties:

- Non-returning
- Non-fallthrough
- Terminal A-MIR operation

Backend determines the correct implementation.

## CPU.Pause

Indicates a processor pause within busy-wait loops.

---

# 10. User Experience

Hardware semantics should feel like ordinary language features.

The developer writes intent.

The compiler writes assembly.

### Permanently halt

```basic
CPU.HaltForever
```

No ISA knowledge required.

---

### Temporarily halt

```basic
PRINT "Waiting..."
CPU.Halt
```

Same source on every architecture.

---

### Busy wait

```basic
WHILE Lock.IsHeld
    CPU.Pause
WEND
```

The backend emits the optimal sequence.

---

### Future examples

```basic
MEMORY.Barrier

CACHE.Flush

ATOMIC.CompareExchange(...)
```

The programmer thinks about synchronization, caching, and memory ordering instead of instruction mnemonics.

---

# 11. Developer Experience

Developers should rarely ask:

> "Which instruction?"

Instead they should ask:

> "What behavior do I need?"

This keeps ArcoBASIC readable while remaining suitable for systems software.

---

# 12. A-MIR Contract

Hardware semantics become explicit A-MIR operations.

Examples:

```text
cpu.halt
cpu.halt_forever
cpu.pause
```

A-MIR contains semantics, never architecture-specific instructions.

---

# 13. Backend Responsibilities

Each backend MUST:

- Validate support.
- Emit correct machine code.
- Reject unsupported semantics.
- Preserve defined behavior.
- Produce deterministic output.

---

# 14. Conformance Requirements

A backend conforms when:

- All required semantics lower correctly.
- Unsupported semantics fail predictably.
- Generated behavior matches RFC definitions.

---

# 15. Security Considerations

Privileged semantics require appropriate build profiles and capabilities.

Normal user applications must not gain unrestricted hardware access.

---

# 16. Performance Considerations

Semantic operations should lower to the smallest correct instruction sequence available for the target architecture.

---

# 17. AI Implementation Guidance

Autonomous coding agents SHALL implement semantics rather than matching instruction names.

Agents SHALL extend existing namespaces before inventing new ones.

---

# 18. Future Extensions

Future RFCs will define:

- MMIO
- Atomics
- Interrupts
- DMA
- Paging
- Cache control
- Power management

---

# 19. Definition of Done

This RFC is implemented when:

1. CPU.Halt exists.
2. CPU.HaltForever exists.
3. CPU.Pause exists.
4. A-MIR represents all three.
5. x86-64 lowers all three.
6. Real hardware validates HaltForever.
7. Unsupported targets emit deterministic diagnostics.

---

# 20. Revision History

| Version | Date | Summary |
|---------|------|---------|
|0.1|2026-08-03|Initial complete draft|
