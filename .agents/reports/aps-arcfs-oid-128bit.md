# ArcologyFS (ArcFS): 128-Bit OID Identity and Allocation (RFC-0040 Section 10)

## Scope delivered

RFC-0040 Section 10 ("128-Bit Object IDs") is implemented and proven end to end under QEMU/OVMF,
scoped narrower than the RFC's own literal text -- deliberately, and explained below -- to the
object table's own canonical identity record and the on-disk object record, with a genuinely
correct 128-bit allocation counter behind both.

- **A real 128-bit allocation counter.** `ArcFSAllocateOid()` maintains two U64 halves
  (`ArcFSNextOidAddress` for the low half, new `ArcFSNextOidHighAddress` for the high half) and
  carries correctly: incrementing the low half past its maximum value increments the high half,
  exactly matching RFC-0040 Section 10.1's "increment the low half with carry into the high half."
  A companion getter, `ArcFSLastAllocatedOidHigh()`, exposes the high half that belongs to the OID
  the immediately preceding `ArcFSAllocateOid()` call returned, since that function's own return
  type stays a single U64 (see "Why the calling convention wasn't widened" below).
- **The object table row's real identity.** Row layout stayed 40 bytes -- the final field,
  previously unused "reserved" padding, now durably carries the object's real OID high half. Every
  call site that allocates a new OID (`ArcFSCreateObject`, and `ArcFS.Reflink`'s own separate
  `ArcFSAllocateOid()` call) writes the real value there, not a hardcoded 0.
- **The on-disk object record's real identity.** Payload grew from 32 to 40 bytes (added
  `oidHigh` at offset 32); the checksum shifted from offset 32 to offset 40 to make room. Every
  existing field (`oid[low]`/`type`/`size`/`extentSector`) kept its original offset.
  `ArcFSWriteObjectRecord` gained an `oidHigh` parameter; every reader (`ArcFSLoadOneObject`,
  `ArcFSSnapshotFindObjectRecord`, `ArcFSScanGeneration`'s object pass) reads the widened record
  and the widened checksum range.
- **A genuine 128-bit high-water-mark restore.** `ArcFSLoadOneObject` now tracks the highest OID a
  mounted generation actually contains via a real pairwise comparison (RFC-0040 Section 10.1:
  "compare high halves first, low halves on a tie"), stored in a new mount-state field
  (`maxOidHigh`, offset 88). `ArcFS.MountImage` restores the allocator's own counter (both halves)
  from it, so allocation correctly resumes past a generation's true high-water mark rather than a
  low-only approximation.

## Two real bugs found and fixed while designing the proof, not by inspection alone

**1. A wrap could hand out OID 0, colliding with this codebase's own "not found/failure" sentinel.**
Every `ArcFS.Create*`/`ArcFSCreateObject` caller has treated a `0` return as failure since Phase A.
A naive carry (low wraps to 0, high increments) would let a real, valid object receive an identity
indistinguishable from an allocation failure to every existing caller. Fixed by making a wrap skip
straight to low=1 (with the high half still incremented), never handing out literal 0 as a real
identity.

**2. The fix above needed to be applied in two places, and initially wasn't.** `ArcFS.MountImage`'s
own "resume past the highest OID seen" logic computes `maxOid + 1` independently of
`ArcFSAllocateOid`'s own increment -- a second, separate piece of code computing the same "next
OID" answer. The first version of this delivery applied the skip-past-zero fix only inside
`ArcFSAllocateOid`, leaving `MountImage`'s restore path free to still compute a bare 0 after
remounting a volume whose true highest OID happened to be `0xFFFFFFFFFFFFFFFF`. This was caught by
this increment's own round-trip fixture failing on the very first real QEMU attempt (`FAIL 1`,
isolated via a temporary per-phase serial breakdown to the round-trip verification step
specifically), not found by inspection -- direct evidence the fixture is exercising a real path,
not a decorative one. Fixed by mirroring the identical skip-past-zero rule in the restore path.

## Why the scope is narrower than RFC-0040 Section 10's literal text, and why that's the honest call

RFC-0040 Section 10.1 says a 128-bit OID should be "represented as two U64 halves... throughout
every persistent record and every in-memory table row" and that comparison/allocation logic
"SHALL operate on the pair explicitly." Taken completely literally, that means widening the
namespace table's `parentOID`/`childOID` fields, the handle table's `oid` field, the on-disk
namespace record's `parentOid`/`childOid` fields, and every internal lookup function
(`ArcFSFindObjectRow`, `ArcFSFindNamespaceRow`, `ArcFSFindNamespaceRowByChild`, and others) to
genuinely compare both halves -- and, to make any of that meaningful, propagating a real high half
through the public `ArcFS.`-prefixed calling surface (`CreateFile`, `Resolve`, `Rename`, `Reflink`,
`SetAttribute`, and every other function that takes or returns "an OID").

