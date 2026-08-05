# Frontend to A-MIR Contract

RFC-0012 makes the parser's canonical AST the sole structural input to A-MIR generation. Lexer
tokens are consumed by the parser and are not passed to the A-MIR builder.

## Pipeline

```text
source -> lexer/preprocessor -> parser + semantic checks -> canonical AST -> AstAmirBuilder -> A-MIR
```

`CanonicalAstNode` in `src/frontend/parser.hpp` is the compiler-facing representation. It records a stable
`AstKind`, source location, names and literal text, child expressions, parameters, and named groups
such as function bodies or conditional branches. Exact source spelling is retained where semantics
need it, including integer literals and strings.

Every accepted `Expr` and `Stmt` supplies `canonical_ast()`. `src/compiler/fission.cpp` accepts the
parsed statement list, obtains those canonical nodes, and lowers only those nodes. It has no token
cursor, statement parser, or expression parser.

## Invariants

- The parser decides whether source is valid.
- Parser-owned semantic checks (systems directives, fixed-width ranges, UEFI field chains, UTF-16
  validity, and freestanding restrictions) complete before A-MIR lowering.
- A-MIR never guesses structure from source text or lexer tokens.
- Terminal AST semantics remain terminal in A-MIR.
- An accepted construct must have an explicit AST kind and an explicit lowering, or produce a
  deterministic unsupported diagnostic.

`arcology-os/tests/systems/systems_frontend_amir_contract_smoke.sh` covers representative expressions, statements,
control flow, functions, classes, and interfaces, and statically rejects reintroduction of the old
token-based builder path.

## Adding Syntax

Add the grammar and semantic validation in the parser, define or reuse a canonical `AstKind`, emit
all required canonical data, then add one A-MIR lowering case. Do not create a second parser in the
compiler backend.
