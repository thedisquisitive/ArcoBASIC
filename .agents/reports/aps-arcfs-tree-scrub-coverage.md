# ArcologyFS (ArcFS): Attribute Tree and Extent Tree Defect-Tolerant Scrub Coverage (RFC-0040)

## Scope delivered

Closes two named, standing gaps: "the Attribute Tree has no defect-tolerant scrub coverage" and
"the Extent Tree has no defect-tolerant scrub coverage." Both trees are now walked by
`ArcFSScanGeneration` (Pass 6 and Pass 7 respectively), counting one structural defect per corrupt
node encountered -- the same discipline Pass 1/2 already established for the Object and Namespace
Trees.

- **Pass 6 (Attribute Tree)**: `ArcFSAttributeTreeCollectAllEntriesTolerant` already existed,
  built alongside the Attribute Tree itself in Phase L -- but it was never actually called from
  anywhere outside its own recursion. It sat in the file as dead-from-the-caller's-perspective
  code, presumably built for symmetry with the other trees and then genuinely never wired in. This
  increment wires it into `ArcFSScanGeneration` for real.
- **Pass 7 (Extent Tree)**: no tolerant variant existed at all. A new
  `ArcFSExtentTreeCollectAllEntriesTolerant` was written, mirroring
  `ArcFSTreeCollectAllEntriesTolerant`'s (Object Tree) own structure exactly, adapted for the
  Extent Tree's 32-byte leaf entries and 256-entry bound.
- **`ArcFSPeekCheckpoint` gained `extentTreeCount`** (peek offset 88, from checkpoint scratch
  offset 96) -- it already carried `extentTreeRoot` (peek offset 80) from the previous increment,
  but not the count needed to cross-check a tolerant walk's own collected total against what the
  checkpoint claims, the same "bounded by collected, not claimed" discipline every other pass in
  this file already establishes.

## A real finding while writing the fixture: what "Corrupt" actually means here

The first fixture draft expected a corrupted tree node to produce `Degraded` (1). It didn't --
`ArcFS.GetHealthState()` correctly reported `Corrupt` (5). Reading `ArcFS.GetHealthState()`'s own
classification logic directly (not guessing) confirmed this is CORRECT, not a bug: any nonzero
`structuralDefectCount` (the class `ArcFSAddStructuralDefect()` increments, as opposed to
`ArcFSAddDefect()`'s repairable-class count) unconditionally classifies the whole volume as
`Corrupt`, before `Degraded` is ever considered -- and Pass 1/2 already call
`ArcFSAddStructuralDefect()` for a corrupt Object/Namespace Tree node, matching Phase M's own
report ("a real corrupted tree-node sector detected as Corrupt"). Pass 6/7 correctly follow the
exact same convention: a corrupt tree node is a structural defect, not a repairable one, regardless
of which tree it's in. The fixture's own expectation was wrong, not the implementation -- fixed by
correcting the fixture, not by changing anything in `arcfs_policy.abas` itself.

## Validation

- All 47 public `ArcFS.`-prefixed entry points, plus the new/touched internal functions, compile
  cleanly at X86_64 codegen level, individually reveal-checked against the live stdlib combined
  with `block_device_policy.abas`.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-tree-scrub-coverage.abas`): a file gets
  real multi-chunk content (8000 bytes, 2 real chunks -- a real Extent Tree entry) AND a real typed
  attribute (a real Attribute Tree entry); after a real commit and remount, `ArcFS.GetHealthState()`
  reports Healthy -- the positive proof that neither new pass false-positives on legitimate data
  coexisting in both trees at once. Directly corrupting the Attribute Tree's own root node on the
  BlockDevice (a single flipped byte breaking its CRC-32C checksum) makes `ArcFS.GetHealthState()`
  genuinely report Corrupt; restoring the original bytes returns Healthy. The identical
  corrupt/restore cycle against the Extent Tree's own root node proves Pass 7 the same way, with
  the file's own 8000 bytes of content confirmed byte-for-byte correct afterward (the corruption
  and restoration never touched real file data, only the tree node used to prove detection).
  Passed after correcting the fixture's own wrong expectation (see above, not an implementation
  bug); a negative control (flipping the expected post-corruption health state) confirmed the
  fixture correctly reports `FAIL 1`; deterministic across 3 repeated runs.
- New smoke test `systems_arco_basic_arcfs_tree_scrub_coverage_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest`.
- Full regression suite re-run clean.

## Documented scope reductions

1. **Both new passes detect corruption at the granularity of a whole tree node, not individual
   leaf entries within an otherwise-valid node.** This matches every other tree's own tolerant-walk
   convention in this file (a node's checksum covers its whole contents; there is no per-entry
   checksum to fail independently).
2. **No repair action exists for either new defect class.** Matching this project's own established
   precedent (Phase F's own report: a tree-node-level structural defect has no correct repair this
   implementation can invent -- RFC-0039 Section 5.3's "Implementations MUST NOT guess silently").
   Detection alone is the deliverable here, exactly as it already was for the Object/Namespace
   Trees' own long-standing structural-defect class.

## Remaining activation gate

RFC-0040's other remaining named gaps are unaffected and unchanged: a full
`(OID, namespace, name/ID)` multi-attribute-per-object model (Phase L); UTF-8 string/binary blob
attribute values (Phase L); extent-overlap repair's authoritative-selection policy still deviates
from the RFC's own literal per-entry-generation-number framing (Phase M, a deliberate, reasoned
design choice, not treated as a gap to close). With this increment, every tree this format defines
(Object, Namespace, Attribute, Extent) now has both real reachability coverage AND real
defect-tolerant scrub coverage -- the last two trees (Attribute, Extent) to lack the latter now
have it.
