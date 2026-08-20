# ArcologyFS (ArcFS) Phase E: Snapshots and Reclamation (RFC-0039)

## Scope delivered

RFC-0039's Phase E ("snapshot roots; generation pinning; shared extent accounting; safe
reclamation") is implemented and proven end to end under QEMU/OVMF.

- **`ArcFS.CreateSnapshot()` / `ArcFS.DeleteSnapshot(snapshotId)`** (RFC-0039 Section 21): pins the
  currently active checkpoint so it survives any number of later commits, and removes that pin.
- **`ArcFS.IsSectorReachable(sector)` / `ArcFS.ReclaimableSectorCount()` / `ArcFS.
  SnapshotRetainedSectorCount()`**: "generation pinning" and "shared extent accounting," made
  concrete. Given the active checkpoint and every live snapshot, these compute exactly which
  sectors are still needed and which are safe for a future allocator to reuse.
- **`ArcFS.SnapshotResolve(snapshotId, path, pathLength)` / `ArcFS.SnapshotReadFile(snapshotId,
  oid, buffer, maxBytes)`**: read-only inspection of a pinned generation's own content, even after
  the live tree has since diverged from it.

## Why nothing has ever been destroyed, and what a snapshot buys anyway

Phase C's bump allocator never reuses a sector. That means every past checkpoint's bytes are still
physically present on the `BlockDevice` for as long as this milestone exists -- literally nothing
has ever been overwritten in place. Given that, what does a snapshot actually accomplish?

