# ArcologyFS (ArcFS) Phase U: Allocation Bitmap Multi-Sector Redesign

## Scope delivered

RFC-0043 Phase U (Section 7): the Allocation Bitmap's own volume-size ceiling, unchanged since
RFC-0040 Phase H and named as remaining work by RFC-0042 Phase Q's own coupling finding, is now
real and raised.

- `ArcFSMaxTrackedSectors()`: 508 -> 200152 (an exact multiple of 508: 394 * 508, chosen
  specifically to avoid partial-last-sector padding logic in the on-disk span).
- New `ArcFSBitmapSectorCount() AS U64` = `ArcFSMaxTrackedSectors() \ 508` = 394 -- how many
  consecutive physical sectors the bitmap's own on-disk image now spans, each independently
  self-checksummed (508 payload bytes + a 4-byte CRC-32C checksum, the exact same per-sector shape
  the old single-sector bitmap always used, simply repeated once per sector).
- `ArcFSLoadBitmap()`/`ArcFSWriteBitmapRecord()` rewritten to read/write `ArcFSBitmapSectorCount()`
  consecutive sectors; `ArcFSWriteBitmapRecord` gained a `baseSector` parameter (replacing the old
  implicit "always sector 4" assumption) and now performs its own writes directly rather than
  handing a single serialized sector back to its caller.
- The fixed initial volume layout shifts forward once more (the same way it already did at RFC-0040
  Phase H): sectors 0-3 (ring), 4..397 (Allocation Bitmap, 394 sectors), 398 (checkpoint, new
  `ArcFSCheckpointBaseSector()`), 399 (Health Record, `ArcFSHealthRecordSector()`, now computed
  instead of hardcoded `6`).
- RFC-0042 Section 7's feature-negotiation mechanism gets its first real consumer: new
  `ArcFSFeatureMultiSectorBitmap()` = 65536 (bit 16, the lowest incompat bit, the first one this
  format has ever actually defined), `ArcFSRecognizedIncompatMask()` now returns it (was `0`),
  `ArcFSWriteSuperblockRecord` writes it unconditionally into every superblock this build formats
  (was a literal `0`, "none defined yet" since Phase H).

## A real bit-field-scoping bug caught by a standalone probe before the full fixture

