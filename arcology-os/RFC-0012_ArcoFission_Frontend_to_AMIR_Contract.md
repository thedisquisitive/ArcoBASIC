
# RFC-0012: ArcoFission Frontend → A-MIR Contract

**RFC Number:** RFC-0012  
**Title:** ArcoFission Frontend → A-MIR Contract  
**Status:** Draft  
**Category:** Compiler Architecture  
**Authors:** Arcology Project  
**Created:** 2026-08-03  
**Related RFCs:** RFC-0000, RFC-0004, RFC-0005

---

# 1. Executive Summary

This RFC defines the authoritative compilation pipeline for ArcoFission.

Its primary purpose is to ensure that every language feature follows a single, deterministic path from source code to executable output.

The compiler shall have **one authoritative interpretation** of a program.

---

# 2. Motivation

During the first UEFI milestone it was discovered that the parser and A-MIR generation interpreted source independently.

This created the possibility that syntax could parse correctly while silently disappearing later in the pipeline.

This RFC eliminates that class of bugs.

---

# 3. Guiding Principles

- One authoritative representation at every stage.
- No duplicated parsing.
- Earlier stages validate structure.
- Later stages never reinterpret source text.
- Errors should be reported as early as possible.

---

# 4. Compiler Pipeline

The authoritative pipeline is:

```text
Source
    ↓
Lexer
    ↓
Parser
    ↓
Abstract Syntax Tree (AST)
    ↓
Semantic Analysis
    ↓
A-MIR Generation
    ↓
Optimization
    ↓
Backend
    ↓
Executable
```

Every stage consumes the output of the previous stage.

---

# 5. Stage Responsibilities

## Lexer

Responsible only for converting source into tokens.

It SHALL NOT infer meaning.

---

## Parser

Responsible only for grammar.

Produces the canonical AST.

It SHALL NOT emit machine code or A-MIR.

---

## Abstract Syntax Tree

The AST is the compiler's authoritative structural representation.

Later stages consume the AST.

Later stages SHALL NOT reinterpret the original token stream.

The AST SHOULD be treated as immutable after successful parsing.

---

## Semantic Analysis

Responsible for:

- name resolution
- scope
- type checking
- profile validation
- capability validation
- compile-time diagnostics

Semantic analysis SHALL annotate the AST rather than replacing it.

---

## A-MIR Generation

A-MIR SHALL be generated exclusively from the semantically validated AST.

A-MIR represents program intent.

It does not contain architecture-specific instructions.

---

## Optimization

Optimization operates only on A-MIR.

Optimizations SHALL preserve observable program behavior.

---

## Backend

The backend lowers A-MIR into executable code for the selected target.

Instruction selection belongs exclusively to the backend.

---

# 6. Forbidden Behaviors

The compiler SHALL NOT:

- reparse source after the parser
- regenerate structure from raw tokens
- silently ignore supported syntax
- perform backend-specific parsing

---

# 7. Adding Language Features

Every new language feature SHALL update:

1. Grammar
2. AST construction
3. Semantic analysis
4. A-MIR generation
5. Every supported backend
6. Tests
7. Documentation

Skipping any stage constitutes an incomplete implementation.

---

# 8. AI Implementation Guidance

Autonomous coding agents SHALL treat the AST as the single source of truth.

Agents SHALL extend the existing pipeline rather than introducing alternate parsing paths.

If a stage cannot represent a new language feature, that stage SHALL be updated instead of bypassed.

---

# 9. Conformance Requirements

The compiler conforms to this RFC when:

- Every accepted construct reaches A-MIR.
- Unsupported constructs produce deterministic diagnostics.
- No language construct disappears silently.
- Every backend receives equivalent semantic input.

---

# 10. Definition of Done

Implemented when:

1. A-MIR generation consumes the authoritative AST.
2. Independent token reinterpretation has been removed.
3. Compiler stages have clearly documented responsibilities.
4. Conformance tests verify every pipeline stage.
5. New syntax requires updates through the complete pipeline.

---

# 11. Revision History

| Version | Date | Summary |
|---------|------|---------|
|0.1|2026-08-03|Initial draft|
