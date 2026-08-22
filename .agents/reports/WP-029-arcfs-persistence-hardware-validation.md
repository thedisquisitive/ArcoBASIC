# WP-029: ArcFS Persistence Physical Hardware Validation Package

**Status:** READY FOR HUMAN EXECUTION — NOT YET VALIDATED
**Date prepared:** 2026-08-22

Do not mark this report complete from QEMU results. Fill every bracketed field during a physical
test and attach photographs using repository-relative paths. Matches `.agents/reports/
WP-026-hardware-validation-package.md`'s own template and standing rule: no result here is ever
inferred from QEMU output, only from an actual person on an actual machine.

## What this validates

RFC-0043 Phase T's own real, QEMU-proven claim — that ArcFS data genuinely survives past the
process that wrote it — re-proven for real on physical hardware, past QEMU/OVMF software emulation
entirely. This is the concrete deliverable RFC-0043 Section 10 names: the same two-process
cross-boot design Phase T already validated under QEMU (`.agents/reports/aps-arcfs-phase-s-t.md`),
run instead as two real, separate power cycles on a real machine.

## Build Identity

- Git commit: `e7b0ff75215933f5bc424747d20319dac0bda036` (`ArcFS Phase W: long-session durability`) — the artifacts below were built from this commit; a later commit that touches `arcfs_policy.abas`/`uefi_block_device_policy.abas`/`block_device_policy.abas` or either fixture in `tests/fixtures/arcfs-persist/` should get a fresh build before physical testing.
- Dirty-tree statement at physical test: `[required; use a clean checkout of the commit above]`
- Compiler/version: `ArcoFission 0.1.0`
- Writer image filename: `arcfs-persist-writer-x86_64.img`
- Reader image filename: `arcfs-persist-reader-x86_64.img`
- Artifact directory: `arcology-os/dist/arcfs-persist-validation/`
- Writer EFI SHA-256: `43978a1f4d4e8a17ad0ec2d2f39b77543df57b54f401ad8618e3d24a9e733c33`
- Reader EFI SHA-256: `8b41317a282da4e4fadbd05b462b364ad8a2fd76446a66e84def217430081529`
- Writer image SHA-256: `5fa1e5a12e217f84d6db4c92fa13c349ec94823990fc0c78a6cd1706938a0196`
- Reader image SHA-256: `048bfbdae7ba8cdbe0d6eddfc93223c0e976202d1781f62026a310a909728451`
- Full checksums: `arcology-os/dist/arcfs-persist-validation/SHA256SUMS`
- Media write command/tool: `[required]`

