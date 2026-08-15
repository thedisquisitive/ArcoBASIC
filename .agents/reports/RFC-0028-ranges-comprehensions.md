# RFC-0028 implementation report

Status: Implemented on 2026-08-14.

ArcoBASIC now supports immutable `RANGE` values through `Range(stop)`, `Range(start, stop)`, and
`Range(start, stop, step)`. Ranges are lazy arithmetic progressions with exclusive stop semantics,
positive and negative steps, `LEN`, zero-based indexing, `FOR IN`, `CONTAINS` / `IN`, `AS RANGE`,
`TYPEOF`, deterministic display, and zero-step / non-integral argument rejection.

The language also supports single-clause array comprehensions with one `FOR name IN iterable` and
one optional trailing `IF` filter. The iterable is evaluated once, filtering happens before result
evaluation, result order follows iteration order, and comprehension bindings do not leak.

Validation covers interpreter and hosted bytecode behavior, range direction/indexing/membership,
comprehension map/filter/order/scope, AST reveal, A-MIR loop lowering, serialized bytecode, and
native capsules. ArcoFission smoke passed. Full runtime test execution is currently not used as
the gating signal because dormant ArcoSH coverage fails before reaching these language tests.
