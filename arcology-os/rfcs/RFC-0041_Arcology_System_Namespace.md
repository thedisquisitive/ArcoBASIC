# RFC-0041: Arcology System Namespace

**RFC Number:** RFC-0041
**Title:** Arcology System Namespace
**Status:** Draft (Phase A implemented and QEMU-proven -- see `.agents/reports/arcology-system-namespace.md`)
**Category:** Substrate / Object Architecture
**Authors:** Arcology Project
**Created:** 2026-08-20
**Last Updated:** 2026-08-20
**Supersedes:** None
**Superseded By:** None
**Related Architecture:** Polymorphic Substrate (APS); Arcology Object Architecture
**Related RFCs:** RFC-0000, RFC-0015 (Runtime Object Handles), RFC-0017 (Substrate Resource Model), RFC-0038 (APS Block Storage and Filesystem Provider Substrate), RFC-0039 (ArcologyFS/ArcFS -- this RFC supplies the attachment target its own Section 10 requires)

------------------------------------------------------------------------

# 1. Executive Summary

RFC-0039 (ArcologyFS) defines a filesystem attachment lifecycle -- DISCOVER, CREATE, ATTACH, VERIFY,
ACTIVATE, then "ATTACH filesystem namespace -> target namespace location" -- and its own Phase G
report is explicit that the last step has no target: there is no Arcology-wide object/namespace
facility anywhere in this repository for a filesystem to attach *into*. Every colon-path resolved
anywhere in this project so far is internal to one thing: ArcFS's own per-volume namespace, or a
single hosted-runtime path service (RFC-0035), never a shared substrate-level namespace spanning
multiple kinds of thing.

This RFC defines that facility: the Arcology System Namespace, a single colon-delimited tree,
separate from and above any individual filesystem's own namespace, in which locations can be
registered as plain containers, opaque object references, or **filesystem attachments** -- a path
prefix that delegates the remainder of a resolved path to an already-mounted filesystem's own
`Resolve` function. A filesystem attachment at `:volumes:data` resolving `:volumes:data:home:notes.txt`
delegates `:home:notes.txt` to that filesystem verbatim, unchanged.

This is architecture-first work, matching this project's own guiding principle: the RFC is written
in full before any implementation, and Phase A's own scope (in-memory, no persistence) matches
every other RFC in this project's own established sequencing discipline.

------------------------------------------------------------------------

# 2. Motivation

## 2.1 ArcFS has nothing to attach to

RFC-0039 Phase G's own report names this precisely: "This repository has no implemented Arcology
object/namespace facility outside ArcFS's own... confirmed the same way RFC-0038's own
implementation confirmed RFC-0017 (Substrate Resource Model) has no concrete freestanding callable
surface anywhere in this repository." ArcFS's own internal namespace (RFC-0039 Phases A-G) is
complete, proven, and entirely unaffected by this RFC -- it needs no changes. What's missing is
somewhere *above* it for that namespace to be reachable from.

## 2.2 RFC-0017 does not fill this gap

RFC-0017's Resource Registry tracks `ResourceRecord`s keyed by `RuntimeHandle` -- ownership,
provider, lifetime policy, lifecycle state. It answers "what is this handle and who owns it," not
"what lives at this path." It is also, per RFC-0015's own "Current implementation" section, a
hosted-runtime-only facility (`include/arco/runtime_handles.hpp`, consumed by the hosted
interpreter) with no freestanding callable surface -- the same finding RFC-0039 Phase A's own scope
reduction #6 and RFC-0038's implementation both independently made. This RFC does not depend on
RFC-0017 and does not attempt to give it a freestanding implementation; that remains separate,
un-started work.

## 2.3 A real, minimal need, not a speculative one

