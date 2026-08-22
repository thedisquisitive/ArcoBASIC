# RFC-0042: ArcologyFS (ArcFS) Format Extensibility and Production Scale

**RFC Number:** RFC-0042\
**Title:** ArcologyFS (ArcFS) Format Extensibility and Production Scale\
**Status:** Draft (Phases N and O implemented and QEMU-proven, delivered together -- Section 7's real feature negotiation for the superblock's own long-unused flags field, Section 8's self-describing checkpoint record (an append-only `recordLength` field disambiguated from every pre-RFC-0042 checkpoint by checksum validity, not a version flag -- moving it to offset 0 was considered and rejected during drafting since it would have broken every existing FMV2 checkpoint), and Section 9's real backward-compatibility proof against an actually-downgraded on-disk checkpoint shape. Two real bugs found and fixed along the way: `ArcFSReadCheckpoint`'s own `attributeTreeCount` bound checked the wrong constant (see `.agents/reports/aps-arcfs-attribute-bound-fix.md`), and `ArcFS.MountImage` never cleared `ArcFSReadOnlySafetyAddress` itself, leaving a ro-compat-triggered read-only condition stuck across later, genuinely clean remounts -- see `.agents/reports/aps-arcfs-phase-n-o.md`. Phases P (reserved row capacity), Q (production-scale capacities), and R (UTF-8/blob attribute values) remain undelivered.)\
**Category:** Storage / Filesystem Architecture

**Authors:** Arcology Project\
**Created:** 2026-08-21\
**Last Updated:** 2026-08-21\
**Supersedes:** None\
**Superseded By:** None

**Related Architecture:** Arcology Object Architecture; Polymorphic Substrate (APS)\
**Related RFCs:** RFC-0000, RFC-0038 (APS Block Storage and Filesystem Provider Substrate), RFC-0039 (ArcologyFS / ArcFS), RFC-0040 (ArcologyFS Production Hardening -- this RFC amends and completes it), RFC-0041 (Arcology System Namespace)

------------------------------------------------------------------------

# 1. Executive Summary

RFC-0040 delivered every named phase (H-M) plus several scoped follow-on increments: real
allocation/reclamation, growable B+tree metadata, 128-bit OIDs, variable-length/sparse files with
on-disk reflink sharing, a genuine multi-attribute-per-object model, and a full 7-state health
model with expanded repair. Every one of those increments shares two properties that this RFC
exists to close.

**First: the format has no real extensibility mechanism.** Every additive change across RFC-0040's
own history was delivered by growing a fixed-size record in place -- moving the checkpoint record's
checksum offset five separate times as fields were added (56->64->72->88->104 bytes) -- and treating
the result as still "FMV2," without a version bump, without any reader-side ability to detect or
tolerate an unfamiliar record shape, and without ever once testing whether an FMV2 image written by
an *older* build of this same major version still mounts under a *newer* one. The superblock already
reserves an 8-byte feature-flags field (`ArcFSWriteSuperblockRecord`, offset 48) -- it has held the
literal value `0` and the comment `feature flags: none defined yet` since Phase H. Nothing reads it.
This RFC gives that field, and the checkpoint record it protects, a real mechanism: a self-describing
record length, a defined compat/incompat/ro-compat bit split, and a mount-time contract that refuses
cleanly on an unrecognized incompat feature instead of silently misreading bytes -- then proves the
promise for real, by mounting an *actually older* frozen image with the *current* code, something no
fixture in this project's history has ever done.

**Second: every capacity in ArcFS is fixed at a scale chosen to be provable under QEMU in a few
seconds, not to hold real data** -- 64 objects, 128 namespace rows, 128 attribute rows, a ~254 KB
volume ceiling, a 16 KB file ceiling, 8 open handles. This RFC raises every one of those to a scale
that is still fixed and still fully QEMU-provable, but is no longer a toy -- and does so using the
extensibility mechanism this RFC itself builds, so raising them again later never again requires a
full reformat.

A smaller, previously-named gap is also closed here: Section 12.2's UTF-8 string and binary blob
attribute values, the one Attribute Tree value type this project has deferred since Phase L.

Multi-writer concurrency is explicitly out of scope (Section 4) -- it needs a real transaction/
isolation model, not a bounded increment, and nothing in this RFC depends on it.

------------------------------------------------------------------------

# 2. Motivation

## 2.1 Every format growth so far has been a silent breaking change

`ArcFSWriteCheckpointRecord`'s payload has grown from 56 bytes (Phase H) to 104 bytes (this
session's own multi-attribute increment) across five separate increments, each one moving the
checksum offset and updating every hardcoded offset constant that reads the record
(`ArcFSReadCheckpoint`, `ArcFSPeekCheckpoint`, `ArcFSSelectRingCheckpoint`, and every repair/scrub
pass that touches it). Every one of those increments happened *within* Format Major Version 2 --
none bumped the magic (`ARCFSB02`), none changed the feature-flags field, and none was ever tested
against a real FMV2 image produced by a build of the compiler from *before* that increment shipped.
If such an image existed today, mounting it with the current code would almost certainly fail the
checksum check and refuse to mount -- which is *safe* (fails closed, per RFC-0039 Section 5.3's own
"MUST NOT guess silently"), but it is not what "FMV2" is supposed to mean, and it means every prior
RFC-0040 increment has quietly redefined its own format version without saying so.

## 2.2 The one reserved field that exists has never been used

`ArcFSWriteSuperblockRecord` already writes an 8-byte feature-flags field at offset 48, inside the
64-byte checksummed region, and has since Phase H. It has always been written as `0`. No function
anywhere in `arcology-os/stdlib/arcfs_policy.abas` reads it. The plumbing for exactly the mechanism
this RFC needs already exists in the on-disk bytes; it has simply never been given semantics.

## 2.3 Every fixed-width row layout has already spent its own spare capacity

The Object Table row (40 bytes: `oidLow`, `type`, `size`, `dataSlot`, `oidHigh`) had exactly one
spare field -- and RFC-0040 Section 10 already spent it, repurposing what was "previously unused
reserved padding" to carry the OID high half. There is nowhere left in that row for a future
per-object field (a generation stamp, a permission bit, a link count) without another width-
widening change to a row every single ArcFS function that touches an object already assumes a fixed
stride for. The Namespace row (64 bytes) and the current Attribute row (56 bytes, this session's own
addition) have the same property: no spare bytes, by construction. The one accidental exception is
the Extent Tree's own leaf entry (32 bytes: `dataSlot`, `chunkIndex`, `extentSector`, `reserved`) --
its fourth field has sat genuinely unused since the increment that created it, purely because nothing
has needed it yet, not because anyone planned ahead.

## 2.4 Every capacity ceiling was chosen for test speed, not for use

`ArcFSMaxObjects()` (64), `ArcFSMaxNamespaceRows()` (128), `ArcFSMaxAttributes()` (128),
`ArcFSMaxTrackedSectors()` (508, a ~254 KB volume ceiling), `ArcFSMaxFileChunks()` (4, a 16 KB file
ceiling), `ArcFSMaxHandles()` (8) -- every one of these is named in its own defining comment as
chosen so "a handful of real objects... provably forces a real multi-node, multi-level tree under
QEMU," not as a considered answer to "how much data does ArcFS need to hold." This has been the
right call at every point up to now (RFC-0040 Section 9's own "small number you can actually exercise
under QEMU" discipline), but it means ArcFS today cannot represent more than a few dozen small files
on a volume smaller than a floppy disk, regardless of how sound its allocator, trees, or health model
are.

------------------------------------------------------------------------

# 3. Goals

1. Give the superblock's existing feature-flags field real compat/incompat/ro-compat semantics, and
   make `ArcFS.MountImage`/`ArcFS.MountImageSafe` refuse cleanly on an unrecognized incompat bit.
2. Make the checkpoint record self-describing (a real, fixed-offset length/version field) so a
   future field addition is read-side additive, not a hardcoded-offset breaking change.
3. Prove the above for real: mount a genuinely older, frozen checkpoint-record shape with the
   current code, and confirm the result is either correct or an honest, clearly-diagnosed refusal --
   not a silent misread. No fixture in this project has ever tested this property; this RFC adds the
   first one.
4. Reserve real spare capacity in the Object, Namespace, and Attribute row layouts for at least one
   future per-row field each, wired end-to-end (cleared at format time, round-tripped through
   commit/mount, covered by checksums) even though nothing populates it yet.
5. Raise every fixed capacity ceiling to a scale suitable for genuine use, not just test speed,
   while keeping every one of them provable under QEMU in a bounded, reasonable time.
6. Close Section 12.2's remaining attribute value types: UTF-8 strings and binary blobs.
7. Do all of the above without a Format Major Version bump and without requiring any existing
   FMV2 volume to be reformatted -- if reformatting turns out to be unavoidable for any single
   piece of this RFC, that piece MUST say so explicitly and be justified on its own.

------------------------------------------------------------------------

# 4. Non-Goals

1. **Multi-writer concurrency or any locking/isolation model.** ArcFS has assumed a single writer
   session since RFC-0039 Phase A ("no transactions... no multi-step staging area to make atomic")
   and still does. Real concurrent access needs a foundational transaction/isolation design of its
   own -- comparable in size to RFC-0039 itself -- not a bounded increment inside this RFC. Nothing
   in this RFC's own requirements depends on it, and nothing here forecloses it later.
2. **A fully dynamic, unbounded-capacity allocator for the in-memory tables themselves** (the Object
   Table, Namespace Table, Attribute Table, Allocation Bitmap, and every tree-build scratch region
   are all fixed MMIO regions at fixed virtual addresses today). This RFC raises those fixed
   capacities to a genuinely useful scale (Section 10) but does not replace the "fixed region at a
   fixed address" architecture itself with real paging or heap allocation -- that is a substrate-
   level capability (RFC-0017's own still-nonexistent Substrate Resource Model, or a future
   AllocatePages-backed redesign) this RFC does not depend on and does not attempt.
3. **Live migration of an existing FMV1 volume.** Unchanged from RFC-0040 Section 15.2's own
   position: FMV1 volumes are read by nothing this project builds going forward; they are historical
   record only, proven exclusively by the frozen fixtures that predate FMV2.
4. **Revisiting the extent-overlap repair's lower-OID-not-generation-number deviation** (RFC-0040
   Section 14.1). Reasoned through directly while scoping this RFC: implementing the RFC's own
   literal "lower generation number is authoritative" policy needs a per-OBJECT generation stamp,
   which is exactly the kind of future per-row field Section 8 of this RFC makes room for -- but
   actually populating and consuming it is left as a genuine future increment once Section 8 ships,
   not bundled in here. The current lower-OID tie-break remains a deliberate, documented, and
   equally arbitrary-but-deterministic choice; neither policy is more *correct* in any way a test
   can observe.
5. **Changing which value types the Attribute Tree supports beyond UTF-8 strings and binary blobs**
   (Goal 6). RFC-0039 Section 24's remaining base value type, "typed locator," stays out of scope,
   matching every prior phase's own reasoning that it names a concept (a reference to another object
   or a location within one) with no concrete consumer anywhere in this codebase yet.

