# RFC-0031: Isolated Modules and Explicit Exports

**RFC Number:** RFC-0031  
**Title:** Isolated Modules and Explicit Exports  
**Status:** Draft  
**Category:** Language / Tooling  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0029, RFC-0034

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds opt-in isolated modules with declared names and exports while preserving legacy
textual `#IMPORT` compatibility. PetriBrain's genome, mutation, crossover, Tic-Tac-Toe, and checkers
components can own private constants and expose collision-free qualified APIs.

------------------------------------------------------------------------

# 2. Motivation

Current imports expose global declarations and alias wrappers mainly for functions. Larger ports
must rename constants and helpers manually to avoid collisions, weakening encapsulation.

------------------------------------------------------------------------

# 3. Goals

- Give a source file one declared module identity.
- Make declarations private by default and exports explicit.
- Initialize modules once and access exports through aliases.
- Detect cycles and duplicate identities deterministically.

------------------------------------------------------------------------

# 4. Non-Goals

- Remote packages, version solving, hot reload, dynamic imports, runtime reflection, or removal of
  legacy imports.

------------------------------------------------------------------------

# 5. Terminology

**Isolated Module:** A source file with its own global scope and declared identity.  
**Export:** A declaration made visible to importers.  
**Legacy Import:** Existing compatibility inclusion without a `MODULE` declaration.

------------------------------------------------------------------------

# 6. Requirements

An isolated file MUST begin after directives/comments with:

```basic
MODULE Petri.Genome
EXPORT Encode, Decode
```

Module names are case-insensitive dotted identifiers. Functions, classes, records, enums, and
module-level values are private unless named by `EXPORT`. Exporting an unknown or duplicate name
MUST fail. Module initialization executes once per runtime in dependency order.

```basic
#IMPORT "petri/genome" AS Genome
bits = Genome.Encode("ACGT")
```

An alias is required for isolated modules unless the importer explicitly uses the declared final
name and no collision exists. All exported kinds, including values and types, MUST be qualified.
Importers cannot access private names or mutate exported module variables unless declared
`EXPORT MUTABLE name`; mutable exports are allowed but SHOULD be rare.

Cycles MUST be diagnosed with the full import chain before module initialization. A module path
resolving to a declared name already loaded from another path MUST fail.

Legacy files without `MODULE` retain existing `#IMPORT` behavior.

------------------------------------------------------------------------

# 7. Architecture

Preprocessing resolves a dependency graph but does not concatenate isolated scopes. Canonical
metadata records module identity, imports, exports, and initialization. Runtime and bytecode use a
per-module environment with qualified symbol resolution.

------------------------------------------------------------------------

# 8. User Experience

```basic
#IMPORT "petri/crossover" AS Crossover
LET (a, b) = Crossover.OnePoint(parentA, parentB, rng)
```

------------------------------------------------------------------------

# 9. Developer Experience

Diagnostics MUST show module, symbol, import path, and cycle chain. Tooling MUST reveal dependency
graphs and exported signatures. `HELP modules` MUST distinguish isolated and legacy behavior.

------------------------------------------------------------------------

# 10. Security Considerations

Isolation is namespace encapsulation, not a capability boundary. Private declarations MUST not be
reachable through aliases or object-property tricks. Existing import path traversal protections
remain required.

------------------------------------------------------------------------

# 11. Privacy Considerations

No data is collected. Diagnostics SHOULD avoid exposing unrelated absolute search paths.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Qualified names and explicit exports improve navigation. Cycle diagnostics MUST be textual and
ordered.

------------------------------------------------------------------------

# 13. Performance Considerations

Dependency resolution is linear in graph size; initialization runs once. Qualified lookup SHOULD
be comparable to existing function lookup.

------------------------------------------------------------------------

# 14. Compatibility

Legacy modules remain supported. `MODULE` and `EXPORT` become reserved. Isolated modules do not
inject compatibility globals, preventing accidental collisions by design.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
' genome.abas
MODULE Petri.Genome
EXPORT Encode
FUNCTION Encode(text)
    RETURN text
END FUNCTION
```

An importer may call `Genome.Encode` but cannot access unexported declarations.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover every exported declaration kind, private rejection, aliases, mutable exports,
initialize-once behavior, diamonds, cycles, duplicate identities, path resolution, legacy imports,
AST/A-MIR/bytecode/capsules, and deterministic module reveal output.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Build a module graph and symbol tables before runtime execution changes. Preserve legacy inclusion
as a separate path. Do not fake isolation with naming prefixes or expose private globals. Stop if
bytecode cannot retain module identity or initialization order.

------------------------------------------------------------------------

# 18. Future Extensions

Package-qualified identities, semantic versions, selective imports, re-exports, and signed modules.

------------------------------------------------------------------------

# 19. Open Questions

None for opt-in isolated local modules.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology RFC Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial isolated-module proposal. |
