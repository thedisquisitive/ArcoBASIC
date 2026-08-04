# RFC-0012 Frontend to A-MIR Contract

**Date:** 2026-08-04
**Baseline:** `b52e6b816d7d061fc37c37ddd3a274a10652e58d`

## Result

Implemented. The parser's canonical AST is now the only structural input to A-MIR lowering.

The former token-based `AmirBuilder`, expression parser, and `build_amir(tokens, ...)` path were
removed. All Fission entry points retain the parser's returned statement tree and pass it to
`AstAmirBuilder`. Every accepted expression and statement exposes a canonical node carrying its
kind, source information, children, parameters, and named bodies/branches.

Parser-integrated semantic validation remains intentionally parser-owned. This does not weaken the
contract: validation completes while constructing the authoritative tree, and downstream lowering
does not reinterpret tokens.

## Verification

- `tests/systems_frontend_amir_contract_smoke.sh` exercises representative accepted constructs
  through A-MIR and statically guards against restoration of the discarded/token path.
- Existing runtime, shell, compiler, systems, and QEMU tests cover the migrated lowering surface.
- Developer contract: `docs/systems/frontend-amir-contract.md`.

## Known Limit

The tree remains an internal C++ interface rather than a serialized stable file format. RFC-0012
requires a canonical compiler-facing AST, not external AST ABI stability.