------------------------------------------------------------------------

# 5. Terminology

- **Compat feature bit**: a feature flag an OLDER reader MAY safely ignore -- the volume remains
  fully readable and writable without understanding it.
- **Incompat feature bit**: a feature flag an OLDER reader MUST refuse to mount for, read-write or
  read-only, because ignoring it risks silent data corruption.
- **Ro-compat feature bit**: a feature flag an OLDER reader MAY mount read-only but MUST NOT mount
  read-write, because writing without understanding it risks corrupting state the feature depends on.
- **Self-describing record**: an on-disk record that carries its own real length/version as one of
  its first, permanently-fixed-offset fields, so a reader can determine how much of the record is
  populated without a compile-time constant baked into the reading code.
- **Reserved field**: a named, currently-unpopulated field inside a fixed-width row, cleared at
  format time and covered by that row's own checksum, whose future use is intentionally deferred.

------------------------------------------------------------------------

# 6. Relationship to RFC-0039 and RFC-0040

This RFC amends RFC-0040 exactly as RFC-0040 amended RFC-0039: it is not a new subsystem, it is the
next named set of gaps in the same implementation, continuing RFC-0040's own phase lettering (N
onward, following Phases H-M). It assumes every RFC-0040 requirement is already implemented and
QEMU-proven (true as of this RFC's own drafting -- see RFC-0040's own Status line for the complete,
dated list) and depends on none of RFC-0040's own explicitly-out-of-scope items (multi-writer
concurrency, live FMV1 migration).

