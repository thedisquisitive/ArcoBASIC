# RFC-0027 implementation report

Status: Implemented on 2026-08-14.

ArcoBASIC now supports first-class `CALLABLE` values from `ADDRESSOF`, indirect invocation with
ordinary call syntax, `AS CALLABLE`, callable `TYPEOF`, named host/user functions, and bound
instance-method callables. `Array.SortBy`, `Array.MinBy`, and `Array.MaxBy` perform stable keyed
ordering, evaluate keys once per item, accept numeric or string keys, reject mixed/unsupported keys,
and return first ties for extrema.

Validation covers interpreter callable invocation, default arguments, typed callable parameters,
bound methods, stable ascending/descending ordering, extrema helpers, key evaluation count,
diagnostics, A-MIR/bytecode opcode 29, bytecode-local callable parameters, serialized bytecode,
and native capsules. ArcoFission smoke passed. Full runtime test execution is currently not used
as the gating signal because dormant ArcoSH coverage fails before reaching these language tests.
