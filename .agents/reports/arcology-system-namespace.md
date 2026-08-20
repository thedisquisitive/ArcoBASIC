# Arcology System Namespace: Phase A (RFC-0041)

## Scope delivered

RFC-0041 Phase A ("in-memory, no persistence yet") is implemented and proven end to end under
QEMU/OVMF. This closes RFC-0039 Phase G's "namespace attachment" item: ArcFS's own internal
namespace (RFC-0039 Phases A-G, completely unmodified by this work) is now reachable through a
real, general, colon-delimited System Namespace that sits above it.

`stdlib/system_namespace_policy.abas` implements:

- `Namespace.Initialize` / `Namespace.CreateContainer` / `Namespace.CreateObject` /
  `Namespace.AttachFilesystem` / `Namespace.DetachFilesystem` / `Namespace.Resolve` /
  `Namespace.GetObjectHandle` / `Namespace.GetObjectKind`.
- Three entry kinds (Container, Object Reference, Filesystem Attachment) in one fixed 32-entry
  table, matching every prior RFC in this project's own "one instance / small fixed table"
  pattern.
- Delegated resolution: `Namespace.Resolve` walks the System Namespace tree, and on reaching a
  Filesystem Attachment before the path is exhausted, reconstructs the unconsumed remainder as a
  root-relative path and hands it to `ArcFS.Resolve` completely unchanged.

## The two-ID-space problem, and why the result is tagged

System Namespace Entry IDs and ArcFS OIDs are different, non-comparable ID spaces. A bare `U64`
return from `Namespace.Resolve` could not distinguish "found System Namespace entry 4" from "found
ArcFS object 4" -- a caller that didn't know which it got could silently operate on the wrong
object. `Namespace.Resolve` instead writes `(resultKind, resultId)` into a dedicated result
location, the same "write results into shared state" convention RFC-0039 Phase E's
`ArcFSPeekStateAddress` already established for exactly this kind of multi-value return.

## What ArcFS did not need to change

Nothing in `arcfs_policy.abas` was modified. `ArcFS.Resolve` is called exactly as proven across
RFC-0039 Phases A-G; delegation is entirely the System Namespace's own responsibility, reconstructing
a well-formed root-relative path (leading `:`) from whatever remainder is left after the attachment
point. This was a deliberate design goal (RFC-0041 Section 11), not an accident -- it means every
one of ArcFS's own already-proven fixtures remains valid evidence, unaffected by this work existing
at all.

## Validation

- Full suite: 57/57 passing (56 pre-existing + 1 new test file). No existing test was touched.
- Every public entry point compiles cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`system-namespace.abas`): builds `:system`
  (container) and `:system:timer` (Object Reference, opaque handle 4242, kind 7) -- resolving
  `:system:timer` reports it as a System Namespace entry and its handle/kind round-trip exactly.
  Formats and populates a real ArcFS volume (`:home:documents:notes.txt`, 70 bytes) and commits it.
  Attaches that volume at `:volumes:data`; a second attachment attempt is correctly rejected
  (RFC-0041 Section 7.5's single-attachment rule). Resolves
  `:volumes:data:home:documents:notes.txt` -- crossing the attachment boundary -- and confirms the
  delegated OID matches a direct `ArcFS.Resolve(":home:documents:notes.txt")` call, then opens a
  handle on it and reads back the real 70 bytes byte-for-byte, proving genuine delegation, not
  merely an ID coincidence. Confirms a missing file under the attachment correctly propagates
  not-found. Confirms resolving the attachment point itself, with no remainder, delegates to the
  attached volume's own root (ArcFS OID 1). Detaches the filesystem and confirms the entire subtree
  it exposed becomes unresolvable. Passed on the first real attempt; deterministic across three
  repeated runs.
- **Negative control on the test harness itself**: flipped the central delegation assertion to
  expect the delegated OID to differ from a direct `ArcFS.Resolve` call (as if delegation silently
  resolved the wrong object) and confirmed the fixture correctly reports `FAIL 1` over serial
  rather than silently passing.

## Remaining activation gate

- **RFC-0017 is still not implemented.** This work does not depend on or unblock it (RFC-0041
  Section 2.2); Object Reference entries store an opaque handle and kind tag with no semantics
  enforced.
- **One filesystem attachment at a time**, matching ArcFS's own single-mounted-volume reality
  across every phase of RFC-0039.
- **No persistence, no authority checks** -- both explicit RFC-0041 Phase A scope reductions,
  matching RFC-0039 Phase A's own identical choices at the same stage.
- RFC-0041's own `Status` stays `Draft`; this is Phase A of a larger, not-yet-fully-specified
  future (Section 12's Future Extensions).
