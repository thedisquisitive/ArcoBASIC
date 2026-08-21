# ArcologyFS (ArcFS): Real Variable-Count Extent Tree and On-Disk Reflink Sharing (RFC-0040 Section 11.1/11.5)

## Scope delivered

Two of RFC-0040 Section 16's own remaining Phase K gaps (named explicitly in that phase's own
report and every increment since): a true variable-count Extent Tree (Section 11.1) and on-disk
reflink sharing (Section 11.5). Both are delivered together, deliberately, because one design
decision satisfies both at once (see below).

- **A real B+tree Extent Tree**, replacing Phase K's fixed 512-byte 4-slot extent LIST as the
  on-disk shape for file chunk extents. Same node format every other tree in this file already
  uses (self-describing, checksummed, bulk-rebuilt fresh every commit from live state) --
  `ArcFSBuildExtentTree`, `ArcFSWriteOneExtentLeafNode`, `ArcFSExtentTreeCollectAllEntriesInto`,
  `ArcFSExtentTreeContainsSectorForSlot` mirror the Object Tree's own
  `ArcFSBuildObjectTree`/`ArcFSWriteOneLeafNode`/`ArcFSTreeCollectAllEntriesInto`/
  `ArcFSTreeContainsSector` exactly, reusing `ArcFSWriteOneInternalNode` unchanged for internal
  levels like every other tree already does. Leaf entries (32 bytes: `[dataSlot][chunkIndex]
  [extentSector][reserved]`) are keyed by `(dataSlot, chunkIndex)`, not by OID.
