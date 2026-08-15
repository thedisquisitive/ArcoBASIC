# RFC-0025: Collection Slices and Explicit Copying

**RFC Number:** RFC-0025  
**Title:** Collection Slices and Explicit Copying  
**Status:** Implemented  
**Category:** Language / Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0024, RFC-0026

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds half-open slice expressions and explicit shallow copying for arrays, strings,
objects, tuples, and bit vectors. It makes population selection, elitism, genome crossover, and
defensive collection copying concise without changing ordinary assignment aliasing.

------------------------------------------------------------------------

# 2. Motivation

ArcoBASIC can index collections but common operations such as `population[0:2]` and splitting a
genome at a crossover point require manual loops. Array/object assignment currently shares storage,
so a visible copy operation is also needed.

------------------------------------------------------------------------

# 3. Goals

- Define predictable zero-based half-open slicing.
- Support negative indices and an optional nonzero step.
- Add explicit shallow copies without altering assignment.
- Support slice replacement for mutable arrays.

------------------------------------------------------------------------

# 4. Non-Goals

- Deep graph cloning, arbitrary views, lazy slices, or implicit copy-on-assignment.
- Object slicing or string/tuple/bit-vector mutation through slice assignment.

------------------------------------------------------------------------

# 5. Terminology

**Slice:** A normalized half-open selection `[start:end:step]`.  
**Shallow Copy:** A new outer container whose contained reference-like values retain identity.

------------------------------------------------------------------------

# 6. Requirements

The parser MUST accept `value[start:end]`, `value[start:end:step]`, omitted bounds, and negative
indices for arrays, strings, tuples, and `BITVECTOR`. The default step is `1`; zero MUST fail.
Positive steps use half-open bounds. Negative steps traverse in reverse with Python-like normalized
bounds, but exact behavior is defined by normative tests rather than host-library delegation.

```basic
elite = population[0:2]
tail = genome[point:]
reversed = values[::-1]
```

Slices MUST return new values. String indices and bounds MUST be Unicode code-point based, matching
`String.Length` and `String.Slice` rather than UTF-8 byte offsets.

The unary expression `COPY value` MUST shallow-copy arrays and objects. It MUST return immutable
strings, numbers, booleans, nulls, tuples, and bit vectors unchanged by value. Handles and class
instances MUST preserve identity; `COPY` MUST NOT duplicate runtime resources.

Array slice assignment MUST accept `array[start:end] = replacementArray` for step `1`, resize the
array as necessary, and reject stepped assignment in this RFC.

------------------------------------------------------------------------

# 7. Architecture

AST and A-MIR gain `Slice` with optional start/end/step children and `Copy` expressions. Indexed
assignment gains a slice target. Interpreter and VM MUST share one normalization algorithm.

------------------------------------------------------------------------

# 8. User Experience

```basic
child = parentA[:point] + parentB[point:]
nextPopulation = COPY population[0:2]
```

------------------------------------------------------------------------

# 9. Developer Experience

Diagnostics MUST distinguish invalid target, zero step, non-integral bound, and invalid replacement.
`HELP slices` and `HELP copy` MUST include aliasing examples.

------------------------------------------------------------------------

# 10. Security Considerations

Normalized arithmetic and result-size calculations MUST be overflow-safe. Copying and replacement
must remain subject to allocation and instruction limits. `COPY` MUST NOT clone privileged handles.

------------------------------------------------------------------------

# 11. Privacy Considerations

No data is collected. Copying does not redact or alter contained values.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Documentation MUST explain half-open bounds with concrete diagrams or examples and not rely only on
mathematical notation.

------------------------------------------------------------------------

# 13. Performance Considerations

Slice creation is linear in result length. Whole-container `COPY` is linear for arrays/objects and
constant time for immutable values. Implementations MAY share immutable backing storage.

------------------------------------------------------------------------

# 14. Compatibility

Colon inside brackets gains slice meaning; existing statement-separator colons outside brackets are
unchanged. Assignment retains existing alias semantics unless `COPY` or slicing is explicit.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
values = [0, 1, 2, 3, 4]
PRINT values[1:4]
copy = COPY values
copy[0] = 9
PRINT values[0]
```

Expected output:

```text
[1, 2, 3]
0
```

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover every omitted-bound combination, positive/negative indices and steps, empty
results, Unicode strings, bit vectors, tuple preservation, array resizing assignment, shallow
nested aliases, handle identity, diagnostics, AST/A-MIR/bytecode parity, and regression of colon
statement separators.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement a single tested slice-normalization helper, then parser/AST, runtime values, assignment,
A-MIR/bytecode, documentation, and parity tests. Do not use host substring behavior as the language
specification, add deep copying, or change assignment. Stop if nested assignment cannot identify a
slice target without redesigning canonical l-values.

------------------------------------------------------------------------

# 18. Future Extensions

Stepped slice assignment, views, deep copy with cycle policy, and multidimensional slices.

------------------------------------------------------------------------

# 19. Open Questions

None; negative-bound behavior must be fixed by normative tables during implementation.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.
- RFC-0024, Bit Vector Values.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial slicing and explicit-copy proposal. |
| 1.0 | 2026-08-14 | Implemented in parser, interpreter, value layer, A-MIR, bytecode VM, and native capsules. |