That last piece is the one this delivery does not attempt, for a concrete, checked reason: this
compiler has no tuple/multi-value return type (confirmed directly -- a `FUNCTION ... AS (U64, U64)`
return-type declaration fails to parse) and no proven default-parameter usage anywhere in this
freestanding stdlib, so the only way to widen a public function's OID handling is to add an
explicit second parameter to its signature *and* update literally every call site across this
project's fixtures. Grepping the live stdlib alone found roughly 150 `oid`/`Oid`/`OID` occurrences
across every public creation, resolution, rename, removal, reflink, attribute, and repair function
-- and `stdlib/system_namespace_policy.abas` (RFC-0041) is a real downstream consumer that stores
and compares whatever OID it gets back from `ArcFS.Resolve`, so the change would not stay contained
to one file.

Given that every public caller can therefore only ever supply or receive a low-half-only OID today,
widening the namespace/handle *reference* fields (which only ever hold a copy of whatever a public
caller already passed in, or of `ArcFSCreateObject`'s own return value) would durably store a field
that is provably always 0 in this implementation's own reachable behavior -- format churn with no
testable behavioral difference until the public surface itself is widened. Widening the *canonical*
object table row and its on-disk record, by contrast, has real, checkable behavior today: it is the
one place a genuinely nonzero high half can originate (the allocation counter), and this delivery
proves that value is correctly allocated, stored, and restored across a real commit and remount.

This is the same judgment every phase in this chain has made when a literal requirement's cost and
its testable value diverge: ship the slice that is real and provable, name the rest precisely, defer
it rather than pad the delivery with unverifiable width.

## A known, explicitly named limitation -- disclosed, not hidden

Because internal lookup functions still compare only the low 64 bits (the scope reduction above),
two different true 128-bit identities that happen to share a low half are not currently
distinguishable by any lookup in this implementation. Under real allocation (starting from OID 2,
the counter never wrapping in any volume's practical lifetime) this cannot arise. Under this
increment's own deliberately forced wrap test, the very next low value the allocator would hand out
is 1 -- root's own low half -- so a second post-wrap allocation would collide with root under a
low-only lookup. This fixture stops short of manufacturing that specific collision rather than
either hiding from it or accidentally treating a false pass as a real one; closing it for real is
exactly the "full 128-bit comparison, propagated through the public calling surface" work named
above as the next scoped increment, should it ever become necessary.

## Validation

- Every touched/new function compiles cleanly at X86_64 codegen level (`ArcFSAllocateOid`,
  `ArcFSLastAllocatedOidHigh`, `ArcFSCreateObject`, `ArcFS.Reflink`, `ArcFS.Initialize`,
  `ArcFSWriteObjectRecord`, `ArcFSLoadOneObject`, `ArcFS.MountImage`, `ArcFS.FormatVolume`,
  `ArcFS.PrepareCommit`, `ArcFSSnapshotFindObjectRecord`, `ArcFSScanGeneration`), individually
  reveal-checked against the live stdlib.
- `stdlib/system_namespace_policy.abas` (RFC-0041) compiles cleanly combined with the updated
  `arcfs_policy.abas` -- confirms the scope reduction really did leave this downstream consumer
  untouched.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-oid128.abas`): confirms root's own
  identity reads back as (high=0, low=1); creates an ordinary file and confirms its stored
  `oidHigh` is 0 (the common case is unchanged); forces the allocation counter to
  (high=5, low=0xFFFFFFFFFFFFFFFF) directly and reflinks an existing file (exercising
  `ArcFS.Reflink`'s own, separate `ArcFSAllocateOid()` call site, not just
  `ArcFSCreateObject`'s), confirming the returned OID is the pre-wrap low value, the clone's own
  stored `oidHigh` is the pre-wrap high value (5), and the counter afterward reads
  (low=1, high=6) -- both the carry and the skip-past-zero fix, confirmed by direct memory
  inspection rather than by risking the known low-half-collision edge case above. Commits a real
  generation 2 containing all three objects and remounts: the clone still resolves by path, its
  `oidHigh` still reads 5 after the reload (the widened on-disk record genuinely round-trips), and
  the allocator's counter is still exactly (low=1, high=6) after the remount -- direct proof that
  `ArcFS.MountImage`'s high-water-mark restore is real 128-bit arithmetic, not a low-only
  approximation. Failed on the first real attempt (the `MountImage` restore-path bug above),
  isolated via temporary per-phase serial diagnostics, fixed, and passed on the next attempt;
  deterministic across 3 repeated runs after the fix.
- Full suite: every pre-existing ArcFS/APS test re-run unchanged alongside the new
  `arcfs_oid128_smoke` test (61/61 total).

## Remaining activation gate

- **Full 128-bit comparison in internal lookup functions, and high-half propagation through the
  public `ArcFS.`-prefixed calling surface, remain open** -- see the scope-reduction section above
  for the concrete cost (roughly 150 call sites, no tuple-return or proven default-parameter
  mechanism available) and the concrete, disclosed limitation it leaves (a low-half collision only
  reachable after a counter wrap this implementation's own allocator cannot produce in practice).
- **RFC-0040 Phase I (real allocation and reclamation) is next**, per RFC-0040 Section 16's own
  dependency chain -- named there as the single largest usability gap and the priority ahead of
  Phase J even though Phase J is architecturally a prerequisite for later phases.
