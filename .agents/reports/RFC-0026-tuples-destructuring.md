# RFC-0026 implementation report

Status: Implemented on 2026-08-14.

ArcoBASIC now supports immutable tuples, tuple literals `()`, `(value,)`, and `(a, b)`, `AS TUPLE`,
tuple display/equality/indexing/iteration/slicing, tuple returns, and flat exact-arity destructuring.
Destructuring evaluates the source once, validates arity before assignment, and supports atomic swaps.

Validation covers tuple construction, grouping ambiguity, function returns, typed parameters/returns,
iteration, slicing, immutability failures, exact arity errors, A-MIR/bytecode opcodes, serialized
bytecode, and native capsules. ArcoFission smoke passed. Full runtime test execution is currently
not used as the gating signal because dormant ArcoSH coverage fails before reaching these language
tests.
