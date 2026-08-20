# ArcologyFS (ArcFS) Phase G: System Integration (RFC-0039)

## Addendum (2026-08-19): "system volume use," narrowed and delivered

The user asked to finish "system volume use" specifically. Doing so honestly required splitting
that one Phase G item into the piece ArcFS itself can deliver and the piece it cannot:

- **DISCOVER** (RFC-0039 Section 10: enumerate real UEFI Block IO Protocol handles instead of the
  fixed-address RAM-disk preload every fixture in this whole chain uses) remains explicitly out of
  scope. It is RFC-0038's own named future work -- that RFC's Section 17.5 stop condition
  specifically warns against improvising new UEFI protocol bindings as a side effect of unrelated
  work, and this addendum does not violate that boundary.
- **ATTACH -> VERIFY -> ACTIVATE** (RFC-0039 Section 37's own activation checklist, plus Section
  38's ReadOnlySafety) is squarely ArcFS's own responsibility, and this addendum delivers it as a
  real boot policy: `ArcFS.ActivateSystemVolume()`.

Phase F already built every primitive this needed (`ArcFS.MountImageSafe`,
`ArcFS.RepairReattachOrphans`, `ArcFS.RepairCommit`) as separate tools a caller composes by hand --
exactly what every Phase F fixture does. `ArcFS.ActivateSystemVolume` composes them automatically,
the way a real boot sequence would: mount safely, and if a recoverable structural defect is found,
repair it and re-verify BEFORE ever surfacing `ReadOnlySafety` to whatever runs next. Read-only is
the honest fallback only when automatic repair genuinely cannot clear the defect -- never a silent
guess (RFC-0039 Section 5.3).

**Real QEMU proof** (`aps-arcfs-system-volume.abas`), all three real outcomes in one continuous
boot session: a healthy volume activates read-write directly (no repair attempted or needed); a
volume with the same orphan defect Phase F's own fixture builds self-heals during activation and
still comes up read-write -- confirmed by resolving the reattached object and reading back its
original content, proving the SAME object was reattached, not a fresh one silently fabricated in
its place; a volume with its superblock magic destroyed reports Unavailable rather than guessing.
Passed on the first real attempt; deterministic across three repeated runs. A negative control
(flipped the self-heal assertion to expect the volume to stay read-only) confirmed the fixture
correctly reports failure rather than silently passing. Full suite: 56/56 passing, no regressions.

This does not change the report's own bottom line below: three Phase G items (namespace
attachment, recovery environment support, graphical storage tooling) remain open for the same
reasons already given, and DISCOVER-level real hardware enumeration remains RFC-0038's own future
work. "System volume use" is the one item promoted from open to delivered.

## Scope delivered

RFC-0039's Phase G ("namespace attachment; system volume use; recovery environment support;
update snapshots; graphical storage tooling; ArcoBASIC bindings") is **partially** addressed --
three items genuinely implemented and QEMU-proven, one item recognized as already satisfied by
construction, and two items explicitly left open with a stated reason each, rather than silently
skipped or fabricated.

- **`ArcFS.RollbackToSnapshot(snapshotId)`** (RFC-0039 Section 51, "update snapshots"):
  implemented and proven. Republishes a previously snapshotted generation as the active
  checkpoint -- a real, atomic undo of a bad update.
- **`ArcFS.ActivateSystemVolume()`** (RFC-0039 Section 37/38, "system volume use," narrowed --
  see the Addendum above): implemented and proven. A real, self-healing boot-activation policy;
  the DISCOVER step of real hardware enumeration remains RFC-0038's own future work.
- **ArcoBASIC bindings**: recognized as already satisfied by construction (see below), not newly
  built.
- **Namespace attachment, recovery environment support, graphical storage tooling**: explicitly
  left open. Each depends on an Arcology subsystem that does not exist anywhere in this repository
  yet -- building one would be separate, real scope creep belonging to that subsystem's own RFC,
  not ArcFS's.

## Why this phase couldn't just implement its own list

RFC-0039 Section 78's own Phase G items are, overwhelmingly, integration surface with OTHER
Arcology subsystems -- not additional ArcFS format or protocol work. Three of the six items name
something that doesn't exist yet in this codebase to integrate with, and remain open. A fourth
(system volume use) named something that partly doesn't exist -- the split is what the Addendum
above delivers on the part that was genuinely ArcFS's own:

- **Namespace attachment** (Section 10's DISCOVER/CREATE/ATTACH/VERIFY/ACTIVATE lifecycle against
  an Arcology namespace *location*) has no concrete target. This repository has no implemented
  Arcology object/namespace facility outside ArcFS's own -- confirmed the same way RFC-0038's own
  implementation confirmed RFC-0017 (Substrate Resource Model) has no concrete freestanding
  callable surface anywhere in this repository (Phase A's own scope reduction #6 cites the
  identical finding, independently re-confirmed here).
- **System volume use**, split: the ATTACH/VERIFY/ACTIVATE part (Section 37/38) is exactly what
  `ArcFS.ActivateSystemVolume` delivers (Addendum above). The DISCOVER part -- a boot path that
  enumerates real storage hardware before anything else runs -- still has no target: no boot-time
  volume-selection mechanism exists anywhere in this project, and real UEFI Block IO Protocol
  enumeration is RFC-0038's own named future work, not something to improvise here.
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

- Full suite: 56/56 passing (54 pre-existing + 2 new test files, one from the original Phase G
  work and one from the system-volume Addendum). Re-ran every existing ArcFS test
  (`arcfs_phase_a/b/c/d/e/f_smoke`) and confirmed all six still pass unchanged.
- `ArcFS.RollbackToSnapshot` and `ArcFS.ActivateSystemVolume` both compile cleanly at X86_64
  codegen level.
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
  direct evidence the check is real. The system-volume Addendum's own fixture
  (`aps-arcfs-system-volume.abas`) has its own separate real-QEMU proof and negative control -- see
  the Addendum above.

## RFC-0039's overall status after Phases A-G

RFC-0039 remains `Status: Draft`, deliberately. Seven phases of implementation work are complete
and QEMU-proven, but the accumulated, honestly-documented scope reductions across all of them mean
this is not a production-ready filesystem:

- No real space reclamation (Phase E proves what's reclaimable; nothing reuses it).
- No sparse files, no on-disk reflink sharing (Phase D).
- No persistent typed attributes (Phase D).
- Only three health states, only one repair class, no scrub/repair history (Phase F).
- No namespace attachment, recovery environment, or graphical tooling (this phase). System volume
  use is now delivered for its ACTIVATE half; real hardware DISCOVERY remains RFC-0038's own
  future work (see Addendum above).

Every one of these is named, in its own phase's report, with the specific reason it was deferred
rather than attempted and gotten wrong. That is the intended reading of "Draft" here: a large,
coherent, real, and repeatedly QEMU-proven implementation of RFC-0039's core object/namespace/
commit/snapshot/recovery model, with its remaining gaps stated precisely enough that a future
increment knows exactly where to start.
