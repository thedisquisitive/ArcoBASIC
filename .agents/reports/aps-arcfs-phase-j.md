# ArcologyFS (ArcFS) Phase J: Growable Object Tree (RFC-0040 Section 9)

## Scope delivered

RFC-0040 Section 16's Phase J ("Object Tree, Namespace Tree as real B+trees (Section 9)... the
largest single rewrite in this RFC") is implemented and proven end to end under QEMU/OVMF, scoped
to the **Object Tree only** -- the Namespace Tree stays a flat array this increment. See "Why the
Object Tree only" below.

- **A real on-disk B+tree** (RFC-0040 Section 9.1): nodes are 4096 bytes (8 sectors, RFC-0039
  Section 13's own logical block size), self-describing (node type, key count) and independently
  CRC-32C checksummed. Leaf nodes hold up to `ArcFSObjectTreeNodeCapacity()` (4, deliberately
  small -- see below) object records; internal nodes hold the same number of real routing keys
  and child pointers.
- **Real node allocation, reused from Phase I unchanged**: a tree node and a file's data extent
  are the identical size, so `ArcFSAllocateTreeNode()` is a one-line call into Phase I's own
  `ArcFSAllocateDataExtent()` -- the same real reuse-before-growth allocator now backs both.
- **Bulk construction, not incremental insert-with-split** (a deliberate departure from RFC-0040
  Section 9.3's literal framing -- see "Why bulk-build, not incremental split/merge" below):
  `ArcFSBuildObjectTree()` sorts the current active object set and builds a fresh, minimally-
  shaped tree bottom-up every commit, exactly mirroring how every other tree/record in this
  reference implementation is already rewritten fresh each generation.
- **Real, verified multi-node reads**: `ArcFSTreeCollectAllEntriesInto` recursively walks a tree,
  checksum-verifying every node it visits, and is the new basis for mounting
  (`ArcFSLoadObjects`/`ArcFSPopulateObjectRow`) and single-key lookup
  (`ArcFSSnapshotFindObjectRecord`).
- **Reachability and the scrubber extended to real tree structure**, not just data (see the two
  "found before it could corrupt anything" sections below): `ArcFSTreeContainsSector` and the
  defect-tolerant `ArcFSTreeCollectAllEntriesTolerant`.

## Why the Object Tree only

RFC-0040 Section 9.4 itself says growable metadata trees are "the single largest implementation
effort in this RFC," and Phase J's own dependency line in Section 16 says it "Depends on Phases H
and I (the Allocation Tree must exist to back a growable tree's own node allocation)" -- meaning
Phase J's own B+tree nodes need Phase I's Allocation Bitmap to allocate themselves from. Attempting
Object Tree, Namespace Tree, and Attribute Tree together in one increment would multiply an already
large rewrite by three, for a marginal proof value: once the Object Tree proves the real, hard
contract (node format, real CoW allocation, real multi-level construction, real checksum-verified
reads, reachability through scattered structure), replicating that SAME proven pattern onto the
Namespace Tree (and, once it has a real persistent design at all -- it does not yet, per Phase D's
own scope reduction -- the Attribute Tree) is comparatively mechanical. This is the same "prove the
hardest case once, defer replication" discipline this whole project has used since Phase F picked
one repair case out of several named defect classes.

## Why bulk-build, not incremental split/merge

RFC-0040 Section 9.3 says node split "on insert into a full node... SHALL follow standard B+tree
algorithms" and merge "on delete leaving a node under a minimum occupancy threshold... SHALL follow
standard B+tree algorithms." Taken completely literally, this implies an incrementally-maintained
on-disk tree that a live insert or delete mutates and rebalances in place.

That is not how anything else in this reference implementation works, and was never going to be:
every tree, record, and now the Allocation Bitmap is already rewritten FRESH, from the complete
current in-memory state, every single commit (RFC-0039 Section 5.2's copy-on-write discipline,
true since Phase C). There is no "existing on-disk node" for an incremental insert to find and
split in the first place -- the entire durable representation is reconstructed each generation
regardless. Given that, `ArcFSBuildObjectTree()` sorts the live key set and bulk-loads a tree
bottom-up: partition into leaves, then repeatedly partition the level below into the level above
until one node remains. This produces a tree that is byte-for-byte indistinguishable in FORMAT
from what an incremental implementation would produce -- same node types, same self-describing
headers, same checksums, same routing shape (RFC-0040 Section 9.1's on-disk contract is fully
satisfied) -- only the algorithm that PRODUCES a new generation's tree differs from the RFC's
literal framing.

This also makes merge/rebalance moot by construction, not merely unimplemented: there is no
"delete leaving a node under-occupied" scenario to repair, because every rebuild is already
minimally and correctly packed from whatever the live key set actually is. Recursion (confirmed
directly under real QEMU execution before this phase began, since a B+tree walk needs it and this
project had never proven it) makes the recursive collection/containment walks straightforward;
bulk construction means neither of them ever needed split-propagation or rebalancing logic at all.

Node occupancy (`ArcFSObjectTreeNodeCapacity`, 4) is deliberately small, not the node's real
physical capacity (roughly 100 entries would fit in 4096 bytes) -- chosen specifically so a
handful of real objects, well under `ArcFSMaxObjects()`'s own 64-row ceiling, provably produces a
real multi-node, multi-level tree under QEMU. RFC-0039 Section 16's own "precise node split
heuristics are implementation-defined as long as the persistent format and ordering rules are
obeyed" already permits this.

## A real, necessary consequence found before it could corrupt anything

`ArcFS.IsSectorReachable`'s existing contiguous-range check (`ArcFSGenerationRangeContains`,
narrowed and renamed `ArcFSGenerationMetadataRangeContains` this phase) covered a generation's
metadata as one span from its object root through its checkpoint. Under Phase J, the Object
Tree's own nodes -- not just the data extents Phase I already had to solve this for -- can now
legitimately live below that span if reused from reclaimed space, or simply be one of several
non-contiguous nodes scattered across a multi-node tree. Left unfixed, a live, in-use tree node
could be reported unreachable and freed by `ArcFS.ReclaimGeneration()` out from under the
generation still using it -- silent corruption, the same class of bug Phase I already had to fix
once for data extents, now recurring one layer up in the format. Fixed with
`ArcFSTreeContainsSector`, which recursively checks whether a target sector falls within the
CURRENT node's own 8-sector span, within a leaf entry's own data extent, or within any child's
subtree -- correct regardless of how the tree's nodes happen to be scattered.

## A second real consequence: the scrubber must tolerate a corrupt node, not abort on one

`ArcFSScanGeneration`'s whole purpose (RFC-0039 Section 39/40) is to keep counting defects and
finish scanning everything it still can, even in the presence of corruption -- the OLD flat-array
Pass 1 tolerated an individual corrupt object record by counting one defect and moving on to the
NEXT, independently-addressable record. A naive tree walk that fails closed on the first checksum
mismatch (correct, and necessary, for mounting) would instead abort the WHOLE scan the first time
any node's checksum failed, under-counting defects and never reaching Pass 2-5's own checks. Fixed
with a second, defect-tolerant walk (`ArcFSTreeCollectAllEntriesTolerant`) built specifically for
the scrubber: a corrupt leaf is skipped and counted as one defect, and -- honestly, not hidden -- a
corrupt INTERNAL or ROOT node is a real, structurally different failure mode from the old flat
array's: every OID reachable only through that node genuinely cannot be recovered by this pass, so
its entire subtree is skipped and counted as exactly one defect rather than silently treated as
empty. Pass 4 and Pass 5's own loop bounds were widened from the checkpoint's claimed object count
to the walk's actual collected count, so a partially-corrupt tree cannot cause a read past what was
really collected.

## Documented Phase J scope reductions

1. **Namespace Tree and Attribute Tree remain flat arrays** this increment -- see "Why the Object
   Tree only" above. `ArcFSGenerationMetadataRangeContains` still covers namespace/bitmap/
   checkpoint placement, unchanged.
2. **In-memory representation stays the existing fixed 64-row table** (`ArcFSMaxObjects()`), not
   RFC-0040 Section 9.2's literal "bounded LRU node cache backed by dynamically allocated pages."
   The tree exists purely as the DURABLE, on-disk serialization -- every ordinary
   `ArcFS.CreateFile`/`Reflink`/etc. call still mutates the same in-memory table exactly as it
   always has, completely unchanged this phase. This is the single largest departure from Section
   9.2's literal text; the working-set ceiling Phase A originally imposed is still in force.
3. **Internal node routing keys are written correctly but not yet consulted for key-guided
   descent.** Every reader in this phase (mount, reachability, snapshot lookup, the scrubber) walks
   every child rather than comparing keys to skip subtrees -- correct (an exhaustive walk visits
   every entry a key-guided search would too) but O(node count), not O(log n). The keys themselves
   are real and correctly computed (`key[i]` = the smallest key reachable through `child[i]`), so a
   future key-guided search can be added without any format change.
4. **A corrupt internal/root node makes its own subtree's objects unrecoverable by the scrubber**
   (see above) -- a real, honestly-documented, structurally different failure mode from the old
   flat array's fully-independent-per-record tolerance. Not exercised by a new dedicated fixture
   this phase (Phase F's own existing corruption-detection fixtures test object-level, not tree-
   node-level, corruption).

## Validation

- Every touched/new function compiles cleanly at X86_64 codegen level (`ArcFSAllocateTreeNode`,
  `ArcFSGatherSortedActiveObjects`, `ArcFSWriteOneLeafNode`, `ArcFSWriteOneInternalNode`,
  `ArcFSBuildObjectTree`, `ArcFSTreeCollectAllEntriesInto`, `ArcFSTreeCollectAllEntriesTolerant`,
  `ArcFSTreeContainsSector`, `ArcFSPopulateObjectRow`, `ArcFSLoadObjects`,
  `ArcFSGenerationMetadataRangeContains`, `ArcFSGenerationContainsSector`,
  `ArcFSSnapshotFindObjectRecord`, `ArcFSScanGeneration`, and every public `ArcFS.`-prefixed
  function), individually reveal-checked against the live stdlib, plus a full sweep of all 35
  public entry points confirming nothing else regressed.
- `stdlib/system_namespace_policy.abas` (RFC-0041) compiles cleanly combined with the updated
  `arcfs_policy.abas`.
- Recursion itself was confirmed working under real QEMU execution (a factorial function, not
  just structurally compiled) before this phase's own recursive tree-walk functions were written
  -- this project had never proven recursive calls before, and the whole design depends on them.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-j.abas`): creates 6 files plus
  root (7 active objects) against a deliberately small node capacity of 4, forcing a real
  multi-leaf, multi-level tree, and commits for real. Confirms every file resolves with correct,
  distinct content after a real commit and remount. Confirms `ArcFS.GetHealthState()` reports
  Healthy through the entirely new tree-walking scrub path. Commits again with no further change,
  superseding generation 2's whole tree, and confirms the old root is now reported unreachable --
  proving reachability follows an entire superseded tree structure, not merely a single sector.
  Reclaims the superseded tree for real and confirms every file still resolves with correct
  content afterward. Passed on the first real QEMU attempt. A negative control (flipping the
  reachability assertion to expect the wrong outcome) confirmed the fixture correctly reports
  `FAIL 1` rather than silently passing. Deterministic across 3 repeated runs.
- Full suite: every pre-existing ArcFS/APS test re-run unchanged alongside the new
  `arcfs_phase_j_smoke` test. Found and fixed one real regression before it could ship: the
  128-bit OID smoke test's own structural-check entry list still named two functions
  (`ArcFSWriteObjectRecord`, `ArcFSLoadOneObject`) this phase genuinely removed/replaced --
  updated to their real successors (`ArcFSWriteOneLeafNode`, `ArcFSPopulateObjectRow`), matching
  the same lesson Phase H's own report already recorded about live structural checks.

## Remaining activation gate

- **Namespace Tree and Attribute Tree remain flat arrays**, the next natural replication of this
  phase's own proven pattern, not yet begun.
- **In-memory representation is not yet the dynamic, unbounded LRU cache RFC-0040 Section 9.2
  literally specifies** -- the working set stays capped at `ArcFSMaxObjects()` (64), Phase A's own
  original scope reduction, still in force.
- **Key-guided descent through internal nodes is not yet implemented** -- every reader walks every
  child. Correct, O(node count) rather than O(log n); the routing keys are already real and
  correctly computed, so this is additive when it becomes worth doing.
- **Phase K (extents/sparse files/on-disk reflink sharing), Phase L (persistent attributes), and
  Phase M (full health/repair) remain ahead**, in that order, per RFC-0040 Section 16's own
  dependency chain. This report covers Phase J (Object Tree only) only; RFC-0040's own `Status`
  remains `Draft`.