- **Genuine on-disk reflink sharing, as a direct structural consequence of that key choice.** A
  FILE object's Object Tree leaf entry now stores its own `dataSlot` directly (the field
  previously named `extentListSector`, repurposed) -- the SAME identity two reflinked-but-
  undiverged OIDs already share in memory (RFC-0039 Phase D). The Extent Tree is built ONCE PER
  UNIQUE dataSlot (`ArcFSGatherUniqueDataSlots` dedupes before any allocation happens), so two
  rows sharing an undiverged slot contribute exactly ONE set of real chunk extents to disk, not
  two. `ArcFSEnsurePrivateSlot` (RFC-0039 Phase D's own copy-on-write gate) needed ZERO changes --
  giving a diverging write a new dataSlot already makes the next commit's Extent Tree build treat
  it as a distinct key, producing genuinely separate extents. This is precisely what RFC-0040
  Section 11.5's own text asks for: "The in-memory copy-on-write gate ... SHALL be generalized to
  operate on Extent Tree entries instead of fixed data-pool slots ... without re-deriving it."
- **Checkpoint/mount-state format growth**: `extentTreeRoot`/`extentTreeCount` added to the
  checkpoint record (payload 88->104 bytes, checksum shifted accordingly -- the fifth such shift
  in this record's history) and mount/peek state, following the exact convention every prior
  field addition in this file already established.
- **Reachability, snapshot reads, and extent-overlap detection all widened to the new tree**:
  `ArcFSTreeContainsSector` (Object Tree reachability) gained an `extentTreeRoot` parameter and now
  delegates per-FILE-entry checks to `ArcFSExtentTreeContainsSectorForSlot`, which treats its own
  tree nodes as unconditionally reachable (matching every other tree's own node-reachability
  pattern) and filters leaf entries by `dataSlot`. `ArcFS.SnapshotReadFile` now walks a PINNED
  generation's own Extent Tree (via its own peeked `extentTreeRoot`) instead of a fixed list.
  `ArcFSScanGeneration`'s Pass 5 and the standalone `ArcFSFindExtentOverlaps` now detect overlap
  via `ArcFSDataSlotsOverlap`, which explicitly treats two entries sharing the SAME dataSlot as
  intentional sharing (not a defect) and rejects that case up front -- the real repair action in
  `ArcFS.RepairResolveExtentOverlaps` needed no changes at all, only the detection primitive it
  consumes.

## Why this scope, and the key design decision

RFC-0040 Section 11 bundles five requirements; Phase K already delivered the Extent Tree's two
"prove it once, honestly" prerequisites (real sparse holes, wider per-slot stride) and Section
11.4 (offset-based I/O) as their own separate increments. This increment targets the two remaining
items -- 11.1 and 11.5 -- together, because **keying the Extent Tree by `dataSlot` instead of `OID`
is the one decision that delivers both**: a variable-count structure (Section 11.1's own literal
ask) that ALSO deduplicates shared, undiverged content by construction (Section 11.5), without a
separate mechanism for either. This is not a shortcut -- it is the natural shape once RFC-0039
Phase D's own dataSlot-sharing identity is taken seriously as the Extent Tree's key.

**Deliberate deviation from Section 11.5's literal "Shared Extent Refcount" field: none is
persisted.** Reachability (`ArcFSTreeContainsSector`/`ArcFS.IsSectorReachable`) already answers "is
this sector referenced by anything still reachable" via a live graph scan, and a dataSlot-keyed
Extent Tree entry is reachable exactly when ANY object referencing that dataSlot is reachable --
correct by construction, with no separate counter that could drift out of sync with the graph it
is supposed to describe. The same judgment Phase M's own lower-OID-not-generation-number deviation
already made for this codebase: choosing the simpler, more robust mechanism over the RFC's literal
field when the two are in tension, and naming the deviation explicitly rather than silently
diverging.

**Sharing does not currently survive a remount.** `ArcFSPopulateObjectRow` still resets a row's
LIVE `dataSlot` to its own table index at mount time (unchanged from every prior phase) -- the
committed `dataSlot` value from the Object Tree entry is used ONLY as a lookup key into the Extent
Tree during that one mount, to load the object's real chunks into its own fresh, private slot.
Content correctness for every object is completely unaffected either way (this reads a real
committed slot's chunks into a private row->index slot regardless); what does not (yet) persist is
two OIDs continuing to share ONE live in-memory slot after a remount. Widening
`ArcFSSlotRefCountAddress`/`ArcFSAllocateDataSlot`'s own 1:1 row<->slot assumption to restore
cross-OID sharing after a mount is real, separate, out-of-scope future work, named here rather than
silently changed -- this increment's own proof deliberately measures sharing via the durable,
on-disk `extentTreeCount` (which DOES correctly reflect sharing within a session, across any number
of commits without an intervening remount), not via post-remount in-memory slot identity.

## A real bug found and fixed by the fixture itself

The first real QEMU attempt failed the very first `ArcFS.MountImage()` call, immediately after
`ArcFS.FormatVolume()` -- the simplest possible case, previously proven working in every earlier
phase. Bisected via serial markers to `ArcFSSelectRingCheckpoint`'s own checkpoint-checksum
verification, which still read the checksum at the OLD offset (88) instead of the new one (104)
after the checkpoint record's payload grew. An earlier `replace_all` edit meant to fix every
occurrence of this exact check only matched one of the two textually-identical-looking call sites
in the file, because the second one sits far deeper inside nested `IF` blocks and has different
leading whitespace -- a real, worth-remembering lesson: `replace_all` matches literal text
including indentation, and a function-format shared verbatim across multiple call sites can have
genuinely different indentation at each site. Fixed by locating and correcting the second
occurrence directly; confirmed via `grep` that no other stray offset-88 checksum reads remained
anywhere in the file before re-testing.

## Validation

- All 49 public `ArcFS.`-prefixed entry points, plus every new/touched internal function, compile
  cleanly at X86_64 codegen level, individually reveal-checked against the live stdlib combined
  with `block_device_policy.abas`.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-extent-tree-reflink.abas`): file A gets
  a real 8000-byte write (spanning exactly 2 real chunks), commits, remounts, content round-trips.
  Reflinking B from A (undiverged) and committing produces EXACTLY 2 real Extent Tree entries, not
  4 -- read directly from the durable, checksummed `extentTreeCount` field after a real remount,
  the direct, unambiguous on-disk-sharing proof -- with both A's and B's content independently
  confirmed correct, and `ArcFS.GetHealthState()` confirming Healthy (Pass 5's own overlap check
  does not false-positive on the legitimate sharing). A 100-byte patch written through B ALONE
  forces `ArcFSEnsurePrivateSlot` to diverge it onto its own private dataSlot; after a real commit
  and remount, `extentTreeCount` genuinely grows to 4 (A's original 2 plus B's own new, separate
  2) -- the divergence-produces-real-on-disk-separation proof -- with A completely unaffected and
  B showing the patch plus the rest of the original content intact. Two more files (C, D) are then
  created and written, pushing the total to 8 real entries -- well past
  `ArcFSExtentTreeNodeCapacity()` (4) -- FORCING a real multi-node, multi-level tree under QEMU
  (the same "small capacity, real object count" discipline Phase J's own Object Tree proof used);
  after a real commit and remount, all four files' content is independently confirmed exactly
  correct, proving the tree genuinely routes/collects across more than one leaf node. A real
  `ArcFS.ReclaimGeneration()` pass afterward (superseding generations 2-4) leaves every file's
  content still exactly intact, proving Extent Tree node reachability correctly protects live tree
  nodes and chunk extents through a real reclaim, the same guarantee Phase I/J already proved for
  data extents and Object Tree nodes respectively. Passed after fixing the checksum-offset bug
  above (a real bug found by the fixture, not a fixture-design issue); deterministic across 3
  repeated runs. A negative control (flipping the expected post-reflink entry count from 2 to 4)
  confirmed the fixture correctly reports `FAIL 1` rather than silently passing -- direct evidence
  the sharing check is real, not vacuous.
- New smoke test `systems_arco_basic_arcfs_extent_tree_reflink_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest`.
- **One real regression found and fixed before shipping**: `systems_arco_basic_arcfs_phase_k_smoke`'s
  own structural-check entry list still named 7 functions this increment genuinely REMOVED
  (`ArcFSAllocateOneSector`, `ArcFSWriteExtentListRecord`, `ArcFSBuildAndWriteExtentList`,
  `ArcFSLoadExtentListIntoPool`, `ArcFSExtentListContainsSector`, `ArcFSExtentListChunkAt`,
  `ArcFSExtentListsOverlap`, all superseded by real successors, not just renamed) -- the same exact
  lesson Phase H's and Phase J's own reports already documented about live structural checks
  needing to track renames. Fixed by updating the entry list to the real successor functions; the
  test's own REAL QEMU proof (a frozen, historical fixture) needed no changes at all.
- Full regression suite re-run clean (72/72 before this increment's own new test; the new test
  makes 73) after both fixes.

## Documented scope reductions

1. **Sharing does not survive a remount** -- see "Why this scope" above. A genuinely separate,
   larger increment (widening the 1:1 row<->dataSlot mount-time assumption) would be needed to
   restore cross-OID slot identity after a mount; content correctness is unaffected either way.
2. **No persisted Shared Extent Refcount field** -- reachability is computed live via the existing
   graph scan instead, a deliberate, more robust choice over the RFC's literal text (see above).
3. **`ArcFSMaxFileChunks()` (4, 16 KiB max file size) is unchanged this increment.** The
   DELIVERABLE is that the on-disk EXTENT REPRESENTATION is now a real, structurally unbounded
   B+tree (any dataSlot can in principle have any number of chunk entries); the in-memory
   working-set ceiling is a separate, orthogonal, session-scoped bound, matching exactly how
   Phase J left `ArcFSMaxObjects()` (64) unchanged while making the Object Tree's own on-disk
   shape genuinely variable-count. Widening `ArcFSMaxFileChunks()` itself needs no Extent Tree
   format changes at all -- direct evidence the format is the part that is now truly unbounded.
4. **The Extent Tree has no defect-tolerant scrub coverage** (no `ArcFSExtentTreeCollectAllEntriesTolerant`
   analog was written) -- matching the Attribute Tree's own already-accepted, named gap (Phase
   L/M's reports). Reachability coverage (the load-bearing half, protecting live nodes from
   reclaim) is fully present; corruption-counting coverage during a scrub is not.
5. **No fixture deliberately constructs an artificial cross-dataSlot extent-overlap defect this
   increment** -- `ArcFSDataSlotsOverlap`'s own detection logic is a direct, mechanical adaptation
   of the already-QEMU-proven (Phase M) pairwise-overlap check to the new key shape, and the real
   repair ACTION (`ArcFS.RepairResolveExtentOverlaps`'s own clearing logic) is completely
   unchanged by this increment -- only the detection primitive it consumes was touched. Stated
   plainly rather than silently left untested, matching Phase F's own precedent for the identical
   gap at the time extent-overlap detection was first introduced.

## Remaining activation gate

RFC-0040's own remaining named gaps, per the Phase M report: a full `(OID, namespace, name/ID)`
multi-attribute-per-object model (still one slot per object, Phase L); UTF-8 string/binary blob
attribute values (Phase L); the Attribute Tree still has no scrub coverage (now matched by the
Extent Tree's own identical, newly-named gap above); extent-overlap repair's authoritative-
selection policy still deviates from the RFC's own literal per-entry-generation-number framing
(Phase M, unchanged). This increment closes RFC-0040 Section 11's remaining two named items
(11.1, 11.5); Section 11 as a whole (11.1-11.5) is now fully addressed across Phase K and this
increment together.
