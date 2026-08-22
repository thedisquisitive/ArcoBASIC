# WP-030: "Render and Halt" Physical Hardware Validation Package (Lenovo E15 Gen2)

**Status:** READY FOR HUMAN EXECUTION — NOT YET VALIDATED
**Date prepared:** 2026-08-22

Do not mark this report complete from QEMU results. Fill every bracketed field during a physical
test and attach photographs using repository-relative paths. Matches `.agents/reports/
WP-026-hardware-validation-package.md`'s own template and standing rule: no result here is ever
inferred from QEMU output, only from an actual person on an actual machine.

## What this validates

This is the FIRST physical-hardware test this project has ever prepared with a real graphics
render, not just a text banner — RFC-0006's own original scope (WP-026, still itself unexecuted)
proved firmware would boot and halt a text-only application; this package proves the real GOP
(Graphics Output Protocol) framebuffer binding (RFC-0014) works on real display hardware, at the
real machine's own real resolution, with the render read back and verified before the halt marker
is ever printed — not merely "GOP discovery succeeded."

**Target platform: Lenovo ThinkPad E15 Gen 2.** This is the first validation package in this
project prepared for a specific, named real machine rather than an unspecified "designated
Arcology test laptop" — fill in the exact CPU/firmware fields below during the real test; this
report does not assume them.

## Build Identity

