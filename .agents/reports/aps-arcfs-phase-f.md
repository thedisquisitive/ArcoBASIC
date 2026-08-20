# ArcologyFS (ArcFS) Phase F: Recovery and Health (RFC-0039)

## Scope delivered

RFC-0039's Phase F ("scrub; offline checker; repair planning; ReadOnlySafety activation; inspector
health interface") is implemented and proven end to end under QEMU/OVMF.

- **`ArcFSScanGeneration(checkpointSector)`**: the one real structural-validation pass everything
  else in this phase builds on. Given a checkpoint sector, it re-verifies every object and
  namespace record's checksum, performs a genuine graph reachability analysis from root through the
  namespace tree (not merely "does some record name me as a child" -- a full transitive-from-root
  check via relaxation), and detects overlapping file extents.
- **`ArcFS.ScrubVolume()` / `ArcFS.GetHealthState()`** (RFC-0039 Sections 20.4 / 36): online,
  read-only integrity scrub and a minimal health-state query, both built on the scan above.
- **`ArcFS.MountImageSafe()`** (RFC-0039 Sections 37/38): activation with real ReadOnlySafety --
  mounting a generation with a structural defect succeeds (the committed checkpoint is trustworthy
  and fully readable) but sets a flag that **`ArcFS.PrepareCommit`** now checks and refuses on.
- **`ArcFS.RepairReattachOrphans()` / `ArcFS.RepairCommit()`** (RFC-0039 Section 39): a real
  Inspect/Plan/Apply/Verify repair for exactly one defect class, matching RFC-0039's own stated
  policy for it.

## The repair case chosen, and why

RFC-0039 Section 39 names several defect classes an offline checker should detect (structural
validation, OID reference validation, namespace reachability, extent overlap, allocation-tree
reconciliation, checksum verification, orphan detection, snapshot reachability, authority-record
validation) but only names ONE concrete resolution policy explicitly: "When data cannot be
reattached to its original namespace, recovery SHOULD preserve it in a clearly identified recovery
namespace/object collection with provenance metadata."

That sentence is directly implementable without inventing policy RFC-0039 doesn't specify, so it's
the one repair this phase builds: `ArcFS.RepairReattachOrphans()` re-parents every orphaned object
into a root-level `:lost+found` directory under a synthetic `orphan-<oid>` name. The OID itself
*is* the provenance record -- it's exactly the identity the object had before it lost its namespace
entry, and it round-trips: the fixture proves the reattached object still reads back its original
content byte-for-byte, confirming the SAME object was reattached, not a fresh copy fabricated in
its place.

Extent-overlap defects are *detected* by `ArcFSScanGeneration` but have no repair in this phase --
fixing one would require choosing which of two overlapping objects is authoritative, a policy
decision RFC-0039 does not make and Section 5.3 explicitly forbids inventing silently ("MUST NOT
guess silently").

## ReadOnlySafety with real teeth

The easy way to "implement" Section 38 would be a function that *reports* a ReadOnlySafety state.
That's not what got built. `ArcFS.MountImageSafe()` sets a flag, and `ArcFS.PrepareCommit()` -- the
single choke point every mutating commit already passes through -- checks it first and refuses
outright. The fixture proves this isn't cosmetic: it creates a file while the flag is armed (an
ordinary in-memory mutation, never guarded) and then calls `ArcFS.CommitImage()`, confirming the
commit itself is refused, not merely that some status value says "degraded."

`ArcFS.RepairCommit()` is the one sanctioned way through that guard, and it doesn't trust its own
repair blindly: it temporarily clears the flag, commits, then re-runs `ArcFSScanGeneration` against
the newly active generation and re-arms the flag if a defect still remains. This *is* RFC-0039
Section 39's Apply-then-Verify staging, concretely -- Verify is re-run for real, not assumed.

## Documented Phase F scope reductions

1. **`ArcFS.GetHealthState()` collapses RFC-0039 Section 36's full state list** (Healthy, Degraded,
   ReadOnlySafety, NeedsScrub, NeedsOfflineCheck, Corrupt, Unavailable) down to three: Healthy (0),
   Degraded (1, defects found but the volume is readable), Corrupt (2, the superblock itself is
   unreadable or invalid). The finer distinctions are real per RFC-0039 but aren't independently
   observable from anything this implementation currently tracks beyond "defects found" -- see
   reduction #3.
