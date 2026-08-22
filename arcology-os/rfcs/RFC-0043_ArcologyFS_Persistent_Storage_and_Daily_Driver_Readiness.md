# RFC-0043: ArcologyFS (ArcFS) Persistent Storage and Daily-Driver Readiness

**RFC Number:** RFC-0043\
**Title:** ArcologyFS (ArcFS) Persistent Storage and Daily-Driver Readiness\
**Status:** Draft (Phase V implemented and QEMU-proven as a proof-only increment -- investigation (per Section 8.2's own mandate) found that neither `ArcFS.ActivateSystemVolume()` (RFC-0039 Phase G's real boot policy) nor RFC-0041's own System Namespace attachment/delegation had ever been proven against a real persistent block device or across a real process boundary (both their own existing fixtures use RAMDisk exclusively), while UEFI's own FAT-boot-medium loading needed no new proof at all (inherent to every fixture in this project already). Closed with a genuine two-process cross-boot fixture pair mirroring Phase T's own technique: a writer formats/commits real content to a real disk; a separate reader process calls `ArcFS.ActivateSystemVolume()` (not plain `MountImage`) against that same real disk, attaches the activated volume into a fresh System Namespace, and resolves a path that delegates across the attachment boundary into the real content, confirmed to reach the same OID a direct `ArcFS.Lookup` would. No ArcFS or Namespace code changed -- a real, honest "the existing mechanism already suffices, once actually exercised together" outcome. See `.agents/reports/aps-arcfs-phase-v.md`. Phase U implemented and QEMU-proven -- the Allocation Bitmap's own volume-size ceiling raised from 508 to 200152 tracked sectors (~97.7 MB, an exact multiple of 508 chosen to avoid partial-sector padding, deliberately past RFC-0042 Phase Q's own 64 MB Data Pool ceiling rather than as large as possible), the bitmap's on-disk image now spanning `ArcFSBitmapSectorCount()`=394 independently self-checksummed sectors instead of one, and RFC-0042 Section 7's feature-negotiation mechanism getting its first real consumer (`ArcFSFeatureMultiSectorBitmap()`, a real incompat bit every volume this build formats now genuinely carries). A real bit-field-scoping bug (`ArcFSRecognizedIncompatMask()` compared the wrong bit-space against the extracted sub-field, causing a real volume to fail to mount immediately after formatting it) was caught by a standalone probe before the full proof fixture was built, and fixed. See `.agents/reports/aps-arcfs-phase-u.md`. Phases S and T implemented and QEMU-proven, delivered together -- Phase S's real, selectable block-device dispatch (`ArcFS.SelectBlockDevice`, mirroring `FAT32.SelectBlockDevice` verbatim; all 37 real `RAMDisk.*` call sites in `arcfs_policy.abas` migrated to dispatch wrappers, default provider unchanged so every existing fixture needed zero changes), and Phase T's real cross-boot proof: two genuinely separate `qemu-system-x86_64` processes sharing nothing but a real disk image file -- a writer that formats/creates/commits to a real attached disk and confirms RAMDisk's own memory stayed untouched, and a separate reader process that mounts the same disk image and reads the content/attribute back byte-for-byte against literal constants in its own source, with an independent host-side check that the disk image file itself carries the real ArcFS superblock magic after the writer fully exited. A real, useful discovery made before fixture design: ArcFS's own full RFC-0042 Phase Q address range works correctly under plain UEFI Boot Services identity mapping with no custom page-table/`ExitBootServices` transition at all, confirmed via a standalone probe -- both Phase T fixtures use this simpler shape instead of the `aps-exception-entry` family's own heavier harness. See `.agents/reports/aps-arcfs-phase-s-t.md`. Remaining: Phase U (Allocation Bitmap multi-sector redesign), Phase V (boot-path integration), Phase W (long-session durability), Phase X (physical-hardware validation package).)

**Category:** Storage / Filesystem Architecture

**Authors:** Arcology Project\
**Created:** 2026-08-21\
**Last Updated:** 2026-08-21\
**Supersedes:** None\
**Superseded By:** None

**Related Architecture:** Arcology Object Architecture; Polymorphic Substrate (APS)\
**Related RFCs:** RFC-0006 (Physical Hardware Bring-Up / firmware quirk handling), RFC-0038 (APS
Block Storage and Filesystem Provider Substrate), RFC-0039 (ArcologyFS / ArcFS), RFC-0040
(ArcologyFS Production Hardening), RFC-0041 (Arcology System Namespace), RFC-0042 (ArcFS Format
Extensibility and Production Scale -- this RFC assumes RFC-0042's five phases (N-R) are complete)

------------------------------------------------------------------------

# 1. Executive Summary

RFC-0039/0040/0042 built a real, QEMU-proven filesystem: copy-on-write commits, checksummed
self-describing B+trees, 128-bit OIDs, reflink sharing, a 7-state health model with repair and
scrub, feature negotiation, and now (RFC-0042 Phase R) variable-length attribute values. Every one
of those proofs shares one property this RFC exists to name and close: **every single ArcFS
fixture in this project's history, without exception, has stored its data in `RAMDisk.*` -- a
fixed range of plain memory the QEMU/OVMF harness pre-populates before boot.** `arcfs_policy.abas`
calls `RAMDisk.*` at every one of its 40+ storage call sites and `UefiBlockDevice.*` at none of
them. "Commit + remount" has always meant "read back the same bytes, in the same process, in the
same boot" -- never a real power cycle, never a real disk.

This is a bigger gap than any of RFC-0042's own named phases addressed, and a different KIND of
gap: RFC-0042 made the FORMAT extensible and the CAPACITIES realistic; this RFC makes the DATA
actually survive. A filesystem that has never once been proven to outlive the process that wrote
it is not a filesystem yet in any usable sense -- it is a correct in-memory data structure with a
disk-shaped API. Closing that gap, and the concrete, bounded set of things that sit next to it
once it's closed, is what "usable as a daily-driver system filesystem" concretely requires.

The good news: the specific mechanism this RFC needs -- dispatching storage I/O between a RAM-only
provider and a real `UefiBlockDevice.*` driver -- already exists, already works, and is already
proven against real hardware. `fat32_policy.abas`'s `FAT32.SelectBlockDevice` solved exactly this
problem for FAT32 (RFC-0038 Section 17.5's own real UEFI Block I/O binding, `.agents/reports/
aps-blockio-fat32-uefi.md`). ArcFS just never received the same treatment. This RFC gives it that
treatment, then proves the result the way this project already knows how to prove hardware claims
honestly: real QEMU/virtio-blk first, then a real physical-hardware validation package (matching
`.agents/reports/WP-026-hardware-validation-package.md`'s own "prepared, human-executed, sign-off
required" template) for genuine daily-driver confidence.

------------------------------------------------------------------------

# 2. Goals

1. Give ArcFS a real, selectable, persistent block-device backing -- the same
   `RAMDisk.*`/`UefiBlockDevice.*` dispatch pattern FAT32 already has -- so data written to a real
   disk is still there after the machine that wrote it has gone away entirely.
2. Prove persistence for real: two genuinely separate QEMU processes, sharing nothing but a real
   disk image file on the host, with the second process mounting and reading back what the first
   one committed and then fully exited. Not a same-process remount -- a real discontinuity.
3. Raise the Allocation Bitmap's own volume-size ceiling (~254 KB, unchanged since RFC-0040 Phase
   H) past the point where it is the binding constraint on real daily-driver usage, using RFC-0042
   Section 7's own feature-negotiation mechanism so the change is a real, self-describing format
   extension rather than another silent breaking growth.
4. Establish, concretely, how a booting Arcology OS instance actually reaches an ArcFS root volume
   from UEFI firmware -- confirming or building the boot-path handoff, not assuming it exists.
5. Prove ArcFS holds up under realistic sustained use -- many sequential create/write/commit/
   delete cycles in one long-running session, not the 1-3 commits per fixture every proof so far
   has exercised -- with health state and reclaimed space verified throughout, not just at the end.
6. Produce a real physical-hardware validation package for the persistence work, matching this
   project's own established human-execution/sign-off discipline (Goal-6 exists because "daily
   driver" is a claim about a real machine, and this project's own standing rule is that no result
   is ever inferred from QEMU alone for a hardware claim -- see `.agents/reports/
   WP-026-hardware-validation-package.md`'s own sign-off template).

------------------------------------------------------------------------

# 3. Non-Goals

1. **Multi-writer concurrency.** Unchanged from RFC-0042 Section 4 Non-Goal 1 -- still out of
   scope, for the same reason (a foundational design comparable in size to RFC-0039 itself).
2. **A general-purpose, fully dynamic table allocator.** Unchanged from RFC-0042 Section 4
   Non-Goal 2. This RFC's own Allocation Bitmap widening (Section 7) raises a fixed ceiling to a
   larger fixed ceiling; it does not remove the "fixed MMIO region" architecture.
3. **A general block-device driver framework (AHCI, NVMe, or any device beyond the virtio-blk/
   UEFI-Block-IO path RFC-0038 already built).** This RFC reuses `UefiBlockDevice.*` exactly as it
   exists today; it does not add new physical transports.
4. **Booting Arcology OS's own kernel/loader stage from an ArcFS volume directly via UEFI
   firmware's own filesystem driver stack.** UEFI firmware understands FAT, not ArcFS. Section 8
   below assumes (and, if not already true, builds) a small FAT32 first-stage handing off to an
   ArcFS-backed system volume -- the same shape most real operating systems use for their own ESP.
   Making firmware itself understand ArcFS is not attempted.
5. **A full removable-media daily-driver install/update workflow, user accounts, or any OS-level
   feature beyond the storage layer.** This RFC is scoped to ArcFS itself being trustworthy enough
   to build those things on top of, not building them.

------------------------------------------------------------------------

# 4. Terminology

- **RAM-only proof**: any prior ArcFS fixture's "commit + remount" cycle, which reads back data
  from the same pre-populated `RAMDisk.*` byte range within one QEMU boot. Every ArcFS fixture in
  this project's history before this RFC is a RAM-only proof.
- **Cross-boot proof**: two genuinely separate process invocations (this RFC: two separate
  `qemu-system-x86_64` launches) sharing no process memory, connected only by a real on-disk image
  file the first process wrote to and fully closed before the second process opens it.
- **Physical-hardware proof**: an actual human, on an actual machine, following a prepared
  checklist and recording real observations -- this project's own established template (`.agents/
  reports/WP-026-hardware-validation-package.md`), never inferred from QEMU output.

------------------------------------------------------------------------

# 5. Requirement: ArcFS Block-Device Dispatch

## 5.1 The pattern to reuse

`fat32_policy.abas` already solves this exact problem:

```
FUNCTION FAT32.SelectBlockDevice(provider AS U32) AS BOOL   ' 0 = RAMDisk, 1 = UefiBlockDevice
FUNCTION FAT32.ActiveBlockDevice() AS U32
FUNCTION FAT32BlockReadSectors(...) AS BOOL   ' dispatches on FAT32.ActiveBlockDevice()
FUNCTION FAT32BlockSectorCount() AS U64       ' dispatches on FAT32.ActiveBlockDevice()
```

defaulting to provider 0 (RAMDisk) so every existing caller that never calls `SelectBlockDevice`
keeps its exact current behavior -- a non-breaking, additive change. `UefiBlockDevice.ReadSectors`/
`SectorCount` already fail closed (return 0 / reject out-of-bounds) if a caller selects provider 1
without discovering a device first, so selecting the wrong provider degrades to a clean failure,
never undefined behavior.

## 5.2 What ArcFS SHALL add

`ArcFS.SelectBlockDevice(provider) AS BOOL` / `ArcFS.ActiveBlockDevice() AS U32`, and every one of
`arcfs_policy.abas`'s 40+ direct `RAMDisk.*` call sites SHALL route instead through
`ArcFSBlockReadSectors`/`ArcFSBlockWriteSectors`/`ArcFSBlockSectorCount`/`ArcFSBlockIsWritable`
wrapper functions that dispatch on `ArcFS.ActiveBlockDevice()` -- the identical shape
`FAT32BlockReadSectors`/`FAT32BlockSectorCount` already establish. Default provider 0 (RAMDisk),
for the identical reason FAT32's own default exists: every one of RFC-0039/0040/0042's existing
frozen fixtures needs zero changes.

## 5.3 What this requirement does NOT change

`ArcFS.FormatVolume`/`MountImage`/`CommitImage`'s own logic, the Object/Namespace/Attribute/Extent
Tree formats, the checkpoint protocol, feature negotiation -- none of it. This requirement changes
WHERE bytes physically go, not what they mean or how they are structured. A volume formatted
against `RAMDisk` and one formatted against a real disk, using the identical code path, are
byte-for-byte the same format.

------------------------------------------------------------------------

# 6. Requirement: A Real Cross-Boot Persistence Proof

## 6.1 Why a same-process remount cannot prove this

Every existing ArcFS fixture's `ArcFS.CommitImage()` / `ArcFS.MountImage()` pair runs inside one
QEMU process's one boot. Nothing distinguishes "the bytes really left the process and came back"
from "the compiler simply never overwrote that memory region." RFC-0042 Section 9's own
backward-compatibility proof faced an analogous problem and reasoned explicitly about why its own
same-process design was still sound (disambiguation is a pure function of on-disk bytes). That
reasoning does NOT transfer here: persistence is not a pure function of bytes at rest, it is a
claim about what happens when the writer's own process, and everything it held in memory, is gone.

## 6.2 The proof this RFC requires

Two genuinely separate `qemu-system-x86_64` invocations (`run-uefi-hello-with-blockio-disk.sh`
already provides the harness shape: a real `virtio-blk-pci` `-drive` distinct from the boot media)
against the SAME real disk image file on the host filesystem:

1. **Process A**: boots a fixture that selects `UefiBlockDevice`, discovers the attached disk,
   formats a fresh ArcFS volume onto it, creates real objects with real content and real
   attributes, commits, and halts. The QEMU process for Process A fully exits -- no shared memory,
   no shared process state, nothing but the disk image file survives.
2. **Process B**: a genuinely separate QEMU launch, pointed at the SAME (now-populated) disk image
   file, boots a SEPARATE fixture that selects `UefiBlockDevice`, mounts the SAME volume, and reads
   every object/attribute back -- verified byte-for-byte against the values Process A wrote,
   compared against literal constants baked into Process B's own fixture (Process B has no
   in-memory record of what Process A did; it only has what it reads from disk).
3. A negative control: corrupt one expected value in Process B's own fixture, confirm a real,
   non-vacuous `FAIL`.
4. Determinism: the full two-process sequence repeated at least 3 times.

This is a strictly stronger proof than RFC-0042 Section 9's own two-frozen-copies design (which
shared one process) -- the first genuinely cross-process proof anywhere in ArcFS's history.

------------------------------------------------------------------------

# 7. Requirement: Allocation Bitmap Multi-Sector Redesign

## 7.1 The problem, quantified

`ArcFSMaxTrackedSectors()` = 508 (one 512-byte on-disk sector: 508 payload bytes + a 4-byte
CRC-32C checksum, unchanged since RFC-0040 Phase H). That is a ~254 KB volume-size ceiling. RFC-0042
Phase Q found this is ALREADY the binding constraint on how many of the new 2048-object live
capacity can be genuinely committed and remounted (proven to ~80 objects on the current bitmap). A
daily-driver root volume needs to be sized in the hundreds of MB to low GB, not KB.

## 7.2 The design

Widen the in-memory mirror (`ArcFSAllocationBitmapAddress`, currently a simple byte-per-sector MMIO
region already trivially widenable) and the ON-DISK representation from one self-checksummed sector
to N self-checksummed sectors, each independently readable/writable/scrubbable (matching this
project's own established "every structure gets a real reachability+scrub pass" precedent from
RFC-0040 Phase L/Section 11). `ArcFSMaxTrackedSectors()` becomes a real, much larger constant (this
RFC does not pre-commit to a specific number -- Section 15.2's own "trace the real footprint before
committing to a number" discipline applies here exactly as it did in RFC-0042 Phase Q).

## 7.3 A real, first use of RFC-0042's own feature-negotiation mechanism

This is an on-disk format change (checkpoint/superblock fields describing bitmap sector count, and
a different on-disk bitmap layout). RFC-0042 Section 7 built exactly the mechanism for this and has
never yet had a real consumer: this requirement SHALL claim a new INCOMPAT feature bit (an older
reader MUST refuse to mount a multi-sector-bitmap volume rather than misread it as a legacy
508-sector one). Proves RFC-0042's own promise for real, using it rather than describing it.

------------------------------------------------------------------------

# 8. Requirement: Boot-Path Integration

## 8.1 What needs confirming or building

UEFI firmware understands FAT, not ArcFS (Non-Goal 4). A real daily-driver boot path needs: a small
FAT32 volume firmware can load `BOOTX64.EFI` from directly (the same role a Linux ESP plays), which
then discovers the real system disk, selects `UefiBlockDevice`, and mounts ArcFS as the actual
system/data volume -- matching RFC-0041's own Arcology System Namespace design, which already
assumes ArcFS is where real system state lives.

## 8.2 Scope

This requirement is investigate-first: confirm what `.agents/reports/aps-arcfs-phase-g.md`'s own
system-volume activation work and RFC-0041 Phase A already established about this handoff before
writing new code, and build only the concrete gap that investigation finds -- honest scoping, not
assumed-necessary new mechanism.

------------------------------------------------------------------------

# 9. Requirement: Long-Session Durability Proof

A single QEMU session performing dozens of sequential object create/write/commit/delete/reflink
cycles (not the 1-3 commits per fixture every proof to date has exercised), with `ArcFS.
GetHealthState()` and a real reachability/scrub pass checked after every few cycles, and the
Allocation Bitmap's own free-sector count independently verified to never drift from what the live
object graph actually needs -- proving reclaimed space really returns to the free pool across many
real commits, not just one.

------------------------------------------------------------------------

# 10. Requirement: Physical-Hardware Validation Package

Once Section 6's cross-boot QEMU proof passes, prepare a validation package for a REAL machine,
matching `.agents/reports/WP-026-hardware-validation-package.md`'s own template exactly: build
identity (commit, checksums), test-platform fields, a checklist (attach real media, boot, confirm
real content survives a REAL power-off/power-on cycle -- not QEMU's `-no-reboot`, an actual cold
boot), an observation section, and a sign-off section requiring a human validator. Status stays
"READY FOR HUMAN EXECUTION -- NOT YET VALIDATED" until an actual person actually runs it -- this
RFC's own report MUST NOT claim physical validation from QEMU results, matching this project's own
standing rule.

------------------------------------------------------------------------

# 11. Format Versioning

Section 5 (block-device dispatch) and Section 8 (boot-path) touch no on-disk format at all. Section
7 (Allocation Bitmap) is the one real format change, and SHALL go through RFC-0042 Section 7's own
incompat-feature-bit mechanism (Section 7.3 above) rather than a magic-number/major-version bump --
the same "no version bump if the self-description mechanism actually works" test RFC-0042 Section
13.1 already established for itself.

------------------------------------------------------------------------

# 12. Implementation Phases

- **Phase S -- Block-Device Dispatch** (Section 5). `ArcFS.SelectBlockDevice`, wrapper functions,
  every existing `RAMDisk.*` call site migrated. Zero behavior change for any existing fixture
  (default provider unchanged).
- **Phase T -- Cross-Boot Persistence Proof** (Section 6). The two-process QEMU/virtio-blk proof --
  the actual load-bearing deliverable of this RFC.
- **Phase U -- Allocation Bitmap Multi-Sector Redesign** (Section 7). Raises the real volume-size
  ceiling; the one on-disk format change, real feature-negotiation bit.
- **Phase V -- Boot-Path Integration** (Section 8). Investigate first; build only the confirmed gap.
- **Phase W -- Long-Session Durability Proof** (Section 9).
- **Phase X -- Physical-Hardware Validation Package** (Section 10). Depends on Phase T; prepared
  but not signed off within this project's own automated work -- a human executes it.

Phase S blocks Phase T (nothing to prove persistent without a real backing first). Phase U SHOULD
follow Phase T (proving persistence against the CURRENT small ceiling first keeps the proof
simple; widening the ceiling is independent of whether persistence itself works). Phase V and
Phase W are independent of each other and of Phase U. Phase X depends only on Phase T.

------------------------------------------------------------------------

# 13. AI Implementation Guidance

## 13.1 Required boundaries

- No phase claims physical-hardware validation from a QEMU result. Section 10's package stays
  unsigned until a real human actually runs it -- this is non-negotiable, matching `.agents/
  reports/WP-026-hardware-validation-package.md`'s own explicit warning.
- Every existing ArcFS fixture (every RFC-0039/0040/0042 fixture, all frozen, all still exercising
  the default RAMDisk provider) MUST keep passing unchanged after Phase S -- the block-device
  dispatch is additive, not a replacement.
- Section 6's proof MUST use two genuinely separate `qemu-system-x86_64` process invocations, not
  a simulated split within one process -- that is the entire point of the requirement.

## 13.2 No silent scope reduction without saying so

Matching RFC-0042 Section 15.2's own rule: if a target number, a phase's own scope, or this RFC's
own phase ordering turns out to be impractical once concretely traced through, state the finding
and the revision plainly in that phase's own report and this RFC's own revision history -- do not
silently ship something smaller without saying so.

## 13.3 Mandatory acceptance evidence

- Phase S: every existing ArcFS smoke test still passes; a new fixture proves `ArcFS.
  SelectBlockDevice(1)` genuinely routes reads/writes through `UefiBlockDevice.*` (not silently
  falling back to RAMDisk).
- Phase T: the real two-process proof described in Section 6.2, including its own negative control
  and determinism check.
- Phase U: a real fixture committing past the CURRENT 254 KB ceiling, remounted, content verified;
  an old-reader-refuses-cleanly proof for the new incompat bit, mirroring RFC-0042 Phase N's own
  acceptance evidence shape.
- Phase V: a real fixture (or an honest report explaining why the existing mechanism already
  suffices, if investigation finds that) demonstrating firmware-to-ArcFS-root handoff.
- Phase W: the long-session fixture described in Section 9, with health/free-space checked at
  multiple points, not just the end.
- Phase X: the package itself, prepared and committed, explicitly marked not-yet-validated.

------------------------------------------------------------------------

# 14. Revision History

| Version | Date       | Changes |
|---------|------------|---------|
| 0.1     | 2026-08-21 | Initial draft, written directly after RFC-0042 Phase R completed and a real, concrete finding: ArcFS has never once been proven to persist data past the process that wrote it (`arcfs_policy.abas` calls `RAMDisk.*` at every storage call site, `UefiBlockDevice.*` at none). Scoped around that finding: real block-device dispatch (Phase S, reusing FAT32's own already-proven pattern), a real cross-boot two-process QEMU proof (Phase T), the Allocation Bitmap's own already-named multi-sector redesign (Phase U, now using RFC-0042's own feature-negotiation mechanism for real), boot-path integration (Phase V, investigate-first), a long-session durability proof (Phase W), and a real physical-hardware validation package matching this project's own established human-sign-off template (Phase X). Status: Draft; no implementation phase has begun. |
| 0.2     | 2026-08-22 | Phases S and T implemented and QEMU-proven, delivered together. Phase S: `ArcFS.SelectBlockDevice`/`ArcFS.ActiveBlockDevice` plus `ArcFSBlockReadSectors`/`WriteSectors`/`SectorCount` dispatch wrappers, mirroring `FAT32.SelectBlockDevice` exactly; all 37 real `RAMDisk.*` call sites in `arcfs_policy.abas` migrated; default provider unchanged (RAMDisk), zero changes needed to any existing fixture. `ArcFSBlockIsWritable` deliberately NOT added (Section 5.2's own text named it, but `RAMDisk.IsWritable` has zero real callers in `arcfs_policy.abas` today -- added only when a real consumer exists, matching this project's own no-speculative-generality discipline). A real, useful discovery made cheaply before fixture design: a standalone probe confirmed ArcFS's full RFC-0042 Phase Q address range (up to `0x1A000000`) works correctly under plain UEFI Boot Services' own default identity mapping, with NO custom page-table construction or `ExitBootServices` transition needed at all -- both Phase T fixtures use `blockio-provider.abas`'s own simpler shape instead of the `aps-exception-entry` family's heavier harness, removing `ExitBootServices` as a potential confound in a proof specifically about the block device. Phase T: two genuinely separate fixtures (`aps-arcfs-persist-writer.abas`/`aps-arcfs-persist-reader.abas`) run as two genuinely separate `qemu-system-x86_64` processes against the same real disk image file -- the writer formats/creates/commits to a real attached disk, confirms RAMDisk's own memory was never touched, and fully exits; an independent host-side check confirms the real disk image FILE itself starts with the real ArcFS superblock magic (`"ARCFSB02"`) after the writer process exited; the separate reader process then mounts the same image and reads content/attribute back byte-for-byte against literal constants in its own source. Negative control (a corrupted expected value in a separately-built reader variant) confirmed a real, non-vacuous mismatch; deterministic across 3 full writer+reader cycles. New smoke test `systems_arco_basic_arcfs_persist_smoke`; full suite re-run -- see `.agents/reports/aps-arcfs-phase-s-t.md` for the final count. |
| 0.3     | 2026-08-22 | Phase U (Section 7, Allocation Bitmap multi-sector redesign) implemented and QEMU-proven. `ArcFSMaxTrackedSectors()` 508 -> 200152 (an exact multiple of 508, chosen deliberately past RFC-0042 Phase Q's own 64 MB Data Pool ceiling rather than as large as possible, matching this RFC's own "past the binding constraint, concretely traced, not maximal" direction); new `ArcFSBitmapSectorCount()`=394 -- the bitmap's on-disk image now spans that many independently self-checksummed sectors, `ArcFSLoadBitmap`/`ArcFSWriteBitmapRecord` rewritten accordingly (the latter gaining a `baseSector` parameter and now performing its own multi-sector writes directly); the fixed initial volume layout shifted forward once more (checkpoint/health-record sectors now computed via new `ArcFSCheckpointBaseSector()`/updated `ArcFSHealthRecordSector()` instead of hardcoded). RFC-0042 Section 7's feature-negotiation mechanism gets its first real consumer: `ArcFSFeatureMultiSectorBitmap()` (bit 16, the first incompat bit this format has ever defined), `ArcFSRecognizedIncompatMask()` now returns it, every volume this build formats carries it unconditionally. A real bit-field-scoping bug caught by a standalone probe BEFORE the full proof fixture was built (the same discipline Phase S/T's own address-range probe already established): `ArcFSRecognizedIncompatMask()` initially returned the RAW-space bit value (`65536`, correct for writing the superblock) instead of the SUB-FIELD-space value (`1`) `ArcFSCheckFeatureFlags`'s own comparison actually needs -- causing a real volume to fail to mount immediately after the SAME build had just formatted it. Fixed (`\ 65536`); the pre-fix probe genuinely failed, confirming the fix mattered. A real, honestly-traced cost named (not hidden): every commit now costs ~395 fresh high-water-mark sectors for the bitmap's own bump-allocated span alone (up from 1 before this phase) -- an existing cost class (real copy-on-write, unreachable until an explicit `ArcFS.ReclaimGeneration` call), simply ~394x larger in absolute terms; not mitigated this phase. Real QEMU/OVMF proof (`aps-arcfs-multi-sector-bitmap.abas`), two steps: a real commit's own checkpoint sector directly read back and confirmed > 508 (something the old bitmap could never represent), with real content/attribute surviving a real remount; and the real on-disk superblock's flags field confirmed to genuinely carry the new bit, with a frozen legacy replica of `ArcFSCheckFeatureFlags` (the same same-process technique RFC-0042 Phase O established) genuinely refusing it. Passed after the bit-scoping fix; negative control confirmed real `FAIL 1`; deterministic across 3 runs. New smoke test `systems_arco_basic_arcfs_multi_sector_bitmap_smoke`; full suite re-run -- see `.agents/reports/aps-arcfs-phase-u.md` for the final count. |
| 0.4     | 2026-08-22 | Phase V (Section 8, boot-path integration) implemented and QEMU-proven, investigate-first per Section 8.2's own mandate. Investigation read `.agents/reports/aps-arcfs-phase-g.md` and `stdlib/system_namespace_policy.abas` directly and confirmed by grep, not assumption: UEFI firmware loading `BOOTX64.EFI` from a FAT-formatted boot medium needs no new proof (inherent to every fixture this project has ever built, RFC-0043 Non-Goal 4); `ArcFS.ActivateSystemVolume()` (RFC-0039 Phase G) exists and is proven, but its own fixture uses `RAMDisk.*` 46 times and `UefiBlockDevice.*` zero times; RFC-0041's own System Namespace attachment/delegation is proven, but its own fixture shows the identical 45-vs-0 split. RFC-0043 Phase S/T (the prior increment) closed the one item `aps-arcfs-phase-g.md` had explicitly named as still-open (real hardware block-device DISCOVER), but its own writer/reader fixtures called plain `ArcFS.MountImage()`, never `ActivateSystemVolume`, and never touched the System Namespace. The concrete gap, confirmed to be a proof gap and not a missing-mechanism gap (`ActivateSystemVolume`'s own body calls only already-Phase-S-dispatched functions, no bitmap/namespace-specific code of its own): neither mechanism had ever been exercised against a real persistent block device or across a real process boundary. Closed with a genuine two-process cross-boot fixture pair (`aps-arcfs-sysvol-writer.abas`/`aps-arcfs-sysvol-reader.abas`, new directory `arcology-os/tests/fixtures/arcfs-sysvol/`), mirroring Phase T's own technique: writer formats/creates/commits real content to a real disk and confirms RAMDisk stayed untouched; a genuinely separate reader process calls `ArcFS.ActivateSystemVolume()` against that same real disk (first time ever against real persistent storage), attaches the activated volume into a fresh System Namespace at `:vol`, and resolves `:vol:doc.txt` -- delegating across the attachment boundary into real content, also for the first time over a real disk that survived a real process boundary -- confirmed to reach the same OID a direct `ArcFS.Lookup` would. A real capacity finding, not a bug: the first attempt failed at commit with a 256 KiB disk (Phase T's own fixture size) -- traced through and confirmed as Phase U's own already-named per-commit bitmap cost (Phase T's own fixtures are FROZEN, pre-Phase-U snapshots, so unaffected; this freshly-authored fixture genuinely needs the real space), fixed by using a 4 MiB disk. No ArcFS or Namespace code changed this phase -- a real "the existing mechanism already suffices, once actually exercised together" outcome. Negative control confirmed real; deterministic across 3 full writer+reader cycles. New smoke test `systems_arco_basic_arcfs_sysvol_persist_smoke`; full suite re-run -- see `.agents/reports/aps-arcfs-phase-v.md` for the final count. |
