# RFC-0035: Core Hosted Console, Directory, and Path Services

**RFC Number:** RFC-0035  
**Title:** Core Hosted Console, Directory, and Path Services  
**Status:** Draft  
**Category:** Hosted Runtime / Standard Library  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0007, RFC-0012, RFC-0034

------------------------------------------------------------------------

# 1. Executive Summary

This RFC promotes a minimal cross-platform console, directory, file-listing, and path API from
ArcoSH-only registration into the core hosted runtime. Interactive PetriBrain play and evolution
setup then behave consistently through ArcoSH, `arco_cli`, hosted bytecode, and native capsules.

------------------------------------------------------------------------

# 2. Motivation

Core file text operations exist, but console input, directory creation/listing, and path transforms
are shell-only. A normal hosted application should not change behavior merely because it runs
outside ArcoSH.

------------------------------------------------------------------------

# 3. Goals

- Provide minimal synchronous console input.
- Provide deterministic directory creation and listing.
- Provide platform-neutral lexical path composition and inspection.
- Keep aliases compatible with current ArcoSH scripts.

------------------------------------------------------------------------

# 4. Non-Goals

- Shell command execution, glob languages beyond listing filters, file watching, permissions,
  recursive deletion, terminal raw mode, password input, dialogs, or freestanding services.

------------------------------------------------------------------------

# 5. Terminology

**Core Hosted Service:** Registered by every hosted `Runtime`, including CLI, shell, VM, and capsule.  
**Lexical Path:** A path transformed without requiring the target to exist.

------------------------------------------------------------------------

# 6. Requirements

Every hosted runtime MUST register:

```text
Console.ReadLine(prompt = "")
Directory.Exists(path)
Directory.Create(path, recursive = TRUE)
File.List(directory, pattern = "*")
Path.Join(part, ...)
Path.BaseName(path)  Path.Stem(path)  Path.Extension(path)
Path.DirName(path)   Path.Normalize(path)  Path.Current()
```

`Console.ReadLine` writes the prompt without a newline, flushes output, and returns one line without
line ending. End-of-input returns `NULL`, distinct from an empty line. Existing `Input` and
`ReadLine` MUST remain case-insensitive compatibility aliases in ArcoSH and become core aliases.

`Directory.Create` returns `TRUE` when the directory exists after the call, including preexistence,
and throws a deterministic error otherwise. It MUST NOT delete or replace existing entries.

`File.List` returns a new array of objects with `Name`, `Path`, `IsDirectory`, `IsFile`, `IsHidden`,
`Size`, and `Extension`. Pattern supports only `*` and `?` against the basename. Results MUST sort
case-insensitively by name with original-name tie-breaking. It is non-recursive.

Path operations use host-native separators in returned host paths, reject embedded NUL, and do not
require existence except `Path.Current`. `Path.Normalize` is lexical and MUST NOT claim to resolve
symlinks or authorize access.

All functions MUST have interpreter, hosted bytecode, serialized bytecode, and capsule parity. A
`#RUNTIME NONE` use MUST fail with an explicit hosted-service diagnostic.

------------------------------------------------------------------------

# 7. Architecture

Implementations move shared logic into core runtime helpers; ArcoSH registers only shell-specific
extensions and compatibility aliases without replacing core semantics. Capsules bind standard
input/output and host filesystem adapters through the same API contract.

------------------------------------------------------------------------

# 8. User Experience

```basic
brains = File.List("brains", "*.txt")
choice = NUMBER(Console.ReadLine("Choose a brain: "))
path = Path.Join("brains", brains[choice].Name)
```

------------------------------------------------------------------------

# 9. Developer Experience

Diagnostics MUST name operation and path without inconsistent shell/core wording. `HELP console`,
`HELP directory`, and `HELP path` MUST identify hosted-only behavior and EOF semantics.

------------------------------------------------------------------------

# 10. Security Considerations

These APIs grant no access beyond process permissions and host capability policy. Paths are not
sandbox authorization. Listing must avoid following directory symlinks recursively. Allocation and
entry counts remain limited.

------------------------------------------------------------------------

# 11. Privacy Considerations

Directory listing exposes filenames already available to the process. Runtimes MUST not transmit
input or listings. Diagnostics SHOULD avoid unrelated path disclosure.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Prompts must work without color and preserve screen-reader-friendly text. EOF is programmatically
distinct from blank input. APIs do not require pointer interaction.

------------------------------------------------------------------------

# 13. Performance Considerations

Listing is `O(n log n)` because ordering is normative. Path transforms are linear in input length.
Console reads may block and MUST be documented as synchronous.

------------------------------------------------------------------------

# 14. Compatibility

Existing ArcoSH names remain aliases with normalized semantics. The EOF change from empty string to
`NULL` is intentionally applied to core aliases and MUST be called out in migration notes.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
Directory.Create("brains")
File.WriteText(Path.Join("brains", "easy.txt"), "ACGT")
PRINT File.List("brains", "*.txt")[0].Name
```

Expected output: `easy.txt` in an isolated fixture directory.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover input prompts/blank/EOF, existing/new/failing directories, pattern matching,
ordering/ties, metadata fields, Unicode names, separators, lexical normalization, NUL rejection,
interpreter/VM/capsule parity, ArcoSH aliases, platform fixtures, capability denial, and freestanding
rejection.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Extract current ArcoSH helpers into core without duplicating behavior, normalize EOF and listing
contracts, then add VM/capsule tests and docs. Do not add deletion, shell execution, recursive glob,
or treat normalization as sandboxing. Stop if a capsule lacks a defined stdin/filesystem adapter.

------------------------------------------------------------------------

# 18. Future Extensions

Recursive traversal, richer globbing, temporary files, file metadata, watchers, raw terminal input,
and permission-aware capability objects.

------------------------------------------------------------------------

# 19. Open Questions

None for the minimal synchronous hosted surface.

------------------------------------------------------------------------

# 20. References

- RFC-0007, ArcoBASIC Interactive Program Model.
- RFC-0034, Package Entry Points and Resource Resolution.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial core hosted console/directory/path proposal. |
