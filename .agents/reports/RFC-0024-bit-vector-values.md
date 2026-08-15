# RFC-0024 implementation report

Status: Implemented on 2026-08-14.

ArcoBASIC now has immutable packed `BITVECTOR` values, separator-aware `BITS "..."` literals,
zero-based indexing, `LEN`, equality, concatenation, `AS BITVECTOR`, and the complete RFC `Bits.*`
API. Constants preserve leading zeroes and exact length through canonical AST, A-MIR, bytecode,
serialized files, and native runtime capsules. Hosted-only diagnostics cover freestanding use.

Validation covers literals, invalid characters, empty/leading-zero values, transformations,
immutability, bound/type failures, interpreter behavior, VM behavior, serialization, and capsules.
Complete CTest suite passed, 37/37.
