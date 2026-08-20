# ArcologyFS (ArcFS) Phase I: Real Free-Space Allocation and Reclamation (RFC-0040 Section 7)

## Scope delivered

RFC-0040 Section 16's Phase I ("Allocation Tree (Section 7.1); allocator integration into commit
(7.2); Reclamation Transactions (7.3); crash-injection proof of 7.4; reuse-safety proof of 7.5") is
implemented and proven end to end under QEMU/OVMF, scoped to file **data extents** specifically --
metadata (object/namespace/checkpoint/bitmap records) stays contiguous-append this increment. See
"Why data extents, not metadata" below for the architectural reason this split is correct, not a
shortcut.

- **A real Allocation Bitmap** (RFC-0040 Section 7.1): `ArcFSBitmapGet`/`ArcFSBitmapSet` maintain
  a byte-per-block in-memory mirror; `ArcFSWriteBitmapRecord`/`ArcFSLoadBitmap` persist and
  checksum-verify it, copy-on-write like every other tree, referenced by a new
  `allocationBitmapSector` field on the checkpoint record. Fixed capacity: 508 tracked blocks (the
  bitmap self-checksums within one 512-byte sector, matching every other table's own "fixed
  capacity, sized for tests" pattern).
- **Real allocation during commit** (RFC-0040 Section 7.2): `ArcFSAllocateDataExtent()` searches
  the bitmap for a free 8-sector run below the current high-water mark before ever growing past
  it. `ArcFS.PrepareCommit`'s object loop calls it for every FILE object instead of unconditionally
  advancing a monotonic cursor.
- **Reclamation is an ordinary commit** (RFC-0040 Section 7.3): `ArcFS.ReclaimGeneration()` does
  exactly one thing -- mark every currently-allocated-but-unreachable sector free in the in-memory
  bitmap. It does not call `PrepareCommit`/`PublishCommit` itself; the caller does, exactly like
  any other mutation (`ArcFS.CreateFile`, `ArcFS.Remove`, ...) already works. There is no separate,
  non-transactional reclamation code path, by construction, not by convention.
- **Crash safety** (RFC-0040 Section 7.4) and **reuse safety** (RFC-0040 Section 7.5): both fall
  out of the existing commit protocol and the fact that only `ArcFS.IsSectorReachable`-confirmed-
  unreachable sectors are ever freed -- see "Why 7.5 needed no special-casing" below.
- **Widened reachability** (a necessary consequence, not a planned deliverable): `ArcFS.
  IsSectorReachable` now also scans a generation's own FILE object records for extent membership,
  not just its contiguous metadata range -- required the moment an extent can legitimately live
  below its owning generation's own metadata span (a reused sector). See "A real, necessary
  consequence found before it could become a bug" below.
- **An exact, durably-stored high-water mark** replacing a conservative estimate: the checkpoint
  record gained a `highWaterMark` field. Phase C's own reader used to re-derive an overestimate
  from `dataRegionStart + objectCount*8`, which only worked because every FILE's extent was
  guaranteed contiguous immediately after the metadata region. Real reuse breaks that guarantee.

## Why data extents, not metadata

RFC-0040 Section 7.2 literally says PrepareCommit's successor "MUST NOT allocate a block the
Allocation Tree marks as currently allocated" for "any new object, namespace, attribute, or data
record" -- read completely literally, that includes metadata placement too. But RFC-0040 Section
9.4 (Growable Metadata Trees, Phase J) says plainly: "every existing read/write function's
`objectRoot + index` addressing assumption changes" is Phase J's own job, and Phase J's own
dependency line says it "Depends on Phases H and I (the Allocation Tree must exist to back a
growable tree's own node allocation)" -- Phase J needs Phase I's bitmap to exist so ITS OWN new
B+tree nodes have somewhere to be allocated from, not the other way around.

Object and namespace records are addressed today as `objectRoot + index`: a flat, contiguous array.
Placing them at scattered, reused sectors would break that addressing scheme immediately -- exactly
the rewrite Section 9.4 names as Phase J's, not Phase I's. Attempting real non-contiguous metadata
placement in Phase I would mean building real tree-node allocation twice: once ad hoc here, then
again "for real" in Phase J. File data extents have no such constraint -- each object's own record
already stores its own `extentSector` field independently, so an extent has always been free to
live anywhere; nothing else in the on-disk format assumes extents are contiguous with each other or
with their own object's metadata. Real reuse for data (typically the dominant consumer of space in
any real filesystem) closes RFC-0040 Section 2.1's own named "single largest reason the current
implementation cannot be used for anything real" for the piece of the format that can safely absorb
it today, while metadata keeps growing via the same bump-style placement Phase C already proved
correct, now also recorded faithfully in the bitmap for accounting purposes even though it isn't
reused yet.

