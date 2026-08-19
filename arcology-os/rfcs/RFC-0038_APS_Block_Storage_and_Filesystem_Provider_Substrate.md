# RFC-0038: APS Storage and Filesystem Provider Model

**RFC Number:** RFC-0038
**Title:** APS Storage and Filesystem Provider Model
**Status:** Draft
**Category:** Substrate / Storage
**Authors:** Arcology Project
**Created:** 2026-08-19
**Last Updated:** 2026-08-19
**Supersedes:** None
**Superseded By:** None
**Related RFCs:** RFC-0000, RFC-0004, RFC-0007, RFC-0017, RFC-0035, RFC-0036, RFC-0037

------------------------------------------------------------------------

# 1. Executive Summary

APS has no way to read a file. There is no block-device driver, no filesystem, and no on-disk
format of any kind reachable from the freestanding profile — every proof fixture that exists is
one statically-linked image with no persistent storage beyond what UEFI loaded it from.

**A note on scope, stated up front:** this RFC does not specify ArcFS. RFC-0004 and RFC-0007 both
explicitly exclude ArcFS from even the eventual Seed release, and a full search of this
repository's history (every commit, every branch) turned up no prior ArcFS design document — only
placeholder mentions of the name. If a design for ArcFS exists outside this repository, this RFC
is written to be reconciled with it, not to preempt it: it defines a **Filesystem Provider
contract** general enough for ArcFS to implement later without this RFC needing to change, and it
specifies exactly one concrete provider now — a read-only FAT32 implementation — chosen
specifically because it needs no new on-disk format at all: it is what UEFI's own boot media
already is, and what `build-arcology-hardware-image.py` already produces.

The result APS gets from this RFC: a Block Device Provider contract, a Filesystem Provider
contract above it, one real implementation of each (a RAM disk, for testing without hardware; and
read-only FAT32, for reading real boot media), and a namespace model where a Common Filesystem
volume and a future ArcFS volume coexist under the same path space through the same contract —
"use both" by construction, not by special-casing either one.

------------------------------------------------------------------------

# 2. Motivation

Every subsystem past this point needs files: a package manager needs to read a package; a desktop
needs to load an icon; a text editor needs to open and save. All of it is blocked on the same two
missing layers — something that can read raw sectors, and something that can interpret a
filesystem's structure on top of those sectors — and both are currently entirely absent.

This RFC also exists to prevent a specific, foreseeable design mistake: building a filesystem
layer that only knows how to talk to one on-disk format. RFC-0004/RFC-0007 already commit this
project to ArcFS eventually existing alongside broad compatibility concerns (reading real UEFI
media, interop with ordinary tooling for debugging). Designing the provider boundary now, before
either concrete filesystem's implementation details leak into how *calling code* asks for a file,
is what makes "common filesystem and ArcFS, both, without every caller caring which" possible at
all.

------------------------------------------------------------------------

# 3. Goals

- Define a Block Device Provider contract: read (and optionally write) fixed-size sectors, report
  geometry, nothing else. Storage-medium-agnostic.
- Define a Filesystem Provider contract above it: mount, open, read, (optionally) write, close,
  enumerate, stat. Format-agnostic.
- Specify one concrete Block Device Provider now (a RAM disk) sufficient to test the Filesystem
  Provider contract without any real storage hardware driver existing yet.
- Specify one concrete Filesystem Provider now: read-only FAT32, sufficient to read the same boot
  media APS already ships on.
- Define how multiple mounted volumes, of potentially different Filesystem Providers, share one
  path namespace — the mechanism ArcFS plugs into later without this RFC changing.
- Reuse RFC-0017's Resource/Authority Contract model for every mounted volume and open handle,
  rather than inventing a parallel ownership concept.

------------------------------------------------------------------------

# 4. Non-Goals

- ArcFS's own on-disk format, object model, transactions, versioning, or chunking. RFC-0004's own
  words: "Seed does not include: ArcFS... Persistent storage." Those remain a future RFC. Where
  this RFC needs to say anything about what ArcFS will eventually need, it says so as informative
  guidance (Section 18), never as a normative requirement.
- A real AHCI, NVMe, or virtio-blk driver. Those are each their own future RFC; this RFC's only
  concrete Block Device Provider is a RAM disk, specifically because it lets the Filesystem
  Provider layer be built and proven correct without waiting on any of them.
