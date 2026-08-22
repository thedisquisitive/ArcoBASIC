# ArcologyFS (ArcFS) Phase X: Physical-Hardware Validation Package

## Scope delivered

RFC-0043 Phase X (Section 10): a real physical-hardware validation package for RFC-0043 Phase T's
own persistence claim, matching `.agents/reports/WP-026-hardware-validation-package.md`'s own
established template exactly. Delivered as `.agents/reports/
WP-029-arcfs-persistence-hardware-validation.md`.

- Build identity (commit, real SHA-256 checksums for both `.EFI` files and both wrapped images).
- Test-platform fields for the human tester to fill in.
- A two-stage checklist (write the writer image, boot, confirm; a REAL cold power-off; write the
  reader image to the same boot media, boot again with the same data disk attached, confirm).
- An observation section and a sign-off section requiring a human validator.
- Status: `READY FOR HUMAN EXECUTION — NOT YET VALIDATED`, and stays that way until an actual
  person actually runs it -- this report does not, and the phase report does not, claim physical
  validation from any QEMU result, matching this project's own standing rule (`.agents/reports/
  WP-026-hardware-validation-package.md`'s own explicit warning, repeated verbatim in the new
  package).

## Reused, not rebuilt

`arcology-os/scripts/build/build-arcology-hardware-image.py` -- this project's own existing,
byte-reproducible FAT32 UEFI-removable-media image builder, originally built for WP-024/WP-026's
own `arcology-seed-0.1` artifact -- was reused directly, unmodified, with RFC-0043 Phase T's own
existing writer/reader fixtures (`tests/fixtures/arcfs-persist/aps-arcfs-persist-{writer,reader}.
abas`) as input. No new build tooling was needed.

## A real self-corruption incident during preparation, found and explained, not hidden

The first attempt to sanity-check the built writer image under QEMU (a single `-drive
file=$IMAGE,format=raw` boot, no second drive attached) silently corrupted the built artifact: the
writer fixture, given no genuinely separate data disk to discover, called `UefiBlockDevice.
Discover` and found its OWN boot media as the only real block device, then formatted a real ArcFS
volume directly onto it -- overwriting the FAT32 structure with a live ArcFS superblock (confirmed
directly: the corrupted file's own first 8 bytes read `ARCFSB02`, not the expected FAT32 boot-
sector bytes). This was traced through and understood before assuming any bug in the image-
building tool -- the tool itself was independently confirmed correct via a byte-for-byte diff
against a fresh rebuild. Fixed by rebuilding clean and being deliberate about which files QEMU
sanity checks ever touch (scratch copies only, never the artifact directory's own files).

## A real safety finding surfaced by that incident, documented in the package itself

The same root cause -- `UefiBlockDevice.Discover`'s own documented, pre-existing scope limitation
(it returns whichever `EFI_BLOCK_IO_PROTOCOL` handle firmware presents first, with no way to target
a specific device among several, see that function's own header note) -- is a REAL risk for a human
tester on real hardware, not just a QEMU artifact-preparation mistake. Real USB/SATA/NVMe
enumeration order cannot be controlled the deterministic way Phase T's own QEMU proof controls it
(explicit PCI `addr=` ordering). The validation package itself now carries an explicit warning:
use a boot medium the tester does not mind erasing, since nothing prevents the writer app from
formatting its own boot media if firmware happens to present it before the intended data disk.

## Validation

- Both wrapped images (`arcfs-persist-writer-x86_64.img`, `arcfs-persist-reader-x86_64.img`)
  confirmed to carry the correct FAT32 boot signature (`55 AA` at offset 510) and boot correctly as
  raw removable-media images via `run-arcology-hardware-image.sh` (the same harness WP-026's own
  artifact uses), using scratch copies, not the artifact directory's own files.
- **The real sanity check, executed under QEMU/OVMF, using the actual wrapped images (not the
  loose `.EFI` files) as boot media**: a real two-drive run (real, distinct `virtio-blk-pci`
  devices, matching Phase T's own harness design) with the writer image as boot media and a
  separate data disk -- confirmed the writer genuinely formats the real, separate data disk (its
  first 8 bytes read `ARCFSB02` directly off the resulting file after the QEMU process exited)
  while leaving its own boot media's FAT32 structure completely untouched; the reader image, run
  as a separate QEMU process against that same data disk, reports `PERSIST READER DONE`.
- This QEMU-level check is explicitly named in the package itself as a sanity check on the
  artifact bytes, not a substitute for the real checklist -- consistent with RFC-0043 Section 10's
  own "no result inferred from QEMU" rule.

## Documented scope reductions

1. **Two separate physical devices required, not one.** Matches Phase T's own real design (a
   genuinely separate data disk, not a partition on the boot media) rather than a simplified
   single-device test -- avoids any real risk of the boot partition and the ArcFS volume
   colliding on one physical disk's own sectors.
2. **No automatic device-targeting.** See the safety-warning section above -- a real fix (targeted
   multi-handle block-device enumeration) is out of this phase's own scope, matching RFC-0038's own
   long-standing, already-documented limitation.

## RFC-0043 status

Phase X is delivered as a prepared, ready-to-execute package -- not "validated," since that
requires an actual human on actual hardware, which this phase's own scope does not (and should
not) attempt to simulate or claim. With Phase X delivered, all six named RFC-0043 phases (S, T, U,
V, W, X) are now complete: five implemented and QEMU-proven, the sixth (X itself) prepared and
awaiting real-world execution.