The concrete requirement is narrow: given a system that has activated an ArcFS volume (RFC-0039
Phase G's `ArcFS.ActivateSystemVolume`), make that volume's content reachable from a path a caller
did not have to already know was "the ArcFS one." That is the entire job this RFC's Phase A needs
to do to unblock RFC-0039's own Phase G.

------------------------------------------------------------------------

# 3. Goals

- A single, freestanding, in-memory, QEMU-provable Arcology System Namespace tree.
- Three entry kinds: Container (pure grouping), Object Reference (an opaque handle registered at a
  path), Filesystem Attachment (a path that delegates path resolution to an already-mounted
  filesystem).
- A resolution contract that correctly composes system-namespace paths with an attached
  filesystem's own internal paths, without requiring any change to that filesystem's own `Resolve`
  implementation.
- A real proof that resolving a path crossing a filesystem attachment boundary reaches the correct
  object in the attached filesystem, reads its real content, and that detaching removes
  reachability.

------------------------------------------------------------------------

# 4. Non-Goals

- **A general Arcology object model.** This RFC's "Object Reference" entry kind stores an opaque
  U64 handle with a caller-supplied kind tag; it does not define, validate, or dispatch on object
  semantics. Building a real device/service object model is separate, larger work this RFC does not
  attempt.
- **RFC-0017 integration or a freestanding Resource Registry.** Explicitly out of scope; see 2.2.
- **Multiple simultaneous filesystem attachments of different volumes.** ArcFS itself mounts
  exactly one volume at a time across every phase of RFC-0039 (Phases A-G); this RFC's Phase A
  matches that reality and supports one active filesystem attachment. See Section 7.5.
- **Persistence.** Like RFC-0039 Phase A before it, this RFC's Phase A is in-memory only, proving
  the contract before any on-disk format exists for it. A persistent System Namespace is future
  work, not attempted here.
- **Authority/capability checks on namespace resolution.** Matching RFC-0039 Phase A's own scope
  reduction #6 (no RFC-0017 surface to check against), this RFC does not add access control.
- **A general-purpose VFS mount table for arbitrary filesystem types.** This RFC proves the
  attachment mechanism against ArcFS specifically, the one filesystem implementation that exists in
  this repository. Generalizing to a `FileSystem`-interface-typed attachment (RFC-0039 Section 42)
  is a natural future extension, not required for Phase A.

------------------------------------------------------------------------

# 5. Terminology

**System Namespace** -- The tree this RFC defines. Root path `:`, colon-delimited components,
identical surface syntax to RFC-0039 Section 9.1's own path grammar but a *distinct* tree from any
individual filesystem's internal namespace.

**Entry** -- A node in the System Namespace tree. Every entry has a stable Entry ID (analogous to,
but a distinct ID space from, RFC-0039's own OID), a parent, and a name.

**Container** -- An entry that exists only to hold child entries (RFC-0039's own DIRECTORY concept,
one level up the stack).

**Object Reference** -- A leaf entry storing one opaque U64 handle and a U64 kind tag, format-only
in this RFC (Section 4).

**Filesystem Attachment** -- A leaf entry marking that path resolution passing through it delegates
the *remaining, unconsumed* path components to an attached filesystem's own `Resolve` function.
This is the concrete mechanism behind RFC-0039 Section 10's "ATTACH filesystem namespace -> target
namespace location."

**Delegated Resolution** -- The act of handing a remainder path to an attached filesystem. The
System Namespace does not reinterpret, translate, or validate that remainder; it reconstructs it as
a root-relative path (a leading `:`) and passes it unchanged to the attached filesystem's existing
`Resolve` contract.

------------------------------------------------------------------------

# 6. Architecture

```text
+--------------------------------------------------------------+
| Arcology System Namespace (this RFC)                          |
|   :                                                            |
|   :system                    (Container)                       |
|   :system:timer               (Object Reference, handle=...)   |
|   :volumes                   (Container)                       |
|   :volumes:data                (Filesystem Attachment)          |
+--------------------------------------------------------------+
                                     |
                                     | delegates remainder
                                     v
+--------------------------------------------------------------+
| ArcFS's own internal namespace (RFC-0039, unchanged)            |
|   :                                                            |
|   :home                                                        |
|   :home:documents                                               |
|   :home:documents:notes.txt                                      |
+--------------------------------------------------------------+
```

Resolving `:volumes:data:home:documents:notes.txt` against the System Namespace:

1. Walk `volumes` -> Container.
2. Walk `data` -> Filesystem Attachment. Every remaining component (`home`, `documents`,
   `notes.txt`) is unconsumed.
3. Reconstruct the remainder as `:home:documents:notes.txt` and call `ArcFS.Resolve` on it,
   completely unchanged from every prior RFC-0039 phase.
4. The result is tagged as an ArcFS object (Section 7.4), not a System Namespace entry, since the
   two ID spaces are not interchangeable.

------------------------------------------------------------------------

# 7. Requirements

## 7.1 Entry table

The System Namespace SHALL be represented as a fixed table of entries (matching the "one instance /
small fixed table" pattern every RFC-0036 through RFC-0040 in this chain has used). Root (Entry ID
1) SHALL always exist as a Container with no parent, created by `Namespace.Initialize`.

## 7.2 Entry kinds

Every entry SHALL be exactly one of: Container, Object Reference, Filesystem Attachment. Only
Containers MAY have children.

## 7.3 Creation and removal

`Namespace.CreateContainer(parentId, name, nameLength) AS U64`, `Namespace.CreateObject(parentId,
name, nameLength, objectHandle, objectKind) AS U64`, `Namespace.AttachFilesystem(parentId, name,
nameLength) AS U64`, and `Namespace.DetachFilesystem(entryId) AS BOOL` SHALL exist. Creation under a
non-Container parent, or a duplicate name under one parent, MUST fail (RFC-0039 Section 5.3: "MUST
NOT guess silently," applied here identically).

## 7.4 Resolution and the two-ID-space problem

`Namespace.Resolve(pathBuffer, pathLength) AS BOOL` SHALL walk the path component by component.
Reaching a Filesystem Attachment entry before the path is exhausted SHALL delegate the remainder to
that filesystem's own `Resolve` per Section 6. Since System Namespace Entry IDs and (in this RFC's
only real consumer) ArcFS OIDs are different, non-comparable ID spaces, the result MUST be reported
as a *tagged* value, not a bare `U64` -- a caller that doesn't check the tag and treats an ArcFS OID
as an Entry ID (or vice versa) risks operating on the wrong object. `Namespace.Resolve` SHALL write
`(resultKind, resultId)` into a dedicated result location and return `BOOL` found/not-found, the
same "write results into shared state" convention RFC-0039 Phase E's `ArcFSPeekStateAddress`
already established for exactly this kind of multi-value return.

## 7.5 Attachment cardinality

Because ArcFS mounts exactly one volume at a time (Section 4), `Namespace.AttachFilesystem` SHALL
reject a second attachment while one is already active, and SHALL fail closed (return 0) rather than
silently detaching the first. `Namespace.DetachFilesystem` SHALL remove the attachment entry without
touching ArcFS's own mount state -- detaching is a namespace-visibility change, not an unmount.

------------------------------------------------------------------------

# 8. Developer Experience

```basic
Namespace.Initialize()
LET systemId AS U64 = Namespace.CreateContainer(1, "system", 6)
LET volumesId AS U64 = Namespace.CreateContainer(1, "volumes", 7)
Namespace.CreateObject(systemId, "timer", 5, timerHandle, 1)

' after ArcFS.ActivateSystemVolume() has mounted a volume:
Namespace.AttachFilesystem(volumesId, "data", 4)

Namespace.Resolve(":volumes:data:home:documents:notes.txt", 34)
' resultKind = 2 (ArcFS object), resultId = the real OID
```

------------------------------------------------------------------------

# 9. Security Considerations

- No authority checks exist yet (Section 4); anything with freestanding code-execution capability
  can resolve, attach, or detach anything. Matches RFC-0039 Phase A's own equivalent scope
  reduction and carries the identical residual risk statement.
- Delegated resolution passes the remainder path unchanged into the attached filesystem's *own*
  validated `Resolve` contract (bounds/format checks already proven in RFC-0039 Phases A-G) -- this
  RFC introduces no new parsing of untrusted path bytes beyond simple component splitting on `:`,
  identical to RFC-0039 Section 9's own grammar.

------------------------------------------------------------------------

# 10. Testing Strategy

- Structural: every public entry point compiles cleanly at X86_64 codegen level.
- Real QEMU/OVMF proof: build a small System Namespace tree, mount a real ArcFS volume, attach it,
  resolve a path crossing the attachment boundary, and read the resolved file's real content back
  -- not merely confirm an ID matches. Confirm a path to a component that does not exist in the
  attached filesystem correctly reports not-found through the delegation. Confirm detaching removes
  reachability.
- Determinism across repeated runs and a negative control on the core delegation assertion,
  matching every RFC-0039 phase's own established discipline.

------------------------------------------------------------------------

# 11. AI Implementation Guidance

- Do not modify any RFC-0039 `ArcFS.*` function to know about the System Namespace. Delegation is
  the System Namespace's own responsibility (Section 6); ArcFS's `Resolve` contract stays exactly
  as proven in Phases A-G.
- Do not attempt to give RFC-0017 a freestanding implementation as part of this work; Section 2.2 is
  explicit that this RFC does not depend on or unblock RFC-0017.
- Use a fixed scratch-address table for the entry store, in the same style and with the same
  "clearly documented, collision-checked" discipline every RFC-0036 through RFC-0040 fixed address
  has used.
- Required deliverable: a QEMU-proven fixture demonstrating the full flow in Section 8, plus a
  negative control on the delegation result, plus a report under `.agents/reports/`.

------------------------------------------------------------------------

# 12. Future Extensions

- Real object semantics for the Object Reference kind (device records, service records) once a
  broader Arcology object model exists.
- Multiple simultaneous filesystem attachments, once ArcFS itself supports mounting more than one
  volume concurrently.
- Persistent System Namespace state, surviving a reboot.
- Generalizing Filesystem Attachment beyond ArcFS specifically to any `FileSystem`-interface-typed
  provider (RFC-0039 Section 42), once a second filesystem implementation exists in this project to
  prove genericity against.

------------------------------------------------------------------------

# 13. Open Questions

- Whether Entry IDs and ArcFS OIDs should eventually be unified into one ID space (a single
  Arcology-wide object identity) is explicitly not resolved here; Section 7.4's tagged-result
  approach is Phase A's deliberate, honest answer to not having decided this yet.

------------------------------------------------------------------------

# 14. References

- RFC-0039 (ArcologyFS/ArcFS), Sections 9, 10, and Phase G's own report
  (`.agents/reports/aps-arcfs-phase-g.md`) -- the gap this RFC closes.
- RFC-0015 (Runtime Object Handles), RFC-0017 (Substrate Resource Model) -- related, not depended
  on; see Section 2.2.

------------------------------------------------------------------------

# 15. Revision History

| Version | Date       | Summary         |
|---------|------------|--------------------|
| 0.1     | 2026-08-20 | Initial draft, written before any implementation, per this project's own "architecture precedes implementation" guiding principle. Scoped narrowly to unblock RFC-0039 Phase G's "namespace attachment" item: a System Namespace with Container/Object-Reference/Filesystem-Attachment entry kinds, and a delegated-resolution contract that lets an attached ArcFS volume's own unmodified `Resolve` answer queries through a System Namespace path. Explicitly does not depend on or implement RFC-0017. |
| 0.2     | 2026-08-20 | Phase A implemented and validated end-to-end under QEMU/OVMF: `Namespace.Initialize`/`CreateContainer`/`CreateObject`/`AttachFilesystem`/`DetachFilesystem`/`Resolve`/`GetObjectHandle`/`GetObjectKind`, all in `stdlib/system_namespace_policy.abas`. Real proof attaches an actual formatted-and-committed ArcFS volume at a System Namespace location and resolves a path crossing the attachment boundary all the way to the attached volume's own real file content (read back byte-for-byte, not merely an ID match), confirms not-found propagates correctly through delegation, confirms the attachment point itself with no remainder delegates to the attached volume's own root, confirms a second simultaneous attachment is rejected (Section 7.5), and confirms detach removes reachability entirely. `arcfs_policy.abas` required zero changes -- delegation is entirely this RFC's own responsibility, reconstructing a well-formed root-relative remainder path for ArcFS's already-proven `Resolve` to answer unchanged. Passed on the first real attempt; deterministic across three repeated runs; negative control confirmed. See `.agents/reports/arcology-system-namespace.md`. |