- Write support for FAT32. The reference implementation in this RFC is read-only. A future RFC
  may add write support once the read path has real-world mileage; read-only is deliberately the
  smaller, safer first step, matching this project's own repeated preference (`aps-owned-gdt.md`'s
  incremental gates, `aps-exception-entry-stub.md`'s narrow first vector-3-only recovery policy).
- Caching, buffering strategy, or any performance optimization beyond correctness. Future
  Extension.
- Permissions, users, or any access-control model beyond RFC-0017's existing Owner/Provider/
  Rights fields.
- Networking-backed storage of any kind.

------------------------------------------------------------------------

# 5. Terminology

**Sector:** The fixed-size (device-reported, commonly 512 or 4096 bytes) atomic unit a Block
Device Provider reads or writes. Never partial.

**Block Device Provider:** A Provider (RFC-0017) exposing `ReadSectors`/`WriteSectors`/
`SectorSize`/`SectorCount` for one underlying storage medium. Knows nothing about filesystems.

**Volume:** A mounted Filesystem Provider instance bound to one Block Device Provider (or, for a
future in-memory-only or overlay filesystem, no block device at all — the contract does not
require one). A Resource in RFC-0017's sense.

**Filesystem Provider:** A Provider implementing Open/Read/Write/Close/Enumerate/Stat against a
Volume's own on-disk structure, translating between that structure and the format-agnostic handle/
entry types this RFC defines.

**Mount Point:** A path prefix under which a Volume's contents appear in the unified namespace.

**Common Filesystem:** This RFC's term for a Filesystem Provider implementing a widely-interoperable,
pre-existing on-disk format rather than an Arcology-native one. FAT32 (read-only) is this RFC's
one concrete Common Filesystem provider; nothing in this RFC assumes it is the *only* one forever.

------------------------------------------------------------------------

# 6. Requirements

## 6.1 Block Device Provider contract

```basic
INTERFACE BlockDevice
    FUNCTION SectorSize() AS U32
    FUNCTION SectorCount() AS U64
    FUNCTION ReadSectors(startSector AS U64, count AS U32, destination AS MMIOPTR) AS BOOL
    FUNCTION WriteSectors(startSector AS U64, count AS U32, source AS MMIOPTR) AS BOOL
    FUNCTION IsWritable() AS BOOL
END INTERFACE
```