------------------------------------------------------------------------

# 7. Requirement: Feature Negotiation

## 7.1 Bit layout

The superblock's existing 64-bit feature-flags field (offset 48, inside the checksummed region)
SHALL be split into three 16-bit sub-fields plus 16 bits of headroom: compat bits (0-15), incompat
bits (16-31), ro-compat bits (32-47), reserved-for-future-splitting (48-63, MUST be written as 0 and
MUST be ignored, not rejected, by a reader that finds them nonzero -- a genuinely forward-compatible
reservation, unlike everything else this RFC replaces).

## 7.2 Mount-time contract

`ArcFS.MountImage`/`ArcFS.MountImageSafe` SHALL read the feature-flags field once the superblock's
own checksum has already validated it, and:

- MUST refuse to mount (return 0/FALSE, the same fail-closed convention every other structural check
  in this file already uses) if any incompat bit is set that this build does not recognize.
- MUST mount read-only (matching the existing `ArcFSReadOnlySafetyAddress` mechanism RFC-0039 Phase F
  already established) if any ro-compat bit is set that this build does not recognize, but no
  unrecognized incompat bit is set.
- MUST mount normally, ignoring any unrecognized compat bit.

No feature bit is defined by this RFC itself -- Section 8's checkpoint self-description is
unconditional format behavior, not an optional feature, and does not need a bit of its own. This
section exists so the *next* RFC that adds an optional, skippable feature has a real mechanism to
declare it in, rather than repeating RFC-0040's own history of silent, undeclared format growth.

## 7.3 Never live inside this RFC's own new fields

The feature-flags mechanism MUST NOT be used to gate anything this RFC itself delivers (Sections
8-11) -- every one of those is unconditional new baseline behavior for any FMV2 volume mounted by
this build, exactly like RFC-0040's own checkpoint field growth was. Feature bits are for *future*
RFCs to use; retrofitting them onto this RFC's own requirements would just be Section 2.1's problem
one layer deeper.

------------------------------------------------------------------------

# 8. Requirement: Self-Describing Checkpoint Record

## 8.1 Why the field cannot simply move to offset 0

The obvious design -- put `recordLength` first, ahead of `generation` -- was considered and
REJECTED during this RFC's own drafting: every existing FMV2 checkpoint already has `generation` at
offset 0. Shifting it (and every field after it) to make room would mean this RFC's own reader could
no longer correctly interpret any checkpoint written before this RFC landed, which is exactly the
breaking, undertested change Section 2.1 exists to stop making. A real per-field shift is a version
bump and a migration by any honest definition, not an additive change.

## 8.2 An append-only field, disambiguated by the checksum itself

