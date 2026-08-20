# ArcologyFS (ArcFS) Phase C: Formatter and Transactional Writer (RFC-0039)

## Scope delivered

RFC-0039's Phase C ("allocation tree; copy-on-write metadata; commit protocol; durable checkpoint
publication; crash-injection tests") is implemented and proven end to end under QEMU/OVMF.

Two new capabilities, both consuming RFC-0038's `RAMDisk`/`BlockDevice` contract for the first time
as a **writer**, not just a reader:

- **`ArcFS.FormatVolume()`** (RFC-0039 Section 40): writes a brand-new generation-1 volume to
  whatever `BlockDevice` is currently backing `RAMDisk.*` -- a superblock, a checkpoint, and a lone
  root-directory object record -- and resets the in-memory tables (Phase A's) to match.
- **`ArcFS.PrepareCommit()` / `ArcFS.PublishCommit()` / `ArcFS.CommitImage()`** (RFC-0039 Section
  15.2): the commit protocol. Whatever Phase A's in-memory tables currently hold -- mutated by
  ordinary `CreateFile`/`CreateDirectory`/`Rename`/`Remove`/`HandleWrite` calls, completely
  unchanged from Phase A -- is written to the `BlockDevice` as a brand-new generation, in the
  copy-on-write sense RFC-0039 Section 5.2 requires: every object/namespace/data record a commit
  needs is written to a sector the previously committed generation never used, and that previous
  generation's own sectors are never modified in place.

## Design: split "prepare" from "publish" so crash-injection needs no real crash

RFC-0039 Section 15.2's own commit ordering collapses to one hard requirement: the new generation
must not become authoritative any earlier than a single, well-defined publication point. In this
implementation's minimal on-disk format that point is exactly one single-sector write: the
superblock's `checkpointSector` field.

Splitting the protocol into `ArcFS.PrepareCommit()` (every write except that one) and
`ArcFS.PublishCommit()` (exactly that one) makes crash-injection testable without simulating a real
power cut: calling `PrepareCommit` and simply never calling `PublishCommit` **is** the crash. Since
nothing on disk points at the new checkpoint yet, any subsequent `ArcFS.MountImage()` call is
provably indistinguishable from a machine that never attempted the commit at all.

`ArcFS.CommitImage()` is the convenience wrapper (`PrepareCommit` then `PublishCommit`) for the
ordinary, non-crash-injection case; it returns `TRUE` only if the entire commit, including
publication, succeeded.

## Documented Phase C scope reductions, in the same spirit as every prior phase

1. **"Allocation tree" is a conservative monotonic bump allocator**, not a real allocation tree
   with free-space reclamation. Tracked in the mount-state scratch (`nextFreeSector`), ratcheted
   forward by every `ReadCheckpoint`/`PrepareCommit` call, it never reuses a sector -- correct
   (trivially satisfies RFC-0039 Section 29's "no committed generation may describe the same
   physical block as simultaneously allocated to incompatible owners"), but wasteful: an old
   generation's now-unreachable sectors are never reclaimed. RFC-0039's own Phase E ("Snapshots and
   reclamation... safe reclamation") is explicitly where that belongs.
2. **"Flush" is a no-op.** RFC-0038's `RAMDisk.WriteSectors` is an ordinary synchronous memory
   copy -- there is no volatile device write-back cache to order against in this milestone's model,
   so Section 15.2's "ensure durability boundary reached" / "flush/order the checkpoint write"
   steps collapse to "the write already happened." A real block device's distinct
   completion-to-cache vs. durable-completion semantics (Section 34) is RFC-0038's own future work.
3. **No intra-transaction rollback.** If `PrepareCommit` fails partway through (e.g. a
   `RAMDisk.WriteSectors` call returns `FALSE` because the device is too small), the sectors
   already written for that attempt are simply abandoned as unreachable garbage -- correct per
   Section 15.3 ("blocks written for an uncommitted transaction are unreachable garbage"), since
   they were never reachable from any checkpoint: the previous generation's checkpoint is
   untouched, and the new one is never written. `PrepareCommit` also clears the pending-commit
   marker as its first action, so a caller can never accidentally publish a half-written attempt.
4. **No multi-writer/multi-transaction concurrency** (Section 32) -- one in-memory tree, one
   `BlockDevice`, matching every prior phase's "one instance" pattern.
5. **Volume UUID remains the Phase B placeholder constant.** `FormatVolume` does not generate a
   real UUID -- no fixture in this chain has yet needed to distinguish one volume from another.

## Validation

- Full suite: 51/51 passing (49 pre-existing + 2 new). No cross-file breakage this time --
  Phase A's and Phase B's own smoke tests, both of which already combine `block_device_policy.abas`
  with `arcfs_policy.abas` for their structural checks (the fix Phase B's own report documented),
  needed no further changes.
- Every new/changed public entry point (`ArcFS.FormatVolume`, `ArcFS.PrepareCommit`,
  `ArcFS.PublishCommit`, `ArcFS.CommitImage`, plus `ArcFS.MountImage` re-checked after the mount-
  state layout grew) compiles cleanly at X86_64 codegen level.
- **The commit-protocol proof, executed under QEMU/OVMF** (`aps-arcfs-phase-c.abas`):
  `ArcFS.FormatVolume()` writes a real generation-1 volume; ordinary `CreateDirectory`/
  `CreateFile`/`HandleWrite` calls build `:home`, `:home:documents`, and a 73-byte
  `:home:documents:notes.txt`; `ArcFS.CommitImage()` publishes generation 2; a fresh
  `ArcFS.MountImage()` (Phase B's own reader, untouched) resolves every path to its exact OID and
  reads the file back byte-for-byte. The fixture then snapshots generation 2's own checkpoint
  sector's raw bytes, adds a second file (`report.txt`, 51 bytes, a deliberately different content
  pattern so a mix-up between the two files would be caught, not masked) and commits generation 3,
  then re-reads that **same** sector number and asserts it is still byte-for-byte identical to the
  snapshot -- the concrete copy-on-write proof, not merely an assertion of it -- before confirming
  generation 3 is itself fully correct (both files present, both round-trip). Passed on the first
  real attempt; deterministic across three repeated runs.
- **The crash-injection proof, executed under QEMU/OVMF** (`aps-arcfs-phase-c-crash.abas`, matching
  RFC-0039 Section 78's own "crash-injection tests" and Section 75.5's requirement): commits a
  known-good baseline generation (`:home`, `:home:documents`), then stages a new file and calls
  `ArcFS.PrepareCommit()` **without** ever calling `PublishCommit` -- the simulated crash. A fresh
  mount afterward proves the baseline generation is the *only* one visible: the staged file
  resolves to nothing. The identical mutation is then redone and this time fully committed
  (`ArcFS.CommitImage()`); a second fresh mount proves the new generation is now visible, with the
  file present and its content round-tripping exactly. Passed on the first real attempt;
  deterministic across three repeated runs.
- **Negative control on the test harness itself**: flipped one of the crash-injection fixture's own
  assertions to expect the wrong outcome (the staged-but-unpublished file being visible) and
  confirmed the fixture now reports `FAIL 1` over serial rather than silently passing -- direct
  evidence the pass/fail checks are real, not vacuous.

## Remaining activation gate

- **Phases D through G are all still ahead** (mutation completeness -- resize/reflinks/typed
  attributes; snapshots and reclamation; recovery and health; system integration). This report
  covers Phase C only; RFC-0039's own `Status` remains `Draft`.
- **No real allocation tree, no reclamation** -- see scope reduction #1 above; explicitly Phase E's
  job.
- **No superblock ring, no checkpoint generation history** -- both still Phase B's own documented
  scope reductions, unaffected by Phase C's additions (Phase C writes a single superblock and a
  single checkpoint per generation, same as Phase B reads).
- **`ArcFS.PrepareCommit()`'s bump allocator has no explicit device-capacity check of its own**
  beyond `RAMDisk.WriteSectors`' own out-of-range rejection -- a commit that would run the volume
  out of space fails closed (every `WriteSectors` call is checked), but there is no advance
  "will this commit fit" estimate offered to a caller before attempting it.
