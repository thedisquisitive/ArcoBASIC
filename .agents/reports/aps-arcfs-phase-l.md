# ArcologyFS (ArcFS) Phase L: Persistent Typed Attributes (RFC-0040 Section 12)

## Scope delivered

RFC-0040 Section 12 is implemented and proven end to end under QEMU/OVMF, scoped to **one typed
attribute slot per object, now durable** -- not RFC-0040 Section 12.1's own literal
`(OID, attribute namespace, attribute name/ID)` multi-attribute-per-object model. See "Why this
scope" below.

- **A real on-disk Attribute Tree** (RFC-0040 Section 12.1), the third and final tree RFC-0040
  Section 9 originally named. Real 4096-byte self-describing, checksummed nodes, bulk-built fresh
  every commit, exactly replicating the Object Tree's (Phase J) and Namespace Tree's (Phase J
  addendum) now-proven pattern -- including reusing `ArcFSWriteOneInternalNode` and
  `ArcFSObjectTreeNodeCapacity()` UNCHANGED for internal-node fanout, since that infrastructure was
  already written generically and belongs to no particular tree.
- **Real typed values** (RFC-0040 Section 12.2): unsigned integer, signed integer (stored as raw
  bits -- this backend has no native signed type, so "signed" is purely the caller's own
  interpretation of the identical bit pattern, the same convention this codebase already uses
  wherever a signed concept is needed but the language has none), boolean, timestamp, and UUID/OID
  (the one type that genuinely needs 128 bits, using the same high/low pair convention this file's
  own OIDs already use). A `type` field doubles as the presence flag (0 = absent).
- **Real durability** (RFC-0040 Section 12.3): closes RFC-0039 Phase D's own explicitly documented
  gap -- "a value set here does NOT survive `ArcFS.MountImage()`" -- for real. `ArcFS.SetAttribute`
  mutations commit through the exact same Prepare/Publish protocol as every other tree mutation and
  are observable after a real remount, proven directly under QEMU.
- **The Checkpoint record gains `attributeTreeRoot`/`attributeTreeCount`** (RFC-0040 Section 12.1's
  own literal requirement, and RFC-0039 Section 15.1's, which the reference implementation had
  always omitted). Payload grew from 72 to 88 bytes; the checksum shifted from offset 72 to offset
  88 -- the fourth time this record's checksum has moved as fields grew (56->64->72->88), the same
  convention every prior growth already used.
- **Reachability widened a third time**: `ArcFSAttributeTreeContainsSector` joins
  `ArcFSTreeContainsSector` (Object) and `ArcFSNamespaceTreeContainsSector` (Namespace) inside
  `ArcFSGenerationContainsSector` -- the same widening discipline Phase I (data extents), Phase J
  (tree nodes), and the Namespace Tree addendum each already established once.

## Why this scope

RFC-0040 Section 12.1's literal `(OID, attribute namespace, attribute name/ID)` key describes a
real multi-attribute-per-object model -- an object could hold many independently-named attributes
across multiple namespaces. Following this project's own repeated "prove the hardest load-bearing
case once, defer full generality" discipline (Phase K's fixed-count extent list instead of a true
variable-length Extent Tree; the Namespace Tree addendum reusing the Object Tree's proven
internal-node shape rather than reinventing it), this increment keeps Phase D's own **one slot per
object** model and makes THAT durable, real, and correctly typed -- rather than simultaneously
inventing a multi-attribute keying scheme this reference implementation has never needed to prove
before. RFC-0040 Section 12.3's own literal requirement ("MUST be observable after a remount") is
squarely about durability, not cardinality; durability is what this increment closes.

A full multi-attribute-per-object model, when it becomes worth building, is now additive on top of
real, proven infrastructure: the Attribute Tree's own leaf/walk/build functions, the checkpoint's
own root+count fields, and the reachability widening are all format-and-code patterns already
established by this increment and its two predecessors (Object Tree, Namespace Tree) -- widening
the leaf entry's key from `(OID)` to `(OID, namespace, name/ID)` would not require re-deriving any
of the surrounding machinery.

## Validation

- Every new/touched function compiles cleanly at X86_64 codegen level (`ArcFS.SetAttribute`,
  `ArcFS.HasAttribute`, `ArcFS.GetAttributeType`, `ArcFS.GetAttribute`, `ArcFS.GetAttributeHigh`,
  `ArcFSAttributeTreeNodeCapacity`, `ArcFSGatherSortedActiveAttributes`,
  `ArcFSWriteOneAttributeLeafNode`, `ArcFSBuildAttributeTree`,
  `ArcFSAttributeTreeCollectAllEntriesInto`/`Tolerant`, `ArcFSAttributeTreeContainsSector`,
  `ArcFSPopulateAttributeRow`, `ArcFSLoadAttributes`, `ArcFSGenerationContainsSector`,
  `ArcFSReadCheckpoint`, `ArcFSWriteCheckpointRecord`, `ArcFSPeekCheckpoint`,
  `ArcFS.FormatVolume`, `ArcFS.PrepareCommit`, `ArcFS.MountImage`), individually reveal-checked
  against the live stdlib, plus a full sweep of all 36 public `ArcFS.`-prefixed entry points
  confirming nothing else regressed.
