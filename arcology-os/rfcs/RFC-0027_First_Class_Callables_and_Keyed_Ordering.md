# RFC-0027: First-Class Callables and Keyed Collection Ordering

**RFC Number:** RFC-0027  
**Title:** First-Class Callables and Keyed Collection Ordering  
**Status:** Implemented  
**Category:** Language / Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0028

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds first-class references to named functions and methods plus stable key-based ordering
helpers. Evolution code can rank genomes with `Array.SortBy(population, ADDRESSOF Fitness)` without
adding anonymous-function syntax or embedding application callbacks in the runtime.

------------------------------------------------------------------------

# 2. Motivation

ArcoBASIC functions can be called only by static name. Algorithms therefore cannot receive a
fitness function, and sorting by computed fitness requires verbose hand-written loops.

------------------------------------------------------------------------

# 3. Goals

- Represent named callables as safe runtime values.
- Invoke callable values with ordinary call syntax.
- Add stable `SortBy`, `MinBy`, and `MaxBy` collection operations.
- Preserve type checks and access control.

------------------------------------------------------------------------

# 4. Non-Goals

- Closures, captured local variables, anonymous lambdas, reflection, dynamic code generation,
  function serialization, or raw code pointers.

------------------------------------------------------------------------

# 5. Terminology

**Callable:** An opaque safe reference to a registered ArcoBASIC or hosted function.  
**Key Function:** A one-argument callable evaluated once per collection element for ordering.

------------------------------------------------------------------------

# 6. Requirements

The parser MUST accept `ADDRESSOF Qualified.Name` and return a `CALLABLE` value. Name resolution,
visibility, class access, and import alias rules MUST be checked when the expression executes.
Unknown or inaccessible names MUST fail deterministically.

A callable value MUST support `callable(arguments...)`. It MUST preserve required/default argument
validation and return-type enforcement. It MUST NOT reveal host addresses or survive serialization
as executable authority.

Bound instance methods MUST be expressible as `ADDRESSOF instance.Method`; the callable retains the
receiver identity safely. Unbound instance methods are outside this RFC.

The runtime MUST provide:

```text
Array.SortBy(values, keyCallable, descending = FALSE)
Array.MinBy(values, keyCallable)
Array.MaxBy(values, keyCallable)
```

`SortBy` MUST return a new array, evaluate the key exactly once per item, and be stable. Keys MUST
all be mutually orderable numbers or strings; mixed or unsupported keys MUST fail. `MinBy` and
`MaxBy` MUST return the first item on ties and reject empty input.

------------------------------------------------------------------------

# 7. Architecture

Callable values use opaque runtime-managed identity, not numeric addresses. AST/A-MIR/bytecode gain
callable reference and indirect call operations. Sort helpers create `(key, originalIndex, value)`
records internally to guarantee stability.

------------------------------------------------------------------------

# 8. User Experience

```basic
ranked = Array.SortBy(population, ADDRESSOF Fitness, TRUE)
best = Array.MaxBy(population, ADDRESSOF Fitness)
```

------------------------------------------------------------------------

# 9. Developer Experience

`TYPEOF` returns `Callable`; `AS CALLABLE` is supported. Diagnostics MUST include the callable name
when available without exposing implementation addresses.

------------------------------------------------------------------------

# 10. Security Considerations

Callables MUST obey existing visibility and host-function registration boundaries. They cannot be
forged from numbers or strings. Sorting remains subject to instruction limits, including callback
execution.

------------------------------------------------------------------------

# 11. Privacy Considerations

Callable display MUST not expose host addresses. No call history is collected.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Named functions keep stack-free diagnostics readable. Documentation SHOULD prefer descriptive key
function names.

------------------------------------------------------------------------

# 13. Performance Considerations

`SortBy` calls the key exactly `n` times and sorts in `O(n log n)` time. It MUST NOT recompute keys
during comparisons. `MinBy` and `MaxBy` are linear.

------------------------------------------------------------------------

# 14. Compatibility

`ADDRESSOF` becomes reserved. Direct named calls are unchanged. Existing `Array.Sort` behavior is
unchanged; keyed ordering is additive.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
FUNCTION Score(item)
    RETURN item.Score
END FUNCTION

ranked = Array.SortBy([{"Score": 1}, {"Score": 3}], ADDRESSOF Score, TRUE)
PRINT ranked[0].Score
```

Expected output: `3`.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover global/imported/hosted/bound-method callables, defaults and types, inaccessible
names, indirect invocation, single key evaluation, numeric/string order, stable ties, descending
order, empty extrema, callback failure propagation, AST/A-MIR/bytecode, capsules, and non-forgeability.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement callable representation and indirect invocation before collection helpers. Reuse runtime
name resolution and access checks. Do not store raw pointers, add closures, or make callables
persistable. Stop if bound receivers cannot retain safe runtime identity.

------------------------------------------------------------------------

# 18. Future Extensions

Closures, anonymous expressions, callable interfaces, filtering/folding, and comparator callables.

------------------------------------------------------------------------

# 19. Open Questions

None; closures are deliberately deferred.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial safe callable and keyed-ordering proposal. |
| 1.0 | 2026-08-14 | Implemented first-class named/bound callables, indirect invocation, `AS CALLABLE`, `Array.SortBy`, `Array.MinBy`, `Array.MaxBy`, A-MIR, bytecode VM, and native capsules. |
