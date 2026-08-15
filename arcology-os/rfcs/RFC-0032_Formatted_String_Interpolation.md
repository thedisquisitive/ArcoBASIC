# RFC-0032: Formatted String Interpolation

**RFC Number:** RFC-0032  
**Title:** Formatted String Interpolation  
**Status:** Draft  
**Category:** Language / Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012

------------------------------------------------------------------------

# 1. Executive Summary

This RFC extends `$"..."` interpolation with a small deterministic format specification for width,
alignment, zero padding, decimal precision, and binary/hexadecimal output. PetriBrain progress and
DNA displays can retain their recognizable terminal layout without manual padding functions.

------------------------------------------------------------------------

# 2. Motivation

Interpolation currently converts values with default string rules only. Generation counters,
fitness values, difficulty labels, and encoded moves require repeated custom formatting.

------------------------------------------------------------------------

# 3. Goals

- Support the common numeric and textual formats needed by terminal applications.
- Keep formatting locale-independent and identical across hosted paths.
- Produce compile-time diagnostics for malformed literal specifications.

------------------------------------------------------------------------

# 4. Non-Goals

- Locale-aware formatting, dates, currencies, arbitrary user formatters, printf compatibility, or
  full Python/.NET format-language compatibility.

------------------------------------------------------------------------

# 5. Terminology

**Replacement Field:** An interpolation expression enclosed by braces.  
**Format Specifier:** Text following the field's top-level colon.

------------------------------------------------------------------------

# 6. Requirements

Interpolated fields MUST accept `{expression:spec}`. The initial grammar is:

```text
[[fill]align][sign][0][width][.precision][type]
align = < | > | ^
sign  = + | -
type  = s | d | f | b | x | X
```

Fill defaults to space; width and precision are non-negative decimal literals. `s` formats text,
`d` integral decimal, `f` finite decimal, `b` non-negative integral binary, and `x`/`X`
non-negative integral hexadecimal. Missing type uses ordinary conversion, except precision requires
`f`. Non-integral values with `d`, `b`, `x`, or `X` MUST fail.

`<`, `>`, and `^` align within minimum width. `0` pads numeric values after any sign. Precision on
`f` means exactly that many digits after the decimal point using round-half-away-from-zero. Output
MUST use ASCII digits and `.` regardless of host locale. Width never truncates. Existing `{{` and
`}}` escapes remain unchanged.

Required examples include `{generation:02d}`, `{difficulty:<10}`, `{fitness:>4.1f}`, and
`{move:04b}`.

------------------------------------------------------------------------

# 7. Architecture

The parser stores a validated format-spec AST beside each interpolation expression. One shared
formatter serves interpreter and VM; implementations MUST NOT delegate semantics to locale-sensitive
host formatting.

------------------------------------------------------------------------

# 8. User Experience

```basic
PRINT $"[{difficulty:<10}] Gen {generation:02d} | Fitness {fitness:>4.1f}"
PRINT $"Move DNA: {move:04b}"
```

------------------------------------------------------------------------

# 9. Developer Experience

Diagnostics MUST identify the unsupported flag/type or invalid value. `HELP strings` MUST include a
compact specifier table and escaping examples.

------------------------------------------------------------------------

# 10. Security Considerations

Width and precision MUST be capped by runtime allocation policy to prevent oversized output.
Expressions retain ordinary evaluation and capability rules.

------------------------------------------------------------------------

# 11. Privacy Considerations

Formatting does not collect data. Developers remain responsible for not interpolating secrets.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Alignment improves scanability but meaning MUST not depend solely on columns. Documentation MUST
remain usable in proportional fonts and narrow terminals.

------------------------------------------------------------------------

# 13. Performance Considerations

Formatting is linear in emitted length. Literal specifications MUST be parsed once, not on every
loop iteration.

------------------------------------------------------------------------

# 14. Compatibility

Existing fields without a top-level colon retain current output. A colon previously accepted as
part of an interpolation expression may require parentheses when ambiguous.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
PRINT $"{3:02d} {2.5:.1f} {5:04b}"
```

Expected output: `03 2.5 0101`.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover every type/alignment/sign/padding combination, zero width/precision, rounding,
negative values, invalid types, overflow caps, escaped braces, expression colons, locale
independence, Unicode fill, AST/A-MIR/bytecode, and capsule parity.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Define and unit-test the format parser and locale-independent renderer first, then integrate
interpolation and compiler stages. Do not call host printf/iostream locale formatting as the
specification or add unrequested format types. Stop if interpreter and capsule rounding differ.

------------------------------------------------------------------------

# 18. Future Extensions

Percentages, scientific notation, grouping separators, date/time values, and record formatters.

------------------------------------------------------------------------

# 19. Open Questions

None for the initial fixed mini-language.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial formatted-interpolation proposal. |