- Confirmed directly (not assumed) that `systems_arco_basic_arcfs_phase_d_smoke.sh` -- the one
  OTHER smoke test whose own structural-check entry list names
  `ArcFS.SetAttribute`/`HasAttribute`/`GetAttribute` -- still passes its structural check against
  the new signature, and its own frozen fixture (which still correctly proves the OLD
  in-memory-only behavior, exactly as it should for a historical record) is unaffected.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-attribute-tree.abas`): creates 5 files,
  sets a distinctly typed attribute on 4 of them (unsigned=12345, signed=99999, boolean=1,
  UUID/OID=(777777, 888888) using BOTH halves) and deliberately leaves the 5th with none at all --
  forcing (`ArcFSAttributeTreeNodeCapacity()`=4, 4 real entries) a real multi-node Attribute Tree.
  Commits, remounts, and confirms every attribute -- including the genuinely-absent case
  (`HasAttribute`=0, `GetAttributeType`=0, `GetAttribute`=0) -- round-trips exactly through a real
  disk cycle. Confirms `ArcFS.GetHealthState()` reports Healthy through the entirely new
  Attribute-Tree-sourced mount path. Commits again with no further change, superseding the WHOLE
  Attribute Tree, and confirms the old attribute root is now reported unreachable -- the same
  "follows an entire superseded tree, not one sector" proof already established for the Object and
  Namespace Trees, now repeated here. Reclaims the superseded tree for real and confirms every
  attribute is still exactly correct afterward. Updates one existing attribute (changing BOTH its
  type and value, from Signed/99999 to Timestamp/424242), commits, remounts, and confirms the
  update -- not a second value -- is what persists, with every other object's attribute completely
  unaffected. Passed on the FIRST real QEMU attempt. A negative control (flipping the
  unreachability assertion) confirmed the fixture correctly reports `FAIL 1` rather than silently
  passing. Deterministic across 3 repeated runs.
- Full suite: every pre-existing ArcFS/APS test re-run unchanged alongside the new
  `arcfs_attribute_tree_smoke` test; no stale structural-check entries found anywhere else.

## Documented scope reductions

1. **One attribute slot per object, not `(OID, namespace, name/ID)`** -- see "Why this scope"
   above. A second `ArcFS.SetAttribute` call on the same OID overwrites the existing slot (proven
   directly by the fixture's own update step), it does not add a second attribute.
2. **UTF-8 string and binary blob attribute values remain unimplemented**, exactly as RFC-0040
   Section 12.2 itself anticipates ("UTF-8 string and binary blob attribute values remain
   unimplemented under this RFC") -- `STRING` has no substring/length operations on this backend,
   the same limitation Phase A's own scope reduction #4 already named.
3. **The Attribute Tree is not scrubbed for orphaned OIDs.** `ArcFSScanGeneration` gained no new
   Pass for attribute-liveness cross-checking this increment; an attribute record naming an OID
   that no longer exists would only surface as a mount-time failure (via
   `ArcFSPopulateAttributeRow`'s own fail-closed `ArcFSFindObjectRow` check), not as a scrub defect
   count. This is not currently reachable through any real write path in this reference
   implementation (there is no operation that deletes an object row while leaving its attribute
   behind), so it is a real, honestly-named gap rather than a demonstrated live bug.
4. **`type` values 1-5 are validated on write (`ArcFS.SetAttribute` rejects 0 or anything above
   `ArcFSAttrTypeUuidOid()`), but nothing enforces that a caller's `valueHigh` is actually 0 for a
   non-UUID/OID type** -- it would simply be stored and returned unchanged via
   `ArcFS.GetAttributeHigh`, silently ignored by every type that doesn't need it. Harmless (no type
   currently interprets that field except UUID/OID) but not actively guarded against misuse.

## Remaining activation gate

- **A full `(OID, namespace, name/ID)` multi-attribute-per-object model is not yet implemented** --
  see scope reduction #1.
- **UTF-8 string and binary blob attribute values remain unimplemented** -- see scope reduction #2,
  matching RFC-0040's own stated expectation.
- **The Attribute Tree is not yet covered by the scrubber's own reachability/orphan analysis** --
  see scope reduction #3.
- **Phase M (full health/repair) remains ahead**, the last of RFC-0040's own named phases. This
  report covers Phase L (Persistent Typed Attributes) only; RFC-0040's own `Status` remains
  `Draft`.
