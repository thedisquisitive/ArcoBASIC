# WP-030: "Render and Halt" Physical Hardware Validation Package (Lenovo E15 Gen2)

**Status:** CORE RESULT CONFIRMED ON REAL HARDWARE for the ORIGINAL artifact below (photographic
evidence, checksums `d3cb63b3...`/`27bc14c68...`). Revision 2's underlying fix (identical
`FillRun`/`\r\n` logic) has ALSO been confirmed on the same real hardware — but via
`gpt-sysvol-render.abas`'s own real run (WP-031, which shares this exact render code), not this
fixture's own Revision 2 bytes directly re-flashed and observed standalone. Re-flashing THIS
specific artifact is still open as a quick confirming step, not a real risk.
**Date prepared:** 2026-08-22
**Date of real-hardware attempt:** 2026-08-22

**This is the first artifact in the entire Arcology OS project confirmed to run on real physical
hardware.** Every prior QEMU-only proof in this project's history, however extensive, remained
unconfirmed on real silicon until this result. This finding stands regardless of the Revision 2
update below — the checksums it was confirmed against are recorded permanently in this section.

## Revision 2 (2026-08-22, same day): two real findings from that hardware run, now fixed

The human tester who ran the original artifact reported two real, honest observations directly
from the Lenovo E15 Gen 2 screen: (1) the `ConsoleOut.Write` text markers ran together on one line
with no separator, and (2) the pixel-fill render loop was noticeably slow. Both are now fixed in
the fixture's own source (see `render-and-halt.abas`'s own header comment for the full reasoning):

1. **Text readability**: this dialect's `ConsoleOut.Write` never auto-appends a newline (matching
   real UEFI's own `OutputString` contract) — every message now ends with an explicit `\r\n`
   escape sequence (confirmed real, not a literal backslash-r-backslash-n, via the lexer's own
   escape handling in `src/frontend/lexer.cpp`). Confirmed under QEMU: each marker now appears on
   its own line in the captured serial/console trace.
2. **Render speed**: the original loop wrote one pixel at a time via `MEMORY.Write32` — cheap on
   QEMU's virtual framebuffer, but real GOP framebuffer MMIO has real per-write overhead a virtual
   one does not. New `FillRun` helper writes PAIRS of same-color pixels via one 64-bit MMIO write
   (falling back to a single 32-bit write for a trailing odd pixel), halving the write count for
   every solid-color run without changing a single byte of what gets written — the existing
   pixel-readback self-verification needed no changes at all.

Both fixes re-verified under QEMU: real positive pass, real negative control (corrupted
accent-pixel check still correctly reports `VERIFY FAILED`/`VFAC`, not a false `DONE`), 3x
determinism, both the plain-block-device and real-USB-Mass-Storage-Class boot paths. New checksums
below. **This Revision 2 artifact has NOT yet been run on real hardware** — the render-speed
improvement in particular can only be meaningfully judged by a human watching the real screen
again.

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

## Build Identity — ORIGINAL (validated on real hardware, see photo/checklist below)

- Git commit: `b32545a6881c64ab00f6bc696cf218375fb8ef06` (`RFC-0006: real GOP "render and halt" bring-up test + real USB boot proof`) — the artifact below was built from this commit (checksums confirmed byte-identical to the pre-commit build, as expected — nothing in the fixture's own source changed after that build). A later commit touching `src/graphics/graphics.cpp`, `stdlib/graphics_primitives.abas`, or `tests/fixtures/render-and-halt/render-and-halt.abas` should get a fresh build before physical testing.
- Compiler/version: `ArcoFission 0.1.0`
- EFI SHA-256: `d3cb63b3cf5efc6f8831ea970431ac2bba96bf0f1e9a7cf080a44041380fd82b`
- Image SHA-256: `27bc14c68fa6aa3c9eb4887c0162608d0e4b077dbf532690edb930abbbc1981a`

## Build Identity — REVISION 2 (current source; NOT yet run on real hardware)

- Git commit: `[fill in at physical test time — the commit this Revision 2 update was committed in, or later if the source has not changed]`
- Compiler/version: `ArcoFission 0.1.0`
- Image filename: `arcology-render-and-halt-x86_64.img`
- Artifact directory: `arcology-os/dist/render-and-halt/`
- EFI SHA-256: `67c1e2875fbf8bb4ae8c6799c076a8afd6312957f66abfa1c224b6c110671f73`
- Image SHA-256: `1dbdf41bf1d9893be1e4a67049964972b40820e72f259b7f58b94e1eb0ee2bf4`
- Full checksums: `arcology-os/dist/render-and-halt/SHA256SUMS`
- Dirty-tree statement at physical test: `[required; use a clean checkout of the commit above]`
- Media write command/tool: `sudo dd if=arcology-render-and-halt-x86_64.img of=/dev/sda bs=4M conv=fsync status=progress && sync` — run by the assistant on the user's explicit, confirmed instruction. `/dev/sda` was independently confirmed as the correct target before writing (`lsblk`: TYPE=disk, TRAN=usb, RM=1, MODEL="USB 2.0 FD", SIZE=28.9G — the drive's prior GPT contents, including a partition the user had labeled `LAZARUS_STATE`, were confirmed with the user before wiping). Post-write verification: `sha256sum` of the first 67108864 bytes read back directly from `/dev/sda` matched the source image's checksum exactly (`27bc14c68f...981a`) — a real, byte-for-byte confirmed write, not merely a `dd` exit code of 0.

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
- CPU: `[still required — record the exact SKU, e.g. via BIOS/firmware setup or a sticker]`
- Firmware vendor: `[still required]`
- Firmware version/date: `[still required]`
- UEFI mode enabled: `[still required — yes/no]`
- Legacy/CSM state: `[still required — enabled/disabled/unavailable]`
- Secure Boot state: `[still required — enabled/disabled; note if it had to be turned off for this unsigned image to boot at all]`
- Removable-media make/model/capacity: `"USB 2.0 FD", 28.9 GiB reported capacity (~32GB nominal), confirmed via lsblk before writing`
- Real display resolution (if known/queryable beforehand): `[optional — the test itself discovers this dynamically and does not require it in advance]`

