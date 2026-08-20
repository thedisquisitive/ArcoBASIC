# ArcologyFS (ArcFS) Phase G: System Integration (RFC-0039)

## Scope delivered

RFC-0039's Phase G ("namespace attachment; system volume use; recovery environment support;
update snapshots; graphical storage tooling; ArcoBASIC bindings") is **partially** addressed --
one item genuinely implemented and QEMU-proven, one item recognized as already satisfied by
construction, and four items explicitly left open with a stated reason each, rather than silently
skipped or fabricated.

- **`ArcFS.RollbackToSnapshot(snapshotId)`** (RFC-0039 Section 51, "update snapshots"):
  implemented and proven. Republishes a previously snapshotted generation as the active
  checkpoint -- a real, atomic undo of a bad update.
- **ArcoBASIC bindings**: recognized as already satisfied by construction (see below), not newly
  built.
- **Namespace attachment, system volume use, recovery environment support, graphical storage
  tooling**: explicitly left open. Each depends on an Arcology subsystem that does not exist
  anywhere in this repository yet -- building one would be separate, real scope creep belonging to
  that subsystem's own RFC, not ArcFS's.

## Why this phase couldn't just implement its own list

RFC-0039 Section 78's own Phase G items are, overwhelmingly, integration surface with OTHER
Arcology subsystems -- not additional ArcFS format or protocol work. Four of the six items name
something that doesn't exist yet in this codebase to integrate with:

- **Namespace attachment** (Section 10's DISCOVER/CREATE/ATTACH/VERIFY/ACTIVATE lifecycle against
  an Arcology namespace *location*) has no concrete target. This repository has no implemented
  Arcology object/namespace facility outside ArcFS's own -- confirmed the same way RFC-0038's own
  implementation confirmed RFC-0017 (Substrate Resource Model) has no concrete freestanding
  callable surface anywhere in this repository (Phase A's own scope reduction #6 cites the
  identical finding, independently re-confirmed here).
- **System volume use** presumes a boot path that selects and activates a system ArcFS volume
  before the rest of the OS starts. No such boot-time volume-selection mechanism exists anywhere
  in this project; every ArcFS fixture in this whole chain formats or mounts its own volume
  explicitly, from a fresh boot, for exactly that one fixture.
- **Recovery environment support** presumes a distinct recovery boot mode. This project has no
  second boot path or recovery-specific entry point anywhere; every fixture boots the same
  ordinary UEFI `Main()` entry every other freestanding fixture in this project does.
- **Graphical storage tooling** presumes a storage-inspector UI (RFC-0039 Section 44's own
  illustrative volume-inspector text layout). `stdlib/graphics_primitives.abas`/
  `graphics_substrate.abas` exist for pixel-level drawing, but nothing resembling an inspector
  shell exists to extend -- building one is a UI-application project, not ArcFS's own.

Building fake infrastructure to claim these "done" -- a pretend namespace to attach to, a pretend
boot path -- would be exactly what RFC-0039 Section 79.2 ("No silent architectural substitution")
forbids. They are left open, with the specific missing dependency named for each, matching RFC-0039
Section 80's own "Required Pre-Implementation Dependencies" list (which already names several of
these as open dependencies -- this phase did not discover something new, it confirmed and recorded
what the RFC itself already flagged).

## What genuinely was buildable, and what it proves

**`ArcFS.RollbackToSnapshot`** reuses `ArcFS.PublishCommit`'s exact mechanism -- stage a checkpoint
sector as "pending," then perform the one single-sector superblock write that makes it active --
rather than duplicating that logic. The only difference from an ordinary commit's publish step is
*which* checkpoint sector gets staged: a snapshot's old one, instead of a freshly prepared new one.
This is the concrete, missing half of RFC-0039 Section 51's own illustrative workflow (`CREATE
snapshot "pre-update" -> ... -> COMMIT -> MARK boot generation`) -- undoing a bad update, which
Phase E's `CreateSnapshot` alone could pin against but never actually restore to.

**ArcoBASIC bindings**: every `ArcFS.*` function across all seven phases already *is* a native
ArcoBASIC-callable entry point -- this compiler has no separate host/guest language boundary or FFI
layer for a "binding" to occupy. RFC-0039 Section 43's own illustrative syntax (`FILE.OPEN(...)`,
`.READTEXT()`, object-inspection properties) describes a friendlier OO-flavored surface this
implementation does not wrap `ArcFS.*` in, but that is a naming/ergonomics choice, not a missing
capability, and RFC-0039 itself never mandates that exact surface -- only that ArcoBASIC be
"pleasant to use... without exposing raw on-disk machinery for ordinary tasks" (Section 43's own
framing), which every `ArcFS.*` function already is.

## Validation

- Full suite: 55/55 passing (54 pre-existing + 1 new test file). Re-ran every existing ArcFS test
  (`arcfs_phase_a/b/c/d/e/f_smoke`) and confirmed all six still pass unchanged -- Phase G added one
  new function and no changes to any existing one.
- `ArcFS.RollbackToSnapshot` compiles cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-g.abas`): formats a volume,
  builds `:home`/`:home:documents`/`:home:documents:v1.txt` (50 bytes of a known-good pattern),
  commits generation 2, and snapshots it ("pre-update"). Overwrites `v1.txt` with 40 bytes of a
  different pattern and commits generation 3 -- confirmed, by reading it back, that the "bad
  update" genuinely took effect. `ArcFS.RollbackToSnapshot` republishes generation 2; a fresh
  remount confirms `v1.txt` is back to its exact original 50 bytes. Finally, confirms the volume is
  fully **read-write** usable after rollback, not merely readable: creates a brand-new file on top
  of the rolled-back tree, commits it, remounts again, and confirms both the new file AND the
  rolled-back file survive that further commit correctly. Passed on the first real attempt;
  deterministic across three repeated runs.
- **Negative control on the test harness itself**: flipped the post-rollback assertion to expect
  the bad update's content instead of the original (as if rollback had silently done nothing) and
  confirmed the fixture correctly reports `FAIL 1` over serial rather than silently passing --
  direct evidence the check is real.

## RFC-0039's overall status after Phases A-G

RFC-0039 remains `Status: Draft`, deliberately. Seven phases of implementation work are complete
and QEMU-proven, but the accumulated, honestly-documented scope reductions across all of them mean
this is not a production-ready filesystem:

- No real space reclamation (Phase E proves what's reclaimable; nothing reuses it).
- No sparse files, no on-disk reflink sharing (Phase D).
- No persistent typed attributes (Phase D).
- Only three health states, only one repair class, no scrub/repair history (Phase F).
- No namespace attachment, system volume use, recovery environment, or graphical tooling (this
  phase).

Every one of these is named, in its own phase's report, with the specific reason it was deferred
rather than attempted and gotten wrong. That is the intended reading of "Draft" here: a large,
coherent, real, and repeatedly QEMU-proven implementation of RFC-0039's core object/namespace/
commit/snapshot/recovery model, with its remaining gaps stated precisely enough that a future
increment knows exactly where to start.
