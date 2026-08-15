# Arcology OS Project Lexicon

This is the authoritative vocabulary for Arcology-specific implementation and diagnostics.

- **Contract**: a runtime object describing capabilities, ownership, permissions, dependencies,
  lifecycle, and communication rules between Arcology components. It is not a legal document,
  employment agreement, API specification, or interface definition by itself.
- **Provider**: an executable implementation of a system capability.
- **Substrate**: the foundational modular execution environment.
- **APS**: Arcology Provider System, responsible for discovering, validating, and coordinating
  Providers.
- **Capsule**: a self-contained Arcology application package containing code, resources, metadata,
  manifests, and contracts.
- **Resource**: a runtime object managed by the Resource Registry.
- **Handle**: an opaque runtime identifier.
- **Capability**: a permission or functional right granted by a Contract.
- **Runtime**: the execution environment that runs Capsules and manages runtime objects.

Project diagnostics and fixtures must use these meanings consistently. In particular, `Contract`
must never be rendered as an employment or other legal agreement.
