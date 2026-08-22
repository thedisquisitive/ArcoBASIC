# RFC-0044 Phase 3 + 4: Combined Fixture, QEMU Proof, and Hardware Validation Package

**Status:** Phase 3 and 4 delivered. QEMU-proven end-to-end; Phase 4's own hardware package
(WP-031) remains unexecuted on real hardware, matching this project's standing rule.

## Scope delivered

Phase 3 (RFC-0044 Section 6): a single fixture, `gpt-sysvol-render.abas`, combining:

1. Real firmware ESP boot from the Phase-2 GPT-partitioned disk image (inherent — no new
   mechanism).
2. Phase 1's real multi-handle `EFI_BLOCK_IO_PROTOCOL` enumeration (`UefiBlockDevice.DiscoverAll`).
3. Selection of the ArcFS partition specifically among the discovered handles.
4. Real `ArcFS.FormatVolume()` / `ArcFS.MountImage()` / a real file created, written, and
   committed / `ArcFS.ActivateSystemVolume()` (RFC-0039 Phase G's own real boot policy).
5. Real GOP render + pixel-readback self-verification, reusing `render-and-halt.abas`'s own
   proven logic (RFC-0006/WP-030), before halting.

Phase 4 (RFC-0044 Section 8): `WP-031-gpt-sysvol-render-hardware-validation.md`, a new hardware
validation package for the same Lenovo ThinkPad E15 Gen 2, matching WP-026/WP-029/WP-030's own
established template.

## A real gap found and closed: discovery order, not the index RFC-0044 §6 assumed

RFC-0044 Section 6 wrote `UefiBlockDevice.SelectDiscovered(1)` — index 1, "the second partition" —
as its own working assumption for which discovered handle is the ArcFS partition. Before writing
the real fixture, this was checked empirically with a standalone probe (reusing the Phase 1
`multi-blockio-discovery.abas` fixture's own diagnostic logic, retargeted at booting from the
Phase 2 GPT image as the SOLE disk rather than two separate `-drive` entries):

```
HC=4
I=0 LB=149536   (the whole disk itself -- LastBlock matches the image's own total sector count - 1)
I=1 LB=0        (an additional handle this project does not explain -- LastBlock=0)
I=2 LB=131071   (the ESP -- LastBlock matches its own 131072-sector size - 1)
I=3 LB=16383    (the ArcFS partition -- LastBlock matches its own 16384-sector size - 1)
```

Four real handles, not two. Index 1 is NOT the ArcFS partition in this real topology — it is some
other handle (LastBlock=0) neither this project's own image nor its own fixtures create; most
likely a firmware/PartitionDxe internal artifact of GPT-aware partition enumeration on top of the
protective-MBR-plus-real-GPT layout, but its exact identity was not investigated further (out of
scope: this project selects by known geometry, not by explaining every handle firmware chooses to
expose).

This is exactly the finding RFC-0044 Section 7.2 anticipated and required to be stated explicitly
rather than silently patched over. The fix: `gpt-sysvol-render.abas` identifies the ArcFS partition
by its own known, real geometry (`DiscoveredSectorCount(i) = 16384`, the fixed size
`build-arcology-gpt-image.py`'s own `ARCFS_SECTORS` reserves) instead of trusting a hardcoded index
— the identical technique Phase 1's own `multi-blockio-discovery.abas` fixture already proved
against a genuinely different two-disk topology. No new binding or mechanism was needed; this is
purely a selection-strategy change in the fixture itself.

## Validation

- Fixture compiles cleanly (`SOURCE ACCEPTED` / `STRUCTURE ASSEMBLED` / `X86_64 GENERATED`).
- Real positive proof, first attempt: booting the Phase-2-shaped GPT image (with this fixture as
  its ESP payload) via `run-uefi-image-with-usb-gpu.sh` (real `qemu-xhci`+`usb-storage`, real
  `-vga std`) reached `ARCOLOGY GPT SYSVOL DONE` on the first try. Full serial trace confirms every
  intermediate stage genuinely happened in order: `STAR` (start) → `CMOK` (format+write+commit
  succeeded) → `ACTO` (real `ArcFS.ActivateSystemVolume` boot-policy re-activation recognized the
  volume as Healthy) → `DONE` (GOP render + both pixel-readback self-verification checks passed).
- Negative control: corrupting the expected accent-pixel value produces `ARCOLOGY GPT SYSVOL VFAC`
  honestly, not a false `DONE`.
- 3x determinism confirmed on the positive path.
- New smoke test `systems_arco_basic_gpt_sysvol_render_smoke` (structural compile check + real
  QEMU proof + 3x determinism + negative control), registered in `Testing.cmake`.
- Full regression suite passing (91 tests total after this increment).

## What this does and does not close

Closes, for the first time in this project: a real ArcFS system volume living on the SAME physical
disk the system booted from, formatted/written/committed/re-activated via the real boot-policy
path, with real GOP confirmation, all in one boot — the actual shape a real installed OS uses.
Every prior ArcFS-on-real-hardware proof (RFC-0043 Phases T/V, WP-029) used two separate
disks/devices.

Does NOT close: RFC-0041 System Namespace attachment was deliberately left out of this fixture,
matching RFC-0044 Section 6's own literal 5-step scope (the RFC does not mention
`Namespace.AttachFilesystem`) — a natural follow-on if wanted, not a silent gap; the four-handle
discovery finding's own third handle (index 1, `LastBlock=0`) was not investigated further, only
worked around by selecting on geometry instead; and — the standing rule that applies to every
package in this project — nothing here has been confirmed on REAL hardware yet. `WP-031` joins
`WP-026`/`WP-029`/`WP-030`, all still `READY FOR HUMAN EXECUTION — NOT YET VALIDATED`.

## Documented scope reductions

- Selection by geometry (`SectorCount` match) rather than partition-type-GUID inspection —
  RFC-0044 Non-Goal 1, unchanged from the RFC's own original scope.
- Namespace attachment out of scope for this fixture (see above).
- The mysterious 4th handle's own identity not investigated (see above) — a real, open, minor
  curiosity, not a blocker for anything this RFC needs.