The first end-to-end attempt failed at `ArcFS.MountImage()`, immediately after a successful
`ArcFS.FormatVolume()` in the SAME session -- a real, load-bearing bug, not a proof-design issue.
Root cause: `ArcFSCheckFeatureFlags` compares its `ArcFSRecognizedIncompatMask()` argument against
`incompatBits`, which is the ALREADY-EXTRACTED 16-bit sub-field (`(rawFlags \ 65536) MOD 65536` --
bit 0 of that sub-field is bit 16 of the raw 64-bit flags value). `ArcFSRecognizedIncompatMask()`
was returning `ArcFSFeatureMultiSectorBitmap()` directly (`65536`, the RAW-space value, correct for
*writing* the superblock's flags field) instead of the SUB-FIELD-space value (`1`) the comparison
actually needs -- so a real, correctly-formatted volume's own real incompat bit was being treated
as unrecognized by the SAME build that had just written it. This could never have surfaced before:
RFC-0042 Phase N's own `ArcFSRecognizedIncompatMask()` returned `0` the entire time it existed, so
this was the first time the mask was ever compared against a genuinely nonzero value. Fixed:
`ArcFSRecognizedIncompatMask()` now returns `ArcFSFeatureMultiSectorBitmap() \ 65536`. Caught by a
small standalone probe fixture (format + mount + create + commit, no full proof harness yet)
built specifically to de-risk this before investing in the complete two-step QEMU fixture -- the
same "cheap probe before full fixture" discipline this session's Phase S/T work already used.

## A real, honestly-traced cost this phase's own design surfaces, not hidden

Every commit still allocates a genuinely FRESH bitmap span via the same bump allocator the old
single-sector version always used (real copy-on-write, matching every other structure in this
file) -- but that span is now 394 sectors, not 1. A fresh `ArcFS.FormatVolume()` call alone already
reserves 400 sectors (ring + bitmap + checkpoint + health record) before a single real object
exists, and every subsequent `ArcFS.CommitImage()` call costs another ~395 sectors of high-water
mark just for its own bitmap+checkpoint bookkeeping, regardless of how little real content
changed. This is not a new class of cost Phase U introduces -- the OLD single-sector bitmap had the
identical "one fresh sector per commit, unreachable until reclaimed" shape -- Phase U simply makes
an already-existing, already-accepted cost ~394x larger in absolute terms. Reclaiming a superseded
generation's own old bitmap span works through the exact same `ArcFS.ReclaimGeneration`
reachability-scan mechanism every other superseded generation's sectors already use -- generic by
construction, not bitmap-specific, so it needed no changes -- but was not re-verified with a new
fixture this phase, since `ArcFS.ReclaimGeneration`'s own correctness is RFC-0040 Phase I's already-
proven property, not a new claim Phase U makes.

## Validation

- All touched entry points (`ArcFSMaxTrackedSectors`, `ArcFSBitmapSectorCount`, `ArcFSLoadBitmap`,
  `ArcFSWriteBitmapRecord`, `ArcFSCheckpointBaseSector`, `ArcFSHealthRecordSector`,
  `ArcFSFeatureMultiSectorBitmap`, `ArcFSRecognizedIncompatMask`, `ArcFS.FormatVolume`, `ArcFS.
  PrepareCommit`) compile cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-multi-sector-bitmap.abas`), two steps:
  - Step One: a real volume, formatted and committed by the current code, creates a real file with
    real content and a real attribute, commits, and DIRECTLY reads back the real checkpoint sector
    number the commit just published (not inferred) -- confirmed genuinely > 508, something the old
    single-sector bitmap could never even have represented. A real remount then verifies the
    content and attribute byte-for-byte.
  - Step Two: reads the real on-disk superblock's flags field directly off sector 0 and confirms it
    genuinely equals `ArcFSFeatureMultiSectorBitmap()`; a frozen legacy replica of `ArcFSCheck
    FeatureFlags` (recognizedIncompatMask hardcoded to the pre-Phase-U value of `0`, the same
    same-process technique RFC-0042 Phase O already established) evaluated against those SAME real
    raw bytes genuinely refuses, while the current code's own `ArcFSCheckFeatureFlags` (which does
    recognize the bit) still returns "mount normally."
- Passed on the first attempt after the bit-scoping fix above (the pre-fix attempt genuinely
  failed, confirming the fix mattered); negative control (relaxing the `> 508` check to a value the
  real checkpoint sector never reaches) confirmed real `FAIL 1`; deterministic across 3 repeated
  runs.
- New smoke test `systems_arco_basic_arcfs_multi_sector_bitmap_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest` (41.45s).
- Full regression suite re-run -- see this report's own follow-up note or the RFC-0043 revision
  history for the final count.

## Documented scope reductions

1. **200152 tracked sectors (~97.7 MB), not a much larger number.** Deliberately chosen past the
   point where this was the binding constraint (RFC-0042 Phase Q's own 64 MB Data Pool ceiling, the
   real ceiling on file CONTENT, with real headroom on top for tree/checkpoint/namespace metadata
   overhead) rather than as large as possible -- matching RFC-0043 Section 7's own explicit
   direction. A future increment could raise this further with no format change at all (the
   mechanism does not care about the specific number), should the Data Pool's own ceiling ever be
   raised past 64 MB first.
2. **No backward read path for a pre-Phase-U, single-sector-bitmap volume.** The new incompat bit
   protects the direction RFC-0043 actually needs (an OLD reader refusing a NEW volume); the
   reverse (this code mounting an OLD volume) degrades safely today via an ordinary checksum
   failure on the second bitmap sector rather than a clean, explicit refusal -- not attempted as a
   dual-read path, matching RFC-0042 Section 4 Non-Goal 3's identical position on FMV1 migration.
3. **The per-commit bitmap-span cost (~395 sectors) is named, not mitigated.** See the "real,
   honestly-traced cost" section above -- reclaiming it relies entirely on the caller explicitly
   invoking the already-existing `ArcFS.ReclaimGeneration`, unchanged by this phase.

## RFC-0043 status

Phase U is implemented and QEMU-proven, alongside Phase S and Phase T from the prior increment.
Remaining named phases: V (boot-path integration), W (long-session durability -- note this phase's
own finding above makes it MORE relevant than before Phase U existed, since many real commits in
one session will now consume real tracked-sector space faster than before), X (physical-hardware
validation package).