## A real, necessary consequence found before it could become a bug

`ArcFSGenerationRangeContains` (Phase E) treats a generation's used sectors as one contiguous range
`[min(objectRoot, checkpointSector), max(...)]`. That was correct under every prior phase, where a
generation's data extents were always placed immediately after its own metadata, inside that same
range. Once data extents can be reused, a generation's own extent can legitimately sit BELOW its
own `objectRoot` -- outside that range entirely. Left unfixed, `ArcFS.IsSectorReachable` would have
incorrectly reported a live, in-use, reused extent as unreachable, and a subsequent
`ArcFS.ReclaimGeneration()` call would have freed a sector still actively holding another object's
real data -- silent corruption, not a crash. Found by tracing the consequence of real reuse through
every existing consumer of "is this sector reachable," not by a failing test. Fixed with a new
`ArcFSGenerationContainsSector`, which falls back to scanning a generation's own FILE object
records for extent membership when the fast range check misses.

## Why 7.5 needed no special-casing

RFC-0040 Section 7.5 forbids handing out a block "until the transaction that marked it free has
been durably published," to prevent a freed block being reused before its freedom is durable,
leaving two live generations describing the same physical block incompatibly. This implementation
never frees a sector for any reason other than `ArcFS.IsSectorReachable` reporting it unreachable
from every committed, durable checkpoint AND every live snapshot -- computed at the moment
`ArcFS.ReclaimGeneration()` runs, against whatever is currently durable. A sector reclaimed and
then immediately reused within the SAME commit is safe by construction: it was already unreachable
from the durable state before this commit began, the durable state is untouched unless this commit
publishes, and if it never publishes, remounting reloads the untouched durable state exactly as
Phase C already proved for every other kind of mutation. No block is ever handed out based on a
free status that depends on THIS commit's own eventual success -- only on the free status already
established by an earlier, durable state. Section 7.5 falls out of the existing commit protocol;
nothing new needed building for it specifically.

## Two real bugs found while building the proof fixture, not by inspection

**1. `ArcFSReadSuperblockRing`'s own embedded checkpoint-checksum check was never updated.** This
phase's own checkpoint-record widening (adding `allocationBitmapSector` and `highWaterMark`) moved
the checksum from offset 56 to offset 72 -- correctly propagated to `ArcFSReadCheckpoint` and
`ArcFSPeekCheckpoint`, but missed in `ArcFSReadSuperblockRing`'s OWN separate embedded
checkpoint-peek (used there purely to compare generation numbers across ring copies). Every mount
after the very first `ArcFS.FormatVolume()` call failed with this bug present -- caught on the
first real QEMU attempt, isolated via a temporary per-phase serial diagnostic to the exact failing
call, fixed by updating the one remaining stale offset.

**2. A test-design flaw, not an implementation bug, that took real debugging to tell apart from
one.** The fixture's original assertion checked that a just-reclaimed sector reads back as free
AFTER the SAME commit that reclaimed it also runs. But that commit also needs a fresh extent for
the only FILE object in the test, and `ArcFSAllocateDataExtent`'s own reuse-first search
legitimately reclaims that exact sector right back within the same commit -- the correct, desired
behavior, not a defect. Fixed by moving the "confirm it's free" check to immediately after
`ArcFS.ReclaimGeneration()` and before `ArcFS.CommitImage()` runs, the one moment that genuinely
isolates what reclamation itself did from what the same commit's own subsequent allocation does.
Distinguishing this from a real bug required actually tracing the allocator's own search order
(starts at sector 4, the lowest legal candidate) against what had just been freed, not just staring
at the failing assertion.

## Documented Phase I scope reductions

1. **Metadata placement (object/namespace/checkpoint/bitmap records) remains contiguous-append,
   not reused** -- see "Why data extents, not metadata" above. This is Phase J's own explicit job
   per RFC-0040 Section 9.4, not deferred out of convenience.
2. **The Allocation Bitmap has fixed capacity: 508 tracked blocks** (254 KB volumes). The same
   "fixed capacity, sized for tests" pattern this entire project has used since Phase A (64
   objects, 128 namespace rows, 8 handles, 8 snapshots).
