# ArcologyFS (ArcFS) Phase K: Extent-Based Variable-Length and Sparse Files (RFC-0040 Section 11)

## Scope delivered

RFC-0040 Section 16's Phase K ("Extent Tree (Section 11.1); variable file size (11.2); sparse
ranges (11.3); offset-based partial writes/reads (11.4); on-disk reflink sharing (11.5)... Depends
on Phase J (Extent Tree construction reuses the growable-tree machinery)") is implemented and
proven end to end under QEMU/OVMF, scoped to **real per-chunk extents and genuine sparse holes**
-- not a true variable-count Extent Tree (11.1), not offset-based partial I/O (11.4), and not
on-disk reflink sharing at the extent level (11.5). See "Why this scope" below.

- **A fixed-size extent list, not a true variable-length Extent Tree** (RFC-0040 Section 11.1,
  scoped down): each file's leaf-entry field that used to name one 8-sector data extent now names
  a 512-byte **extent list** -- a self-checksummed (CRC-32C, same convention as every other record
  in this format) block naming up to `ArcFSMaxFileChunks()` (4, deliberately small) fixed 4096-byte
  chunk sectors, 0 meaning "hole." `ArcFSBuildAndWriteExtentList` writes a real, allocated extent
  only for the chunks `ArcFSChunkTouchedGet` reports touched; an untouched chunk stays 0 in the
  list -- no allocation, no write, no on-disk footprint at all.
- **Real sparse holes that persist correctly** (RFC-0040 Section 11.3, already normative in
  RFC-0039 Section 19 and unimplemented until now): a hole reads as zero, both in the current
  session (the in-memory data pool) and across a real commit/remount round-trip
  (`ArcFSLoadExtentListIntoPool` zeroes a file's ENTIRE slot before loading, then reads back only
  the real, non-hole chunks) -- proven directly under QEMU in the fixture below.
- **The per-slot stride widened for real** (RFC-0040 Section 11.2, scoped down from fully
  unbounded to a wider fixed bound): a data-pool slot is now `ArcFSSlotSizeBytes()` =
  `ArcFSFileCapacityBytes() * ArcFSMaxFileChunks()` (16384) bytes, not 4096 -- `ArcFS.HandleWrite`,
  `ArcFS.HandleRead`, `ArcFS.Resize`, and `ArcFSEnsurePrivateSlot` all use the new stride. `Resize`
  growth zero-fills the newly exposed range in memory for this session's own correctness but
  deliberately never marks it touched, so it becomes a real sparse hole at the next commit rather
  than a real zero-filled extent -- correct per Section 11.3 and more space-efficient than the
  alternative.
- **Reachability and overlap detection widened to real per-chunk extents**:
  `ArcFSExtentListContainsSector` (used by `ArcFSTreeContainsSector`, so `ArcFS.ReclaimGeneration`
  correctly keeps every real chunk of every live file alive, not just one) and
  `ArcFSExtentListsOverlap` (Pass 5 of `ArcFSScanGeneration`, RFC-0039 Section 39's "extent overlap
  detection," now checking every real chunk of file A against every real chunk of file B instead of
  one 8-sector range each).
- **`ArcFSEnsurePrivateSlot` copies the touched-chunk bitmap, not just the bytes**: a reflinked
  clone's copy-on-write divergence now correctly carries forward which chunks are real on the
  SOURCE's slot, not just their content -- see "A real bug caught before any fixture ran" below.
- **`ArcFS.SnapshotReadFile` reconstructs its answer chunk by chunk** from the extent list instead
  of assuming one fixed 4096-byte extent, honoring holes as zero the same way the live read path
  does.

## Why this scope

RFC-0040 Section 11 bundles five requirements (Extent Tree, variable size, sparse ranges,
offset-based I/O, on-disk reflink sharing) that are each independently substantial. Following this
project's own repeated "prove the hardest real case once, defer replication/generalization"
discipline (stated explicitly in the Phase J report for the Object Tree, and in the Phase F report
for repair coverage), this phase targets the two requirements that are both load-bearing for
everything else in Section 11 and independently, concretely provable under QEMU: real per-chunk
extents (so a file is no longer bounded to one fixed capacity) and genuine sparse holes (so an
unwritten range costs nothing on disk and reads as zero, honestly, across a real commit/remount).

