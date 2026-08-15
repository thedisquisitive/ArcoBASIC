# RFC-0017: Substrate Resource Model

Status: Draft

The Resource Registry is the substrate-visible metadata layer above Runtime Handles. A handle
continues to provide identity and generation validation; a `ResourceRecord` describes what that
identity means without exposing provider internals.

Each record contains the handle, kind, owner, provider, lifetime policy, lifecycle state, and
directional dependency names. The registry supports lookup, enumeration, diagnostics, dependency
registration, validated lifecycle transitions, and removal on destruction. It is intentionally
independent from `RuntimeHandleTable`.

Initial graphics records use:

- memory surface: `SURFACE`, owner `Execution Context`, provider `Software Graphics Provider`,
  lifetime `Explicit`, state `Ready`;
- primary surface: `SURFACE`, owner `Graphics Runtime`, provider `Software Graphics Provider`,
  lifetime `RuntimeOwned`, state `Ready`, dependencies `Graphics Provider` and `Primary Display Backend`.

The hosted runtime exposes `RESOURCE.Describe()` for diagnostics. Physical UEFI/GOP provider names
will replace the hosted provider while preserving the record contract.