Instead, `recordLength` SHALL be appended immediately after the existing, UNCHANGED 104-byte payload
(`generation` through `extentTreeCount` keep every offset they already have), at offset 104 -- the
exact byte that has always held the checksum until now. The checksum itself moves to immediately
after `recordLength`: offset 112 for any record written by this RFC's own build (104 existing bytes
+ 8 for `recordLength` itself), or further out once a future RFC appends more fields, always at
offset = `recordLength`.

This creates a genuine ambiguity at read time -- the 8 bytes at offset 104 are either the OLD
checksum (a record written before this RFC) or the NEW `recordLength` field (a record written by
this RFC or later) -- and that ambiguity is resolved not by a version flag but by the checksum's own
validity, the same "try the interpretation, trust it only if it actually checks out" principle a
self-describing format needs at exactly one transition point, never again after:

1. **Try the legacy interpretation first**: verify the checksum at the fixed offset 104 over bytes
   0-103, exactly as every build before this RFC always has. If it validates, this is a record from
   before this RFC -- proceed exactly as `ArcFSReadCheckpoint` already does today, with
   `recordLength` implicitly 104 and no new fields present. This path costs nothing new for any
   already-existing volume.
2. **Only if that fails**, try the self-describing interpretation: read a candidate `recordLength`
   from offset 104, reject it outright (structural defect, fail closed) if it is not plausible (less
   than 112 -- the smallest length any record produced by this RFC's own build can honestly have --
   or large enough that its own checksum would fall outside one 512-byte sector), then verify the
   checksum at offset `recordLength` over bytes `0..recordLength-1`. If THAT validates, this is a
   record written by this RFC's own build or later; read every field this build recognizes and
   ignore any trailing bytes it does not.
3. If NEITHER interpretation validates, this is genuine corruption -- fail closed exactly as today.

A real CRC-32C match happening by coincidence under the wrong interpretation is a 2^-32 event per
attempt; this project already accepts checksum collision odds of that same order everywhere else it
relies on CRC-32C, so this is not a new category of risk, only a new place the existing risk is
accepted.

## 8.3 Reader behavior once past disambiguation

`ArcFSReadCheckpoint`/`ArcFSPeekCheckpoint`/`ArcFSSelectRingCheckpoint` SHALL apply Section 8.2's
two-step disambiguation and then:

- MUST treat a self-describing `recordLength` smaller than this build's own minimum understood size
  (112, once this RFC lands) as a structural defect -- a record shorter than any version this build
  can interpret is not survivable, matching a checksum mismatch's own existing fail-closed behavior.
- MUST tolerate a self-describing `recordLength` greater than this build's own maximum understood
  size: read every field this build recognizes, verify the checksum over the record's own full
  stated length (even though this build cannot interpret the trailing bytes), and ignore the unread
  trailing bytes entirely. This -- not Section 8.2's own one-time disambiguation -- is what makes
  every FUTURE field addition genuinely additive from this point forward: a build older than the one
  that wrote a longer record still mounts it correctly, using only the fields it understands.

## 8.4 What this does and does not solve

This makes checkpoint GROWTH forward-compatible (an older reader tolerates a newer, longer record).
It does not make a field's MEANING renegotiable, and it does not let an older reader use a field it
doesn't know how to interpret -- a genuinely new tree type, for instance, still needs a real feature
bit (Section 7) gating whatever WRITES that field, so an old reader that can't interpret it never
sees a volume claiming to use it read-write. Section 8 and Section 7 are complementary, not
substitutes for each other: length-tolerance handles "a reader is older than the writer that grew
this record"; feature bits handle "a reader must actively refuse, not just skip, unrecognized
semantics it cannot safely ignore."

------------------------------------------------------------------------

# 9. Requirement: A Real Backward-Compatibility Proof

## 9.1 What must actually be tested

Every prior RFC-0040 increment's QEMU proof formats a fresh volume with the CURRENT build and
mounts it with the SAME build, in the same continuous boot session -- provably correct, but never
once establishing that a REAL PRIOR SHAPE actually still mounts. This RFC's own acceptance MUST
include a fixture that:

1. Formats, mounts, and commits a real volume with real content using the CURRENT build (ordinary
   `ArcFS.FormatVolume`/`ArcFS.CommitImage`), producing a genuine self-describing (`recordLength`
   -bearing) checkpoint on the simulated disk.
2. Reads every field back out of that ACTUAL on-disk checkpoint sector (not re-derived from live
   in-memory state) and re-writes the SAME sector using a byte-for-byte REPLICA of the
   pre-Section-8 `ArcFSWriteCheckpointRecord` shape -- 104-byte payload, checksum at the fixed
   legacy offset 104, no `recordLength` field at all -- confirming first that the record being
   downgraded really was self-describing (so there is something genuine to prove).
3. Mounts that now-legacy-shaped sector using the CURRENT build and confirms it reads back
   correctly, via Section 8.2's LEGACY interpretation path specifically, with the real content
   (files, attributes) still resolving correctly -- an actually-older on-disk BYTE SHAPE, read by
   the current reader, not merely a claim.
4. Separately, commits a FRESH generation using the CURRENT build (writing a real
   `recordLength`-bearing record again) and confirms it ALSO mounts correctly, via Section 8.2's
   SELF-DESCRIBING interpretation path -- proving both disambiguation branches, not just the legacy
   one, and proving a volume can move between shapes in place, mid-lifetime, with no reformat.
