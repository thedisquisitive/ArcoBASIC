# ArcologyFS (ArcFS) Phase V: Boot-Path Integration

## Scope delivered

RFC-0043 Phase V (Section 8) -- investigate-first, per this phase's own explicit mandate: "confirm
what `.agents/reports/aps-arcfs-phase-g.md`'s own system-volume activation work and RFC-0041 Phase
A already established about this handoff before writing new code, and build only the concrete gap
that investigation finds."

## The investigation

Read `.agents/reports/aps-arcfs-phase-g.md` (RFC-0039 Phase G, all four addenda) and
`stdlib/system_namespace_policy.abas` (RFC-0041) directly, then confirmed by grep rather than
assumption what each of their own existing QEMU fixtures actually exercises:

- **UEFI firmware loading `BOOTX64.EFI` from a FAT-formatted boot medium** ("the same role a Linux
  ESP plays," RFC-0043 Section 8.1's own framing): already true of literally every UEFI fixture
  this entire project has ever built, inherent to the UEFI spec itself (firmware understands FAT,
  nothing else -- RFC-0043 Non-Goal 4). Not a gap; nothing to build.
- **`ArcFS.ActivateSystemVolume()`** (RFC-0039 Phase G, the real boot-time system-volume policy:
  mount safely, self-heal a recoverable defect automatically, re-verify before ever surfacing
  ReadOnlySafety): exists and is already proven under QEMU (`aps-arcfs-system-volume.abas`) -- but
  `grep -c "RAMDisk\." aps-arcfs-system-volume.abas` returns 46, `UefiBlockDevice\.` returns 0.
  Every existing proof of this function uses RAMDisk exclusively.
- **The RFC-0041 Arcology System Namespace's own filesystem attachment/delegation**
  (`Namespace.AttachFilesystem`/`Namespace.Resolve`, proven in `system-namespace.abas`): the
  identical finding -- 45 `RAMDisk.*` references, 0 `UefiBlockDevice.*`.
- **RFC-0043 Phase S/T** (this session's own prior increment) closed the ONE item `aps-arcfs-
  phase-g.md` explicitly named as still-open at the time it was written ("DISCOVER-level real
  hardware enumeration remains RFC-0038's own future work") -- real `UefiBlockDevice.Discover` plus
  `ArcFS.SelectBlockDevice` now exist and are QEMU-proven, including surviving a real process
  boundary. But Phase T's own writer/reader fixtures called plain `ArcFS.MountImage()`, never
  `ArcFS.ActivateSystemVolume()`, and never touched the System Namespace layer at all.

**The concrete gap, and only it**: neither `ArcFS.ActivateSystemVolume()` nor the System
Namespace's own attachment/delegation mechanism has ever been proven against a REAL persistent
block device, or across a real process boundary. Both mechanisms are internally generic over
"whatever `ArcFSBlockReadSectors`/etc. currently dispatch to" (confirmed by reading `ArcFS.
ActivateSystemVolume`'s own body: it calls only `ArcFS.MountImageSafe`/`RepairReattachOrphans`/
`RepairCommit`, all of which already route through the Phase S dispatch layer with zero
bitmap/namespace-specific code of their own) -- so this is a proof gap, not a missing-mechanism
gap. No new ArcFS or Namespace function was needed.

## What was built

Two genuinely separate fixtures (`aps-arcfs-sysvol-writer.abas`/`aps-arcfs-sysvol-reader.abas`,
new directory `arcology-os/tests/fixtures/arcfs-sysvol/`), mirroring RFC-0043 Phase T's own
two-process cross-boot technique exactly, run as two separate `qemu-system-x86_64` processes
against the same real disk image file:

- **Writer**: discovers a real attached disk (separate from the FAT-formatted boot medium every
  fixture already uses), selects it, formats a fresh ArcFS volume, creates a real file with real
  content, commits, confirms RAMDisk's own memory was never touched, fully exits.
- **Reader**: a genuinely separate process/compile. Discovers the SAME real disk, calls `ArcFS.
  ActivateSystemVolume()` -- not a plain `MountImage()` -- for the first time against a real
  persistent block device. Then `Namespace.Initialize()`s a fresh System Namespace, `Namespace.
  AttachFilesystem`es the just-activated volume at `:vol`, and `Namespace.Resolve`s `:vol:doc.txt`
  -- a path that delegates across the attachment boundary into real ArcFS content, also for the
  first time over a real disk that has genuinely survived a real process boundary. Confirms the
  delegated resolution reaches the SAME OID a direct `ArcFS.Lookup` call would, not a
  coincidentally-matching but different object.

## A real capacity finding, not a bug -- the same cost RFC-0043 Phase U already named

The first attempt failed at `ArcFS.CommitImage()` with a 256 KiB (512-sector) disk -- the same size
Phase T's own fixtures use. Traced through concretely before assuming a logic bug: Phase T's own
`aps-arcfs-persist-writer.abas` is a FROZEN fixture (this project's established convention --
inlines its own complete copy of `arcfs_policy.abas` as of Phase T's own authoring time, BEFORE
Phase U's multi-sector bitmap existed), so it was never affected. This Phase V fixture, freshly
authored against the CURRENT live stdlib, genuinely needs the real space Phase U's own report
already named: `ArcFS.FormatVolume()` alone now reserves ~400 sectors, and the first real
`ArcFS.CommitImage()` allocates another ~395 for its own fresh bitmap span -- comfortably exceeding
a 512-sector disk. Confirmed directly (a 4 MiB disk resolved it immediately, isolating the cause to
capacity, not logic) before changing the fixture's own disk size to 4 MiB.

## Validation

- All touched entry points (`ArcFS.ActivateSystemVolume`, `ArcFS.SelectBlockDevice`, `Namespace.
  Initialize`, `Namespace.AttachFilesystem`, `Namespace.Resolve`) compile cleanly at X86_64
  codegen level.
- **The real proof, executed under QEMU/OVMF**: writer formats/creates/commits real content to a
  real disk and confirms RAMDisk stayed untouched; a genuinely separate reader process activates
  the same real volume via `ArcFS.ActivateSystemVolume()`, attaches it into a fresh System
  Namespace, resolves `:vol:doc.txt` across the attachment boundary, reads the real content back
  byte-for-byte, and confirms the resolved OID matches a direct `ArcFS.Lookup`.
- Negative control: a reader variant with the OID-match check inverted produces a real, non-vacuous
  `SYSVOL READER OID MISMATCH`, not a false pass.
- Deterministic across 3 full writer+reader cycles, each against a freshly zeroed disk image.
- New smoke test `systems_arco_basic_arcfs_sysvol_persist_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest` (76.68s).
- Full regression suite re-run -- see this report's own follow-up note or the RFC-0043 revision
  history for the final count.

## Documented scope reductions

1. **Only the healthy-activation path is proven against a real disk.** `ArcFS.
   ActivateSystemVolume`'s own self-healing branch (repair-then-reactivate on a recoverable
   defect) already has a real, separate QEMU proof (`aps-arcfs-system-volume.abas`, RAMDisk-backed)
   from RFC-0039 Phase G -- re-proving self-healing specifically over a real persistent disk was
   not required by this phase's own acceptance criteria (RFC-0043 Section 13.3 asks for "a real
   fixture... demonstrating firmware-to-ArcFS-root handoff," which the healthy path already
   satisfies) and was not attempted here, to keep this increment narrowly scoped to the concrete
   gap investigation actually found.
2. **One file, no directories, no attributes.** The proof is deliberately minimal -- enough to
   exercise real activation + real namespace delegation end to end across a real process boundary,
   not a stress test. RFC-0043 Section 9 (long-session durability, Phase W) is the separate,
   already-scoped place for a larger/longer proof.

## RFC-0043 status

Phase V is implemented and QEMU-proven (as a proof-only increment; no ArcFS or Namespace code
changed). Remaining named phases: W (long-session durability), X (physical-hardware validation
package, depends only on Phase T, unblocked).