3. **`ArcFSAllocateDataExtent`'s free-run search is O(highWaterMark × 8) per call** -- a linear
   byte-by-byte scan, matching the same performance scope reduction Phase E's own
   `ReclaimableSectorCount`/`ArcFS.IsSectorReachable` already carry ("O(sectors) reclaim scan not
   an interval index, fine at test scale").
4. **Files still have exactly one fixed 8-sector extent each** (Phase A's own scope reduction,
   unchanged). Variable-length/sparse multi-extent files are RFC-0040 Section 11, Phase K.
5. **No automatic/background reclamation.** `ArcFS.ReclaimGeneration()` must be called explicitly;
   nothing in this phase runs it on a schedule or as part of ordinary commits.

## Validation

- Every touched/new function compiles cleanly at X86_64 codegen level (`ArcFSBitmapGet`,
  `ArcFSBitmapSet`, `ArcFSLoadBitmap`, `ArcFSWriteBitmapRecord`, `ArcFSAllocateDataExtent`,
  `ArcFSGenerationContainsSector`, `ArcFS.IsSectorReachable`, `ArcFS.ReclaimGeneration`,
  `ArcFS.ReclaimableSectorCount`, `ArcFS.FormatVolume`, `ArcFS.PrepareCommit`,
  `ArcFS.PublishCommit`, `ArcFS.MountImage`, `ArcFSWriteCheckpointRecord`, `ArcFSReadCheckpoint`,
  `ArcFSPeekCheckpoint`, `ArcFSScanGeneration`, `ArcFS.ScrubVolume`, `ArcFS.GetHealthState`,
  `ArcFS.MountImageSafe`, `ArcFS.RollbackToSnapshot`, `ArcFS.ActivateSystemVolume`,
  `ArcFS.RepairCommit`), individually reveal-checked against the live stdlib.
- `stdlib/system_namespace_policy.abas` (RFC-0041) compiles cleanly combined with the updated
  `arcfs_policy.abas`.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-i.abas`): formats a volume,
  creates `file.txt` with 32 bytes of content, and commits for real (generation 2), capturing its
  real on-disk extent sector directly from the record (never hand-computed or hardcoded, so the
  fixture stays correct even if the exact allocation arithmetic shifts). Commits again with no
  further tree mutation (generation 3) -- ordinary copy-on-write alone supersedes generation 2's
  own extent. Confirms `ArcFS.IsSectorReachable` correctly reports that superseded extent
  unreachable, and confirms it is still marked allocated (nothing has freed it yet). Calls
  `ArcFS.ReclaimGeneration()` then `ArcFS.PrepareCommit()` ONLY, no publish -- the simulated crash
  -- and confirms a remount shows the sector STILL allocated, the reclamation attempt genuinely
  invisible. Redoes the reclamation for real, this time with a full `ArcFS.CommitImage()`
  (generation 4), and confirms the sector reads free immediately after `ReclaimGeneration()` runs
  (the one moment that isolates reclamation's own effect). Finally confirms `file.txt`'s real,
  on-disk extent sector after generation 4 is strictly below the high-water mark that existed
  before reclamation ran -- direct, dynamically-captured proof of genuine reuse, not growth past
  the frontier -- and that its 32 bytes of content still round-trip exactly after being relocated
  across three generations. Failed on the first two real attempts (the two bugs above), fixed, and
  passed on the third; deterministic across 3 repeated runs after the fixes.
- Full suite: every pre-existing ArcFS/APS test re-run unchanged alongside the new
  `arcfs_phase_i_smoke` test.

## Remaining activation gate

- **Metadata reuse (object/namespace/checkpoint/bitmap placement) remains contiguous-append**,
  pending Phase J's growable B+trees (RFC-0040 Section 9), which this phase's own Allocation
  Bitmap now exists to back.
- **Phase J (growable metadata trees), Phase K (extents/sparse files/on-disk reflinks), Phase L
  (persistent attributes), and Phase M (full health/repair) remain ahead**, in that order, per
  RFC-0040 Section 16's own dependency chain. This report covers Phase I only; RFC-0040's own
  `Status` remains `Draft`.
- **128-bit OID propagation through the public calling surface** (the increment named as open in
  `.agents/reports/aps-arcfs-oid-128bit.md`) remains separately open and unrelated to this phase.
