# RFC-0024: Bit Vector Values

**RFC Number:** RFC-0024  
**Title:** Bit Vector Values  
**Status:** Implemented  
**Category:** Language / Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0023, RFC-0025

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds an immutable `BITVECTOR` value, `BITS "..."` literals, indexed reads, concatenation,
and a `Bits.*` API for compact genome, flag-stream, protocol, and binary-data manipulation. It lets
PetriBrain express genomes directly instead of representing every bit as a dynamic `Number`.

------------------------------------------------------------------------

# 2. Motivation

Numeric arrays can represent genomes, but they waste space, admit values other than zero and one,
and obscure intent. Strings are compact but make bit counting and mutation needlessly indirect.

------------------------------------------------------------------------

# 3. Goals

- Add a compact, deterministic, value-semantic bit sequence.
- Support construction, reading, slicing, concatenation, counting, replacement, and conversion.
- Preserve interpreter, bytecode, capsule, and embedding parity.

------------------------------------------------------------------------

# 4. Non-Goals

- Arbitrary-precision integer arithmetic.
- Mutable aliasing, atomic operations, cryptography, or raw pointer access.
- Implicit conversion between bit vectors, numbers, strings, and byte arrays.

------------------------------------------------------------------------

# 5. Terminology

**Bit Vector:** An ordered zero-indexed sequence whose elements are exactly `0` or `1`.  
**Bit Length:** The number of stored bits, including leading zeroes.

------------------------------------------------------------------------

# 6. Requirements

`BITVECTOR` MUST be a runtime type accepted by `AS BITVECTOR`. The lexer/parser MUST accept:

```basic
genome = BITS "00011011"
```

Only ASCII `0`, ASCII `1`, and `_` separators are valid between quotes; separators do not become
bits. Empty `BITS ""` is valid. Invalid characters MUST be source-located errors.

Indexing MUST return numeric `0` or `1`. `LEN(bits)` MUST return bit length. Equality MUST compare
length and contents. `+` MUST concatenate two bit vectors. Bit vectors MUST have immutable value
semantics: assignment may share private storage, but no public operation may mutate an existing
value.

The runtime MUST provide:

```text
Bits.FromString(text)        Bits.ToString(bits)
Bits.FromArray(values)       Bits.ToArray(bits)
Bits.Get(bits, index)        Bits.Set(bits, index, value)
Bits.Flip(bits, index)       Bits.Count(bits, value = 1)
Bits.Slice(bits, start, length = remaining)
Bits.Replace(bits, start, length, replacement)
Bits.Reverse(bits)
```

Transforming operations MUST return new values. Indices MUST be integral and bounds-checked.
`Bits.FromArray` and `Bits.Set` MUST reject values other than integral zero or one.

------------------------------------------------------------------------

# 7. Architecture

The canonical value layer gains a packed bit container with explicit bit length. AST adds a bit
literal node; A-MIR and bytecode add a bit constant representation. Hosted serialization MUST
preserve leading zeroes and exact length.

------------------------------------------------------------------------

# 8. User Experience

```basic
parent = BITS "1111_0000"
mutated = Bits.Flip(parent, 3)
PRINT Bits.ToString(mutated)
```

------------------------------------------------------------------------

# 9. Developer Experience

APIs use zero-based indexing consistently with arrays. Diagnostics MUST name the operation, index,
length, and invalid bit value where applicable. `HELP bits` MUST document literals and functions.

------------------------------------------------------------------------

# 10. Security Considerations

Length arithmetic and packed allocation MUST be overflow-checked. Conversions MUST respect runtime
allocation and instruction limits. This API is not a cryptographic bit-string abstraction.

------------------------------------------------------------------------

# 11. Privacy Considerations

No information is collected. Diagnostics SHOULD NOT dump an entire large bit vector.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Underscore separators improve readability. Diagnostics MUST identify positions textually and not
depend on color.

------------------------------------------------------------------------

# 13. Performance Considerations

Storage SHOULD use packed bits. Indexing, set, and flip SHOULD be constant time; count and
conversion are linear. Implementations MAY use copy-on-write internally while preserving value
semantics.

------------------------------------------------------------------------

# 14. Compatibility

`BITS` becomes reserved when followed by a string literal. Existing bitwise numeric operations are
unchanged. Arrays remain dynamic collections and do not become bit vectors implicitly.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
genome = BITS "00011011"
PRINT LEN(genome)
PRINT Bits.Count(genome)
PRINT Bits.ToString(Bits.Flip(genome, 0))
```

Expected output:

```text
8
4
10011011
```

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover empty and separated literals, invalid characters, leading zeroes, every public
operation, boundary failures, value semantics after assignment, large vectors, AST/A-MIR/bytecode
roundtrips, hosted parity, and deterministic freestanding rejection until separately specified.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement in this order: value representation, lexer/parser and canonical AST, equality/string
rendering, runtime helpers, A-MIR/bytecode constants, compiler parity, documentation, tests.

Do not represent public bits as floating-point arrays, silently coerce invalid values, expose
storage words, or add implicit numeric conversion. Stop if bytecode cannot preserve exact length or
if value semantics require changing array/object semantics.

------------------------------------------------------------------------

# 18. Future Extensions

Bitwise vector operations, byte-order conversions, fixed-length annotations, and freestanding
lowering are deferred.

------------------------------------------------------------------------

# 19. Open Questions

None for the initial immutable hosted value.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.
- RFC-0023, Hosted Evolutionary Workload Support.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial bit-vector value proposal. |
