# RFC-0029: Records and Enumerations

**RFC Number:** RFC-0029  
**Title:** Records and Enumerations  
**Status:** Draft  
**Category:** Language / Type System / Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0026, RFC-0031

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds immutable named records and typed enumerations. Board pieces, moves, coordinates, and
strategy settings can use checked domain values instead of magic strings and positional arrays.

------------------------------------------------------------------------

# 2. Motivation

Classes are heavier than fixed data and objects provide no declared shape. Checkers needs a closed
set of piece values and small fixed move records whose mistakes should fail at construction.

------------------------------------------------------------------------

# 3. Goals

- Define concise fixed-shape immutable values.
- Define closed typed member sets with optional scalar representations.
- Integrate both with annotations, equality, objects, arrays, modules, and tooling.

------------------------------------------------------------------------

# 4. Non-Goals

- Algebraic variants with payloads, pattern matching, record inheritance, methods, mutable fields,
  bit flags, or automatic database schemas.

------------------------------------------------------------------------

# 5. Terminology

**Record:** An immutable named value with declared fields.  
**Enumeration:** A named type containing a closed set of singleton members.

------------------------------------------------------------------------

# 6. Requirements

The parser MUST accept:

```basic
RECORD Move
    Source AS Number
    Destination AS Number
END RECORD

ENUM Piece
    Empty = " "
    Red = "r"
    RedKing = "R"
END ENUM
```

Records MUST construct positionally in declaration order, as in `Move(source, destination)`. Every
field is required unless its declaration has a constant default, and fields with defaults MUST
follow required fields. Field reads use property syntax; field writes MUST fail. Equality requires
the same record type and equal fields in declaration order. Named call arguments are outside this
RFC.

Enum members MUST be referenced as `Piece.Red`. Explicit values may be unique strings or safe
integers and MUST have one consistent scalar type within an enum. Omitted values auto-number from
zero. Duplicate member names or scalar values MUST fail. Enum values expose read-only `.Name` and
`.Value`. Equality requires the same enum type and member; no implicit scalar equality is allowed.

Record and enum names MUST work in `AS` annotations, arrays, objects, tuples, imports, A-MIR, and
hosted bytecode. `TYPEOF` MUST return `Record` or `Enum`; `CLASSOF(value)` MUST return the declared
record or enum type name.

------------------------------------------------------------------------

# 7. Architecture

Canonical declarations register immutable type metadata. Record values store type identity and
ordered fields. Enum values store type identity and member ordinal; scalar values remain metadata.

------------------------------------------------------------------------

# 8. User Experience

```basic
LET move AS Move = Move(12, 19)
LET piece AS Piece = Piece.Red
PRINT move.Destination
PRINT piece.Value
```

------------------------------------------------------------------------

# 9. Developer Experience

Diagnostics MUST identify missing, duplicate, unknown, and wrongly typed fields or members. Reveal
output and `HELP records`/`HELP enum` MUST display declaration order and defaults.

------------------------------------------------------------------------

# 10. Security Considerations

Constructors MUST validate before producing a value. Type identity cannot be forged by adding
metadata properties to an ordinary object. Deserialization support is outside this RFC.

------------------------------------------------------------------------

# 11. Privacy Considerations

No information is collected. Default display SHOULD avoid exposing private application data beyond
the explicitly stored fields.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Named fields and members reduce positional and magic-value ambiguity. Diagnostics MUST use declared
names rather than numeric ordinals alone.

------------------------------------------------------------------------

# 13. Performance Considerations

Construction and equality are linear in record field count and constant time for enums. Immutable
metadata SHOULD be shared.

------------------------------------------------------------------------

# 14. Compatibility

`RECORD` and `ENUM` become reserved declaration keywords. Existing classes and objects are
unchanged. Records do not implement class inheritance or interfaces.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
RECORD Move
    Source AS Number
    Destination AS Number
END RECORD

move = Move(5, 12)
PRINT move.Source
```

Expected output: `5`.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover declarations, defaults, positional construction, immutability, equality,
type annotations, invalid fields, enum auto/explicit values, duplicate rejection, type-safe
equality, module qualification, AST/A-MIR/bytecode, capsules, and anti-forgery behavior.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Reuse class/type registries only where immutable semantics remain distinct. Implement declarations,
metadata, values, constructors, type checks, compiler stages, then documentation/tests. Do not
lower records to untyped public objects or enums to bare numbers. Stop if identity cannot survive
bytecode/capsule boundaries.

------------------------------------------------------------------------

# 18. Future Extensions

Record update expressions, methods, tagged unions, pattern matching, flag enums, and serialization.

------------------------------------------------------------------------

# 19. Open Questions

None for immutable records and scalar-backed closed enums.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial records and enumerations proposal. |