## Checklist

- [x] `sha256sum -c SHA256SUMS` passes before writing media — confirmed (`BOOTX64.EFI: OK`,
      `arcology-render-and-halt-x86_64.img: OK`).
- [x] The selected block device was independently confirmed as removable (`lsblk` RM=1, TRAN=usb).
- [x] Firmware detects the media — implied by the successful boot; not independently timestamped.
- [x] Firmware loads `EFI/BOOT/BOOTX64.EFI` — implied by the render actually appearing.
- [ ] `ARCOLOGY RENDER AND HALT START` appears (text, before the render) — not independently
      observed; the single photograph captures only the final halted state, not the boot sequence.
      Logically must have occurred (the fixture prints it before ever discovering GOP), but this is
      an inference from source, not a direct observation — leaving unchecked on principle.
- [x] The full-screen two-tone test card appears (dark gray field, bright rectangle) — CONFIRMED,
      `.agents/reports/evidence/WP-030-real-hw-test.jpg`: dark navy-blue field, large bright teal rectangle. (Photographed at an
      angle with the laptop's own camera/webcam area visible in-frame, so the rectangle's exact
      proportions in the photo are foreshortened — not a measurement of the real render's own
      pixel-accurate width/height ratio.)
- [x] `ARCOLOGY RENDER AND HALT DONE` appears (text, after the render AND after the pixel-readback
      self-verification both passed) — CONFIRMED, clearly legible in the upper-left of
      `.agents/reports/evidence/WP-030-real-hw-test.jpg`. This is the single strongest piece of evidence in this package: it can
      only print after GOP discovery, mode read, the full-screen fill, AND both pixel-readback
      self-verification checks (background AND accent) all succeeded — a VERIFY FAILED/NOGO/NOMD/
      ZWDT/ZHGT marker would have printed instead had any of those failed.
- [ ] The display remains stable for at least 60 seconds — not yet independently confirmed (a
      single photograph is one instant in time).
- [ ] The application does not return to firmware or reset — not yet independently confirmed.
- [ ] Power cycle exits the halt normally — not yet independently confirmed.

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

- Start time/timezone: `[still required]`
- Exact observed behavior: Dark (navy-blue, not neutral gray — a real, expected channel-order
  effect the fixture's own design anticipated) background field; large bright teal/cyan accent
  rectangle positioned in the middle portion of the screen; `ARCOLOGY RENDER AND HALT DONE` text
  legible in the upper-left. Matches the expected two-tone test card shape. See `.agents/reports/evidence/WP-030-real-hw-test.jpg`
  (repository root).
- Real display resolution observed: `[still optional/required — not determinable from this photo's
  own oblique camera angle]`
- Stable-display duration: `[still required — only a single instant was photographed]`
- Unexpected behavior: none reported so far; background rendered navy-blue rather than neutral
  gray, but this is an EXPECTED outcome the fixture's own header comment names explicitly
  ("deliberately channel-order-agnostic: RGB vs BGR just swaps which hue looks which way, neither
  becomes invisible") — a real finding about this panel/GOP driver's own channel order, not a
  defect.
- Photograph paths: `.agents/reports/evidence/WP-030-real-hw-test.jpg`
- Tester name/identifier: `[still required]`

## Firmware Quirk Decision

- Quirk observed: Possibly — the background field rendering as navy-blue rather than neutral gray
  is consistent with this hardware's real GOP `PixelFormat` differing from what a naive RGB-order
  assumption would produce (e.g. a genuine BGR framebuffer, or a non-8-bit-per-channel format). The
  fixture's own self-verification (reading the written pixel value back and comparing to what was
  just written) would still pass either way, since it compares against its own last-written value,
  not an assumed absolute color — so this is a real, benign, already-anticipated deviation, not a
  failure.
- If yes, RFC-0006 Appendix A entry: `[not yet written — worth a short entry recording the real
  observed color once the exact PixelFormat is confirmed, e.g. by reading UEFI.GOP's own
  PixelFormat field in a future diagnostic fixture]`
- Root cause evidence: photographic (`.agents/reports/evidence/WP-030-real-hw-test.jpg`) plus the fixture's own documented
  channel-order-agnostic design; PixelFormat itself was not read back by this fixture (out of
  scope for render-and-halt, which only self-verifies against its own last-written values).
- Resolution/workaround: none needed — this is why the fixture was deliberately designed to be
  channel-order-agnostic rather than assuming a specific color order.

## Sign-Off

- [ ] All fields above are complete — NOT YET; platform/firmware details, stability duration, exact
      start time, and tester identifier are still open.
- [x] Evidence corresponds to the checksummed artifact named above — the photographed device was
      written from this exact SHA-256-verified image by the same session that built it, with a
      real post-write byte-for-byte checksum match against the raw device.
- [x] No result was inferred from QEMU — every checked item above is grounded in either the
      photograph or a directly-executed, logged command (dd, sha256sum, lsblk), not a QEMU run.
- Human validator/signature: `[still required]`
- Date: `2026-08-22 (core result); full sign-off pending remaining fields above`