**Pre-physical-test sanity check already performed (QEMU, not a substitute for the checklist
below, but confirms these exact artifact bytes are mechanically sound before spending real media/
time on them):** both images boot correctly as raw removable-media images (`dd`-equivalent write,
`run-arcology-hardware-image.sh`); a real two-drive QEMU run (real, distinct `virtio-blk-pci`
devices, matching Phase T's own harness) confirmed the writer image formats a real ArcFS
superblock onto a genuinely separate data disk (`b'ARCFSB02'` read directly off the resulting
image file) while leaving its own boot media's FAT32 structure untouched, and the reader image,
run as a separate QEMU process against that same data disk, reports `PERSIST READER DONE`.

## Test Platform

- Laptop/PC manufacturer/model: `[required]`
- CPU: `[required]`
- Firmware vendor: `[required]`
- Firmware version/date: `[required]`
- UEFI mode enabled: `[yes/no]`
- Legacy/CSM state: `[enabled/disabled/unavailable]`
- Secure Boot state: `[enabled/disabled]`
- Boot media (removable) make/model/capacity: `[required]`
- Data disk (target ArcFS volume) make/model/capacity: `[required]`

## Building these artifacts

`arcology-os/dist/` is gitignored (matching this project's own existing convention for the
original `arcology-seed-0.1` artifact) — the images and checksums above are not committed, but are
fully, deterministically reproducible from the commit named above:

```
cd arcology-os
ArcoFission build tests/fixtures/arcfs-persist/aps-arcfs-persist-writer.abas \
    -o dist/arcfs-persist-validation/writer-BOOTX64.EFI --target uefi-x86_64 --entry Main
ArcoFission build tests/fixtures/arcfs-persist/aps-arcfs-persist-reader.abas \
    -o dist/arcfs-persist-validation/reader-BOOTX64.EFI --target uefi-x86_64 --entry Main
python3 scripts/build/build-arcology-hardware-image.py \
    dist/arcfs-persist-validation/writer-BOOTX64.EFI \
    dist/arcfs-persist-validation/arcfs-persist-writer-x86_64.img
python3 scripts/build/build-arcology-hardware-image.py \
    dist/arcfs-persist-validation/reader-BOOTX64.EFI \
    dist/arcfs-persist-validation/arcfs-persist-reader-x86_64.img
cd dist/arcfs-persist-validation
sha256sum writer-BOOTX64.EFI reader-BOOTX64.EFI \
    arcfs-persist-writer-x86_64.img arcfs-persist-reader-x86_64.img > SHA256SUMS
```

`build-arcology-hardware-image.py` is this project's own existing, byte-reproducible FAT32
UEFI-removable-media image builder (originally built for WP-024/WP-026's own `arcology-seed-0.1`
artifact) — reused here directly, unmodified, with the writer/reader `.EFI` files as input.

## A real safety warning, found while preparing this package, not assumed away

`UefiBlockDevice.Discover` (RFC-0038) has a documented, pre-existing scope limitation: it returns
whichever `EFI_BLOCK_IO_PROTOCOL` handle firmware presents FIRST, with no way to target a specific
device among several — real multi-handle enumeration/selection was never built (see that
function's own header note in `stdlib/uefi_block_device_policy.abas`). Under QEMU, Phase T's own
proof controls this deterministically via explicit PCI `addr=` ordering; **real hardware's own
USB/SATA/NVMe enumeration order cannot be controlled the same way**, and a self-test during this
package's own preparation confirmed the writer app, booted alone with no second drive attached,
successfully discovers and formats **its own boot media** — there is nothing that stops it from
doing the same on a real machine if the boot device happens to enumerate before the intended data
disk. **Use a boot medium you do not mind erasing.** Do not use a drive containing anything else
you value as the boot medium for this test. The data disk is the one whose contents this test is
actually about — treat the boot medium as fully disposable.

## Materials

Two SEPARATE physical storage devices are required, matching the real, separate `-drive` entries
the QEMU proof itself uses (RFC-0043 Phase T's own design, not a simplification here):

1. **Boot media** — any removable drive the firmware can boot `BOOTX64.EFI` from. Reused for both
   stages below (its own `BOOTX64.EFI` is swapped between the writer and reader image).
2. **Data disk** — any spare disk or removable drive at least 4 MiB, completely separate from the
   boot media. ArcFS formats this itself; no pre-formatting needed. **This drive's existing
   contents will be overwritten during Stage 1 — use a spare or already-blank device.**

## Checklist

### Stage 1: Writer

- [ ] `sha256sum -c SHA256SUMS` passes for `arcfs-persist-writer-x86_64.img` before writing media.
- [ ] The boot media was independently confirmed as removable.
- [ ] The data disk was independently confirmed as removable/spare (its contents will be lost).
- [ ] `arcfs-persist-writer-x86_64.img` written to the boot media.
- [ ] Both the boot media and the data disk are attached to the target machine.
- [ ] Firmware detects the boot media and loads `EFI/BOOT/BOOTX64.EFI`.
- [ ] `PERSIST WRITER PRE` appears on screen.
- [ ] `PERSIST WRITER RAMDISK CLEAN` appears (confirms the write genuinely went to the real data
      disk, not a RAM-only fallback).
- [ ] `PERSIST WRITER DONE` appears.
- [ ] The machine is powered OFF completely — a real cold shutdown, not a warm reboot, and not
      QEMU's own `-no-reboot` convention. Wait at least 10 real seconds before proceeding.

### Stage 2: Reader

- [ ] `sha256sum -c SHA256SUMS` passes for `arcfs-persist-reader-x86_64.img` before writing media.
- [ ] `arcfs-persist-reader-x86_64.img` written to the SAME boot media, replacing the writer image.
- [ ] The SAME data disk from Stage 1 (untouched, not reformatted) is attached to the target
      machine, together with the boot media.
- [ ] The machine is powered ON — a genuine cold boot, not a resume/wake from the Stage 1 shutdown.
- [ ] Firmware detects the boot media and loads `EFI/BOOT/BOOTX64.EFI`.
- [ ] `PERSIST READER PRE` appears on screen.
- [ ] `PERSIST READER DONE` appears — this is the real proof: the reader process shares no memory
      with the writer process (a different boot of a different program entirely), and the content
      it just read back was written by Stage 1, then survived a real, complete power cycle on real
      storage hardware.
- [ ] The display remains stable for at least 60 seconds after `PERSIST READER DONE`.
- [ ] The application does not return to firmware or reset on its own.
- [ ] A second power cycle (off, wait, on) exits the halt normally.

## Observation

- Stage 1 start time/timezone: `[required]`
- Stage 1 exact observed behavior: `[required]`
- Stage 1→2 power-off duration: `[required]`
- Stage 2 start time/timezone: `[required]`
- Stage 2 exact observed behavior: `[required]`
- Failure classification, if any (RFC-0006 section 9): `[required if applicable]`
- Stable-halt duration (Stage 2): `[required]`
- Unexpected behavior: `[none or details]`
- Photograph paths (both stages): `[required]`
- Tester name/identifier: `[required]`

## Firmware Quirk Decision

- Quirk observed: `[yes/no]`
- If yes, RFC-0006 Appendix A entry: `[link/section]`
- Root cause evidence: `[required if claimed]`
- Resolution/workaround: `[required if applied]`

## Sign-Off

- [ ] All fields above are complete.
- [ ] Evidence corresponds to the checksummed artifacts named above.
- [ ] No result was inferred from QEMU.
- Human validator/signature: `[required]`
- Date: `[required]`