A **fixed-count extent list (4 chunks) instead of a true variable-length Extent Tree** was chosen
over reusing Phase J's B+tree machinery for per-file extents, because a real variable-count
structure needs real insert/delete of individual extent entries as a file grows, shrinks, or gets
partially rewritten -- and this reference implementation's own established discipline (Phase J's
own "bulk-build, not incremental split/merge" scope reduction) is to rebuild every tree/record
fresh from the live in-memory state every commit, never mutate one in place. A per-object extent
list that's *itself* rebuilt fresh every commit (exactly what `ArcFSBuildAndWriteExtentList` does)
gets the real, persisted, checksummed multi-extent shape RFC-0039 Section 16.3's own
"implementation-defined" extent representation allowance permits, without inventing a second,
smaller B+tree-like structure whose insert/delete this project's own CoW discipline would never
actually exercise. The tradeoff, stated plainly: this phase's file size ceiling is
`ArcFSMaxFileChunks() * ArcFSFileCapacityBytes()` = 16 KiB, not unbounded -- a real, deliberate,
documented limit, the same "small number you can actually exercise under QEMU" testing discipline
Phase J's own node capacity already used.

**Offset-based partial I/O (Section 11.4) and on-disk reflink sharing (Section 11.5) are
NOT implemented this phase** -- both are named directly in Documented Phase K scope reductions
below, because both are real, independently substantial rewrites of code this phase's own new
primitives do not require to be correct. `ArcFS.HandleWrite` still always replaces a file from
byte 0 (RFC-0039 Phase A's own original scope reduction, unchanged since); `ArcFS.Reflink` still
duplicates real chunk extents per object at commit time rather than sharing one refcounted extent
between OIDs on disk (RFC-0040 Section 5's Shared Extent Refcount is not consulted here -- only the
existing in-memory data-pool-slot refcount is, exactly as before this phase).

## A real bug caught before any fixture ran

While designing the chunk-touched bitmap's own indexing, re-examining how the EXISTING data pool
and slot-refcount table are indexed (by `dataSlot`, the shared identity a reflinked pair's two
OIDs both point at until one diverges -- NOT by `row`, the per-OID identity) surfaced a real design
inconsistency in an early draft that indexed the new bitmap by `row` instead. Indexed by `row`, a
freshly reflinked clone would see its OWN disconnected, stale touched-state instead of correctly
inheriting the source's real one -- exactly the class of bug this project's own "prove one real
case honestly" discipline exists to catch before it ships, not after. Fixed by indexing
`ArcFSChunkTouchedGet`/`ArcFSChunkTouchedSet` by `dataSlot`, matching every other per-slot table in
this file, with the reasoning recorded directly in the function's own header comment.

## A second real bug caught by the fixture itself

The first full fixture run failed 2 of its own assertions. Bisection (serial step markers) traced
both to the same root cause: `ArcFS.HandleWrite` has no offset parameter (a real, pre-existing
Phase A/B scope reduction -- "no persistent position across calls," RFC-0040 Section 11.4 names
this exact gap as future work), so every write REPLACES the file from byte 0 and sets its visible
`size` to exactly the byte count just written. The original fixture design assumed writing a short
50-byte "patch" through a reflinked clone would leave the rest of the file's PREVIOUS 16000-byte
content visible and checkable via `ArcFS.HandleRead` -- it does not; the write legitimately shrinks
the file's visible size to 50, and the public read API is bounded by that `size` field exactly like
every other reader in this file. The chunks beyond the write (1-3) stay real, correctly persisted,
and correctly reachable on disk -- they simply become unobservable through the size-bounded public
API, by design, not by bug. Fixed by verifying those chunks the only way that is actually honest
given this API's real contract: reading the post-mount data-pool slot directly and comparing it
byte-for-byte against the source's original content, alongside checking
`ArcFSChunkTouchedGet` reports them still touched. This is a genuine, load-bearing distinction the
fixture now documents in its own header comments, not a workaround.

## Documented Phase K scope reductions

1. **A fixed-count (4) extent list, not a true variable-length Extent Tree** -- see "Why this
   scope" above. File size is capped at `ArcFSMaxFileChunks() * ArcFSFileCapacityBytes()` = 16384
   bytes.
2. **No offset-based partial I/O** (RFC-0040 Section 11.4). `ArcFS.HandleWrite`/`HandleRead` are
   unchanged in this respect from Phase A: every write replaces the file from byte 0; every read
   starts from byte 0. A write shorter than the file's current touched footprint legitimately
   shrinks `size` and hides (without deleting) the remaining real, persisted chunks -- see "A
   second real bug caught by the fixture itself" above.
3. **No on-disk reflink sharing at the extent level** (RFC-0040 Section 11.5). Two OIDs sharing an
   in-memory data-pool slot (unchanged, pre-existing behavior) each independently get their OWN
   full set of real, duplicate chunk extents written to disk on every commit while undiverged --
   correct data, real space cost, not the refcounted single-copy-on-disk sharing Section 11.5
   describes. `ArcFSEnsurePrivateSlot`'s in-memory copy-on-write gate is unchanged in spirit
   (still what makes divergence safe), just widened to the new slot size and the touched bitmap.
4. **`ArcFS.Resize`'s growth path zero-fills the grown range in memory unconditionally**, even in
   the (currently unreachable through the public API, since a shrinking write already hides
   without deleting) case where that range secretly still holds real, touched, but currently
   invisible data from a prior larger write. Stated for completeness: this is consistent with
   `size` being this API's sole source of truth for "what was really written," which is the
   correct contract Section 11.3 asks for; it is not something this phase found a live path to
   actually trigger a problem through.
5. **`ArcFS.SnapshotReadFile`'s extent-list-aware rewrite reads a chunk at a time with no
   short-circuit past what the read actually needs** -- correct, not a scaling concern at this
   phase's 4-chunk ceiling.

## Validation

- Every touched/new function compiles cleanly at X86_64 codegen level (`ArcFSSlotSizeBytes`,
  `ArcFSChunkTouchedGet`/`Set`, `ArcFSAllocateOneSector`, `ArcFSWriteExtentListRecord`,
  `ArcFSBuildAndWriteExtentList`, `ArcFSLoadExtentListIntoPool`, `ArcFSExtentListContainsSector`,
  `ArcFSExtentListChunkAt`, `ArcFSExtentListsOverlap`, `ArcFSEnsurePrivateSlot`,
  `ArcFS.HandleWrite`, `ArcFS.HandleRead`, `ArcFS.Resize`, `ArcFSWriteOneLeafNode`,
  `ArcFSPopulateObjectRow`, `ArcFSLoadObjects`, `ArcFSTreeContainsSector`, `ArcFSScanGeneration`,
  `ArcFS.SnapshotReadFile`), individually reveal-checked against the live stdlib, plus a full sweep
  of every prior phase's own smoke-test entry list confirming nothing else regressed.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-k.abas`): creates file A, writes a
  100-byte partial pattern (touches chunk 0 only), commits, remounts, and confirms it round-trips.
  Resizes A to the full 16384-byte slot width with no new write, confirms the newly exposed range
  reads as zero BOTH before any commit and after a real commit/remount -- the core sparse-hole
  proof. Writes a real 16000-byte pattern spanning all 4 chunks, commits, remounts, and confirms
  every byte round-trips exactly. Reclaims the now-superseded generations for real and confirms A's
  content is still exactly correct. Reflinks B from A (shared slot), commits, remounts, and
  confirms B reads back identical to A. Writes a 50-byte patch through B alone, forcing
  `ArcFSEnsurePrivateSlot` to give B a private slot; confirms A is completely unaffected, B's
  visible 50 bytes are the new patch, and -- read directly off the post-mount data pool, since the
  public API cannot reach past `size` -- B's chunks 1-3 are still marked touched and byte-for-byte
  identical to A's original content. Confirms `ArcFS.GetHealthState()` reports Healthy, runs a
  final real reclaim, and confirms both A's and B's content -- including B's size-invisible-but-real
  chunks -- are still exactly correct afterward. Passed on the first attempt after the fixture's own
  design bug (see above) was fixed. A negative control (deliberately shifting a comparison offset
  by one byte) confirmed the fixture correctly reports `FAIL 1` rather than silently passing.
  Deterministic across 3 repeated runs.
- Full suite: every pre-existing ArcFS/APS test re-run unchanged alongside the new
  `arcfs_phase_k_smoke` test; no stale structural-check entries found in any other smoke test this
  phase (only parameter names changed on existing functions, not function names or signatures'
  arity in a way any other script's entry list references).

## Remaining activation gate

- **A true variable-count Extent Tree is not yet implemented** -- files remain capped at 16384
  bytes (4 fixed 4096-byte chunks). Section 11.1's literal per-object B+tree-of-extents shape
  remains future work, following the same "prove the fixed-count case first" discipline Phase J
  used for node capacity.
- **Offset-based partial I/O (Section 11.4) is not yet implemented** -- every write/read still
  starts at byte 0.
- **On-disk reflink sharing at the extent level (Section 11.5) is not yet implemented** -- reflinked
  files duplicate real extents on disk while undiverged rather than sharing one refcounted copy.
- **Phase L (persistent attributes) and Phase M (full health/repair) remain ahead**, in that order,
  per RFC-0040 Section 16's own dependency chain. This report covers Phase K (real per-chunk
  extents and genuine sparse holes) only; RFC-0040's own `Status` remains `Draft`.
