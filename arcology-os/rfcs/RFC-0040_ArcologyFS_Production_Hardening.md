# RFC-0040: ArcologyFS (ArcFS) Production Hardening

**RFC Number:** RFC-0040
**Title:** ArcologyFS (ArcFS) Production Hardening
**Status:** Draft (Phase H implemented and QEMU-proven -- see `.agents/reports/aps-arcfs-phase-h.md`; Section 10's 128-bit OID identity and allocation implemented and QEMU-proven as a follow-on scoped increment -- see `.agents/reports/aps-arcfs-oid-128bit.md`; Phase I implemented and QEMU-proven, scoped to file data extents -- see `.agents/reports/aps-arcfs-phase-i.md`; Phase J implemented and QEMU-proven -- Object Tree AND Namespace Tree both real B+trees -- see `.agents/reports/aps-arcfs-phase-j.md` and its own dated Addendum; Phase K implemented and QEMU-proven, scoped to real per-chunk extents and genuine sparse holes, PLUS real offset-based partial reads/writes (Section 11.4) as a follow-on increment -- see `.agents/reports/aps-arcfs-phase-k.md` and `.agents/reports/aps-arcfs-phase-k-offset-io.md` (still not a true variable-count Extent Tree or on-disk reflink sharing); Phase L implemented and QEMU-proven -- the Attribute Tree (the third and final tree Section 9 named), real typed values (unsigned/signed/boolean/timestamp/UUID-OID), and real durability closing RFC-0039 Phase D's own documented "does not survive a remount" gap, scoped to one attribute slot per object (not RFC-0040 Section 12.1's own literal multi-attribute-per-object model) -- see `.agents/reports/aps-arcfs-phase-l.md`; Phase M implemented and QEMU-proven -- all six of ArcFS.GetHealthState's own reportable states (Healthy/Degraded/ReadOnlySafety/NeedsOfflineCheck/Corrupt/Unavailable) plus NeedsScrub via the new ArcFS.GetHealthStateCached, a persisted Health Record, real extent-overlap repair (deviating from Section 14.1's own literal "lower generation number" policy to lower-OID, since no per-entry generation number exists in this format), and allocation-bitmap reconciliation -- see `.agents/reports/aps-arcfs-phase-m.md`. **All six of RFC-0040's own named phases (H-M) are now implemented and QEMU-proven** -- Status stays Draft nonetheless, matching RFC-0039's own precedent: the accumulated, honestly-documented scope reductions across every phase (listed in each phase's own report, summarized in Phase M's own "Remaining activation gate") mean this is a large, real, repeatedly-proven implementation, not a production-complete one. Section 11's remaining two named items (11.1: a true variable-count Extent Tree; 11.5: on-disk reflink sharing) are now ALSO implemented and QEMU-proven as their own scoped follow-on increment, delivered together since keying the new Extent Tree by dataSlot rather than OID satisfies both at once -- see `.agents/reports/aps-arcfs-extent-tree-reflink.md`. Section 11 (11.1-11.5) is now fully addressed across Phase K and this increment together. That increment's own one named scope reduction -- reflink sharing not surviving a remount -- is now ALSO closed, as its own scoped follow-on -- see `.agents/reports/aps-arcfs-remount-reflink-sharing.md`)
**Category:** Storage / Filesystem Architecture
**Authors:** Arcology Project
**Created:** 2026-08-19
**Last Updated:** 2026-08-20
**Supersedes:** None
**Superseded By:** None
**Related Architecture:** Arcology Object Architecture; Polymorphic Substrate (APS)
**Related RFCs:** RFC-0000, RFC-0017 (Substrate Resource Model), RFC-0038 (APS Block Storage and Filesystem Provider Substrate), RFC-0039 (ArcologyFS / ArcFS -- this RFC amends and completes it)

------------------------------------------------------------------------

# 1. Executive Summary

RFC-0039 defined ArcologyFS and its reference implementation reached Phases A through G:
object/namespace/handle semantics, a real on-disk format, a crash-safe copy-on-write commit
protocol, reflinks with genuine copy-on-write, snapshots, a recovery/health model with real
ReadOnlySafety enforcement, and one concrete system-integration primitive (update-snapshot
rollback). Every phase was proven under QEMU on real hardware semantics, not simulated.

None of that work claims to be production-ready, and RFC-0039 itself never claimed otherwise --
`Status: Draft` was kept deliberately through all seven phases. Each phase's own report names
specific, bounded simplifications made so the *contract* could be proven before the *scale*
mattered: fixed-capacity tables sized for tests (64 objects, 128 namespace entries), a bump
allocator that never reuses a sector, one fixed-size extent per file, in-memory-only attributes,
64-bit OIDs instead of the specified 128-bit, and a handful of smaller gaps.

This RFC is the plan for closing every one of those *ArcFS-owned* gaps: real free-space
reclamation, growable metadata trees, variable-length and sparse files, on-disk reflink sharing,
persistent attributes, 128-bit OIDs, and a complete health/repair model. It explicitly does **not**
attempt the remaining items from RFC-0039's own Phase G (namespace attachment, system volume use,
recovery environment support, graphical storage tooling) -- those depend on Arcology subsystems
that do not exist anywhere in this repository yet, a finding RFC-0039's own Phase G report already
made in detail (`.agents/reports/aps-arcfs-phase-g.md`), and this RFC does not re-litigate it.

The expected outcome is a filesystem that has closed the gap between "provably correct at small,
fixed scale" and "usable as a real filesystem" -- without touching anything that already works.

------------------------------------------------------------------------

# 2. Motivation

RFC-0039's own implementation record is unusually explicit about what it did not do, and why. That
record is the motivation for this RFC: every requirement below traces to a named, dated
simplification in a specific phase report, not a speculative wish list.

## 2.1 The allocator never gives anything back

Every commit under RFC-0039's reference implementation (Phase C onward) allocates sectors with a
monotonically increasing cursor and never reuses one. RFC-0039 Phase E built the exact safety
analysis a reclaiming allocator would need (`ArcFS.IsSectorReachable`,
`ArcFS.ReclaimableSectorCount`) specifically so this gap could be closed later without redoing that
analysis -- but nothing consumes it. A volume under the current implementation grows without bound
and never shrinks. This is the single largest reason the current implementation cannot be used for
anything real.

## 2.2 Everything is sized for tests, not for use

64 objects. 128 namespace entries. 4096 bytes per file, one fixed extent, no fragmentation. These
numbers were chosen in Phase A specifically so the object/namespace/handle *contract* could be
proven against real memory and a real CPU before disk-format complexity existed at all -- a
deliberate, stated engineering sequencing decision, not an oversight. But a filesystem that can
hold 64 files and no file larger than 4 KiB is not a filesystem anyone can use.

## 2.3 Format-level gaps accumulated across every phase

Phase B's on-disk format is its own documented minimal subset of RFC-0039's real format: one
superblock instead of a redundant ring, one checkpoint instead of generation history, flat
fixed-size record arrays instead of real B+trees, an additive checksum instead of a
cryptographic-strength one. Phase D's reflinks share data in memory but duplicate it on disk. Phase
D's attributes never survive a remount at all. Every one of these was named as deferred, not
missing by accident.

## 2.4 Existing solutions are insufficient because there aren't any yet

There is no other filesystem implementation in this project to fall back on for real workloads.
Closing these gaps is the only path from "proven correct" to "usable."

------------------------------------------------------------------------

# 3. Goals

This RFC SHALL define:

- A real free-space allocator with crash-safe reclamation, closing RFC-0039 Phase C/E's own
  documented gap.
- Growable, disk-backed B+tree metadata structures for the Object Tree, Namespace Tree, and
  Attribute Tree, replacing the fixed-capacity in-memory arrays every phase through G used.
- Variable-length, multi-extent, sparse-capable file storage, replacing the fixed single 4096-byte
  extent per file.
- On-disk reflink sharing with a persisted, crash-safe reference count, replacing the current
  per-generation duplication.
- Persistent typed attributes, surviving a remount.
- 128-bit Object IDs, matching RFC-0039 Section 17's own specification.
- A complete RFC-0039 Section 36 health-state model and expanded repair coverage.
- An explicit on-disk format version bump and migration policy covering all of the above.
- A phased implementation and validation plan in the same style, and to the same evidentiary
  standard, as RFC-0039's own Phases A-G.

------------------------------------------------------------------------

# 4. Non-Goals

The following are explicitly out of scope for this RFC:

- **Namespace attachment, system volume use, recovery environment support, graphical storage
  tooling** (the remaining items from RFC-0039 Section 78 Phase G). Each depends on an Arcology
  subsystem that does not exist anywhere in this repository -- see
  `.agents/reports/aps-arcfs-phase-g.md` for the specific missing dependency named for each. This
  RFC does not attempt them and does not re-derive that finding.
- **Encryption and compression.** Still explicitly reserved-but-unimplemented per RFC-0039 Sections
  27-28; this RFC does not change that.
- **Multi-writer / concurrent access.** The reference implementation remains single-threaded,
  matching every prior phase's own scope.
- **Live in-place migration from format major version 1 to major version 2.** This RFC defines the
  new format and requires old-format volumes to be explicitly reformatted and copied, not upgraded
  in place. See Section 15.
- **A real cryptographic hash.** Section 8 upgrades the checksum algorithm to a real
  non-cryptographic integrity hash (CRC-32C or equivalent); a cryptographic-strength hash remains a
  future extension, not a requirement here.
- **UTF-8 string-valued attributes.** Attribute persistence (Section 12) widens the value-type
  model, but STRING still has no substring/length operations on this compiler backend (RFC-0039
  Phase A's own scope reduction #4, unchanged) -- string-valued attributes remain out of reach until
  that is a compiler-level capability, which is outside this RFC's control. See Section 17.5.

------------------------------------------------------------------------

# 5. Terminology

Terms already defined in RFC-0039 Section 6 apply unchanged. New terms:

**Free Extent** -- A contiguous, currently-unallocated range of logical blocks, tracked by the
Allocation Tree.

**Reclamation Transaction** -- A commit whose only purpose is to mark a superseded, unreachable
generation's blocks free in the Allocation Tree. Ordinary commit rules apply; there is no separate
reclamation code path outside the normal commit protocol.

**Format Major Version 2 (FMV2)** -- The on-disk format this RFC defines. Volumes formatted under
RFC-0039's reference implementation (FMV1) are FMV1 and are not readable by an FMV2-only
implementation; see Section 15.

**Shared Extent Refcount** -- A persisted count, stored per extent in the Extent Tree, of how many
reachable generations (active checkpoint or any live snapshot) reference that extent. An extent is
reclaimable only when this count reaches zero.

------------------------------------------------------------------------

# 6. Relationship to RFC-0039

This RFC does not restate RFC-0039's architecture, design principles, namespace model, security
model, or terminology; all of that remains normative and unchanged. This RFC amends specific
RFC-0039 sections whose *requirements* were already normative but whose reference implementation
fell short, and specifies the concrete mechanism to close each gap:

| RFC-0039 Section | What it already requires | What this RFC adds |
|---|---|---|
| 14 (Superblock Ring) | Redundant superblock copies | Section 7: real ring, quorum selection |
| 15.1 (Checkpoint contents) | Full root-reference set including AllocationTreeRoot, AttributeTreeRoot | Section 8/12: those roots, actually populated |
| 16 (Metadata Trees) | Checksummed copy-on-write B+trees | Section 9: real growable B+trees |
| 17 (Object IDs) | 128-bit OIDs | Section 10: widened from the reference implementation's 64-bit |
| 18-19 (File Data, Sparse Files) | Multi-extent, sparse-capable files | Section 11: real Extent Tree |
| 20.2 (File data checksums) | "SHOULD use a modern high-performance checksum" | Section 8.4: CRC-32C |
| 22 (Reflinks) | Shared extents, no data duplication | Section 11.5: on-disk sharing with persisted refcounts |
| 24 (Typed Attributes) | Persistent, typed | Section 12: Attribute Tree |
| 29 (Free-Space Management) | Transactional allocation/reclamation | Section 7: the allocator itself |
| 36 (Health Model) | Full state list | Section 13: all seven states, persisted history |
| 39 (Offline Check and Repair) | Full defect-class coverage | Section 14: allocation/extent-overlap repair |

Everything in the table's middle column was already `SHALL`/`MUST` in RFC-0039. This RFC is
implementation completion, not a new architectural direction.

------------------------------------------------------------------------

# 7. Requirement: Real Free-Space Allocation and Reclamation

## 7.1 Allocation Tree

ArcFS SHALL maintain a persistent Allocation Tree (RFC-0039 Section 16.4) tracking every logical
block's state: free, or allocated with an owning class (metadata, file data, checkpoint/reserved).
The Allocation Tree SHALL itself be a copy-on-write B+tree (Section 9), checksummed and versioned
identically to every other metadata tree.

## 7.2 Allocation during commit

`ArcFS.PrepareCommit`'s successor SHALL consult the Allocation Tree for free extents before writing
any new object, namespace, attribute, or data record, MUST NOT allocate a block the Allocation Tree
marks as currently allocated, and MUST mark every block it allocates as allocated in a
copy-on-write update to the Allocation Tree that becomes part of the SAME transaction's checkpoint.

## 7.3 Reclamation is an ordinary commit

Reclaiming a superseded, unreachable generation's blocks SHALL be performed as a Reclamation
Transaction -- an ordinary commit whose only content is: for every block in a generation
`ArcFS.IsSectorReachable` (RFC-0039 Phase E, unchanged) reports unreachable, mark it free in the
Allocation Tree. There SHALL be no separate, non-transactional reclamation code path. This follows
directly from RFC-0039 Section 5.2 ("committed state is immutable... mutations produce new
blocks/extents and become visible only when a new checkpoint commits") applying to the Allocation
Tree exactly as it applies to every other tree.

## 7.4 Crash safety

A crash at any point during a Reclamation Transaction MUST leave every affected block in one of
exactly two states after recovery: still marked allocated (as if the reclamation transaction never
ran), or fully marked free (as if it completed). A hybrid -- some blocks freed, others not, from one
logical reclamation pass -- MUST NOT be observable after recovery. This is RFC-0039 Section 75.5's
own crash-injection requirement, applied to reclamation specifically, and MUST be proven the same
way RFC-0039 Phase C proved it for the commit protocol: a fixture that halts a reclamation
transaction after some writes but before publication, and confirms recovery shows the fully
pre-reclamation state.

## 7.5 Reuse safety

A block MUST NOT be handed out by Section 7.2's allocation step until the transaction that marked
it free has been durably published (RFC-0039 Section 15.2's own durability-before-exposure rule,
applied here). This prevents the classic use-after-crash bug where a freed block is reused before
its freedom is itself durable, and a crash then leaves two live generations referencing the same
physical block for incompatible data -- exactly what RFC-0039 Section 29 already forbids
("no committed generation may describe the same physical block as simultaneously allocated to
incompatible owners").

------------------------------------------------------------------------

# 8. Requirement: Format-Level Durability Upgrades

## 8.1 Superblock ring

ArcFS SHALL maintain at least 4 redundant superblock copies at fixed, predetermined sectors.
Activation SHALL read all copies, validate each independently (magic, checksum, format version),
and select the most recent valid copy by comparing each copy's referenced checkpoint's generation
number. If copies disagree in a way that cannot be resolved to a single most-recent valid state,
writable activation MUST fail (RFC-0039 Section 14, already normative).

## 8.2 Checkpoint publication across the ring

Publishing a new checkpoint SHALL write the new superblock content to all ring copies, in a fixed
order, before the transaction is considered durably published. A crash partway through updating the
ring MUST still leave at least one superblock copy correctly pointing at either the old or the new
checkpoint -- never a torn/inconsistent single copy accepted as authoritative.

## 8.3 Generation history

The Checkpoint record SHALL gain a `PreviousGeneration` checkpoint-sector field (RFC-0039 Section
15.1 already lists `PreviousGeneration` as a required field; the reference implementation's
Checkpoint record omits it). This SHALL form a backward-linked chain of checkpoints independent of
snapshots, enabling forensic inspection of recent history without requiring an explicit snapshot to
have been taken in advance.

## 8.4 Checksum algorithm

ArcFS SHALL replace the additive sum-of-bytes checksum (Phase B's own documented placeholder) with
CRC-32C for both metadata and file-data checksums, satisfying RFC-0039 Section 20.2's "SHOULD use a
modern high-performance checksum with strong accidental-corruption detection." The algorithm choice
SHALL be recorded in the superblock's checksum-algorithm-identifier field (RFC-0039 Section 14,
already normative) so a future format revision can change it without breaking this one's readers.

------------------------------------------------------------------------

# 9. Requirement: Growable Metadata Trees

## 9.1 On-disk shape

The Object Tree, Namespace Tree, and Attribute Tree SHALL each be real copy-on-write B+trees
(RFC-0039 Section 16, already normative), not flat fixed-size record arrays. Node size SHALL match
the logical block size (4096 bytes, RFC-0039 Section 13). Each node SHALL be self-describing
(node type, key count, checksum covering the whole node) and independently checksummed.

## 9.2 In-memory shape

The in-memory representation SHALL be a bounded LRU node cache backed by dynamically allocated
pages (via the same `AllocatePages` mechanism every fixture in this chain already uses for its own
scratch/page-table needs), not a single fixed-size MMIO region. Table capacity SHALL be limited
only by available memory and the 64-bit block-count fields already present in the checkpoint
format, not by a compile-time constant.

## 9.3 Split and merge

Node split (on insert into a full node) and merge (on delete leaving a node under a minimum
occupancy threshold) SHALL follow standard B+tree algorithms. RFC-0039 Section 16's own "precise
node split heuristics are implementation-defined as long as the persistent format and ordering
rules are obeyed" remains the governing constraint -- this RFC does not mandate a specific split
policy beyond correctness.

## 9.4 Migration note

This is the single largest implementation effort in this RFC and the one every other on-disk
change (Sections 8, 11, 12) depends on, since it replaces the record-array addressing scheme
(`objectRoot + index`) every existing reader/writer function currently assumes. See Section 16 for
phase ordering.

------------------------------------------------------------------------

# 10. Requirement: 128-Bit Object IDs

## 10.1 Representation

An OID SHALL be represented as two U64 halves (high, low) throughout every persistent record and
every in-memory table row, matching RFC-0039 Section 17's "128-bit identifier" requirement. This
compiler backend has no native 128-bit integer type (confirmed during RFC-0039 Phase A's own
implementation), so all OID comparison, allocation-counter increment, and equality logic SHALL
operate on the pair explicitly (compare high halves first, low halves on a tie; increment the low
half with carry into the high half).

## 10.2 Allocation

The OID allocation counter (RFC-0039 Section 17: "OID allocation MUST make accidental reuse
practically impossible") SHALL be a 128-bit monotonic counter using the representation in 10.1.
Given the reference implementation's 64-bit counter already treats overflow as a non-practical
concern at any sane commit rate (RFC-0039 Phase A's own scope reduction #1), a 128-bit counter's
overflow is stronger evidence of the same, not a new analysis.

------------------------------------------------------------------------

# 11. Requirement: Extent-Based Variable-Length and Sparse Files

## 11.1 Extent Tree

ArcFS SHALL maintain a per-volume Extent Tree (RFC-0039 Section 16.3) mapping `(OID, logical byte
range)` to `(physical block range, length, flags, data checksum reference)`. A single file MAY be
represented by any number of extents.

## 11.2 Variable file size

A file's logical size SHALL NOT be bounded by a fixed per-file capacity. `ArcFS.Resize` (RFC-0039
Phase D) SHALL grow a file by allocating additional extents as needed (via the Section 7 allocator)
rather than failing once RFC-0039 Phase D's own fixed 4096-byte ceiling is reached.

## 11.3 Sparse ranges

A logical byte range with no corresponding Extent Tree entry SHALL read as zero bytes without any
physical allocation (RFC-0039 Section 19, already normative, unimplemented in the reference
implementation). Writing into a sparse range SHALL allocate only the extent(s) actually touched.

## 11.4 Partial writes and reads

`ArcFS.HandleWrite`/`ArcFS.HandleRead`'s successors SHALL accept an explicit byte offset (RFC-0039
Phase A's own scope reduction #3 named this as deferred: "no persistent position across calls...
RFC-0039's own Seek/partial-write semantics are Phase C+ work once real extents exist to seek
within" -- this is that phase) and SHALL walk the Extent Tree to resolve which physical extent(s)
a given `(offset, length)` range touches, splitting the I/O across extent boundaries as needed.

## 11.5 On-disk reflink sharing

`ArcFS.Reflink` (RFC-0039 Phase D) SHALL, once the Extent Tree exists, share the SOURCE's actual
extent entries with the new OID rather than the current implementation's per-commit duplication.
Each shared extent's Shared Extent Refcount (Section 5) SHALL be incremented on reflink and
decremented when a generation referencing it becomes unreachable and is reclaimed (Section 7.3). An
extent SHALL be returned to the free pool only when its refcount reaches zero. The in-memory
copy-on-write gate RFC-0039 Phase D already built (`ArcFSEnsurePrivateSlot`) SHALL be generalized to
operate on Extent Tree entries instead of fixed data-pool slots, preserving its existing safety
property ("a reflink MUST NOT cause later writes to one file to modify the visible data of the
other") without re-deriving it.

------------------------------------------------------------------------

# 12. Requirement: Persistent Typed Attributes

## 12.1 Attribute Tree

ArcFS SHALL maintain a per-volume Attribute Tree (RFC-0039 Section 16.5) mapping `(OID, attribute
namespace, attribute name/ID)` to a typed value, replacing RFC-0039 Phase D's in-memory-only
single-U64-slot implementation. The Checkpoint record SHALL gain `AttributeTreeRoot` and
`AttributeTreeCount` fields (RFC-0039 Section 15.1 already lists these as required; the reference
implementation's Checkpoint record omits them).

## 12.2 Value types

The Attribute Tree SHALL support, at minimum, the integer-representable subset of RFC-0039 Section
24's base value types: unsigned integer, signed integer, boolean, timestamp, and UUID/OID. UTF-8
string and binary blob attribute values remain unimplemented under this RFC -- see Section 4's own
Non-Goals and Section 17.5.

## 12.3 Durability

Attribute mutations SHALL commit through the same Prepare/Publish protocol as every other tree
mutation (RFC-0039 Section 15.2) and MUST be observable after a remount -- closing RFC-0039 Phase
D's explicitly documented gap ("a value set here does NOT survive `ArcFS.MountImage()`").

------------------------------------------------------------------------

# 13. Requirement: Full Health Model

## 13.1 State list

`ArcFS.GetHealthState`'s successor SHALL report all seven RFC-0039 Section 36 states (Healthy,
Degraded, ReadOnlySafety, NeedsScrub, NeedsOfflineCheck, Corrupt, Unavailable), not the reference
implementation's three-state collapse. `ReadOnlySafety` SHALL be reported specifically when
`ArcFSReadOnlySafetyAddress`'s successor state is armed (RFC-0039 Phase F), distinctly from a
general `Degraded` finding that has not (yet) armed that flag.

## 13.2 Persisted health history

A small fixed-size Health Record (not a tree -- RFC-0039 Section 36's own health-detail fields are
bounded and low-cardinality) SHALL be added near the superblock, containing: last scrub
generation/timestamp/result, last repair generation/timestamp/summary, and cumulative
checksum-error and I/O-error counters. This record SHALL be updated transactionally alongside
whatever commit produced the result it records (a scrub's own findings are not committed --
RFC-0039 Section 20.4 keeps scrubbing read-only -- but a subsequent repair's own commit SHALL record
the scrub that motivated it).

------------------------------------------------------------------------

# 14. Requirement: Expanded Repair Coverage

## 14.1 Extent-overlap repair

Once the Allocation Tree (Section 7) and Extent Tree (Section 11) exist, ArcFS SHALL define a
resolution policy for extent-overlap defects (RFC-0039 Phase F detects these but explicitly does
not repair them, citing the absence of an authoritative-ownership policy). This RFC establishes
that policy: **the object whose Extent Tree entry has the lower generation number is authoritative;
the later, conflicting entry is treated as an orphaned extent claim and the OFFENDING OBJECT is
routed through the existing orphan-reattachment repair (RFC-0039 Phase F) with its data extent
reference cleared** (an empty file, not deleted -- consistent with RFC-0039 Section 39's own "recovery
SHOULD preserve it... with provenance metadata" policy, applied to metadata even when the data
itself cannot be trusted).

## 14.2 Allocation-tree reconciliation

ArcFS SHALL define a repair pass that recomputes the Allocation Tree's free/allocated state
directly from every reachable generation's actual extent references (the authoritative source per
RFC-0039 Section 29) and reconciles any divergence from the persisted Allocation Tree, favoring the
recomputed (conservative -- more things marked allocated, never fewer) result. This is the offline
analogue of RFC-0039 Section 39's own named "allocation-tree reconciliation" defect class.

------------------------------------------------------------------------

# 15. Format Versioning and Migration

## 15.1 Major version bump

Every requirement in Sections 7-13 changes the on-disk checkpoint record shape, tree structure, or
both. This RFC SHALL therefore define Format Major Version 2 (FMV2), distinct from the reference
implementation's FMV1 (RFC-0039 Phases B-G). Per RFC-0039 Section 62.3 (already normative), an
FMV1-only reader MUST NOT attempt to interpret an FMV2 volume, and vice versa -- these are
incompatible feature sets, not a compatible extension.

## 15.2 No live migration

This RFC does NOT specify in-place migration from FMV1 to FMV2. An FMV1 volume MUST be migrated by
formatting a new FMV2 volume and copying content across the generic `FileSystem`/`ByteStream`
interfaces (RFC-0039 Section 42) -- the same path any two independent filesystem implementations
would use. In-place migration is a legitimate future extension (RFC-0039 Section 63 already
anticipates "ArcFS migration tooling") but is explicitly deferred here, matching this RFC's Section
4 Non-Goals.

## 15.3 Existing FMV1 fixtures

RFC-0039 Phases A-G's own fixtures and reports remain valid as a record of FMV1's behavior and are
NOT retroactively invalidated by this RFC. FMV1 is not deprecated by this RFC; it simply stops
being the format new work targets once FMV2 exists.

------------------------------------------------------------------------

# 16. Implementation Phases

Phase lettering continues RFC-0039 Section 78's own A-G sequence.

**Phase H -- Format foundation**

Implement: FMV2 superblock/checkpoint shape (Sections 8, 15); 128-bit OIDs (Section 10); CRC-32C
checksums (Section 8.4). Deliberately touches every record's byte layout while the underlying
storage model (flat arrays) is UNCHANGED -- a bounded, provable warm-up before Phase I's much
larger rewrite, matching this project's own established pattern of proving smaller pieces first.

**Phase I -- Real allocation and reclamation**

Implement: Allocation Tree (Section 7.1); allocator integration into commit (7.2); Reclamation
Transactions (7.3); crash-injection proof of 7.4; reuse-safety proof of 7.5. Depends on Phase H's
checkpoint shape. This closes the single largest gap named in Section 2.1 and SHOULD be prioritized
over Phase J even though Phase J is architecturally prerequisite to some later phases, because a
volume that still cannot reclaim space is not meaningfully more usable after Phase J alone.

**Phase J -- Growable metadata trees**

Implement: Object Tree, Namespace Tree as real B+trees (Section 9). The largest single rewrite in
this RFC; every existing read/write function's `objectRoot + index` addressing assumption changes.
Depends on Phases H and I (the Allocation Tree must exist to back a growable tree's own node
allocation).

**Phase K -- Extents and sparse files**

Implement: Extent Tree (Section 11.1); variable file size (11.2); sparse ranges (11.3); offset-based
partial I/O (11.4); on-disk reflink sharing with persisted refcounts (11.5). Depends on Phase J (the
Extent Tree is itself a B+tree) and Phase I (extent allocation/reclamation uses the same
allocator).

**Phase L -- Persistent attributes**

Implement: Attribute Tree (Section 12). Depends on Phase J's B+tree infrastructure; otherwise
independent of Phase K and MAY be implemented in parallel with it.

**Phase M -- Full health and repair**

Implement: seven-state health model (13.1); persisted health record (13.2); extent-overlap repair
(14.1); allocation-tree reconciliation (14.2). Depends on Phases I and K (both repair classes
require the Allocation Tree and Extent Tree to exist).

------------------------------------------------------------------------

# 17. AI Implementation Guidance

## 17.1 Required boundaries

Every boundary RFC-0039 Section 79.1 already establishes remains in force unchanged (generic
`FileSystem` interfaces separate from ArcFS-specific format code; block-storage driver code outside
ArcFS; explicit serialization functions; checked integer arithmetic; OID identity separate from
runtime handles; namespace relationships separate from object records; allocation accounting
derived from committed transactions; never mutate committed metadata in place).

## 17.2 No silent architectural substitution

RFC-0039 Section 79.2's list remains in force. This RFC adds one item specific to itself: an agent
MUST NOT implement Section 15's FMV2 format changes as an in-place mutation of FMV1 volumes,
presented as "migration," when Section 15.2 explicitly requires reformat-and-copy instead.

## 17.3 Mandatory acceptance evidence

RFC-0039 Section 79.4's evidence requirements apply unchanged, phase by phase, to every phase in
Section 16 above: tests added, tests executed under QEMU (not merely compiled), exact pass/fail
results, corruption/failure injections performed (Section 7.4 in particular requires this, the same
way RFC-0039 Phase C's crash-injection fixture proved the original commit protocol), documented
deviations, remaining unsafe assumptions. A phase report in the same style and rigor as
`.agents/reports/aps-arcfs-phase-a.md` through `aps-arcfs-phase-g.md` is expected for each phase in
Section 16.

## 17.4 Regression discipline

Every phase in Section 16 MUST be validated against the FULL existing RFC-0039 Phase A-G smoke-test
suite in addition to its own new fixtures -- not because those tests are expected to still pass
unchanged against FMV2 volumes (they are not; FMV1 and FMV2 are incompatible per Section 15.1), but
because an FMV1 compatibility mode (reading/writing FMV1 volumes with the pre-Phase-H code paths)
MUST remain available and MUST keep passing throughout, so that Section 15.2's reformat-and-copy
migration path has a working FMV1 reader to copy FROM. Do not delete or break FMV1 support as a
side effect of building FMV2.

## 17.5 Stop conditions

In addition to RFC-0039 Section 79.5's own list, an agent implementing this RFC MUST stop and
report rather than improvise if:

- STRING gains real substring/length/formatting operations on this compiler backend during this
  work -- that changes Section 12.2's and Section 4's own Non-Goal boundary around string-valued
  attributes, and the RFC's scope should be revisited before proceeding, not silently expanded.
- The B+tree rewrite in Phase J cannot preserve the existing `ArcFS.Resolve`/`ArcFS.CreateFile`/
  etc. public API signatures -- RFC-0039 Section 42's own native API shape is normative, and a
  breaking signature change is an architectural decision, not an implementation detail.
- Any Phase in Section 16 discovers that RFC-0039's own architecture (not just the reference
  implementation) is insufficient to express a required behavior -- that is grounds for a further
  RFC amendment, not a workaround.

------------------------------------------------------------------------

# 18. Testing Strategy

Each phase in Section 16 SHALL include, at minimum:

- **Structural tests**: every new/changed public entry point compiles cleanly at X86_64 codegen
  level (matching every prior phase's own structural check).
- **Real QEMU/OVMF proof**: the actual behavior under a real CPU, not merely a clean compile --
  matching every RFC-0039 Phase A-G fixture's own standard.
- **Determinism**: every new fixture run at least three times with identical results, matching
  established project practice.
- **Negative controls**: for any fixture asserting a safety property (crash-injection results,
  ReadOnlySafety enforcement, refcount-gated reclamation), a deliberately flipped assertion MUST be
  shown to fail loudly rather than silently pass -- matching the negative-control discipline used
  throughout RFC-0039 Phases C-G.
- **Crash injection** for Phase I specifically (Section 7.4), following RFC-0039 Phase C's own
  prepare-without-publish technique, extended to reclamation transactions.
- **Regression** against the full existing FMV1-path test suite per Section 17.4.

------------------------------------------------------------------------

# 19. Security Considerations

Sections 26 (Authority and Security Model), 73 (Fuzzing and Hostile Media), and 74 (Security Threat
Model) of RFC-0039 remain fully in force and are not amended by this RFC. Two additions specific to
this RFC's own new surface:

- The Allocation Tree (Section 7) becomes a new attack surface for a hostile/corrupt image: a
  crafted Allocation Tree claiming blocks are free when they are actually referenced by a reachable
  generation could cause the allocator to hand out and overwrite live data. Every allocation
  decision MUST cross-check the Allocation Tree's claim against actual reachability
  (`ArcFS.IsSectorReachable`, RFC-0039 Phase E, unchanged) before trusting a "free" marking from an
  UNTRUSTED image at mount time -- matching RFC-0039 Section 5.3's "Implementations MUST NOT guess
  silently."
- Shared Extent Refcounts (Section 5, 11.5) are a new integrity-critical field: an underflowed or
  corrupted refcount could cause a still-referenced extent to be freed and reused while a live
  generation still points at it. Refcount fields MUST be checksummed as part of their containing
  Extent Tree node (Section 9.1's own per-node checksum already covers this if refcounts live
  inside extent records, which they SHALL).

------------------------------------------------------------------------

# 20. Performance Considerations

RFC-0039 Section 60's performance expectations (O(log n) lookup, sequential I/O near backing-device
capability, bounded commit latency, low-cost snapshots, reflink creation independent of file size)
become achievable only after Phase J's real B+trees replace the reference implementation's flat
arrays and linear scans. RFC-0039 Phase B/D/E/F's own documented "O(n) fine at test scale" notes are
the specific debts Phase J-M pay down. This RFC does not add new performance requirements beyond
RFC-0039 Section 60's own already-normative targets; it is the path to meeting them.

------------------------------------------------------------------------

# 21. Compatibility

This RFC is compatible with RFC-0038 (Block Storage) without modification -- every phase continues
to consume the `RAMDisk`/`BlockDevice` contract exactly as RFC-0039 Phases B-G already do. This RFC
is NOT backward-compatible with RFC-0039 Phase A-G's own FMV1 on-disk format at the volume level
(Section 15); it IS compatible with RFC-0039's architecture, since every requirement in this RFC
closes a gap RFC-0039 itself already specified normatively.

------------------------------------------------------------------------

# 22. Open Questions

- Should Phase M's Health Record (13.2) live in a fixed reserved area near the superblock, or as
  its own tiny tree? This RFC recommends fixed (Section 13.2's own reasoning: low cardinality,
  bounded fields), but a future increment adding per-scrub history (not just "the last one") would
  need to revisit this.
- Section 14.1's extent-overlap resolution policy ("lower generation number wins") is a reasonable
  default but not the only defensible one; RFC-0039 Section 39 does not mandate a specific policy,
  and this RFC's choice should be revisited if real-world corruption patterns suggest otherwise.
- Whether Phase K's on-disk reflink sharing should extend to cross-snapshot deduplication (two
  files, never reflinked, that happen to contain identical bytes) is explicitly not addressed --
  that is content-addressed deduplication, an RFC-0039 Section 2 Non-Goal this RFC does not revisit.

------------------------------------------------------------------------

# 23. References

- RFC-0039: ArcologyFS (ArcFS) -- the architecture this RFC completes.
- RFC-0038: APS Block Storage and Filesystem Provider Substrate -- the `BlockDevice` contract every
  phase in this RFC continues to consume unchanged.
- `.agents/reports/aps-arcfs-phase-a.md` through `aps-arcfs-phase-g.md` -- the implementation record
  every requirement in this RFC traces back to.

------------------------------------------------------------------------

# 24. Revision History

| Version | Date       | Summary                                                        |
|---------|------------|------------------------------------------------------------------|
| 0.1     | 2026-08-19 | Initial draft. Written directly from RFC-0039 Phases A-G's own accumulated, dated scope-reduction notes -- every requirement in Sections 7-14 traces to a specific named gap in a specific phase report, not a speculative addition. Defines Format Major Version 2 and a six-phase (H-M) implementation plan. Explicitly excludes RFC-0039 Phase G's remaining namespace-attachment/system-volume/recovery-environment/graphical-tooling items, matching `.agents/reports/aps-arcfs-phase-g.md`'s own finding that their dependencies do not exist in this repository. Status: Draft; no implementation phase has begun. |
| 0.2     | 2026-08-20 | Phase H implemented and QEMU-proven: FMV2 superblock ring (4 redundant copies, Section 8.1/8.2), generation history (`previousCheckpointSector`, Section 8.3), CRC-32C checksums replacing the additive-sum placeholder for every record type (Section 8.4), Format Major Version 2 activation (magic `ARCFSB02`, Section 15.1). 128-bit OIDs (Section 10) deliberately NOT delivered this phase -- named, sized (~150 call sites plus a downstream RFC-0041 consumer), and sequenced as the immediate next increment rather than bundled in under-tested; see the phase report for the full reasoning. Real proof under QEMU/OVMF: a fresh FMV2 format/mount, a real generation-2 commit whose checkpoint correctly links back through `previousCheckpointSector`, CRC-32C genuinely catching a flipped byte and failing the mount closed, and the ring correctly tolerating two of its four copies being reverted to stale content. Found and fixed one real consequence of the format-shape change (`ArcFS.ReclaimableSectorCount`'s reachability scan start point) and one real regression in a smoke test's own structural-check entry list (function renames, not the frozen fixture it later runs). Every FMV1 fixture (RFC-0039 Phases A-G, system-volume, RFC-0041's system-namespace, recovery-environment, graphical-storage-tooling) required zero changes -- each inlines its own frozen snapshot of the pre-Phase-H stdlib, matching Section 15.3. See `.agents/reports/aps-arcfs-phase-h.md`. |
| 0.3     | 2026-08-20 | Section 10's 128-bit OID identity and allocation implemented and QEMU-proven, scoped narrower than this section's literal text: the object table's own canonical row and the on-disk object record are genuinely 128-bit, backed by a real carry-correct allocation counter, but namespace/handle *reference* fields and the public `ArcFS.`-prefixed calling surface stay 64-bit this increment (no tuple-return or proven default-parameter mechanism exists on this backend to widen ~150 call sites' signatures without touching every caller, including RFC-0041's `system_namespace_policy.abas`) -- full reasoning, and the one disclosed resulting limitation (a low-half collision only reachable after a counter wrap this allocator cannot produce in practice), in the dedicated report. Found and fixed two real bugs while designing the proof: a wrap could hand out OID 0, colliding with this codebase's own "not found/failure" sentinel used everywhere since Phase A; and `ArcFS.MountImage`'s own independent "resume past the highest OID seen" computation needed the identical fix, which the fixture's own first real QEMU attempt caught failing before it was applied there too. See `.agents/reports/aps-arcfs-oid-128bit.md`. |
| 0.4     | 2026-08-20 | Phase I (Section 7, real free-space allocation and reclamation) implemented and QEMU-proven, scoped to file DATA EXTENTS specifically -- metadata (object/namespace/checkpoint/bitmap records) stays contiguous-append pending Phase J's own B+tree rewrite (Section 9.4 already names that as Phase J's job, and Phase J itself depends on this phase's Allocation Bitmap existing first). A real byte-per-block Allocation Bitmap, checksummed and referenced from a new checkpoint field, backs `ArcFSAllocateDataExtent`'s real reuse-before-growth search; `ArcFS.ReclaimGeneration()` mutates only the in-memory bitmap and relies entirely on the existing commit protocol for durability, satisfying Section 7.3's "no separate, non-transactional reclamation code path" by construction; Sections 7.4 and 7.5 fall out of the same construction without needing special-casing (full reasoning in the report). Found and fixed a real, necessary consequence before it could corrupt anything: `ArcFS.IsSectorReachable`'s old contiguous-range check would have missed a reused extent living below its own generation's metadata span, incorrectly permitting a live sector to be reclaimed -- fixed with a new `ArcFSGenerationContainsSector` that additionally scans a generation's own FILE records. Found and fixed two real bugs designing the proof: `ArcFSReadSuperblockRing`'s own embedded checkpoint-checksum check was missed when this phase's checkpoint-record widening moved the checksum offset; and a test-design flaw (not an implementation bug) that took real tracing to tell apart from one, since the same commit that reclaims a sector also legitimately reuses it right back for the only file the fixture ever creates. Real QEMU/OVMF proof: a superseded generation's own extent becomes genuinely unreachable, crash-injection leaves an unpublished reclamation invisible after remount, a published reclamation actually frees it, and the next file created afterward gets a real, dynamically-captured extent sector below the pre-reclaim high-water mark with its content intact after three generations. See `.agents/reports/aps-arcfs-phase-i.md`. |
| 0.5     | 2026-08-20 | Phase J (Section 9, growable metadata trees) implemented and QEMU-proven, scoped to the OBJECT TREE only -- Namespace and Attribute Trees stay flat arrays this increment, replicating this phase's now-proven pattern onto them is the natural next step, not started here. A real on-disk B+tree (4096-byte self-describing checksummed nodes, RFC-0040 Section 9.1) is BULK-BUILT fresh from the live key set every commit rather than incrementally split/merged (a deliberate, explained departure from Section 9.3's literal framing: this whole reference implementation already rewrites every tree fresh each generation, so there is no existing on-disk node to incrementally mutate, and bulk construction makes merge/rebalance moot by construction rather than merely unimplemented). Node occupancy deliberately small (4) so a handful of real objects, well under the in-memory table's own 64-row ceiling, provably forces a real multi-node, multi-level tree under QEMU. Tree node allocation reuses Phase I's own data-extent allocator unchanged (a node and an extent are the same size). Recursion was confirmed working under real QEMU execution (a factorial function) before this phase's own recursive tree-walk functions were written -- never previously proven in this project, and the whole design depends on it. Found and fixed two real, necessary consequences before either could corrupt anything or silently under-report defects: reachability had to be widened (again, one layer up from Phase I's own data-extent fix) so a live tree node scattered by reuse is never mistaken for free space; and the scrubber needed a SEPARATE, defect-tolerant tree walk, since a naive checksum-fail-closed walk (correct for mounting) would abort the whole scan on the first corrupt node instead of counting one defect and continuing, the same tolerance the old flat array always had. Real QEMU/OVMF proof: 6 files plus root force a real 2-leaf-plus-internal-root tree; every file resolves with correct content after a real commit and remount; the new tree-walking scrub reports Healthy; reachability correctly follows an ENTIRE superseded tree (root, leaves, and internal node), not just one sector; real reclamation frees superseded tree structure itself, not just file data, with every file intact afterward. Passed on the first real QEMU attempt; a negative control confirmed the fixture's own checks are real. Found and fixed one real regression before it shipped: a sibling smoke test's own structural-check entry list still named two functions this phase genuinely removed. See `.agents/reports/aps-arcfs-phase-j.md`. |
| 0.6     | 2026-08-20 | Phase K (Section 11, extent-based variable-length and sparse files) implemented and QEMU-proven, scoped to REAL PER-CHUNK EXTENTS and GENUINE SPARSE HOLES only -- not a true variable-count Extent Tree (11.1: a fixed-count 4-chunk extent list instead, following Phase J's own "bulk-rebuild every commit, no incremental split/merge" discipline rather than inventing a second B+tree whose insert/delete this project's CoW model would never exercise), not offset-based partial I/O (11.4: `ArcFS.HandleWrite`/`HandleRead` still always start at byte 0, a pre-existing Phase A scope reduction), and not on-disk reflink sharing (11.5: two OIDs sharing a data-pool slot still each get their own full duplicate set of real chunk extents on disk while undiverged). A file's leaf-entry field now names a self-checksummed extent-list sector (up to 4 chunk sectors, 0 = hole) instead of one fixed 4096-byte extent; the per-slot stride widens to `ArcFSSlotSizeBytes()` (16384 bytes) throughout `HandleWrite`/`HandleRead`/`Resize`/`EnsurePrivateSlot`; `Resize` growth zero-fills in memory but never marks the grown range touched, so it becomes a real sparse hole (not a zero-filled extent) at the next commit; reachability (`ArcFSExtentListContainsSector`) and Pass 5's overlap detection (`ArcFSExtentListsOverlap`) both widen from one 8-sector range per file to every real chunk. Found and fixed one real design bug BEFORE any fixture ran: an early draft indexed the new chunk-touched bitmap by object row instead of data-pool slot, which would have given a freshly reflinked clone its own stale, disconnected touched-state instead of correctly inheriting the source's real one -- caught by checking the bitmap's indexing against every other per-slot table in the file, fixed before it ever executed. Found and fixed one real fixture-design bug the first QEMU run itself caught: the fixture assumed a short write through a reflinked clone would leave the rest of the file's prior content visible via `HandleRead`, but `HandleWrite`'s real (pre-existing, documented) contract replaces the file from byte 0 and shrinks `size` to exactly what was written -- the untouched-by-this-write chunks stay real and correctly persisted but become genuinely unobservable through the size-bounded public API, so the fixture was corrected to verify them directly against the post-mount data pool instead. Real QEMU/OVMF proof: a 100-byte partial write round-trips; resizing to the full 16384-byte width with no new write reads as zero both before and after a real commit/remount (the core sparse-hole proof); a real 16000-byte write spanning all 4 chunks round-trips exactly; a real reclaim afterward leaves it intact; a reflink shares one slot between two OIDs; a small write through the clone forces private-slot divergence, verified through the public API (source untouched, clone's visible bytes correct) AND directly against the post-mount data pool (the clone's other three chunks still touched and byte-identical to the source, proving the touched-bitmap copy, not just the byte copy, is correct); a final scrub/reclaim leaves both files -- including the size-invisible-but-real chunks -- exactly intact. Passed after the fixture's own design bug was fixed; a negative control confirmed the fixture's checks are real; deterministic across 3 repeated runs. No stale structural-check entries found in any other smoke test this phase. See `.agents/reports/aps-arcfs-phase-k.md`. |
| 0.7     | 2026-08-20 | Phase J finished (Section 9): the Namespace Tree, replicating the Object Tree's own proven pattern onto the second of the two tables Phase J originally left as a flat array (Attribute Tree remains flat, deferred to Phase L). A namespace leaf entry (56 bytes: parentOid+name+nameLength+childOid) uses its own leaf writer/walker (`ArcFSWriteOneNamespaceLeafNode`, `ArcFSNamespaceTreeCollectAllEntriesInto`/`Tolerant`, `ArcFSNamespaceTreeContainsSector`), but internal nodes are NOT duplicated -- `ArcFSWriteOneInternalNode`'s own (key, childSector) pair layout was already fully generic, so both trees share it and its fanout constant unchanged. A real, load-bearing difference from the Object Tree that this increment had to handle for the first time: the Namespace Tree can be GENUINELY EMPTY (a freshly formatted volume has zero namespace entries -- root itself has no namespace record, it IS the implicit root), so `namespaceRoot = 0` is now the explicit sentinel for "no entries," checked before ever treating it as a sector, everywhere the checkpoint's namespace fields are consumed. `ArcFSGenerationMetadataRangeContains`'s own contiguous-range reachability check narrowed further (Phase J had already pulled the Object Tree out of it; this increment pulls the Namespace Tree out too), down to just [allocationBitmapSector, checkpointSector] -- the one remaining genuinely contiguous pair -- with `ArcFSGenerationContainsSector` now checking both trees' own `ContainsSector` functions explicitly. `ArcFSScanGeneration`'s Pass 2 (namespace validation) and Pass 3 (namespace reachability relaxation) were rewritten to consume a real tree walk instead of a flat per-record scan, while Pass 3's own relaxation algorithm and the 16-byte-per-entry scratch namespace array it reads stayed completely unchanged -- only where that array's contents come from changed. `ArcFSSnapshotFindNamespaceRecord` was rewritten the same way `ArcFSSnapshotFindObjectRecord` already was in Phase J itself (walk the whole tree, linear-scan the collected entries). `ArcFSWriteNamespaceRecord` (the old flat per-record writer) was removed, matching Phase J's own precedent of deleting genuinely-superseded functions rather than leaving dead code. Real QEMU/OVMF proof (`aps-arcfs-namespace-tree.abas`): mounts a volume whose namespace is genuinely empty (the FIRST real exercise of the new `namespaceRoot=0` short-circuit, a case no fixture in this chain had ever hit before since none previously used a tree-based namespace loader); 6 files under root force a real multi-leaf, multi-level Namespace Tree; every file resolves with correct content after a real commit and remount; the scrub reports Healthy through the entirely new tree-sourced Pass 2/3 path; committing again with no further change supersedes the WHOLE namespace tree, and reachability correctly reports the old namespace root unreachable; real reclamation frees the superseded tree with every file still intact afterward; and orphaning one file (removing only its namespace entry, the object staying alive -- Phase F's own established mechanism) correctly flips the scrub to Degraded, proving orphan detection still fires when its input comes from a real tree walk. Passed on the first real QEMU attempt; a negative control (flipping the unreachability assertion) confirmed real `FAIL 1`; deterministic across 3 repeated runs. Full suite confirmed clean (every prior ArcFS/APS test re-run unchanged alongside the new `arcfs_namespace_tree_smoke` test; no stale structural-check entries found elsewhere). See `.agents/reports/aps-arcfs-phase-j.md`'s own dated Addendum. |
| 0.8     | 2026-08-20 | Phase K, offset-based partial I/O (Section 11.4), closing the second of Phase K's own three originally-deferred items (variable-count Extent Tree and on-disk reflink sharing remain deferred). `ArcFS.HandleWrite`/`ArcFS.HandleRead` gain a fourth `offset` parameter and now behave like real `pwrite(2)`/`pread(2)`: a write no longer unconditionally replaces the file from byte 0 -- `size` becomes `max(oldSize, offset+written)`, never shrinking on its own (only `ArcFS.Resize` does that deliberately); a write whose offset starts past the current size zero-fills the gap in memory (mirroring `ArcFS.Resize`'s own growth path) without marking it touched, so it becomes a real sparse hole at the next commit, the same construction Section 11.3 already established, now reachable through a write's own offset+growth as well as through an explicit `Resize`; `HandleRead` clamps to `[offset, size)` and returns 0 at or past EOF. This is a genuine RETROACTIVE FIX, not just an addition: Phase K's own report documents a fixture-design bug caused directly by this gap (a short write through a reflinked clone unexpectedly hid the rest of the file behind a shrunk `size`) -- that exact class of surprise is now closed for real. Zero internal callers of either function existed anywhere in `arcfs_policy.abas`, so the signature change had zero blast radius within the live stdlib; every frozen fixture in this chain is unaffected as usual. Real QEMU/OVMF proof (`aps-arcfs-offset-io.abas`): a mid-file write (offset 40, within a 100-byte file) patches its own range while `size` stays 100 and the surrounding content is genuinely preserved and independently re-verifiable via an offset-based read of just the patched slice; a write at offset 9000 (far past a size of 100) grows the file to 9030 and creates a REAL multi-chunk sparse gap -- chunk 0's own tail, ALL of chunk 1, and the start of chunk 2 -- reading as zero and leaving chunk 1 genuinely untouched (verified directly against the touched-chunk bitmap), both in-session and after a real commit/remount; health scrub and a real reclaim afterward leave everything intact. Passed on the first real QEMU attempt; a negative control confirmed real `FAIL 1`; deterministic across 3 repeated runs. Full suite re-run clean alongside the new `arcfs_offset_io_smoke` test; confirmed directly (not assumed) that the two OTHER smoke tests whose own structural-check entry lists name `ArcFS.HandleWrite`/`HandleRead` still pass against the new signature. See `.agents/reports/aps-arcfs-phase-k-offset-io.md`. |
| 0.9     | 2026-08-21 | Phase L (Section 12, Persistent Typed Attributes), the third and final tree Section 9 originally named -- Object and Namespace Trees were real B+trees since Phase J; this delivers the Attribute Tree the same way, closing the set. Scoped to ONE typed attribute slot per object, now DURABLE -- not Section 12.1's own literal `(OID, attribute namespace, attribute name/ID)` multi-attribute-per-object model, following this project's own "prove the hardest load-bearing case once, defer full generality" discipline (the same reasoning Phase K's fixed-count extent list and the Namespace Tree's internal-node reuse already established). A real on-disk Attribute Tree (4096-byte self-describing checksummed nodes, bulk-built fresh every commit, reusing `ArcFSWriteOneInternalNode` and `ArcFSObjectTreeNodeCapacity()` UNCHANGED for internal fanout -- generic infrastructure belonging to no particular tree) replaces Phase D's own in-memory-only single slot. Real typed values (unsigned integer, signed integer stored as raw bits since this backend has no native signed type, boolean, timestamp, and UUID/OID using the same high/low pair convention this file's own OIDs already use) via a `type` field that doubles as the presence flag. Real durability: `ArcFS.SetAttribute` mutations commit through the exact same Prepare/Publish protocol as every other tree mutation and survive a real remount, closing RFC-0039 Phase D's own explicitly documented gap ("a value set here does NOT survive `ArcFS.MountImage()`") for real, RFC-0040 Section 12.3's own literal requirement. Checkpoint record gained `attributeTreeRoot`/`attributeTreeCount` (RFC-0039 Section 15.1's own long-omitted required fields); payload grew from 72 to 88 bytes, checksum shifted from offset 72 to 88 -- the fourth such move this record has needed as fields grew (56->64->72->88). Reachability widened a third time: `ArcFSAttributeTreeContainsSector` joins the Object and Namespace Trees' own inside `ArcFSGenerationContainsSector`, the same widening discipline Phase I/J/Namespace-Tree-addendum each already established once. Real QEMU/OVMF proof (`aps-arcfs-attribute-tree.abas`): 5 files get distinctly typed attributes (unsigned=12345, signed=99999, boolean=1, UUID/OID=(777777,888888) using BOTH halves), one deliberately left with none at all, forcing a real multi-node Attribute Tree; every one -- including the genuinely-absent case -- round-trips through a real commit and remount; health scrub reports Healthy through the entirely new tree-sourced path; committing again with no further change supersedes the WHOLE Attribute Tree and reachability correctly reports the old root unreachable; real reclamation leaves everything intact; updating an existing attribute (changing BOTH type and value) persists correctly with every other object's attribute untouched. Passed on the first real QEMU attempt; negative control confirmed real `FAIL 1`; deterministic across 3 repeated runs. Confirmed directly (not assumed) that the one other smoke test naming the attribute functions in its own structural-check list (`arcfs_phase_d_smoke`) still passes against the new signature, its frozen fixture still correctly proving the OLD non-durable behavior as a historical record. Documented, honestly-named scope reductions: no multi-attribute-per-object model, no UTF-8 string/binary blob values (RFC-0040 Section 12.2's own stated expectation), and the Attribute Tree is not yet covered by the scrubber's own orphan/reachability analysis (not reachable through any real write path in this implementation, so a real but currently unexercised gap). See `.agents/reports/aps-arcfs-phase-l.md`. |
| 1.0     | 2026-08-21 | Phase M (Sections 13-14, Full Health Model and Expanded Repair Coverage), the last phase RFC-0040's own implementation plan named -- all six phases (H-M) are now implemented and QEMU-proven, Status stays Draft regardless, matching RFC-0039's own precedent (see this RFC's own Status line for the full reasoning). `ArcFS.GetHealthState` now reports six of RFC-0039 Section 36's seven states directly (Healthy, Degraded, ReadOnlySafety, NeedsOfflineCheck, Corrupt, Unavailable), each derived from something this implementation can genuinely observe; the seventh, NeedsScrub, is reachable through a new, separate `ArcFS.GetHealthStateCached` (which reads the persisted Health Record instead of always scrubbing fresh, since `GetHealthState` itself preserves its own pre-existing "always accurate, never stale" contract unchanged and so can never itself report "hasn't been scrubbed"). `ArcFSSelectRingCheckpoint`, a new side-effect-free extraction of `ArcFSReadSuperblockRing`'s own ring-selection scan, is now shared by mounting, `ArcFS.ScrubVolume` (widened from its own documented "ring slot 0 only" scope reduction to real ring-wide selection), and `ArcFS.GetHealthState`; a new `ArcFSRingIsConsistent` detects a ring copy that VALIDATES but disagrees with the winning generation -- a real anomaly distinct from Section 8.2's own already-tolerated "stale-and-unreadable copy self-heals silently" case -- and is what makes NeedsOfflineCheck genuinely reachable. A persisted Health Record (Section 13.2): one small, PERMANENT sector (6, reserved alongside the ring at format time) holding last scrub generation/result, last repair generation/summary, and cumulative checksum/I-O error counters, updated non-transactionally by `ArcFS.ScrubVolume` and transactionally by `ArcFS.RepairCommit`. No further checkpoint field growth was needed this phase (Phase L's own already covers the last such change). Extent-overlap repair (Section 14.1): `ArcFS.RepairResolveExtentOverlaps`, backed by a new `ArcFSFindExtentOverlaps` -- DEVIATES from the RFC's own literal "lower generation number is authoritative" policy (no per-entry generation number exists anywhere in this format's leaf entries) to lower-OID-authoritative instead, a stated, deliberate substitution; the offending object is cleared to an empty file, not deleted, matching Section 39's own "recovery SHOULD preserve it... with provenance metadata." Allocation-bitmap reconciliation (Section 14.2): `ArcFS.RepairReconcileAllocationBitmap`, built on the already-authoritative `ArcFS.IsSectorReachable` (already covering every reachable generation including snapshots), corrects the bitmap CONSERVATIVELY -- only ever marking a wrongly-free sector allocated, never the reverse, matching the RFC's own literal "more things marked allocated, never fewer." A REAL DESIGN CONFLICT found and fixed while building the fixture: the first attempt to prove NeedsOfflineCheck by corrupting the active checkpoint's own bytes instead produced Unavailable, because `ArcFSSelectRingCheckpoint` (inherited unchanged from the pre-existing ring-selection logic) already re-verifies a candidate checkpoint's own checksum as part of RING SELECTION itself -- meaning "a selected checkpoint later fails its own peek during the scan" is architecturally UNREACHABLE through any real corruption this implementation's own ring-selection discipline can produce, traced to its root cause and given the genuinely-independent `ArcFSRingIsConsistent` trigger instead (the original checkpoint-unreadable tracking was kept as a real defensive safeguard, not removed). A REAL, LONG-STANDING GAP CLOSED AS A SIDE EFFECT: Phase F's own report states plainly that no fixture in this project had ever constructed a real overlapping-extent scenario under QEMU -- this phase's own fixture had to build one directly (corrupting one file's on-disk extent list to claim another's real chunk sector) to prove its new repair function, closing that gap for the first time. Real QEMU/OVMF proof (`aps-arcfs-health-model.abas`, the largest fixture in this chain): NeedsScrub on a never-scrubbed volume then Healthy immediately after; a real extent-overlap constructed, detected as Degraded, and repaired, leaving the lower-OID file intact and the higher-OID file genuinely empty; allocation-bitmap reconciliation correcting a deliberately wrongly-cleared bit for a still-reachable sector; orphaning a file detected as Degraded then (via a real safe mount) specifically as ReadOnlySafety, with in-memory mutation still allowed but the COMMIT genuinely refused, repaired and restored to read-write Healthy; a real ring-copy inconsistency (one slot redirected to generation 1's own still-intact original checkpoint) detected as NeedsOfflineCheck and clearing once restored; a real corrupted tree-node sector detected as Corrupt; a fully zeroed ring detected as Unavailable. Passed after fixing the fixture's own design conflict (not an ArcFS defect); negative control confirmed real `FAIL 1`; deterministic across 3 repeated runs. Full suite re-run clean alongside the new `arcfs_health_model_smoke` test; confirmed directly that `arcfs_phase_f_smoke` (the smoke test most heavily exercising the touched health/repair surface) still passes in full. See `.agents/reports/aps-arcfs-phase-m.md`. |
| 1.1     | 2026-08-21 | Section 11's remaining two named items -- 11.1 (a true variable-count Extent Tree) and 11.5 (on-disk reflink sharing) -- delivered together, since keying the new Extent Tree by `dataSlot` instead of `OID` satisfies both at once: two reflinked-but-undiverged OIDs already share one dataSlot in memory (RFC-0039 Phase D), so building the tree ONCE PER UNIQUE dataSlot means a shared, undiverged pair contributes exactly one set of real chunk extents to disk, not two, by direct structural consequence rather than a separate mechanism. A real B+tree replaces Phase K's fixed 512-byte 4-slot extent LIST, same node format every other tree already uses, bulk-rebuilt fresh every commit, reusing `ArcFSWriteOneInternalNode` unchanged for internal levels. `ArcFSEnsurePrivateSlot` (RFC-0039 Phase D's own COW gate) needed ZERO changes -- a diverging write already gets a new dataSlot, which the next commit's tree build naturally treats as a distinct key, producing genuinely separate extents -- precisely RFC-0040's own "generalized... without re-deriving it" framing. DEVIATES from Section 11.5's literal "Shared Extent Refcount" field: none is persisted: reachability already answers "is this sector referenced by anything still reachable" via the existing live graph scan, correct by construction with no separate counter that could drift out of sync -- the same judgment Phase M's own lower-OID deviation already made for this codebase. Checkpoint gained `extentTreeRoot`/`extentTreeCount` (payload 88->104 bytes, the fifth such checksum-offset shift in this record's history). Reachability, snapshot reads, and extent-overlap detection all widened to the new tree; the real extent-overlap REPAIR action itself needed no changes, only the detection primitive it consumes. Named scope reduction: sharing does not yet survive a remount (`ArcFSPopulateObjectRow` still resets a row's live dataSlot to its own table index at mount time, using the committed value only as a one-time lookup key) -- content correctness is unaffected either way; this increment's own proof measures sharing via the durable on-disk `extentTreeCount` within a session instead. A REAL BUG found by the fixture itself: the very first `ArcFS.MountImage()` after `ArcFS.FormatVolume()` failed on the first QEMU attempt -- traced to `ArcFSSelectRingCheckpoint`'s own checkpoint-checksum check still reading the OLD offset (88) after the record grew to 104, missed by an earlier `replace_all` edit because a second, deeply-nested occurrence of the identical-looking check had different leading whitespace and didn't match. Real QEMU/OVMF proof (`aps-arcfs-extent-tree-reflink.abas`): file A gets a real 8000-byte write (2 real chunks); reflinking B and committing produces EXACTLY 2 real Extent Tree entries (not 4), read directly from the durable `extentTreeCount` field after a real remount -- the direct on-disk-sharing proof -- with `ArcFS.GetHealthState()` confirming no false-positive on the legitimate sharing; a 100-byte patch through B alone forces divergence, and `extentTreeCount` genuinely grows to 4 after a real commit+remount -- the divergence-produces-real-separation proof; two more files (C, D) push the total to 8 entries, past `ArcFSExtentTreeNodeCapacity()` (4), FORCING a real multi-node, multi-level tree, with all four files' content independently confirmed correct after a real remount; a real `ArcFS.ReclaimGeneration()` pass afterward leaves everything intact, proving Extent Tree node reachability holds. Passed after fixing the checksum-offset bug (a real bug, not a fixture-design issue); negative control (flipping the expected post-reflink count from 2 to 4) confirmed real `FAIL 1`; deterministic across 3 repeated runs. One real regression found+fixed before shipping: `arcfs_phase_k_smoke`'s own structural-check entry list still named 7 functions this increment genuinely removed (not renamed) -- the same lesson Phase H/J's own reports already documented. Full suite re-run clean alongside the new `arcfs_extent_tree_reflink_smoke` test. Section 11 (11.1-11.5) is now fully addressed across Phase K and this increment together. See `.agents/reports/aps-arcfs-extent-tree-reflink.md`. |
| 1.2     | 2026-08-21 | Closes the one scope reduction rev 1.1 named explicitly: reflink sharing now SURVIVES a real remount, not just a single continuous session. New `ArcFSMountSlotOwnerAddress` (a mount-time-only table, reset once per `ArcFSLoadObjects` call, direct-indexed by committed `dataSlot`) tracks which row has claimed the real live slot for each committed identity this mount; `ArcFSPopulateObjectRow` now makes the FIRST row referencing a given committed `dataSlot` its real owner (loads chunks, refcount 1) and every LATER row sharing it point at that SAME owner (refcount incremented, no redundant load) instead of unconditionally resetting every row's live `dataSlot` to its own table index. Needed no changes anywhere else in the file: `ArcFSSlotRefCountAddress`/`ArcFSEnsurePrivateSlot`/`ArcFSAllocateDataSlot` never actually assumed `dataSlot == row index`, only that a row's own `dataSlot` field is authoritative -- mount time was the one place that discarded a genuine committed relationship, and fixing exactly that one point was sufficient. `ArcFSNextDataSlotAddress`'s own resume point stays correct unchanged, since every live `dataSlot` assigned during mount (owner or sharer) is always some row's own index, strictly less than `loadedObjectCount`. Real QEMU/OVMF proof (`aps-arcfs-remount-reflink-sharing.abas`): after a real reflink+commit+remount, A's and B's own LIVE `dataSlot` fields (read directly via `ArcFSFindObjectRow`, not inferred from content) are confirmed IDENTICAL, with the shared slot's own refcount reading exactly 2; a SECOND consecutive remount with no changes still shares; a write through B alone forces real divergence via the unchanged `ArcFSEnsurePrivateSlot`, and a further remount correctly shows A and B on DIFFERENT live slots (each refcount 1) -- not silently re-merged -- with content independently correct for both; a real reclaim and one more remount afterward leaves both files intact. Passed on the first real QEMU attempt; negative control (flipping the core sharing-survives-remount assertion) confirmed real `FAIL 1`; deterministic across 3 repeated runs. New smoke test `systems_arco_basic_arcfs_remount_reflink_sharing_smoke`; full suite re-run clean. See `.agents/reports/aps-arcfs-remount-reflink-sharing.md`. |
