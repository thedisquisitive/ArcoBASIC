# RFC-0022 implementation report

Status: Implemented on 2026-08-14.

`THROW` is implemented in the lexer, parser, canonical AST, A-MIR, bytecode format, hosted VM,
interpreter, and native runtime capsule path. Thrown strings produce catch objects with
`Type = "UserError"`; ordinary failures use `RuntimeError`. Nested propagation, evaluation errors,
source locations, malformed statements, and `#RUNTIME NONE` rejection have focused coverage.

Validation: complete CTest suite passed, 37/37.