- Git commit: `b32545a6881c64ab00f6bc696cf218375fb8ef06` (`RFC-0006: real GOP "render and halt" bring-up test + real USB boot proof`) — the artifact below was built from this commit (checksums confirmed byte-identical to the pre-commit build, as expected — nothing in the fixture's own source changed after that build). A later commit touching `src/graphics/graphics.cpp`, `stdlib/graphics_primitives.abas`, or `tests/fixtures/render-and-halt/render-and-halt.abas` should get a fresh build before physical testing.
- Dirty-tree statement at physical test: `[required; use a clean checkout of the commit above]`
- Compiler/version: `ArcoFission 0.1.0`
- Image filename: `arcology-render-and-halt-x86_64.img`
- Artifact directory: `arcology-os/dist/render-and-halt/`
- EFI SHA-256: `d3cb63b3cf5efc6f8831ea970431ac2bba96bf0f1e9a7cf080a44041380fd82b`
- Image SHA-256: `27bc14c68fa6aa3c9eb4887c0162608d0e4b077dbf532690edb930abbbc1981a`
- Full checksums: `arcology-os/dist/render-and-halt/SHA256SUMS`
- Media write command/tool: `[required]`

**Pre-physical-test sanity check already performed (QEMU, not a substitute for the checklist
below):** the built image carries the correct FAT32 boot signature (`55 AA` at offset 510) and
boots correctly as a single removable-media image (`run-arcology-hardware-image.sh`, the same
harness WP-026's own artifact uses). Separately, the underlying `.EFI` application was proven under
QEMU/OVMF with a real `-vga std` GOP device attached: it discovers the real framebuffer, fills the
ENTIRE screen (not a fixed small square — the render loop reads the real, discovered
Width/Height/PixelsPerScanLine and scales to them) with a two-tone test card, reads two pixels back
(one from the background field, one from the accent rectangle's own center) and confirms both match
what was just written before ever printing the completion marker. A negative control (corrupting
the expected accent-pixel value) confirmed the verification is real, not vacuous. Deterministic
across 3 repeated QEMU runs.

**Real USB Mass Storage Class boot, specifically, also proven under QEMU** (`run-uefi-image-with-
usb-gpu.sh`, a real `qemu-xhci` USB 3 host controller + `usb-storage` device, not a plain `-drive`
block device): booting via a generic block-device path (as every other check above does) does NOT
prove USB boot works -- real UEFI firmware routes USB media through its own, genuinely distinct USB
host-controller + Mass Storage Class driver stack. This harness exercises that real path. The
built image, attached as a real USB device, was discovered by OVMF's own boot-manager as `"UEFI
... USB HARDDRIVE"` and booted correctly, with both the start and completion markers appearing.
Negative control and 3x determinism confirmed the same way as the GOP-only check above.

## Writing the image to a real USB stick

**Use a raw, byte-for-byte block write (`dd`-equivalent) — never a "burn/extract to filesystem"
tool.** This image is a complete, self-contained FAT32 filesystem starting at byte 0 (a
"superflopy"-style UEFI removable-media image, the same shape WP-026's own artifact already uses)
-- copying it INTO a filesystem on the stick, or letting a tool "extract" it, produces an
unbootable result. The whole point of `dd`-style writing is that the stick's own first bytes
become the image's own first bytes, with no partition table or filesystem-inside-a-filesystem step.

- **Linux/macOS**: `sudo dd if=arcology-render-and-halt-x86_64.img of=/dev/sdX bs=4M status=progress conv=fsync && sync` — replace `/dev/sdX` with the REAL device node for the USB stick (confirm with `lsblk`/`diskutil list` first; writing to the wrong device destroys it). On macOS, unmount first (`diskutil unmountDisk /dev/diskN`) and use `/dev/rdiskN` (the raw, unbuffered device) for speed.
- **Windows**: use Rufus in **DD Image mode** (not the default ISO/partition mode) — Rufus will
  offer to write this file as a raw disk image if selected as "DD Image" rather than treated as an
  installer ISO.
- **Any platform**: `sha256sum -c SHA256SUMS` on the image file BEFORE writing it, and (if your
  tool/OS supports reading back the raw device) re-verify the same checksum against the first
  `67108864` bytes of the USB device AFTER writing, before ever attempting to boot from it.

## Building this artifact

`arcology-os/dist/` is gitignored (matching this project's own existing convention) — the image and
checksums above are not committed, but are fully, deterministically reproducible from the commit
named above:

```
cd arcology-os
ArcoFission build tests/fixtures/render-and-halt/render-and-halt.abas \
    -o dist/render-and-halt/BOOTX64.EFI --target uefi-x86_64
python3 scripts/build/build-arcology-hardware-image.py \
    dist/render-and-halt/BOOTX64.EFI \
    dist/render-and-halt/arcology-render-and-halt-x86_64.img
cd dist/render-and-halt
sha256sum BOOTX64.EFI arcology-render-and-halt-x86_64.img > SHA256SUMS
```

## What you should see

A single, distinctive test card filling the ENTIRE screen: a dark gray field with a large bright
rectangle (yellow or cyan, depending on the real panel's own channel order — either is a correct,
expected result) covering the middle half of the screen in both directions. This is deliberately
NOT a small corner square — if the whole screen doesn't show the two-tone pattern, something is
genuinely wrong (wrong resolution assumed, stride miscomputed, etc.), not just a minor cosmetic
issue.

## Test Platform

- Laptop model: `Lenovo ThinkPad E15 Gen 2`
- CPU: `[required — record the exact SKU, e.g. via BIOS/firmware setup or a sticker]`
- Firmware vendor: `[required]`
- Firmware version/date: `[required]`
- UEFI mode enabled: `[yes/no]`
- Legacy/CSM state: `[enabled/disabled/unavailable]`
- Secure Boot state: `[enabled/disabled]`
- Removable-media make/model/capacity: `[required]`
- Real display resolution (if known/queryable beforehand): `[optional — the test itself discovers this dynamically and does not require it in advance]`

## Checklist

- [ ] `sha256sum -c SHA256SUMS` passes before writing media.
- [ ] The selected block device was independently confirmed as removable.
- [ ] Firmware detects the media.
- [ ] Firmware loads `EFI/BOOT/BOOTX64.EFI`.
- [ ] `ARCOLOGY RENDER AND HALT START` appears (text, before the render).
- [ ] The full-screen two-tone test card appears (dark gray field, bright rectangle covering the
      middle half of the screen in both dimensions) — see "What you should see" above.
- [ ] `ARCOLOGY RENDER AND HALT DONE` appears (text, after the render AND after the pixel-readback
      self-verification both passed).
- [ ] The display remains stable for at least 60 seconds.
- [ ] The application does not return to firmware or reset.
- [ ] Power cycle exits the halt normally.

## Failure Signatures to Watch For (not exhaustive — record whatever actually happens)

- `ARCOLOGY RENDER AND HALT GOP UNAVAILABLE` — real GOP discovery failed on this firmware/panel.
- `ARCOLOGY RENDER AND HALT MODE UNAVAILABLE` — GOP found but its current mode info could not be
  read.
- `ARCOLOGY RENDER AND HALT ZERO WIDTH` / `ZERO HEIGHT` — GOP reported a degenerate resolution.
- `ARCOLOGY RENDER AND HALT VERIFY FAILED` — the render appeared to run, but reading a pixel back
  did not match what was written; this would indicate a real, novel finding (a genuine framebuffer
  read/write inconsistency on this specific hardware) worth its own RFC-0006 Appendix A entry.
- A screen that stays blank/black/frozen with no text at all — capture whatever IS on screen and
  record it verbatim in Observation below, even if it doesn't match any marker here.

## Observation

- Start time/timezone: `[required]`
- Exact observed behavior: `[required]`
- Real display resolution observed (if determinable from the render, e.g. by comparing the
  accent-rectangle's own proportions to the physical screen): `[optional]`
- Stable-display duration: `[required]`
- Unexpected behavior: `[none or details]`
- Photograph paths: `[required]`
- Tester name/identifier: `[required]`

## Firmware Quirk Decision

- Quirk observed: `[yes/no]`
- If yes, RFC-0006 Appendix A entry: `[link/section]`
- Root cause evidence: `[required if claimed]`
- Resolution/workaround: `[required if applied]`

## Sign-Off

- [ ] All fields above are complete.
- [ ] Evidence corresponds to the checksummed artifact named above.
- [ ] No result was inferred from QEMU.
- Human validator/signature: `[required]`
- Date: `[required]`
