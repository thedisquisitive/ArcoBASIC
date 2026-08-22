# ArcologyFS (ArcFS) Phases N and O: Feature Negotiation, Self-Describing Checkpoint, and a Real Backward-Compatibility Proof

## Scope delivered

RFC-0042 Phases N and O, delivered together since Phase O's own fixture needed Phase N's own
self-describing mechanism to exist before there was anything real to prove backward-compatible:
real feature negotiation for the superblock's own long-unused flags field (Section 7), a
self-describing checkpoint record that survives future growth without breaking readers built before
that growth happened (Section 8), and a genuine backward-compatibility proof against an actually
older on-disk checkpoint shape (Section 9).

### Section 7: Feature Negotiation

The superblock's own feature-flags field (offset 48, inside the checksummed region) has held a
literal `0` and the comment `feature flags: none defined yet` since Phase H -- nothing ever read it.
It is now split into three 16-bit sub-fields (compat: bits 0-15; incompat: bits 16-31; ro-compat:
bits 32-47) plus 16 bits of genuine forward-compatible headroom (48-63, ignored unconditionally, by
construction -- nothing in this build even reads them). `ArcFS.MountImage` now:

- Refuses to mount outright (`RETURN 0`) if any unrecognized incompat bit is set.
- Mounts successfully but arms `ArcFSReadOnlySafetyAddress` (RFC-0039 Phase F's own existing
  repair-gate flag, reused rather than duplicated) if an unrecognized ro-compat bit is set with no
  unrecognized incompat bit.
- Mounts normally otherwise -- including the all-zero-flags case, every volume this build has ever
  formatted.

No feature bit is actually defined by this RFC itself (`ArcFSRecognizedIncompatMask`/
`ArcFSRecognizedRoCompatMask` both return `0`) -- the mechanism is the deliverable; a future RFC
that needs a real optional, skippable feature now has somewhere real to declare it.

### Section 8: Self-Describing Checkpoint Record

The checkpoint record's payload has grown five times since Phase H (56->104 bytes), each time by
moving the checksum offset and updating every hardcoded constant that reads it -- a real, untested
breaking change bundled silently into "FMV2" every time. `ArcFSVerifyCheckpointChecksum` replaces
every one of the four call sites that used to inline this check (`ArcFSRingSlotGeneration`,
`ArcFSSelectRingCheckpoint`, `ArcFSReadCheckpoint`, `ArcFSPeekCheckpoint`) with one shared,
self-describing disambiguation:

1. Try the LEGACY interpretation first: checksum at the fixed offset 104, covering bytes 0-103,
   exactly as every build before this RFC always has. If it validates, this is a pre-RFC-0042
   record -- proceed exactly as before, `recordLength` implicitly 104.
2. Only if that fails, try the SELF-DESCRIBING interpretation: read a candidate `recordLength` from
   offset 104 (rejecting it outright if implausible -- below 112 or leaving no room for its own
   checksum in one 512-byte sector), then verify the checksum at that offset. If it validates, this
   is a record written by this build or later.
3. If neither validates, genuine corruption -- fail closed exactly as before.

**A design flaw caught and corrected during drafting, before any code was written**: the RFC's
first draft put `recordLength` at offset 0, ahead of `generation`. This was rejected once traced
through -- it would have made every already-existing FMV2 checkpoint unreadable outright, exactly
the breaking, untested growth this RFC exists to stop making. Appending `recordLength` instead,
disambiguated by checksum validity rather than a version flag, costs nothing for an already-existing
record and needs this one transitional trick exactly once; every future field addition after this
one is unambiguously self-described by `recordLength` alone.

`ArcFSWriteCheckpointRecord` now always writes the self-describing shape (`recordLength=112`,
checksum immediately after it) -- every checkpoint this build writes going forward is
self-describing from the start; only reading needs to handle both shapes.

## Two real bugs found while building this, both fixed before shipping

1. **`ArcFSReadCheckpoint`'s own `attributeTreeCount` sanity bound checked against the wrong
   constant** (`ArcFSMaxObjects()` instead of `ArcFSMaxAttributes()`) -- found while touching this
   exact function's own read path, unrelated to Phase N's own mechanism but caught by the same
   scrutiny. Fixed and proven separately; see `.agents/reports/aps-arcfs-attribute-bound-fix.md`.
2. **`ArcFS.MountImage` never cleared `ArcFSReadOnlySafetyAddress` itself.** Only
   `ArcFS.MountImageSafe` cleared it defensively before calling plain `MountImage` -- meaning a
   ro-compat-triggered read-only condition from one mount stayed permanently armed across every
   LATER plain `ArcFS.MountImage()` call, even after the condition that caused it was gone (flags
   restored to 0). Found by this Phase's own fixture on its first real attempt (Step 5's own
   "resume read-write" check failed with the flag still stuck at 1, and the subsequent
   `ArcFS.CommitImage()` was genuinely refused as a direct consequence). Fixed by having
   `ArcFS.MountImage` itself clear the flag unconditionally at the start of every mount attempt,
   before evaluating whether THIS mount's own conditions call for re-arming it -- confirmed safe
   against every other caller (`ArcFS.RepairCommit` never calls `MountImage` at all;
   `ArcFS.MountImageSafe`/`ArcFS.ActivateSystemVolume` both already re-derive the flag fresh on
   every call, so the fix is a strict correctness improvement, not a behavior change for them).

