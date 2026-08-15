# RFC-0034: Package Entry Points and Resource Resolution

**RFC Number:** RFC-0034  
**Title:** Package Entry Points and Resource Resolution  
**Status:** Draft  
**Category:** Tooling / Hosted Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0031, RFC-0033, RFC-0035

------------------------------------------------------------------------

# 1. Executive Summary

This RFC defines a dependency-free local package manifest, named entry points, and safe package
resource resolution. PetriBrain can launch evolution or interactive play and load bundled brain
files independently of the process working directory.

------------------------------------------------------------------------

# 2. Motivation

Python packaging names a nonexistent PetriBrain CLI, while direct scripts assume the current
directory contains `brains/`. A port needs explicit local application entry points and stable asset
paths without requiring a third-party package manager.

------------------------------------------------------------------------

# 3. Goals

- Define one simple local manifest with application metadata and entry points.
- Resolve source and declared resources relative to the package root.
- Add `arco run PACKAGE [ENTRY]` and capsule build integration.

------------------------------------------------------------------------

# 4. Non-Goals

- Remote registries, dependency solving, installation databases, signing, publishing, semantic
  version negotiation, or general build-system replacement.

------------------------------------------------------------------------

# 5. Terminology

**Package Root:** Directory containing `arcobasic.package`.  
**Entry Point:** A named module function with no required parameters.  
**Resource:** A manifest-declared non-source file beneath the package root.

------------------------------------------------------------------------

# 6. Requirements

The UTF-8 manifest `arcobasic.package` MUST use line-oriented `key = value` syntax with quoted
strings, string arrays, comments beginning with `#`, and these fields:

```text
name = "PetriBrain"
version = "0.1.0"
source-root = "src"
default-entry = "play"
entry.play = "TTT.Play.Main"
entry.evolve = "TTT.Evolve.Main"
resources = ["brains/*.txt"]
```

Names, version, source root, at least one entry, and default entry are required. Unknown keys MUST
be preserved for forward compatibility but warned about. Entry functions resolve through RFC-0031
modules, accept no required parameters, and MAY read `Args` for remaining CLI arguments.

`arco run PATH [ENTRY] [-- arguments...]` MUST locate the manifest, select the named/default entry,
set package metadata, and invoke it. ArcoSH MUST support the equivalent invocation. ArcoFission
build/native MUST accept `--package PATH --entry NAME` and embed declared resources when requested.

The hosted runtime MUST provide:

```text
Package.Name()  Package.Version()  Package.Root()
Package.Resource(relativePath)  Package.HasResource(relativePath)
```

Resource resolution MUST reject absolute paths, `..` escape, undeclared files, and symlink escape.
Returned host paths are read-only discovery values; ordinary file APIs still govern access.

------------------------------------------------------------------------

# 7. Architecture

Tools parse the manifest without executing source, build the RFC-0031 module graph, select an
exported entry, and create a package context. Resource matching is canonicalized at build/run time;
capsules use an internal read-only resource table or extracted private package root.

------------------------------------------------------------------------

# 8. User Experience

```text
arco run . play
arco run . evolve -- --difficulty hard
ArcoFission build --package . --entry play -o petribrain
```

------------------------------------------------------------------------

# 9. Developer Experience

Diagnostics MUST name manifest path, key, entry, and resource pattern. `arco package check` MUST
validate without executing. Runtime package functions fail clearly outside package context.

------------------------------------------------------------------------

# 10. Security Considerations

Manifest parsing executes no code. Resource paths MUST remain beneath canonical package root and
capsules MUST not overwrite host files when materializing resources. Packages gain no capabilities.

------------------------------------------------------------------------

# 11. Privacy Considerations

No registry or telemetry exists. `Package.Root()` exposes the local package path to the running
program only; diagnostics SHOULD use relative paths where adequate.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Named entries and validation diagnostics must be textual. Help must show discoverable entry names
and default selection.

------------------------------------------------------------------------

# 13. Performance Considerations

Manifest parsing and resource enumeration occur once. Tools SHOULD cache validated manifests by
content identity without hiding file changes.

------------------------------------------------------------------------

# 14. Compatibility

Direct script execution remains supported. Existing `#ENTRY` compile metadata may be mapped into a
single-file implicit package but does not override an explicit manifest.

------------------------------------------------------------------------

# 15. Reference Implementation

Given the manifest above, `arco run .` invokes `TTT.Play.Main`; `Package.Resource("brains/easy.txt")`
returns the declared easy-brain resource regardless of current working directory.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover parsing, required/unknown/duplicate keys, entry resolution/signatures, argument
passing, default selection, resource globs, traversal and symlink rejection, changed working
directory, package check, bytecode/capsule embedding, direct-script compatibility, and deterministic
diagnostics.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement a small dedicated parser, canonical path policy, package context, runner integration,
then capsule resources. Do not adopt TOML libraries, network registries, or execute manifests. Stop
if capsule resources cannot be made read-only and traversal-safe.

------------------------------------------------------------------------

# 18. Future Extensions

Local dependencies, build profiles, signing, registries, installed commands, and semantic versions.

------------------------------------------------------------------------

# 19. Open Questions

None for dependency-free local packages.

------------------------------------------------------------------------

# 20. References

- RFC-0031, Isolated Modules and Explicit Exports.
- RFC-0035, Core Hosted Console, Directory, and Path Services.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial local package, entry-point, and resource proposal. |