`ReadSectors`/`WriteSectors` MUST reject (return `FALSE`, MUST NOT partially transfer) a request
whose `startSector + count` exceeds `SectorCount()`. `WriteSectors` on a device where
`IsWritable()` is `FALSE` MUST return `FALSE` without touching the medium. Every concrete
implementation of this interface MUST be synchronous from its caller's point of view for this
RFC's scope — a Block Device Provider backed by a real, interrupt-completion-driven controller
(a future AHCI/NVMe driver) is expected to block the calling hook internally (via RFC-0037's Loop,
waiting on its own device's completion interrupt) rather than exposing asynchrony to Filesystem
Provider code, which this RFC keeps deliberately synchronous end to end.

## 6.2 Reference Block Device Provider: RAM Disk

```basic
RAMDisk.Create(sizeBytes AS U64) AS BlockDevice
```

Backed by ordinary allocated memory (`AllocatePages`-sourced, per the same pattern every other
freestanding fixture in this project already uses), sector size fixed at 512 bytes, always
writable. Exists specifically so the Filesystem Provider contract (6.3) and the FAT32
implementation (6.4) can be built, tested, and proven correct under QEMU/OVMF without any real
storage driver existing — mirroring exactly how RFC-0036's timer testing strategy prefers a
controllable, deliberate trigger (`CPU.Interrupt(n)`) over an uncontrolled real-world condition.

## 6.3 Filesystem Provider contract

```basic
TYPE DirectoryEntry
    Name AS STRING
    IsDirectory AS BOOL
    SizeBytes AS U64
END TYPE

INTERFACE FilesystemProvider
    FUNCTION Mount(device AS BlockDevice) AS BOOL
    FUNCTION Unmount() AS BOOL
    FUNCTION IsWritable() AS BOOL

    FUNCTION Open(path AS STRING, writable AS BOOL) AS FileHandle
    FUNCTION Read(handle AS FileHandle, buffer AS MMIOPTR, maxBytes AS U64) AS U64
    FUNCTION Write(handle AS FileHandle, buffer AS MMIOPTR, byteCount AS U64) AS U64
    FUNCTION Seek(handle AS FileHandle, offset AS U64) AS BOOL
    FUNCTION Close(handle AS FileHandle) AS BOOL

    FUNCTION Enumerate(directoryPath AS STRING) AS DirectoryEntry[]
    FUNCTION Stat(path AS STRING) AS DirectoryEntry
END INTERFACE
```

`Mount` MUST validate the device's structure enough to reject a device that does not contain this
provider's format (a FAT32 provider checking the boot sector signature, for example) and MUST
return `FALSE` rather than mounting something it cannot actually interpret — a Filesystem Provider
that mounts unconditionally and fails unpredictably on first real access is explicitly the wrong
shape here. `Write`/a `writable=TRUE` `Open` on a provider where `IsWritable()` is `FALSE` MUST
fail cleanly (RFC-0022's runtime-error story once available on this profile, or a documented
sentinel return until then — consistent with how this RFC's own FAT32 provider is read-only and
therefore always reports `IsWritable() = FALSE`).

`path` is always a **volume-relative** path (no mount-point prefix); the unified-namespace layer
(6.5) strips the prefix before calling into a specific provider. A Filesystem Provider never sees,
and never needs to know about, any other mounted volume.

## 6.4 Reference Filesystem Provider: read-only FAT32

Implements the contract in 6.3 against the FAT32 on-disk structure (boot sector, FAT table(s),
root directory cluster chain, 8.3 and — if practical within this milestone's scope — VFAT long
filenames for `Enumerate`/`Stat` display purposes). `Mount` validates the boot sector's `FAT32 `
filesystem-type string and the standard `0x55 0xAA` boot-sector signature before accepting the
device. `IsWritable()` always returns `FALSE`; `Write` always fails. This is intentionally the
same read-only-first discipline this project already applied to firmware GDT/IDT ownership
(`aps-owned-gdt.md`, `aps-owned-idt.md`) — read and prove correctness first, add mutation as a
separate, later, separately-tested increment.

FAT32 is chosen over any alternative "common" format specifically because:

1. It requires no new format design — it is what the project's own
   `scripts/build/build-arcology-hardware-image.py` already writes, and what UEFI's own
   `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` already reads from the same media in the pre-`ExitBootServices`
   world this project's fixtures already boot through.
2. It gives APS the ability to read its *own boot volume's* contents post-`ExitBootServices` —
   directly useful (loading a second program, a font, a config file from the same media the
   substrate itself booted from) well before any real block-device driver exists, since the RAM
   disk (6.2) can be loaded from that same media by UEFI-era code before `ExitBootServices` and
   handed off as a pre-populated RAM disk for the FAT32 provider to mount post-transition.
3. Broad interop: any real hardware's boot partition, and any development-host tooling, already
   speaks it.

## 6.5 Unified namespace and mounting

```basic
Volumes.Mount(mountPoint AS STRING, provider AS FilesystemProvider) AS BOOL
Volumes.Unmount(mountPoint AS STRING) AS BOOL
Volumes.Resolve(path AS STRING) AS FilesystemProvider, STRING  ' provider, volume-relative remainder
```

Every mounted volume MUST be registered with the Resource Registry (RFC-0017) as a Resource with
`ResourceType = VOLUME`, `Owner` = the caller of `Volumes.Mount`, `Provider` = the
`FilesystemProvider` instance, `Lifetime = Explicit`, and a dependency edge to the underlying
`BlockDevice`'s own Resource record. `Volumes.Resolve` is longest-mount-point-prefix matching
(`/` MUST always be mountable and, once mounted, matches any path with no more specific mount
point registered) — this is the entire mechanism by which a Common Filesystem volume and a future
ArcFS volume coexist: `Volumes.Mount("/", fatProvider)` and, later,
`Volumes.Mount("/arcfs", someArcFSProvider)` (or ArcFS mounted at `/` with FAT32 relegated to
`/boot`, an ordering decision left to whoever mounts volumes at bootstrap time, not fixed by this
RFC) both work through the identical call, because neither provider nor caller code needs to know
the other format exists.

File-facing calling code (`Files.Open`, etc., once specified — see Section 16's note on RFC-0035)
goes through `Volumes.Resolve` and never talks to a `FilesystemProvider` directly by name, which is
what makes "a common filesystem and ArcFS, both" transparent to that calling code rather than
something it has to branch on.

------------------------------------------------------------------------

# 7. Architecture

```text
+--------------------------+     +--------------------------------+
| BlockDevice: RAMDisk      |     | (future) BlockDevice: AHCI/... |
+-------------+--------------+     +----------------+-----------------+
              |                                     |
              v                                     v
   +----------------------+              +------------------------+
   | FilesystemProvider:    |              | (future) FilesystemProvider: |
   | FAT32 (read-only)       |              | ArcFS                        |
   +-----------+-------------+              +--------------+----------------+
               |                                            |
               v                                            v
        Volumes.Mount("/boot", fat32)          Volumes.Mount("/", arcfs)
               |                                            |
               +---------------------+-----------------------+
                                     |
                                     v
                        +--------------------------+
                        |   Unified namespace        |
                        |   Volumes.Resolve(path)     |
                        +--------------------------+
                                     ^
                                     |
                         Files.Open / Enumerate / Stat
                         (format-agnostic caller)
```

Every box left of "Unified namespace" is a Resource under RFC-0017; every arrow into
`Volumes.Mount` is that Resource's registration.

------------------------------------------------------------------------

# 8. User Experience

None directly at this layer — no interactive surface exists yet. This RFC is what makes a future
file-open dialog, package manager, or text editor's "Open" possible at all; it has no UI of its
own.

------------------------------------------------------------------------

# 9. Developer Experience

```basic
LET disk AS BlockDevice = RAMDisk.Create(16777216)    ' 16 MiB
' ... disk populated from UEFI-era boot media before ExitBootServices, or by a real driver later ...
LET fat AS FilesystemProvider = FAT32Provider.Create()
IF fat.Mount(disk) THEN
    Volumes.Mount("/", fat)
    LET entries AS DirectoryEntry[] = fat.Enumerate("/")
    LET handle AS FileHandle = fat.Open("/README.TXT", FALSE)
    ' Read(handle, ...), Close(handle)
END IF
```

Calling code that only ever uses `Volumes.Resolve`/`Files.*` instead of a specific provider by
name is unaffected the day a second provider (ArcFS) is mounted alongside or instead of this one.

------------------------------------------------------------------------

# 10. Security Considerations

A Filesystem Provider parses untrusted, attacker-influenceable on-disk structures (a
maliciously-crafted FAT32 volume is real attack surface the moment removable media is involved) —
unlike RFC-0036/RFC-0037, this RFC's trust boundary is not "APS trusts its own platform devices."
The read-only FAT32 provider MUST validate every on-disk structure it reads (cluster chain
bounds, directory entry counts, string lengths) against the device's own reported geometry before
trusting it, and MUST fail closed (return `FALSE`/an error, never read or write outside the
validated bounds) on any inconsistency, rather than assuming a well-formed volume. This RFC does
not attempt a full threat model for FAT32 parsing (a well-studied format with known historical
parser vulnerabilities elsewhere) beyond stating the requirement; the implementation report MUST
document what bounds-checking was actually done.

------------------------------------------------------------------------

# 11. Privacy Considerations

Files may contain user data the moment any Filesystem Provider is mounted read-write in a future
increment; this RFC's own reference implementation is read-only and introduces no new persistent
write surface. No telemetry or data collection of any kind is in scope.

------------------------------------------------------------------------

# 12. Accessibility Considerations

None directly. Filesystem access is a precondition for accessible-format documents, user-
configurable assistive-technology settings persisted across boots, and similar — all future work
built on top of this RFC, not part of it.

------------------------------------------------------------------------

# 13. Performance Considerations

The RAM disk (6.2) has no meaningful I/O latency; the FAT32 provider's performance profile is
dominated by FAT table walks for large or fragmented files, unoptimized in this first
implementation (no caching — Non-Goals, Section 4). This is an accepted cost for a read-only,
correctness-first milestone; a future RFC may add FAT/cluster caching once real hardware latency
(a real block device, not the RAM disk) makes it worth measuring against.

------------------------------------------------------------------------

# 14. Compatibility

Additive. Nothing existing changes behavior. `Volumes.Mount("/", ...)` is not called by any
existing fixture; every current proof continues to run with no mounted volumes at all, exactly as
today.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
' Illustrative sketch of FAT32 boot-sector validation, not a complete implementation.
FUNCTION FAT32Mount(provider AS FAT32Provider, device AS BlockDevice) AS BOOL
    LET sector AS MMIOPTR = ScratchSectorBuffer()
    IF device.ReadSectors(0, 1, sector) = 0 THEN RETURN 0
    IF MEMORY.Read16(ADDRESS.Offset(sector, 510)) <> 43605 THEN RETURN 0   ' 0xAA55, little-endian read
    ' ... validate bytes-per-sector, sectors-per-cluster, FAT32-specific fields, "FAT32   " label ...
    provider.Device = device
    provider.FatBeginSector = ComputeFatBeginSector(sector)
    provider.RootCluster = ComputeRootCluster(sector)
    RETURN 1
END FUNCTION
```

Cluster-chain walking, directory-entry parsing, and long-filename reconstruction are left to
implementation; this RFC's normative content is the provider contract (6.1, 6.3) and the mount-
validation requirement (6.4), not FAT32's on-disk layout itself, which is externally specified by
Microsoft's own FAT documentation and not reproduced here.

------------------------------------------------------------------------

# 16. Testing Strategy

- Block Device Provider: unit-level tests against the RAM disk for out-of-bounds rejection,
  read/write round-trip correctness, and `IsWritable()` enforcement — these do not require QEMU/
  OVMF, since a RAM disk has no hardware dependency, and SHOULD be validated at this level first.
- Filesystem Provider contract: build a small, known-good FAT32 image (this project already has
  the tooling — `build-arcology-hardware-image.py` — to produce one deterministically) with known
  file contents and directory structure, load it into a RAM disk, and confirm `Enumerate`/`Open`/
  `Read`/`Stat` against it produce exactly the expected bytes and metadata.
- **QEMU/OVMF-executed**: a fixture that populates a RAM disk from real boot media (or from an
  embedded known-good image) before `ExitBootServices`, transitions, mounts the FAT32 provider
  post-transition, reads a known file, and confirms its exact contents over serial — proving the
  whole chain (RAM disk -> FAT32 parse -> Volumes.Resolve -> Open/Read) under the same real-CPU
  discipline every other proof in this chain has used.
- A deliberate negative test: mounting a FAT32 provider against a RAM disk that does not contain a
  valid FAT32 volume (e.g., zeroed) and confirming `Mount` returns `FALSE` rather than succeeding
  and failing unpredictably later — directly exercising the fail-closed requirement in Section 10.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

## 17.1 Implementation boundaries

In scope: the `BlockDevice` and `FilesystemProvider` interfaces, the RAM disk, the read-only FAT32
provider, and `Volumes.Mount`/`Unmount`/`Resolve`. Out of scope: ArcFS itself (Non-Goals, Section
4), any real hardware block-device driver, and FAT32 write support.

## 17.2 Prohibited implementation choices

- Any Filesystem Provider API shape that requires calling code to know which concrete provider it
  is talking to (a `FAT32Open` distinct from an `ArcFSOpen`, for example). The entire point of this
  RFC is that calling code only ever sees `FilesystemProvider`/`Volumes.*`.
- Mounting a FAT32 volume without validating its boot-sector signature and filesystem-type label
  first (Requirement 6.3, 6.4).
- Silently truncating or wrapping an out-of-bounds sector request instead of rejecting it
  (Requirement 6.1).

## 17.3 Required deliverables

- `BlockDevice`/`FilesystemProvider` interface definitions and the RAM disk implementation.
- The read-only FAT32 provider.
- `Volumes.Mount`/`Unmount`/`Resolve` and their RFC-0017 Resource Registry integration.
- A deterministically-built known-good FAT32 test image and the tests described in Section 16.
- A QEMU/OVMF-executed fixture and matching `systems_arco_basic_*_smoke.sh` test.
- An implementation report under `.agents/reports/`, explicitly noting (per this RFC's own
  Executive Summary) that ArcFS remains unspecified and that reconciliation with any external
  ArcFS design, once located, is expected future work rather than something this implementation
  attempted to guess at.

## 17.4 Acceptance criteria

- `ctest` suite passes in full, including RAM-disk unit tests and the QEMU/OVMF fixture.
- The FAT32 provider correctly reads a real, independently-verifiable file's contents from a real
  FAT32 image (byte-for-byte, not approximately) under QEMU/OVMF.
- Mounting an invalid volume fails closed, verified by a dedicated negative test.

## 17.5 Stop conditions

Stop and request human review before: adding any write path to the FAT32 provider; designing
ArcFS's own on-disk format as part of implementing this RFC (even partially, even as a
placeholder) rather than leaving `Volumes.Mount("/arcfs", ...)`-shaped room for it; or discovering
that the boot-media RAM-disk-population step needs UEFI protocol surface this project's frontend
does not yet bind (`uefi-bindings.md` is the source of truth for what's currently bound — extend it
following that document's own conventions rather than improvising a new binding style).

## 17.6 Assumptions and dependencies

Depends on RFC-0017 (Resource Registry) for volume/handle ownership tracking. Soft dependency on
RFC-0037 (the Loop) for how a *future* interrupt-driven real block device would report completion
— the RAM disk and this RFC's synchronous contract (6.1) do not themselves require the Loop to
exist, but a real driver satisfying the same `BlockDevice` contract later will. RFC-0035's hosted
`Directory`/`File`/`Path` services are a **separate, hosted-only** layer (explicitly out of scope
for "freestanding services" per its own Non-Goals) — naming consistency between the two is
worth pursuing for developer familiarity but this RFC does not require it, and RFC-0035 is not a
dependency.

------------------------------------------------------------------------

# 18. Future Extensions

- ArcFS as a second `FilesystemProvider` implementation, once its own RFC exists — this is this
  RFC's central intended extension point, not a hypothetical. Informative guidance only (not
  normative — Section 4): the roadmap's own description of ArcFS ("Object storage, Transactions,
  Versioning, Chunking, Stable identity, Namespace model") suggests its `Open`/`Read`/`Write` may
  eventually need richer semantics (object versions, transactional multi-file commits) than this
  RFC's minimal contract expresses; if so, those are expected to arrive as an *extension* to the
  `FilesystemProvider` contract (additional optional interface, capability-queried) rather than a
  breaking change to it, exactly as this project has already handled other capability growth
  (`CPU.ReloadStackSegment` added alongside, not instead of, `CPU.ReloadCodeSegment`).
- FAT32 write support.
- Real block-device drivers: AHCI, NVMe, virtio-blk (QEMU-native, useful for faster iteration than
  AHCI emulation during this project's own development).
- A page-cache / buffer-cache layer shared across Filesystem Providers.
- exFAT or other Common Filesystem alternatives, if FAT32's 4 GiB file-size ceiling becomes a real
  constraint.
- Filesystem change notification, once RFC-0037's event-source model is proven with more than one
  real Provider.

------------------------------------------------------------------------

# 19. Open Questions

- Should `/` be reserved for whichever provider is APS's "primary" volume by convention, with
  every other Filesystem Provider necessarily mounted at a non-root prefix — or should mount-point
  assignment be entirely bootstrap-policy-defined with no reserved meaning for `/`? This RFC
  intentionally does not decide (Requirement 6.5 supports either), but a real bootstrap sequence
  will need to pick one, and consistency across future fixtures/documentation would benefit from
  deciding explicitly rather than per-fixture.
- Once ArcFS's real design is located or written, does it satisfy this RFC's `FilesystemProvider`
  contract as specified, or does reconciling the two need contract changes? Cannot be answered
  until that design exists; flagged here so it is not forgotten.
- Long-filename (VFAT) support in the read-only FAT32 provider: required for realistic
  interoperability, but adds real parsing complexity. Left to implementation judgment (Section
  17) rather than mandated here; 8.3-name-only is an acceptable first cut if long-filename support
  meaningfully delays the rest of this RFC's deliverables.

------------------------------------------------------------------------

# 20. References

- RFC-0004, RFC-0007 (both explicitly exclude ArcFS/persistent storage from Seed)
- RFC-0017 (Substrate Resource Model)
- RFC-0035 (hosted-only Directory/File/Path services — related by naming convention, not a
  dependency)
- `arcology-os/scripts/build/build-arcology-hardware-image.py` (existing FAT32 image tooling)
- Microsoft FAT32 File System Specification (primary source for the on-disk format; this project's
  own established practice is verification against primary sources — see
  `docs/systems/x86-64-codegen.md`)

------------------------------------------------------------------------

# 21. Revision History

| Version | Date       | Summary       |
|---------|------------|---------------|
| 0.1     | 2026-08-19 | Initial draft |