2. **Repair covers exactly one defect class** (orphaned objects); extent-overlap defects are
   detected but not repaired, for the policy reason given above.
3. **No scrub/repair history is persisted** (Section 36's own "scrub history... repair history"
   health-detail fields). Every function computes its answer fresh, live, each call.
4. **`ArcFSScanGeneration` is O(objectCount² + objectCount × namespaceCount)**, matching Phase E's
   own documented performance scope reduction -- fine at this milestone's tiny test scale.
5. **Extent-overlap detection's logic is exercised by direct review**, and the fixture's orphan
   scenario exercises every OTHER pass in `ArcFSScanGeneration` end to end, but no fixture in this
   phase constructs a deliberately overlapping-extent image to exercise that one specific defect
   class under QEMU -- stated plainly rather than silently left untested.

## Validation

- Full suite: 54/54 passing (53 pre-existing + 1 new test file). Re-ran every existing ArcFS test
  (`arcfs_phase_a/b/c/d/e_smoke`) and confirmed all five still pass unchanged. `ArcFS.PrepareCommit`
  gained one new guard clause at its very top; every prior fixture's own commits happen with the
  ReadOnlySafety flag never set (nothing in Phases A-E ever calls `ArcFS.MountImageSafe`), so the
  guard is a no-op for all of them, confirmed by the regression run.
- Every new public entry point compiles cleanly at X86_64 codegen level; forward references from
  `ArcFS.PrepareCommit` (defined in the Phase C section) to `ArcFSReadOnlySafetyAddress` (defined
  later, in this phase's own section) compile without needing to reorder anything -- this
  compiler's whole-module `reveal` handles forward references to later-defined functions natively.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-f.abas`): formats a volume,
  builds `:home`/`:home:documents`/`:home:documents:notes.txt` (60 bytes), commits generation 2 --
  `ArcFS.ScrubVolume()` returns 0 and `ArcFS.GetHealthState()` returns Healthy. Deletes
  `notes.txt`'s namespace entry only (Phase A's own `ArcFS.Remove`, leaving the object row alive --
  a real orphan) and commits generation 3: `ScrubVolume` now returns a positive defect count and
  `GetHealthState` returns Degraded. `ArcFS.MountImageSafe()` reports read-only (2); a file created
  in memory during this state cannot be committed (`ArcFS.CommitImage()` returns `FALSE`) -- the
  guard genuinely blocks the write. `ArcFS.RepairReattachOrphans()` reattaches exactly 1 object;
  `ArcFS.RepairCommit()` succeeds (its own re-scan came back clean); `ScrubVolume`/`GetHealthState`
  return to Healthy/0. A fresh `ArcFS.MountImageSafe()` now reports read-write (1), and an ordinary
  `ArcFS.CommitImage()` -- refused two functions earlier -- succeeds. Finally, the reattached object
  resolves at `:lost+found:orphan-<notesOid>` and reads back its original 60 bytes exactly. Passed
  on the first real attempt; deterministic across three repeated runs.
- **Negative control on the test harness itself**: flipped the ReadOnlySafety-blocks-commit
  assertion to expect the commit to succeed instead of being refused, and confirmed the fixture
  correctly reports `FAIL 1` over serial rather than silently passing -- direct evidence the guard
  is actually being exercised, not just assumed.

## Remaining activation gate

- **Phase G is still ahead** (system integration). This report covers Phase F only; RFC-0039's own
  `Status` remains `Draft`.
- **No extent-overlap repair, no allocation-tree reconciliation repair, no authority-record
  repair** -- Section 39 names several defect classes this phase detects only a subset of (orphans
  and overlaps) and repairs an even narrower subset (orphans only).
- **No offline mode distinct from online.** RFC-0039 distinguishes an offline checker (run in a
  recovery environment, possibly without full activation) from an online scrub (Section 20.4). This
  implementation has one code path (`ArcFSScanGeneration`) serving both roles -- a real distinction
  would matter once an actual recovery-environment boot mode exists elsewhere in this project,
  which it does not yet (see Phase G's own report for why).
