# RFC-0044: Single-Disk GPT Boot — Real ArcFS System Volume Alongside a Real ESP

**RFC Number:** RFC-0044\
**Title:** Single-Disk GPT Boot — Real ArcFS System Volume Alongside a Real ESP\
**Status:** Draft (all 4 phases implemented and QEMU-proven; Status stays Draft because the
resulting hardware package, WP-031, remains unexecuted on real hardware — matching this project's
own standing rule that no RFC closes on QEMU results alone)

**Category:** Platform / Storage / Filesystem Architecture

**Authors:** Arcology Project\
**Created:** 2026-08-22\
**Last Updated:** 2026-08-22\
**Supersedes:** None\
**Superseded By:** None

**Related Architecture:** Arcology Object Architecture; Polymorphic Substrate (APS)\
**Related RFCs:** RFC-0006 (Real Hardware UEFI Bring-Up), RFC-0014 (Graphics Surface Binding),
RFC-0038 (APS Block Storage and Filesystem Provider Substrate — Section 17.5's own "does not
attempt multi-handle enumeration" scope reduction, closed here), RFC-0043 (ArcFS Persistent
Storage and Daily-Driver Readiness — Section 8's own "a small FAT32 volume... which then
discovers the real system disk" design, made real on a SINGLE physical disk here instead of two)

------------------------------------------------------------------------

# 1. Executive Summary

Every ArcFS-on-real-hardware proof so far (RFC-0043 Phases T/V, and this session's own `render-
and-halt` work) uses TWO separate physical/virtual disks: one whole-device "superfloppy" FAT32
image as boot media, one entirely separate device for ArcFS. That is a real, valid, already-proven
design — but it is not what a real installed operating system looks like. A real OS lives on ONE
disk: a small EFI System Partition (ESP) firmware boots from, and a second partition holding the
actual system.

This RFC closes that gap for real: a single GPT-partitioned disk image, an ESP containing
`BOOTX64.EFI`, and a second partition ArcFS formats and mounts as its own real system volume — all
discovered and distinguished from firmware's own real, multi-handle block-device enumeration, not
assumed or hardcoded. The user's own explicit choice, made directly: "One disk, real ESP + ArcFS
partition."

------------------------------------------------------------------------

# 2. Goals

1. Real multi-handle `EFI_BLOCK_IO_PROTOCOL` enumeration — discover EVERY block device handle
   firmware currently publishes (not just "whichever one `LocateProtocol` happens to return
   first," `UefiBlockDevice.Discover`'s own existing, documented limitation), and select a
   SPECIFIC one among them.
2. A real GPT-partitioned disk image: a genuine EFI System Partition (FAT32, `BOOTX64.EFI`) plus a
   second partition ArcFS owns, built by a new, deterministic, byte-reproducible tool matching
   `build-arcology-hardware-image.py`'s own established discipline.
3. A real fixture proving the whole chain on ONE disk: firmware boots the ESP's own `BOOTX64.EFI`,
   the running application discovers the SAME disk's second partition specifically (not the ESP,
   not a hardcoded assumption), formats/mounts a real ArcFS volume there, and renders a visible
   confirmation before halting.
4. A real QEMU proof using a genuinely GPT-partitioned disk image (not two separate `-drive`
   entries) — the actual artifact shape a real USB stick will have.
5. An updated physical-hardware validation package for the same named test platform (the user's
   own Lenovo ThinkPad E15 Gen 2), single-disk this time.

------------------------------------------------------------------------

# 3. Non-Goals

1. **Real partition-type-GUID-aware selection.** Distinguishing "the ArcFS partition" from "the
   ESP" by real device-path or `EFI_PARTITION_INFO_PROTOCOL` inspection would need MORE new
   bindings than this RFC's own scope (device-path parsing has no binding surface in this compiler
   at all today). This RFC selects by discovery-order INDEX instead — a real, deliberate,
   documented simplification, not a silent gap (see Section 7.2).
2. **Full `LocateHandleBuffer`'s own pool-allocation semantics.** `LocateHandle` (caller-provided
   fixed buffer, no `AllocatePool`/`FreePool` lifecycle) is used instead — simpler, and this
   project's own established "no heap allocator on this backend" precedent already prefers fixed
   scratch buffers everywhere else.
3. **General multi-device support beyond exactly two partitions on one disk.** The discovery
   mechanism itself is real and general (finds every BlockIo handle, not just two), but the
   fixture and image-building tool this RFC delivers are scoped to the ESP-plus-one-data-partition
   shape a real daily-driver install needs, not arbitrary N-partition layouts.
4. **Booting the resulting system into anything beyond a render-and-halt confirmation.** What the
   OS actually DOES once ArcFS is mounted (a shell, a service loop, anything interactive) is
   explicitly out of scope — matching RFC-0043's own Non-Goal 5.

------------------------------------------------------------------------

# 4. Requirement: Real Multi-Handle BlockIo Enumeration

## 4.1 The compiler-level gap

`UEFI.BLOCKIO.DISCOVER` (RFC-0038) is a hand-assembled `LocateProtocol` call — by the UEFI
specification's own definition, `LocateProtocol` returns AT MOST ONE handle, implementation-chosen
among however many actually offer the protocol. There is no way, using only what this compiler
already binds, to ask firmware "show me every block device," which is exactly what selecting a
SPECIFIC partition among several on one disk requires.

## 4.2 What closes it, and why it's smaller than it first looks

Two EFI Boot Services this compiler does not yet bind, `HandleProtocol` (`EFI_BOOT_SERVICES`
offset `0x98`) and `LocateHandle` (offset `0xB0`), can be added as ordinary declarative
`UefiField` table entries in `include/arco/uefi_bindings.hpp` — the exact same mechanism
`GetMemoryMap`/`AllocatePages`/`AllocatePool`/`LocateProtocol` already use (confirmed directly:
`GetMemoryMap`'s own real call site, `tests/fixtures/uefi-memory-map/acquire-memory-map.abas`,
already takes five raw pointer/scalar arguments through this identical generic `CallExternal`
path — the same shape `LocateHandle` needs). No new hand-assembled x86-64 codegen is needed, unlike
`BLOCKIODISCOVER`'s own special-cased path.

`LocateHandle(SearchType, Protocol, SearchKey, *BufferSize, *Buffer)` with `SearchType=2`
(`ByProtocol`) and a fixed, generously-sized caller-provided buffer (enough real `EFI_HANDLE`
slots for any realistic disk topology) returns every handle offering `EFI_BLOCK_IO_PROTOCOL`.
`HandleProtocol(Handle, Protocol, *Interface)` then resolves each discovered handle to its real,
usable `EFI_BLOCK_IO_PROTOCOL*` — reusing the SAME verified GUID bytes `BLOCKIODISCOVER` already
uses.

## 4.3 New stdlib surface

- `UefiBlockDevice.DiscoverAll(systemTable) AS U64` — populates a fixed scratch table (handle,
  MediaId, BlockSize, LastBlock, writable flag per slot) and returns the real count found.
- `UefiBlockDevice.SelectDiscovered(index) AS BOOL` — points the SAME state
  `ArcFSBlockReadSectors`/etc. already dispatch through at the `index`-th discovered device
  (0-based, in whatever order firmware itself returned handles) — everything downstream
  (`ArcFS.SelectBlockDevice`, `ArcFS.FormatVolume`, `ArcFS.ActivateSystemVolume`) works completely
  unchanged once this points at the right device.

------------------------------------------------------------------------

# 5. Requirement: A Real GPT-Partitioned Image

A new, deterministic tool (matching `build-arcology-hardware-image.py`'s own established
discipline: fixed geometry, byte-reproducible output, no external dependencies beyond `python3`)
builds a single raw disk image containing:

1. A protective MBR (required by the GPT specification so non-GPT-aware tools see one large,
   already-allocated partition rather than a seemingly-blank disk).
2. A real GPT header and partition table (primary and backup).
3. Partition 1 — the ESP: FAT32, type GUID `C12A7328-F81F-11D2-BA4B-00A0C93EC93B`, containing
   `/EFI/BOOT/BOOTX64.EFI` (reuses the existing FAT32-building logic from
   `build-arcology-hardware-image.py` directly, refactored into a shared, importable function
   rather than duplicated).
4. Partition 2 — reserved, unformatted space for ArcFS (ArcFS formats it itself via
   `ArcFS.FormatVolume()`; the image-building tool only needs to reserve real, correctly-declared
   LBA space for it, matching real installer conventions of leaving the target partition raw
   until the OS's own installer/first-boot formats it).

------------------------------------------------------------------------

# 6. Requirement: The Real Fixture and QEMU Proof

A new fixture, combining this session's own `render-and-halt.abas` (real GOP render + pixel
verification) with RFC-0043 Phase V's own already-proven `ArcFS.ActivateSystemVolume()`/System
Namespace pattern:

1. Firmware boots `BOOTX64.EFI` from the ESP (inherent — no new mechanism, matches every fixture
   in this project's history).
2. `UefiBlockDevice.DiscoverAll` finds every real BlockIo handle on the (single, real) disk.
3. `UefiBlockDevice.SelectDiscovered(1)` (index 1 — the second partition, matching Section 5's own
   layout) targets ArcFS's own dispatch at the data partition specifically, not the ESP.
4. `ArcFS.FormatVolume()`/`ArcFS.ActivateSystemVolume()` — a real ArcFS volume, live on the second
   partition of the SAME physical disk the system just booted from.
5. A real GOP render (this session's own `render-and-halt.abas` logic) confirms visibly, with the
   same pixel-readback self-verification already proven, before halting.

Proven under QEMU with a genuinely GPT-partitioned disk image attached as a single, real device
(not two separate `-drive` entries) — the same real device topology a physical USB stick will
have.

------------------------------------------------------------------------

# 7. AI Implementation Guidance

## 7.1 Required boundaries

- Every new EFI Boot Services binding gets its GUID/offset verified against the same TianoCore
  EDK2 reference headers `uefi-bindings.md` already cites for every existing binding, not assumed
  from memory alone — cross-check in generated x86-64 machine code before trusting it, matching
  this project's own established discipline for every prior UEFI binding.
- A structural (AST/A-MIR/X86_64) smoke test proves each new binding compiles correctly BEFORE
  any real QEMU proof is attempted, matching `systems_blockio_discovery_smoke`'s own precedent.
- The real QEMU proof MUST use a genuinely multi-handle disk topology (at minimum: the GPT image's
  own two real partitions, each independently discoverable) — a proof against a single-handle
  setup would not actually exercise the new enumeration/selection mechanism.

## 7.2 No silent scope reduction without saying so

If discovery-order index selection (Section 4.2) turns out to be unreliable across repeated QEMU
runs (firmware enumeration order not staying consistent), or if the fixed scratch buffer size
turns out to be genuinely too small for some real topology, state the finding and the revised
approach explicitly in that phase's own report — matching RFC-0042 Section 15.2's own mandate.

## 7.3 Mandatory acceptance evidence

- Phase 1 (multi-handle enumeration): a real fixture with at least two genuinely distinct BlockIo
  devices attached discovers BOTH (real, different MediaId/LastBlock values read back for each,
  not just a count), and `SelectDiscovered` genuinely routes subsequent reads/writes to the
  selected one — not the other.
- Phase 2 (GPT image tool): the built image's own GPT header and partition entries independently
  verified (parseable structure, correct type GUIDs, correct LBA ranges) before ever being booted.
- Phase 3 (fixture + QEMU proof): a real single-disk boot, real partition-2 discovery/selection,
  real ArcFS format+mount+render, with a negative control and 3x determinism, matching every other
  proof in this project's history.
- Phase 4 (hardware package): matches `WP-026`/`WP-029`/`WP-030`'s own established template; status
  stays `READY FOR HUMAN EXECUTION — NOT YET VALIDATED`.

------------------------------------------------------------------------

# 8. Implementation Phases

- **Phase 1 — Multi-Handle Enumeration** (Section 4). New `HandleProtocol`/`LocateHandle`
  compiler bindings; new `UefiBlockDevice.DiscoverAll`/`SelectDiscovered` stdlib surface.
- **Phase 2 — GPT Image Builder** (Section 5). New, deterministic, byte-reproducible tool.
- **Phase 3 — Fixture and QEMU Proof** (Section 6).
- **Phase 4 — Hardware Validation Package**. A new `WP-0xx` package for the same Lenovo E15 Gen 2,
  single-disk layout.

Phase 1 blocks Phase 3 (nothing to select without real discovery). Phase 2 is independent of
Phase 1 and can proceed in parallel. Phase 4 depends on Phase 3.

------------------------------------------------------------------------

# 9. Revision History

| Version | Date       | Changes |
|---------|------------|---------|
| 0.1     | 2026-08-22 | Initial draft, written directly after the user's own explicit choice ("One disk, real ESP + ArcFS partition") over the simpler two-USB-stick alternative. Scoped around the real, concrete gap that choice exposes: `UefiBlockDevice.Discover`'s own pre-existing "first handle wins" limitation (already named as a real safety caveat in `WP-029`) cannot distinguish an ESP from an ArcFS partition on the same disk. Four phases: real multi-handle enumeration (reusing the existing generic `CallExternal` binding mechanism, not new hand-assembled codegen — confirmed smaller in scope than initially estimated by checking `GetMemoryMap`'s own real multi-pointer-argument call shape first), a real GPT image-building tool, a real single-disk fixture+QEMU proof, and an updated hardware validation package. Status: Draft; no implementation phase has begun. |
| 0.2     | 2026-08-22 | All four phases implemented and QEMU-proven. Phase 1 (`HandleProtocol`/`LocateHandle` bindings, `UefiBlockDevice.DiscoverAll`/`SelectDiscovered`) proved real, DIFFERENT `LastBlock` values across genuinely distinct handles and that `SelectDiscovered` genuinely routes I/O to the selected device, not another (commit `2b4aa12`). Phase 2 (`build-arcology-gpt-image.py` + shared `fat32_esp_image.py` + `verify_gpt_image.py`) built a real GPT-partitioned image (protective MBR, real primary/backup GPT, ESP + reserved ArcFS partition), independently verified three separate ways (a real from-scratch parser with a real negative control, `parted`+`sfdisk` cross-checks, a real loop-mounted ESP read) and confirmed booting under real firmware GPT-aware enumeration (commit `52013dd`). Phase 3 (`gpt-sysvol-render.abas`) combined the whole chain in one boot — real ESP boot, real multi-handle discovery, real `ArcFS.FormatVolume`/write/commit/`ActivateSystemVolume`, real GOP render + pixel-readback self-verification — with 3x determinism and a real negative control. **A real, honest finding, exactly the kind Section 7.2 anticipated**: Section 6's own literal `SelectDiscovered(1)` assumption was WRONG for the real topology — a single GPT disk under this QEMU/OVMF combination yields FOUR real BlockIo handles (whole disk, one unexplained additional handle, ESP, ArcFS), not two. Phase 3's fixture instead identifies the ArcFS partition by its own known geometry (`SectorCount = 16384`), the same technique Phase 1's own `multi-blockio-discovery.abas` already proved — not a silent workaround, stated explicitly in the fixture's own header and in `WP-031`. Phase 4 (`WP-031-gpt-sysvol-render-hardware-validation.md`) packaged for the same Lenovo ThinkPad E15 Gen 2; status `READY FOR HUMAN EXECUTION — NOT YET VALIDATED`, matching every prior hardware package in this project. Full regression suite passing throughout. |
