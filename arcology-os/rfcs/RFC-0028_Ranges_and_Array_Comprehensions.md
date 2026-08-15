# RFC-0028: Ranges and Array Comprehensions

**RFC Number:** RFC-0028  
**Title:** Ranges and Array Comprehensions  
**Status:** Implemented  
**Category:** Language / Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0027

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds lazy numeric `RANGE` values and single-clause array comprehensions. Population
creation, legal-move selection, genome conversion, and board scans become compact while retaining
ordinary ArcoBASIC loop semantics and instruction accounting.

------------------------------------------------------------------------

# 2. Motivation

Many PetriBrain loops exist only to create an array. Repeating accumulator initialization,
`Array.Add`, and index arithmetic obscures the algorithm without adding safety.

------------------------------------------------------------------------

# 3. Goals

- Add bounded, deterministic, lazy integer ranges.
- Add readable map/filter array comprehensions.
- Preserve lexical scope and instruction limits.

------------------------------------------------------------------------

# 4. Non-Goals

- Infinite ranges, generator functions, async iteration, parallel comprehensions, set/dictionary
  comprehensions, or multiple clauses in the initial RFC.

------------------------------------------------------------------------

# 5. Terminology

**Range:** An immutable iterable arithmetic progression.  
**Comprehension:** An expression that evaluates another expression for selected iterable elements.

------------------------------------------------------------------------

# 6. Requirements

The runtime MUST provide `Range(stop)`, `Range(start, stop)`, and `Range(start, stop, step)`. Bounds
and step MUST be safe integers; step cannot be zero. Stop is exclusive. Positive and negative steps
are supported. `RANGE` supports `LEN`, `FOR IN`, indexing, the `CONTAINS` operator, and deterministic
string display, but not mutation.

The parser MUST accept:

```basic
population = [RandomGenome(length, rng) FOR i IN Range(populationSize)]
empty = [i FOR i IN Range(9) IF board[i] = " "]
```

The initial grammar contains exactly one `FOR name IN expression` and one optional trailing `IF`.
The iterable is evaluated once. Each iteration binds a fresh local value visible only to the result
and filter expressions. The filter executes before the result expression. Result order follows
iteration order. Runtime errors propagate normally and discard the incomplete result.

Each iteration and evaluated expression MUST count under ordinary instruction accounting.

------------------------------------------------------------------------

# 7. Architecture

`RANGE` stores start, stop, step, and computed length without materializing elements. AST/A-MIR gain
an array-comprehension expression lowered to a scoped loop and append operation.

------------------------------------------------------------------------

# 8. User Experience

```basic
bits = [Random.Integer(0, 1, rng) FOR i IN Range(genomeLength)]
legal = [i FOR i IN Range(9) IF board[i] = " "]
```

------------------------------------------------------------------------

# 9. Developer Experience

`TYPEOF(Range(3))` returns `Range`. Diagnostics MUST identify invalid bounds, zero step, or a
non-iterable source. AST reveal MUST show result, binding, iterable, and optional filter distinctly.

------------------------------------------------------------------------

# 10. Security Considerations

Range length calculation MUST be overflow-safe. Large ranges and comprehensions remain bounded by
instruction and allocation limits and MUST not preallocate unchecked sizes.

------------------------------------------------------------------------

# 11. Privacy Considerations

No information is collected. Partially built results do not escape a failed comprehension.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Documentation MUST show the exclusive stop rule and equivalent expanded loop.

------------------------------------------------------------------------

# 13. Performance Considerations

Range construction is constant time and space. Comprehensions are linear in visited elements and
SHOULD reserve capacity when a safe exact length is known and no filter exists.

------------------------------------------------------------------------

# 14. Compatibility

`Range` becomes a core function/type name. Existing array literal syntax remains unchanged unless
the contained grammar has the new `FOR` form.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
squares = [i * i FOR i IN Range(1, 5)]
PRINT squares
```

Expected output: `[1, 4, 9, 16]`.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover every Range signature, both directions, empty and one-element ranges, zero step,
overflow, indexing, length, comprehension filtering/order/scope, single evaluation, failure cleanup,
instruction exhaustion, AST/A-MIR/bytecode, and hosted parity.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement range arithmetic and iteration first, then the comprehension AST and lowering. Reuse
existing `FOR IN` scope behavior. Do not materialize ranges or add hidden instruction exemptions.
Stop if comprehension locals leak or bytecode cannot preserve evaluation order.

------------------------------------------------------------------------

# 18. Future Extensions

Nested clauses, tuple destructuring targets, object comprehensions, and generator expressions.

------------------------------------------------------------------------

# 19. Open Questions

None for bounded integer ranges and one-clause array comprehensions.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial range and comprehension proposal. |
| 1.0 | 2026-08-14 | Implemented immutable ranges, range iteration/indexing/membership, single-clause array comprehensions, A-MIR lowering, bytecode VM, and native capsules. |