5. Separately, mounts a DELIBERATELY corrupted/implausible `recordLength` (below this build's own
   minimum) written directly onto a real checkpoint sector, and confirms the mount fails closed with
   a clear structural-defect result -- the negative control this property specifically needs,
   distinct from every other fixture's own "flip an assertion" negative control.

**A scope note decided during implementation, stated honestly rather than silently substituted**:
step 2's replica writer runs inside the SAME compiled program as the current reader, not as a
genuinely separate historical binary in a separate QEMU boot. This is a deliberate, reasoned
substitution, not a shortcut -- true cross-boot memory transfer (dump one QEMU guest's RAM to a
host file, preload it into a second) needs real new harness infrastructure (`run-uefi-hello-with-
preload.sh` already proves the PRELOAD direction; nothing in this project yet proves the DUMP
direction) that does not exist yet and is not itself in this RFC's own scope. The substitution is
sound specifically because Section 8.2's disambiguation is a pure function of on-disk BYTES -- it
reads only what `RAMDisk.ReadSectors` returns and shares no in-memory state whatsoever with
whatever wrote those bytes. A byte-for-byte replica of the removed function, confirmed identical by
direct comparison against source control history, produces on-disk bytes indistinguishable from
what a genuinely separate old binary would have produced; the reader cannot tell the difference and
neither can this proof. A true two-binary proof remains a stronger, independently valuable future
enhancement (Section 20's own Open Questions notes this), not a gap in what this specific
requirement needed to establish.

## 9.2 Why this belongs in this RFC, not as an afterthought

Section 8 without Section 9 is exactly the same unverified promise every prior checkpoint growth
already made -- "this should be backward compatible" was true, in spirit, of every one of the five
prior checkpoint-growth increments too, and none of them were ever actually tested that way. This
requirement exists specifically so this RFC does not repeat that pattern for its own change.

------------------------------------------------------------------------

# 10. Requirement: Reserved Growth Capacity in Fixed Rows

## 10.1 Object Table row

The Object Table row SHALL widen from 40 to 56 bytes, adding 16 reserved bytes (two U64 fields,
`reserved0`/`reserved1`) after the existing `oidHigh` field. Both MUST be written as 0 by every
function that constructs a row (`ArcFSCreateObject`, `ArcFS.Reflink`, `ArcFSPopulateObjectRow`) and
MUST be round-tripped byte-for-byte through the existing Object Tree (no change to the tree's own
40-byte on-disk leaf entry shape needed -- the extra bytes live only in the LIVE in-memory row; the
on-disk entry continues to store exactly the fields it already does, since nothing populates the new
reserved bytes yet and there is nothing to persist). A future RFC that needs a real per-object field
(the generation stamp Section 4 Non-Goal 4 names, a permission bit, a link count) widens the ON-DISK
leaf entry to match at that point, using Section 8's own `recordLength`-style self-description
pattern generalized to tree leaf entries (out of this RFC's own scope to generalize further than
naming the pattern here).

## 10.2 Namespace row

The Namespace row SHALL widen from 64 to 80 bytes, adding 16 reserved bytes after the existing
`active` field, with the identical "cleared, round-tripped, on-disk leaf entry unchanged until a
real consumer exists" treatment as Section 10.1.

## 10.3 Attribute row

The Attribute row (56 bytes as of this session's own multi-attribute increment) SHALL widen to 64
bytes, adding 8 reserved bytes, with the same treatment.

## 10.4 What this section deliberately does not do

It does not invent a use for any reserved field -- that is what makes it a real reservation and not
disguised scope creep. It does not touch the Extent Tree's own already-reserved field (Section 2.3),
which needs no widening. It does not widen the ON-DISK tree leaf entry shapes (only the live row
layouts), so it carries zero Section 8/9-style compatibility burden of its own: an old reader
mounting a volume written by this RFC's own build sees exactly the same on-disk leaf bytes it always
would have, since the reserved bytes are never written to disk in the first place.

------------------------------------------------------------------------

# 11. Requirement: Production-Scale Capacities

## 11.1 Target capacities

Every fixed capacity constant SHALL be raised as follows, chosen to remain fully provable under
QEMU within this project's own established time budget (a full regression run staying within
several minutes, matching RFC-0040's own precedent) while no longer being a toy:

| Constant                        | RFC-0040 value | This RFC's target |
|----------------------------------|----------------|--------------------|
| `ArcFSMaxObjects()`              | 64             | 4096               |
| `ArcFSMaxNamespaceRows()`        | 128            | 8192               |
| `ArcFSMaxAttributes()`           | 128            | 8192               |
| `ArcFSMaxHandles()`              | 8              | 64                 |
| `ArcFSMaxTrackedSectors()`       | 508 (~254 KB)  | 131064 (~64 MB)    |
| `ArcFSMaxFileChunks()`           | 4 (16 KB)      | 256 (1 MB)         |

`ArcFSMaxTrackedSectors()` stays governed by RFC-0040 Section 7.1's own one-sector, self-checksummed
Allocation Bitmap constraint (508 payload bytes + 4-byte checksum = 512); reaching a real 64 MB
ceiling with that constraint needs the bitmap to become a real multi-sector structure (its own
small tree or a fixed run of self-checksummed sectors, whichever proves simpler) -- named here as
this section's own largest single piece of work, not assumed trivial.

## 11.2 Fixed-address scratch region consequences

Every table this RFC widens currently lives at a fixed MMIO virtual address sized for its RFC-0040
capacity. This RFC MUST re-plan those addresses (following the exact "fresh, correctly-sized
addresses instead of trying to cram into the old gap" judgment the Namespace Tree's own scratch
region already established in RFC-0040 Phase J) rather than assume the existing gaps between
addresses are large enough -- several are not (the Object Table's own live row region alone grows
from 64*56=3584 bytes to 4096*56=229376 bytes, more than sixty times its RFC-0040 size).

## 11.3 Tree node capacity is unaffected

`ArcFSObjectTreeNodeCapacity()`/`ArcFSNamespaceTreeNodeCapacity()`/`ArcFSAttributeTreeNodeCapacity()`/
`ArcFSExtentTreeNodeCapacity()` (all currently 4, deliberately tiny so a handful of real rows already
forces a real multi-node, multi-level tree) are NOT raised by this section -- they remain small on
purpose, per RFC-0040 Section 9.1's own reasoning, which is completely orthogonal to how many total
rows the surrounding table can hold. Raising the row ceiling from 64 to 4096 objects with node
capacity staying at 4 means a full table now provably builds a REAL deep multi-level tree (many more
internal levels than any existing fixture forces today) -- itself a meaningful, previously-untested
scale of proof this RFC's own fixture should exercise.

------------------------------------------------------------------------

# 12. Requirement: UTF-8 String and Binary Blob Attribute Values

## 12.1 Representation

A new `ArcFSAttrTypeString()` (6) and `ArcFSAttrTypeBlob()` (7) join the existing
`ArcFSAttrType*` constants. Both value kinds are variable-length, which the current Attribute row's
fixed `valueLow`/`valueHigh` pair cannot hold directly -- the row's `valueLow` field SHALL instead
store a DATA-POOL SLOT (the same slot-allocation mechanism `ArcFSAllocateDataSlot`/`ArcFSNextDataSlot
Address` already provides for file content, reused here rather than inventing a second allocator)
and `valueHigh` SHALL store the value's real byte length. The referenced slot's content is the raw
UTF-8 bytes (for `ArcFSAttrTypeString`) or raw bytes (for `ArcFSAttrTypeBlob`), unconstrained beyond
this backend's existing "no LEN/MID-style text OPERATIONS on the stored bytes" limitation --
RFC-0042's own new freestanding `LEN`/`MID` builtins (`.agents/reports/freestanding-len-mid.md`)
operate on in-memory `STRING` pointers, not on-disk attribute blobs, and this section does not
change that boundary.

## 12.2 Public API

`ArcFS.SetStringAttribute(oid, attributeId, bufferPtr, byteLength) AS BOOL` and
`ArcFS.GetStringAttribute(oid, attributeId, destPtr, destCapacity) AS U64` (returning the actual byte
length copied, 0 if absent or `destCapacity` is insufficient -- caller-fails-closed, matching this
project's own established convention rather than silently truncating) round out the attribute API
for both new types; `ArcFS.RemoveAttribute` (already generic over `attributeId`) needs no change.

## 12.3 Durability

Both new types commit through the exact same Prepare/Publish protocol and the exact same
`(oid, attributeId)`-keyed Attribute Tree this session's own multi-attribute increment already
delivers -- the on-disk Attribute Tree entry format is unchanged (it already stores `valueLow`/
`valueHigh` generically); only the MEANING of those two fields for these two new type codes is new,
requiring no entry-width change.

------------------------------------------------------------------------

# 13. Format Versioning and Migration

## 13.1 No major version bump

Every requirement in this RFC is additive under Section 8's own self-description contract or is a
capacity increase with no on-disk shape implication (Section 11) or touches only live, never-
persisted row bytes (Section 10). None require a Format Major Version bump; the magic stays
`ARCFSB02`. This is the direct, load-bearing test of whether Section 8 actually delivers what
Section 2.1 says every prior increment failed to: if implementing this RFC's own requirements turns
out to need a version bump after all, that is itself a finding this RFC's own report MUST state
plainly, not something to route around silently.

## 13.2 Existing FMV2 fixtures

Every existing frozen FMV2 fixture (RFC-0040 Phases H-M and every scoped follow-on, including this
session's own multi-attribute and LEN/MID increments) needs zero changes -- each already inlines its
own frozen stdlib copy, exactly like every FMV1 fixture survived RFC-0040's own FMV1->FMV2 bump
untouched. The one NEW thing this RFC's own Section 9 fixture does that no prior fixture has: embed
TWO frozen copies in one test (an old writer, a new reader) rather than one frozen copy read by
itself.

------------------------------------------------------------------------

# 14. Implementation Phases

Continuing RFC-0040's own phase lettering.

- **Phase N -- Feature Negotiation and Self-Describing Checkpoint** (Sections 7, 8). The mechanism
  itself: bit layout, mount-time refusal contract, `recordLength`, tolerant reader.
- **Phase O -- Real Backward-Compatibility Proof** (Section 9). The fixture that actually tests
  Phase N's own promise against a genuinely older frozen shape, plus its own negative control.
- **Phase P -- Reserved Growth Capacity** (Section 10). Widen the three live row layouts, wire the
  reserved bytes through format/commit/mount, prove they round-trip as zero and stay zero across a
  real commit+remount.
- **Phase Q -- Production-Scale Capacities** (Section 11). Raise every named constant, re-plan
  fixed scratch addresses, extend the Allocation Bitmap past one sector, prove a genuinely deep
  multi-level tree at the new object ceiling.
- **Phase R -- UTF-8/Blob Attribute Values** (Section 12). The new attribute type codes and public
  API, reusing the existing data-pool slot allocator.

Phases are independent of each other except: Phase O depends on Phase N (there is nothing to prove
compatible without it); Phase Q's Allocation Bitmap work SHOULD land before or alongside its own
capacity bump (raising `ArcFSMaxTrackedSectors()` without the multi-sector bitmap first would just
recreate a smaller version of the same ceiling). No other phase blocks any other; they may ship in
any order, or in parallel scoped increments, matching RFC-0040's own precedent of shipping named
phases and scoped follow-ons as separate, independently-committed increments.

------------------------------------------------------------------------

# 15. AI Implementation Guidance

## 15.1 Required boundaries

Each phase gets its own real QEMU/OVMF proof, its own `.agents/reports/*.md` report, its own smoke
test registered in `arcology-os/cmake/Testing.cmake`, and its own revision-history entry in this
RFC -- the same discipline every RFC-0040 increment already followed, with no exception.

## 15.2 No silent architectural substitution

If a target number in Section 11.1's table turns out to be impractical under this project's own
QEMU time budget once actually measured, state the finding and the revised number explicitly in
that phase's own report -- do not silently ship a smaller number without saying so, and do not
silently ship a larger one either (a bigger number is not free: it changes fixed scratch address
math throughout the file, per Section 11.2).

## 15.3 Mandatory acceptance evidence

- Phase N: an unrecognized incompat bit set on a real formatted volume genuinely refuses to mount;
  an unrecognized ro-compat bit genuinely mounts read-only; an all-zero flags field (every existing
  volume) mounts exactly as before, unchanged.
- Phase O: the two-frozen-copies proof described in Section 9.1, passing on real, separately
  compiled programs -- not simulated within one build.
- Phase P: the three widened rows' reserved bytes read back as exactly zero after a real
  commit+remount, with every existing structural check (object/namespace/attribute lookup, rename,
  reflink, attribute set/get/remove) still passing unchanged.
- Phase Q: a real fixture reaching deep enough into the new object ceiling to force a tree with
  more levels than any existing fixture produces, with content independently verified correct
  throughout.
- Phase R: a UTF-8 string attribute and a binary blob attribute, each surviving a real
  commit+remount byte-for-byte, coexisting with existing integer-typed attributes on the same
  object without disturbing them.

## 15.4 Stop conditions

If any phase's own honest scoping discovers a requirement that DOES need a Format Major Version
bump after all (Section 13.1), stop, document the specific requirement and why in that phase's own
report, and treat the version bump as its own separate, explicitly-flagged decision -- never bundle
a real breaking change into a phase whose entire premise is "this does not need one."

------------------------------------------------------------------------

# 16. Testing Strategy

Unchanged in kind from RFC-0040 Section 18: every phase's real proof runs under QEMU/OVMF, every
fixture is deterministic across at least 3 repeated runs, every fixture includes a real negative
control, and the full regression suite is re-run clean after every phase before it is considered
done. Phase O adds a new proof SHAPE this project has not used before (two independently-frozen
copies of the format-writing logic in one fixture) -- every phase after it that touches an on-disk
record shape SHOULD consider whether it also needs this shape, not just the "grow live, prove within
one session" shape every prior phase has used.

------------------------------------------------------------------------

# 17. Security Considerations

An unrecognized incompat bit refusing to mount (Section 7.2) is a deliberate fail-closed choice --
the alternative (silently ignoring an incompat feature) is exactly the class of bug RFC-0039 Section
5.3 already forbids ("MUST NOT guess silently"). A `recordLength` claiming an implausibly large value
(Section 8.2) MUST still be bounded against the record's own fixed 512-byte sector capacity before
any read loop uses it as a bound -- an unbounded `recordLength` read is a real out-of-bounds read
risk this RFC's own implementation must guard against explicitly, not an incidental detail.

------------------------------------------------------------------------

# 18. Performance Considerations

Phase Q's raised capacities mean every full-table scan this project already performs (the active-row
gather passes every tree-build function uses, `ArcFSScanGeneration`'s own reachability walk) now
iterates a table up to 64 times larger. None of these scans are currently more than linear in table
size, so this is a real, expected, and acceptable slowdown in wall-clock QEMU proof time, not a
complexity regression -- but it is exactly why Section 15.2 requires measuring rather than assuming
the target numbers stay practical.

------------------------------------------------------------------------

# 19. Compatibility

Full backward compatibility with every existing FMV2 volume and every existing frozen fixture is
this RFC's own central design constraint (Sections 8, 9, 13), not an afterthought applied after the
fact. Forward compatibility -- can a FUTURE RFC add a field without reformatting -- is exactly what
Section 8's `recordLength` tolerance and Section 7's feature-bit contract together provide; this RFC
is, in that specific sense, primarily about ArcFS's compatibility properties rather than about any
one new capability.

------------------------------------------------------------------------

# 20. Open Questions

1. Should `recordLength`-style self-description generalize to the superblock and bitmap records too,
   or is the checkpoint record (the one that has actually grown five times) sufficient for now? This
   RFC scopes to the checkpoint record only, on the grounds that it is the only record with an actual
   history of growth to learn from; if the superblock or bitmap record ever needs its first field
   addition, that is the moment to decide whether to generalize the pattern.
2. Section 10's reserved fields are unnamed placeholders. Should this RFC or a future one commit to
   a SPECIFIC first use for each (the generation stamp Section 4 Non-Goal 4 already names is one
   candidate for the Object row's own reserved bytes) now, while the design is fresh, or leave them
   genuinely open until a real consumer exists? Left open here deliberately, matching Section 10.4's
   own "a real reservation, not disguised scope creep" reasoning.
3. Phase Q's Allocation Bitmap multi-sector redesign is the single largest piece of new mechanism in
   this RFC, and the least like anything RFC-0040 already built (every other bitmap operation this
   project has is a single self-checksummed sector). Whether it should be its own small tree
   (matching every other RFC-0040 growable structure) or a fixed run of sectors (simpler, if the new
   ~64 MB ceiling is itself considered a acceptable NEW fixed ceiling rather than the start of a
   fully dynamic bitmap) is left to that phase's own report to decide and justify.
4. Phase O's own backward-compatibility proof (Section 9.1) ended up substituting a same-process
   byte-for-byte replica of the removed legacy writer for a genuinely separate historical binary in
   a separate QEMU boot, reasoned through as sound for that specific byte-level property (see
   Section 9.1's own scope note). A real dump-guest-RAM-to-host-file mechanism (the missing half of
   what `run-uefi-hello-with-preload.sh` already proves in the PRELOAD direction) would make a
   genuinely stronger, general-purpose version of this proof possible for future format-shape work
   too -- worth building as its own small harness enhancement at some point, but not required by
   anything this RFC itself needs.

------------------------------------------------------------------------

# 21. References

- RFC-0038 (APS Block Storage and Filesystem Provider Substrate)
- RFC-0039 (ArcologyFS / ArcFS)
- RFC-0040 (ArcologyFS Production Hardening)
- RFC-0041 (Arcology System Namespace)
- `.agents/reports/aps-arcfs-phase-h.md` (the superblock ring and the feature-flags field's own
  original introduction)
- `.agents/reports/aps-arcfs-multi-attribute.md` (the checkpoint record's most recent growth, and
  the Attribute row layout Section 10.3/12 build on)
- `.agents/reports/freestanding-len-mid.md` (referenced in Section 12.1 to draw the boundary between
  in-memory STRING operations and on-disk attribute blob storage)

------------------------------------------------------------------------

# 22. Revision History

| Version | Date       | Summary        |
|---------|------------|-----------------|
| 0.1     | 2026-08-21 | Initial draft. Written directly from a live status/planning conversation (RFC-0040's own accumulated increments up through the multi-attribute model and freestanding LEN/MID) that surfaced two previously-unnamed gaps: no real format-extensibility mechanism (every checkpoint growth so far has been a silent, untested breaking change within "FMV2"), and every capacity ceiling still sized for QEMU test speed rather than genuine use. Five phases (N-R): feature negotiation, self-describing checkpoint record, a real two-frozen-copies backward-compatibility proof (a genuinely new fixture shape for this project), reserved growth capacity in the three live row layouts, production-scale capacity constants, and UTF-8/binary-blob attribute values. Status: Draft; no implementation phase has begun. |
| 0.2     | 2026-08-21 | Section 8's own mechanism revised DURING drafting, before any code was written: the original "recordLength at offset 0, generation shifts to offset 8" design was traced through and rejected -- it would have made every existing FMV2 checkpoint unreadable outright. Replaced with an append-only `recordLength` (offset 104, where the checksum used to live) disambiguated from a legacy record purely by which interpretation's own checksum validates, not a version flag -- sound because a legacy record's real checksum essentially never coincidentally validates under the new interpretation (a real CRC-32C collision is a 2^-32 event, an odds class this project already accepts everywhere else it relies on CRC-32C). Section 9's own fixture plan revised to match: downgrade a REAL committed checkpoint's own on-disk bytes in place (read every field back out, re-write with a byte-for-byte replica of the removed legacy writer) rather than reformat with a frozen historical stdlib copy, since the property under test is checksum disambiguation, not the whole format's own history. Phases N and O implemented and QEMU-proven, delivered together. Two real bugs found and fixed along the way (see the Status line's own summary and `.agents/reports/aps-arcfs-phase-n-o.md`/`.agents/reports/aps-arcfs-attribute-bound-fix.md`). New smoke tests `systems_arco_basic_arcfs_checkpoint_self_description_smoke` and `systems_arco_basic_arcfs_feature_negotiation_smoke`; full suite re-run clean (80/80). |