## A real, previously-undocumented reserved word found

`flags` is a reserved word in this ArcoBASIC dialect (confirmed directly -- the same class of
gotcha this project has already hit for `next`). `ArcFSCheckFeatureFlags`'s own parameter is named
`rawFlags` instead.

## Validation

- All 48 public `ArcFS.`-prefixed entry points, plus every new/touched internal function, compile
  cleanly at X86_64 codegen level.
- **Section 8's real proof** (`aps-arcfs-checkpoint-self-description.abas`): a real committed
  checkpoint (self-describing, `recordLength=112`) has its own on-disk SHAPE downgraded in place to
  a byte-for-byte replica of the pre-RFC-0042 layout and re-mounts correctly via the LEGACY
  interpretation, with real file content and a real attribute intact. A fresh commit afterward
  writes a real self-describing record again and mounts via the SELF-DESCRIBING interpretation --
  proving a volume moves between both shapes in place, mid-lifetime, with no reformat anywhere in
  the fixture's own history. A deliberately implausible `recordLength` written directly onto a real
  checkpoint sector makes the mount fail closed. See that report's own scope note on why the
  "legacy writer" runs in the same compiled program as the current reader rather than a genuinely
  separate historical binary -- a deliberate, reasoned substitution (Section 8.2's disambiguation is
  a pure function of on-disk bytes, sharing no in-memory state with whatever wrote them), not a
  shortcut.
- **Section 7's real proof** (`aps-arcfs-feature-negotiation.abas`): an unrecognized incompat bit
  (bit 16), written directly onto a real superblock ring slot with a correctly recomputed checksum,
  genuinely refuses the mount; restoring flags to 0 mounts the SAME volume normally again. An
  unrecognized ro-compat bit (bit 32) mounts successfully but read-only, with a real
  `ArcFS.CreateFile`+`ArcFS.CommitImage` genuinely refused by the existing `ArcFS.PrepareCommit`
  gate; restoring flags to 0 resumes real read-write commits. A bit set only in the reserved-for-
  future range (bit 48) mounts normally, full read-write, proving those bits are genuinely ignored,
  not merely unrejected by accident.
- Both fixtures passed after fixing the `ArcFS.MountImage` ReadOnlySafety-clearing bug above (Phase
  N's own fixture caught it directly); negative controls (flipping a core assertion in each)
  confirmed real `FAIL 1`/`FAIL 4`; deterministic across 3 repeated runs each.
- Two new smoke tests, `systems_arco_basic_arcfs_checkpoint_self_description_smoke` and
  `systems_arco_basic_arcfs_feature_negotiation_smoke`, registered in
  `arcology-os/cmake/Testing.cmake`, both pass through `ctest`.
- Full regression suite re-run clean.

## Documented scope reductions

1. **No feature bit is actually defined by this phase.** The mechanism itself is the deliverable;
   both recognized-bit masks return `0`, matching RFC-0042 Section 7.3's own "never live inside this
   RFC's own new fields" boundary.
2. **`recordLength`-style self-description covers only the checkpoint record.** The superblock and
   bitmap records are unchanged -- RFC-0042 Section 20's own Open Question 1 leaves generalizing the
   pattern to either of them until one of them actually needs its first field addition.
3. **The backward-compatibility proof substitutes a same-process byte-for-byte replica writer for a
   genuinely separate historical binary** (RFC-0042 Section 9.1's own scope note) -- sound for this
   specific byte-level property, but a real dump-guest-RAM-to-host-file harness mechanism (the
   missing half of what `run-uefi-hello-with-preload.sh` already proves in the preload direction)
   would make a stronger, general-purpose version of this kind of proof possible for future
   format-shape work, and remains a named, real future enhancement (RFC-0042 Section 20, Open
   Question 4), not required by anything this phase itself needed.

## Remaining activation gate

RFC-0042's other phases are unaffected and unchanged: Phase P (reserved growth capacity in the
Object/Namespace/Attribute rows); Phase Q (production-scale capacities); Phase R (UTF-8/blob
attribute values). With this increment, the format has a real, proven answer to "can a future field
be added without reformatting" for the one record that has actually needed to grow five times
already, a real, proven mechanism for declaring an optional feature that older code can safely
refuse or degrade for, and a real, executed proof that an actually-older on-disk shape still mounts
under current code -- RFC-0042's own central motivating gap (Section 2.1/2.2) is closed.