The answer is the entire reason this phase exists: a snapshot is not needed to keep data alive
*today* (nothing has ever taken it away), it is needed to tell a *future reclaiming allocator*
which generations it is NOT allowed to destroy once reclamation is wired up for real.
`ArcFS.IsSectorReachable` / `ArcFS.ReclaimableSectorCount` are that safety analysis, proven correct
now, ready for a future allocator to consult. This report is explicit that this phase computes what
is reclaimable; it does not make the allocator reuse it (see scope reduction #1 below).

## Design: snapshots are read-only and never touch the live write-path tables

The obvious-looking shortcut -- reuse `ArcFS.MountImage()` against a snapshot's checkpoint sector,
loading it into the same live Object/Namespace/data-pool tables Phase A defined -- was considered
and rejected. Those tables are the write path's own state: `ArcFS.PrepareCommit` reads directly
from them, and `ArcFSMountStateAddress`'s "active checkpoint" and "next free sector" bookkeeping
would get silently repointed at the OLD generation's much smaller numbers. The very next
`ArcFS.CommitImage()` call would then allocate sectors a newer, real generation already occupies --
corrupting it.

Instead, every snapshot-read function scans the snapshot's own on-disk records directly via
`RAMDisk.ReadSectors`, mirroring Phase B's own loader logic almost line-for-line, but writing
nothing into the live tables and touching no write-path state at all (`ArcFSPeekCheckpoint` reads
into its own dedicated scratch block, never `ArcFSMountStateAddress`). Slower per call than a table
lookup, but the live tree's integrity can never depend on which snapshot was most recently
inspected -- a correctness property worth the extra code.

One bug caught before it shipped: the first draft of `ArcFS.SnapshotReadFile` read a file's full
4096-byte extent straight into the caller's own buffer, which is smaller than 4096 bytes in every
real call site -- an out-of-bounds write. Fixed by reading into a dedicated 4096-byte extent
scratch buffer first, then copying only the requested prefix into the caller's buffer.

## A generation's range, computed uniformly

`ArcFS.PrepareCommit` always writes a generation's checkpoint record LAST, at the highest sector
number of that generation's range (`[objectRoot, checkpointSector]`). `ArcFS.FormatVolume`'s own
generation 1 is the one exception -- its checkpoint is written FIRST, at sector 1, with its lone
root object at sector 2. `ArcFSGenerationRangeContains` uses `[min(objectRoot, checkpointSector),
max(objectRoot, checkpointSector)]`, which gives the correct range in both cases without needing a
special case for generation 1.

## Documented Phase E scope reductions

1. **No real space reuse.** `ArcFS.ReclaimableSectorCount()` proves WHAT could be reclaimed;
   nothing in this phase teaches `ArcFS.PrepareCommit`'s bump allocator to actually consult it and
   reuse those sectors. Doing so changes the bump allocator's core "never reuse a sector" invariant
   Phase C established specifically to avoid a free-list's own crash-safety questions -- a real
   free-list needs the same careful crash-injection analysis Phase C gave the commit protocol
   itself, and is deliberately left for a later increment.
2. **Fixed-capacity snapshot table** (8 slots), matching every "one instance / small fixed table"
   pattern this whole project chain has used.
3. **No snapshot metadata beyond generation + checkpoint sector.** RFC-0039 Section 21's "human
   label... creator/authority metadata... optional descriptive attributes" are not implemented --
   the same STRING-has-no-operations limitation Phase A's own scope reduction #4 already gave for
   why labels are awkward on this backend.
4. **`ArcFS.ReclaimableSectorCount()` is a straightforward O(sectors x live-references) scan**, not
   an interval index -- fine at this milestone's tiny (sub-300-sector) test scale, not a production
   algorithm.

## Validation

- Full suite: 53/53 passing (52 pre-existing + 1 new test file). Re-ran every existing ArcFS test
  (`arcfs_phase_a/b/c/d_smoke`) and confirmed all four still pass unchanged -- Phase E added new
  functions and one new fixed-address table; it did not modify any function Phase A-D already
  proved.
- Every new public entry point compiles cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-e.abas`): formats a volume,
  builds `:home`/`:home:documents`/`:home:documents:notes.txt` (80 bytes of pattern A), and commits
  it as generation 2. Snapshots generation 2. Overwrites `notes.txt` with 60 bytes of a different
  pattern and commits generation 3 -- capturing that generation's own checkpoint sector for later.
  Adds a second file (`extra.txt`, 40 bytes of a third pattern) and commits generation 4, now
  active. At this point generation 2 is snapshotted (live), generation 3 is superseded AND unpinned
  (garbage), generation 4 is active (live) -- and `ArcFS.IsSectorReachable` is asked about all
  three against the exact same function and answers correctly for each. `ArcFS.
  ReclaimableSectorCount()` and `ArcFS.SnapshotRetainedSectorCount()` are both confirmed non-zero.
  `ArcFS.SnapshotResolve`/`SnapshotReadFile` against the snapshot read back generation 2's own
  80-byte pattern-A content for `notes.txt` -- and confirm `extra.txt` does not exist in that
  generation at all -- even though the live tree has since overwritten `notes.txt` once and added
  `extra.txt` since. A fresh `ArcFS.MountImage()` remount confirms the live/active tree really does
  have the diverged 60-byte and 40-byte content (not leftover in-memory state). Finally,
  `ArcFS.DeleteSnapshot()` removes the pin: `SnapshotResolve` against the deleted snapshot now
  fails, `IsSectorReachable` for generation 2 flips to `FALSE`, `ReclaimableSectorCount` strictly
  grows, and `SnapshotRetainedSectorCount` drops to zero. Passed on the first real attempt;
  deterministic across three repeated runs.
- **Negative control on the test harness itself**: flipped the snapshot-read assertion to expect
  generation 3's content instead of generation 2's (simulating the snapshot having been
  corrupted/aliased by the later overwrite) and confirmed the fixture correctly reports `FAIL 1`
  over serial rather than silently passing -- direct evidence the check is real.

## Remaining activation gate

- **Phases F and G are still ahead** (recovery and health; system integration). This report covers
  Phase E only; RFC-0039's own `Status` remains `Draft`.
- **No real space reuse** -- named above as scope reduction #1, the largest remaining gap between
  this phase and a genuinely space-efficient filesystem. A future allocator increment would consult
  `ArcFS.IsSectorReachable` before reusing a sector and would need its own crash-injection proof,
  matching Phase C's own commit-protocol precedent.
- **Snapshots cannot be written to** (matching RFC-0039 Section 21's own "Initial ArcFS snapshots
  SHALL be read-only" -- not a shortfall, the specified behavior for this milestone). Writable
  clones are named in RFC-0039 itself as a later, compatible extension.
- **No snapshot enumeration API** -- a caller must already know a snapshot's ID (returned once, at
  creation) to inspect or delete it; there is no `ArcFS.ListSnapshots()`.
