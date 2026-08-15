# RFC-0023 implementation report

Status: Implemented on 2026-08-14.

The milestone includes RFC-0022 errors, independent `Random.Clone` resources, and hosted
instruction-limit requests. `#INSTRUCTION_LIMIT` is canonical compile metadata preserved in A-MIR,
serialized bytecode, bytecode execution, and native runtime capsules. Runtime policy defaults to
rejecting source authority for embeddings; first-party hosted tools authorize finite requests and
support operator `COUNT|unlimited` overrides. Hard maxima reject rather than clamp.

Validation covers clone state/lifecycle independence, directive grammar and policy precedence,
interpreter execution, VM execution, serialized bytecode, native capsules, and CLI failures.
Complete CTest suite passed, 37/37.
