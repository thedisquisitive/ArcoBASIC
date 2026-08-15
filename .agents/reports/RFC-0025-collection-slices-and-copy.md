# RFC-0025 implementation report

Status: Implemented on 2026-08-14.

ArcoBASIC now supports collection slices for arrays, strings, tuples, and bit vectors, including
omitted bounds, negative bounds, positive and negative steps, and Unicode code-point string slicing.
Mutable array slice replacement supports step-1 slices and resizes arrays. `COPY` performs explicit
shallow copying for arrays and objects while preserving scalar, immutable, handle, and class-instance
identity rules.

Validation covers runtime slicing/copy behavior, Unicode strings, bit vectors, shallow aliasing,
array replacement diagnostics, A-MIR/bytecode opcodes, serialized bytecode, and native capsules.
ArcoFission smoke passed. Full runtime test execution is currently not used as the gating signal
because dormant ArcoSH coverage fails before reaching these language tests.
