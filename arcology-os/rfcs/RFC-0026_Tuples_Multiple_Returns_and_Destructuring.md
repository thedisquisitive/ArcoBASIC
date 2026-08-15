# RFC-0026: Tuples, Multiple Returns, and Destructuring

**RFC Number:** RFC-0026  
**Title:** Tuples, Multiple Returns, and Destructuring  
**Status:** Implemented  
**Category:** Language / Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0025, RFC-0029

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds immutable tuples and destructuring assignment. Functions still return one value, but
that value may be a tuple naturally unpacked by callers. Crossover offspring, coordinates, scored
moves, and quotient/remainder results become explicit fixed-shape data.

------------------------------------------------------------------------

# 2. Motivation

Arrays can emulate pairs, but they imply variable length and invite positional mistakes. Returning
two children or decomposing a board coordinate is common enough to deserve fixed-arity values and
checked unpacking.

------------------------------------------------------------------------

# 3. Goals

- Add immutable ordered fixed-arity tuple values.
- Add tuple literals, indexing, equality, iteration, and destructuring.
- Detect arity mismatches deterministically.

------------------------------------------------------------------------

# 4. Non-Goals

- Named fields, algebraic data types, pattern matching, variadic rest patterns, or mutable tuples.
- Multiple VM return registers; functions continue returning one `Value`.

------------------------------------------------------------------------

# 5. Terminology

**Tuple:** An immutable ordered fixed-length value.  
**Destructuring:** Binding or assigning tuple/array elements to multiple targets by position.

------------------------------------------------------------------------

# 6. Requirements

The parser MUST accept `(a, b)`, `(a,)`, and `()` tuple literals. Parenthesized single expressions
without a trailing comma remain grouping expressions. `TUPLE` MUST be a valid type annotation.

Tuples MUST support `LEN`, zero-based indexing, `FOR IN`, equality, nesting, and RFC-0025 slicing.
They MUST NOT support indexed assignment or mutating `Array.*` functions.

Bindings and assignments MUST accept:

```basic
LET (childA, childB) = Crossover(parentA, parentB)
(row, column) = DivMod(index, 8)
```

The right side MUST be an array or tuple with exactly matching arity. It MUST be evaluated once.
All element values MUST be captured before any target is assigned, so `(a, b) = (b, a)` swaps
correctly. Nested patterns MAY be implemented only if fully supported across all hosted paths; the
initial required grammar is flat identifier targets.

------------------------------------------------------------------------

# 7. Architecture

The value layer gains immutable tuple storage. AST/A-MIR gain tuple construction and a destructure
statement. Bytecode MAY lower destructuring into one evaluated temporary, arity check, indexed
loads, then stores.

------------------------------------------------------------------------

# 8. User Experience

```basic
FUNCTION Crossover(a, b)
    RETURN (a, b)
END FUNCTION

LET (first, second) = Crossover(parentA, parentB)
```

------------------------------------------------------------------------

# 9. Developer Experience

Diagnostics MUST report expected and actual arity. AST reveal MUST distinguish tuple literals from
grouping and destructuring from sequential assignment.

------------------------------------------------------------------------

# 10. Security Considerations

Tuple allocation and nesting remain subject to runtime limits. Destructuring MUST not partially
assign before validation completes.

------------------------------------------------------------------------

# 11. Privacy Considerations

No information is collected or exposed beyond ordinary values.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Documentation MUST explain the single-element trailing comma and offer arrays or records when
fixed positional data would be unclear.

------------------------------------------------------------------------

# 13. Performance Considerations

Tuple construction is linear in arity. Destructuring evaluates the source once and performs one
load per target. Immutable storage MAY be shared.

------------------------------------------------------------------------

# 14. Compatibility

Existing grouping syntax remains unchanged. A comma inside parentheses gains tuple meaning.
Functions and the C ABI still transport one ordinary ArcoBASIC value.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
a = 1
b = 2
(a, b) = (b, a)
PRINT a
PRINT b
```

Expected output is `2` then `1` on separate lines.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover zero/one/many elements, grouping ambiguity, indexing, equality, iteration, slicing,
immutability, exact arity errors, right-side single evaluation, swaps, function returns, typed
parameters/returns, AST/A-MIR/bytecode, capsules, and malformed patterns.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement value/type support before grammar, then tuple AST/A-MIR/bytecode and finally atomic
destructuring. Do not model tuples as publicly mutable arrays or add multiple-return ABI machinery.
Stop if the compiler cannot guarantee validation before stores.

------------------------------------------------------------------------

# 18. Future Extensions

Nested patterns, ignored targets, rest patterns, named tuples, and match expressions.

------------------------------------------------------------------------

# 19. Open Questions

None for flat exact-arity destructuring.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.
- RFC-0025, Collection Slices and Explicit Copying.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial tuple and destructuring proposal. |
| 1.0 | 2026-08-14 | Implemented immutable tuples, flat destructuring, type support, A-MIR, bytecode VM, and native capsules. |
