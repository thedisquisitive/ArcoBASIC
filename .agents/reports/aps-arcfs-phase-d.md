# ArcologyFS (ArcFS) Phase D: Mutation Completeness (RFC-0039)

## Scope delivered

RFC-0039's Phase D ("create/write/resize; rename/move; delete; sparse files; reflinks; typed
attributes") is implemented and proven end to end under QEMU/OVMF.

Rename, move, and delete already existed (Phase A's `ArcFS.Rename`/`ArcFS.Remove`) and already
survived a commit/remount cycle unchanged -- they mutate the same in-memory tables Phase C's commit
protocol already durably writes. Phase D added no new API for them; it closed a real gap instead:
no fixture in this chain had ever actually renamed, moved, or deleted something and then proven it
through a real commit + remount. This phase's fixture does.

What's genuinely new:

- **`ArcFS.Resize(handle, newSize)`**: grows or shrinks a file within its existing fixed capacity.
  Growing zero-fills the newly exposed range (RFC-0039 Section 19's "a logical range with no
  physical extent SHALL read as zero bytes," applied to the bytes between the old and new size).
- **`ArcFS.Reflink(sourceOid, destParentOid, name, nameLength)`** (RFC-0039 Section 22): creates a
  new OID that shares the source's data-pool slot -- nothing is copied at reflink time. The first
  write through *either* OID afterward triggers copy-on-write before touching a single byte.

## Design: decouple data-slot allocation from object-row allocation

Every phase through C relied on one convention: an object's data-pool slot number always equaled
its own object-table row index. That convention has no room for two rows to point at the same
slot, which is exactly what Reflink needs.

Phase D introduces a slot allocator (`ArcFSAllocateDataSlot`, a bump counter in the same spirit as
Phase C's own sector allocator) fully decoupled from row allocation, plus a per-slot reference-
count table (`ArcFSSlotRefCountAddress`). `ArcFSEnsurePrivateSlot` is the copy-on-write gate every
mutating data-pool access (`HandleWrite`, `Resize`) now passes through: if the row's current slot
is exclusively its own (refcount <= 1) it's returned unchanged -- byte-for-byte the same behavior
every object had before Phase D existed, since a slot's refcount only ever exceeds 1 after a
`Reflink`. Otherwise, a fresh slot is allocated, the shared bytes are copied into it, *then* the row
is repointed -- so the write that follows never lands on a byte another OID can still see.

This is what makes RFC-0039 Section 22's requirement ("a reflink MUST NOT cause later writes to one
file to modify the visible data of the other") hold by construction rather than by convention, and
it's what the fixture's central proof actually demonstrates: read the clone before any write (must
match the source exactly, since nothing was copied yet, only shared), write new content through the
clone, then read the *source* again and confirm it is byte-for-byte unchanged.

One real generalization this forced: `ArcFS.PrepareCommit` (Phase C) had taken a shortcut --
reading file data from the data pool at `row * 4096` instead of reading the row's own `dataSlot`
field, relying on the row-equals-slot convention Reflink now breaks. Fixed to read the field, like
`HandleWrite`/`HandleRead` already correctly did. For every object created before Phase D existed,
`dataSlot` still equals `row`, so this produces byte-identical output for Phase C's own fixtures --
confirmed by re-running them unchanged after the fix (see Validation below).

## Documented Phase D scope reductions

1. **Sparse files are out of scope.** Every `FILE` object still gets exactly one mandatory fixed
   4096-byte extent (Phase A's own scope reduction #3) -- there is no "hole" concept possible
   without a real multi-extent-per-file model, a bigger undertaking than this phase's budget.
   Deferred to whichever future phase replaces the fixed-single-extent design.
2. **On-disk reflink sharing is not implemented.** `ArcFS.CommitImage()` still writes each active
   object's *current* bytes to its own independent on-disk extent. Two objects still sharing a
   data-pool slot in memory at commit time simply get two identical extents on disk, not one shared
   one. The in-memory copy-on-write *semantic* is fully proven; the on-disk *space saving* a real
   reflink implies is not -- that needs a reference-counted allocation tree, already named as Phase
   E's job in Phase C's own report.
3. **Typed attributes are in-memory only.** `ArcFS.SetAttribute`'s value does not survive
   `ArcFS.MountImage()`. Persisting it would have required extending Phase B's/Phase C's already-
   proven, committed checkpoint format (new `attributeRoot`/`attributeCount` fields, a new on-disk
   record type, a new sector region) -- judged out of scope for this phase given the size of the
   reflink/resize work already involved; deferred to a later phase. Only the integer-representable
   subset of RFC-0039 Section 24's base value types is even reachable on this backend regardless
   (STRING has no substring/length operations here, per Phase A's own scope reduction #4), so the
   feature was kept to exactly one fixed U64-valued slot per object.
4. **No advance capacity check.** `ArcFSEnsurePrivateSlot`/`ArcFS.Reflink` fail closed (return a
   sentinel/`FALSE`) when the data pool is exhausted, but there is no "will this fit" query offered
   ahead of attempting a mutation, matching Phase C's own documented gap for commits.

## Validation

- Full suite: 52/52 passing (51 pre-existing + 1 new test file, itself covering two fixtures). Re-
  ran every existing ArcFS test (`arcfs_phase_a/b/c_smoke`) after the Phase D stdlib changes and
  confirmed all three still pass unchanged -- the row-equals-slot generalization in
  `PrepareCommit` and the new copy-on-write gate in `HandleWrite`/`Resize` are both strict,
  behavior-preserving generalizations for every object that predates Reflink.
- Every new/changed public entry point compiles cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-d.abas`): creates `:home`,
  `:home:documents`, and a 200-byte `original.txt`; reflinks it to `clone.txt` and confirms the
  clone reads the *exact* pre-write bytes; writes 150 bytes of a deliberately different pattern
  through the clone's handle; re-reads the *original* and confirms it is still exactly its own 200
  bytes, untouched -- the central copy-on-write proof. Grows the original from 200 to 300 bytes and
  confirms the new 100-byte range reads as zero while the original 200 bytes are unchanged; shrinks
  the clone from 150 to 50 bytes and confirms both the new size and the retained content. Renames
  the clone within the same directory, then moves it to a different one, confirming OID-preserving
  resolution at each step and that the old path is gone. Deletes the original and sets a typed
  attribute on `:home`. Commits everything as a real generation and remounts from scratch: confirms
  the moved/renamed/resized file resolves and reads back correctly, confirms the deleted file stays
  deleted, confirms the directory survives, and confirms the attribute does **not** survive --
  positively proving the documented limitation rather than merely asserting it in a comment. Passed
  on the first real attempt; deterministic across three repeated runs.
- **Negative control on the test harness itself**: flipped the central copy-on-write assertion to
  expect the wrong outcome (the source appearing to have been corrupted by the clone's write) and
  confirmed the fixture correctly reports `FAIL 1` over serial rather than silently passing --
  direct evidence the check is real.

## Remaining activation gate

- **Phases E through G are all still ahead** (snapshots and reclamation; recovery and health;
  system integration). This report covers Phase D only; RFC-0039's own `Status` remains `Draft`.
- **Sparse files, on-disk reflink sharing, and attribute persistence** -- all three named above as
  explicit, documented scope reductions, each with a named reason it's deferred rather than simply
  missing.
- **The data pool's 64-slot ceiling is now shared between ordinary object creation and reflink
  copy-on-write**, whereas before Phase D it was implicitly 1:1 with object-row capacity. A
  reflink-heavy workload with many subsequent writes can exhaust data slots strictly faster than it
  exhausts object rows; both paths fail closed correctly, but a caller has no way to distinguish
  "object table full" from "data pool full" without inspecting which call failed.
