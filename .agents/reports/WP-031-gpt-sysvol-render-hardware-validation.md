# WP-031: Single-Disk GPT ArcFS System Volume + Render Physical Hardware Validation Package (Lenovo E15 Gen 2)

**Status:** CORE RESULT CONFIRMED ON REAL HARDWARE for the ORIGINAL artifact below (informal
real-time report, not a full structured checklist — see Observation) — but the fixture's own
source has SINCE CHANGED (Revision 2, see below) in response to real findings from that exact
hardware run. The bytes currently in `arcology-os/dist/gpt-sysvol-render/` no longer match the
confirmed checksums and have NOT yet been re-confirmed on real hardware.
**Date prepared:** 2026-08-22
**Date of real-hardware attempt:** 2026-08-22

**This is the first time the real single-disk GPT ESP+ArcFS boot chain (RFC-0044) has run on real
physical hardware.** The human tester reported: the boot reached the real GOP test card (matching
WP-030's own already-validated render), confirming real firmware ESP boot, real multi-handle
BlockIo discovery, and real `ArcFS.FormatVolume`/write/commit/`ActivateSystemVolume` all completed
against the real physical USB flash drive before the render started. Two real, honest observations
came with it — see Revision 2 immediately below; both are now fixed in the current source.

## Revision 2 (2026-08-22, same day): two real findings from that hardware run, now fixed

Identical findings and identical fixes to `WP-030-render-and-halt-hardware-validation.md`'s own
Revision 2 (this fixture's Main logic is a close relative of `render-and-halt.abas`'s own, sharing
the same GOP render code) — see that document for the full reasoning:

1. **Text readability**: every `ConsoleOut.Write` call now ends with an explicit `\r\n`. The
   original run reported "on the same line with no separator" between the `START` marker and the
   next one, exactly matching the concatenated-output finding WP-030 also made.
2. **Render speed**: the original tester reported "that slow software render is... noticeable." New
   `FillRun` helper (identical technique to `render-and-halt.abas`'s own fix) halves the MMIO write
   count for the render by writing pixel PAIRS via 64-bit writes instead of one 32-bit write per
   pixel — no change to what bytes get written, so the existing pixel-readback self-verification
   needed no changes.

Both fixes re-verified under QEMU: real positive pass on the GPT-partitioned disk image (real USB
Mass Storage Class + real GOP), real negative control, 3x determinism, and the real ArcFS
format/write/commit/activate chain all still passing (full serial trace: `STAR` → `CMOK` → `ACTO` →
`DONE`, each on its own line now). New checksums below. **This Revision 2 artifact has NOT yet been
run on real hardware.**

Do not mark this report complete from QEMU results. Fill every bracketed field during a physical
test and attach photographs using repository-relative paths. Matches `.agents/reports/
WP-030-render-and-halt-hardware-validation.md`'s own template and standing rule: no result here is
ever inferred from QEMU output, only from an actual person on an actual machine.

## What this validates

This is the FIRST validation package in this project for a real, SINGLE-DISK GPT-partitioned
layout — every prior real-hardware ArcFS proof (RFC-0043 Phases T/V) and WP-029's own package used
TWO separate physical/virtual disks: one FAT32 boot medium, one entirely separate device for ArcFS.
That is a real, valid, already-proven design, but not what a real installed OS looks like. This
package proves the real thing: ONE physical disk with a real GPT partition table, a real EFI System
Partition firmware boots `BOOTX64.EFI` from, and a real ArcFS volume — formatted, written to,
committed, and re-activated via the same real boot-policy path (`ArcFS.ActivateSystemVolume`) an
actual boot sequence would use — living on the SECOND partition of that SAME disk. A real GOP
render (the SAME test card WP-030 already validated on this exact machine) confirms visibly before
halting, so a human watching the screen has the same clear pass/fail signal WP-030 already
established, now on top of a real ArcFS boot chain instead of a bare render-and-halt.

**Target platform: Lenovo ThinkPad E15 Gen 2** (same machine WP-030 was validated on).

## A real, honest finding from development (RFC-0044 Section 7.2)

RFC-0044's own Section 6 assumed `UefiBlockDevice.SelectDiscovered(1)` (a hardcoded index) would
select "the second partition" — the ArcFS one. Real QEMU/OVMF testing found this is WRONG for the
real topology: a single GPT-partitioned disk yields **four** real BlockIo handles under this
firmware, not two — the whole disk itself, one additional handle this project does not explain,
then the ESP, then the ArcFS partition, in that order. The fixture this package validates does NOT
use a hardcoded index — it identifies the ArcFS partition by its own known, real geometry
(`SectorCount = 16384`, the fixed size `build-arcology-gpt-image.py` reserves for it), the same
technique RFC-0044 Phase 1's own `multi-blockio-discovery.abas` fixture already proved. This is
software-level robustness already built into the artifact below — nothing extra the human tester
needs to do — but it is worth knowing this is a REAL discovery-order finding, not a hypothetical
one, in case real hardware's own enumeration order differs yet again from what QEMU showed.

## Build Identity — ORIGINAL (run on real hardware, see Observation below)

- Git commit: `090b061` (`RFC-0044 Phases 3+4: real single-disk ArcFS boot chain + hardware package`)
- Compiler/version: `ArcoFission 0.1.0`
- EFI SHA-256: `878bab42975eab39bd3115cfe4f9d42cefe5308730953dff00c042894ce11175`
- Image SHA-256: `d234dff7fe7393f74636d425daa0c92a0f0d2cc83d5b76fb396aac7ced2b7e27`
- Media write command/tool: `sudo dd if=arcology-gpt-sysvol-render-x86_64.img of=/dev/sda bs=4M conv=fsync status=progress && sync` — run by the assistant on the user's explicit instruction, onto the same `/dev/sda` USB stick WP-030 already used. Post-write verification: `sha256sum` of the first 76562944 bytes read back directly from `/dev/sda` matched the source image's checksum exactly, AND `parted -s /dev/sda unit s print` independently confirmed the real, physical partition table matched the intended layout (ESP 2048s-133119s fat32 boot/esp; ArcFS 133120s-149503s msftdata) — not just a file-level check.

## Build Identity — REVISION 2 (current source; NOT yet run on real hardware)

- Git commit: `[fill in at physical test time — the commit this Revision 2 update was committed in, or later if the source has not changed]`
- Compiler/version: `ArcoFission 0.1.0`
- Image filename: `arcology-gpt-sysvol-render-x86_64.img`
- Artifact directory: `arcology-os/dist/gpt-sysvol-render/`
- EFI SHA-256: `d8f9bb1f954e433b09808de95b646872825d65152bef6ac552061f8dd1f1cc5e`
- Image SHA-256: `7518bca7b07f7f95094a6bad31d45672155d20cd9346224f2761a10c23fdf4f0`
- Full checksums: `arcology-os/dist/gpt-sysvol-render/SHA256SUMS`
- Image size: 76,562,944 bytes (~73 MiB) — a real GPT-partitioned disk image (protective MBR + primary/backup GPT + 64 MiB ESP + 8 MiB reserved ArcFS partition), NOT the superfloppy whole-device-FAT32 shape WP-026/WP-030's own images use.
- Media write command/tool: `[required]`

**Pre-physical-test sanity check already performed (QEMU, not a substitute for the checklist
below):** the image's own GPT structure was independently verified THREE separate ways before ever
being booted — a real, from-scratch parser (`verify_gpt_image.py`, recomputes both GPT CRC32s,
confirms primary/backup agree, confirms both partitions' type GUIDs/LBA ranges, walks the ESP's own
FAT32 tree to extract and checksum `BOOTX64.EFI`, with a real negative control catching a corrupted
partition array), two independent system tools (`parted -s IMAGE unit s print`, `sfdisk -l IMAGE`,
both correctly auto-recognized the ESP and Basic-Data partition types), and a real loop-mounted
read of the ESP's own filesystem content. Separately, the full application was proven under
QEMU/OVMF, booting from this exact real single-disk GPT image via a real USB Mass Storage Class
device (`qemu-xhci` + `usb-storage`, not a plain `-drive`) with a real GOP device attached: real
firmware GPT-aware boot-device enumeration finds the ESP, real multi-handle BlockIo discovery finds
the ArcFS partition by its own geometry, `ArcFS.FormatVolume`/write/commit/`ActivateSystemVolume`
all succeed against it, and the same two-tone test card WP-030 already validated on real hardware
renders and self-verifies before halting. Negative control (corrupting the expected accent-pixel
value) confirmed the verification is real, not vacuous. Deterministic across 3 repeated QEMU runs.

## Writing the image to a real USB stick

**Use a raw, byte-for-byte block write (`dd`-equivalent) — never a "burn/extract to filesystem"
tool.** Unlike WP-026/WP-030's own superfloppy-style images, this one has a REAL partition table
(GPT) — but the writing method is identical: the stick's own first bytes must literally BE the
image's own first bytes, with no filesystem-inside-a-filesystem step. A tool that "extracts"
partitions individually instead of writing the whole image raw will destroy the GPT structure.

- **Linux/macOS**: `sudo dd if=arcology-gpt-sysvol-render-x86_64.img of=/dev/sdX bs=4M status=progress conv=fsync && sync` — replace `/dev/sdX` with the REAL device node for the USB stick (confirm with `lsblk`/`diskutil list` first; writing to the wrong device destroys it). The image is ~73 MiB — much smaller than a real USB stick's own capacity; whatever content previously existed on the stick BEYOND the image's own extent is left physically untouched (irrelevant to boot, since the image's own GPT header declares the disk's usable range starting from its own first bytes).
- **Windows**: use Rufus in **DD Image mode** (not the default ISO/partition mode).
- **Any platform**: `sha256sum -c SHA256SUMS` on the image file BEFORE writing it, and re-verify the same checksum against the first `76562944` bytes of the USB device AFTER writing, before ever attempting to boot from it.

## Building this artifact

`arcology-os/dist/` is gitignored — the image and checksums above are not committed, but are fully,
deterministically reproducible from the commit named above:

```
cd arcology-os
ArcoFission build tests/fixtures/gpt-sysvol-render/gpt-sysvol-render.abas \
    -o dist/gpt-sysvol-render/BOOTX64.EFI --target uefi-x86_64 --entry Main
python3 scripts/build/build-arcology-gpt-image.py \
    dist/gpt-sysvol-render/BOOTX64.EFI \
    dist/gpt-sysvol-render/arcology-gpt-sysvol-render-x86_64.img
cd dist/gpt-sysvol-render
sha256sum BOOTX64.EFI arcology-gpt-sysvol-render-x86_64.img > SHA256SUMS
```

Before writing to real media, independently verify the image's own structure:
```
python3 scripts/build/verify_gpt_image.py dist/gpt-sysvol-render/arcology-gpt-sysvol-render-x86_64.img
```
This should print a series of `OK:` lines ending in `PASS: all structural checks succeeded`. Do not
proceed to writing physical media if this reports `FAIL`.

## What you should see

The SAME two-tone test card WP-030 already validated on this exact machine: a dark background field
with a large bright rectangle (yellow or cyan, depending on the real panel's own channel order —
either is a correct, expected result) covering the middle half of the screen in both directions.
There is no additional visible signal for the ArcFS format/mount/commit/activate steps that
happened before the render — those are confirmed via the checklist's own text markers, not visually
distinguishable on screen from WP-030's own render.

## Test Platform

- Laptop model: `Lenovo ThinkPad E15 Gen 2`
- CPU: `[required — reuse WP-030's own recorded value if this is the same physical machine and nothing has changed]`
- Firmware vendor: `[required]`
- Firmware version/date: `[required]`
- UEFI mode enabled: `[required — yes/no]`
- Legacy/CSM state: `[required — enabled/disabled/unavailable]`
- Secure Boot state: `[required — enabled/disabled]`
- Removable-media make/model/capacity: `[required]`

## Checklist

- [ ] `sha256sum -c SHA256SUMS` passes before writing media.
- [ ] `verify_gpt_image.py` reports `PASS: all structural checks succeeded` before writing media.
- [ ] The selected block device was independently confirmed as removable.
- [ ] Firmware detects the media and its real GPT partition table (some firmware setup screens show
      partition info — note if observed, not required).
- [ ] Firmware loads `EFI/BOOT/BOOTX64.EFI` from the ESP.
- [ ] `ARCOLOGY GPT SYSVOL START` marker reached.
- [ ] `ARCOLOGY GPT SYSVOL COMMIT OK` marker reached (real ArcFS format+write+commit succeeded).
- [ ] `ARCOLOGY GPT SYSVOL ACTIVATED` marker reached (real `ArcFS.ActivateSystemVolume` boot-policy
      path succeeded — the volume just committed was recognized as Healthy on re-activation).
- [ ] The full-screen two-tone test card appears (see "What you should see" above).
- [ ] `ARCOLOGY GPT SYSVOL DONE` marker reached (render AND pixel-readback self-verification both
      passed).
- [ ] The display remains stable for at least 60 seconds.
- [ ] The application does not return to firmware or reset.
- [ ] Power cycle exits the halt normally.

Note: this fixture reports over ConsoleOut only (no serial dual-report) — on a machine with a real
GOP device attached, OVMF's own ConOut typically defaults to the graphics console rather than
serial, so these text markers should be visible directly on screen, scrolling above the final
render. If markers are NOT visible on screen at all (e.g. this firmware defaults ConOut to serial
regardless), that is itself worth recording as an Observation below — it would mean only the FINAL
visual state (the test card or its absence) is directly observable, not the intermediate steps.

## Failure Signatures to Watch For (not exhaustive — record whatever actually happens)

- `ARCOLOGY GPT SYSVOL TOO FEW HANDLES` — fewer than 2 real BlockIo handles discovered; would
  indicate this firmware enumerates the single GPT disk very differently from QEMU/OVMF.
- `ARCOLOGY GPT SYSVOL ARCFS PARTITION NOT FOUND` — handles were found but none matched the ArcFS
  partition's own known 16384-sector geometry; a real, novel finding worth its own RFC-0044
  Appendix entry if it happens on real hardware.
- `ARCOLOGY GPT SYSVOL SELECT FAILED` / `PROVIDER SELECT FAILED` — discovery succeeded but
  selecting the found device for ArcFS to use did not.
- `ARCOLOGY GPT SYSVOL FORMAT FAILED` / `MOUNT FAILED` / `CREATE FAILED` / `OPEN FAILED` /
  `WRITE FAILED` / `COMMIT FAILED` — a real ArcFS operation against real hardware failed at that
  specific step; record exactly which one.
- `ARCOLOGY GPT SYSVOL ACTIVATE FAILED` — the volume was committed successfully but the real
  boot-policy re-activation path did not recognize it as healthy — a real, novel finding.
- `ARCOLOGY GPT SYSVOL GOP UNAVAILABLE` / `MODE UNAVAILABLE` / `ZERO WIDTH` / `ZERO HEIGHT` /
  `VERIFY FAILED` — matches WP-030's own identically-named GOP-render failure signatures.
- A screen that stays blank/black/frozen with no text at all — capture whatever IS on screen and
  record it verbatim in Observation below, even if it doesn't match any marker here.

## Observation

**For the ORIGINAL artifact** (informal real-time report, not a full structured session — a fresh
run against Revision 2 with the full checklist above still needed):

- Start time/timezone: `[not recorded]`
- Exact observed behavior: the first text marker (`ARCOLOGY GPT SYSVOL START`) appeared, then a
  real, noticeable pause (consistent with real `ArcFS.FormatVolume`/write/commit I/O against the
  physical USB 2.0 flash drive, genuinely slower than QEMU's virtual block device), then another
  marker printed quickly on the same line with no separator (the concatenated-text finding, now
  fixed in Revision 2), then the display switched to graphics mode and rendered the test card. The
  render itself was reported as noticeably slow (the render-speed finding, now fixed in Revision 2).
  Tester's own words: "Yep that seems to work. Not a very descriptive text output... I saw the
  first message, it pauses for a bit, then on the same line with no separator quickly prints
  something else before going into graphics mode. That slow software render is uh... noticeable."
- Which markers (if any) were visible on screen before the final render: at least `START` and one
  subsequent marker (exact identity not confirmed — could be `COMMIT OK` or `ACTIVATED`, both print
  in quick succession per the QEMU trace) were directly visible, confirming this firmware does NOT
  default `ConsoleOut` to serial-only with a real GOP device attached.
- Stable-display duration: `[not recorded]`
- Unexpected behavior: none beyond the two Revision 2 findings above (both expected/benign, not
  correctness failures).
- Photograph paths: none for this run (informal report only).
- Tester name/identifier: `[not recorded]`

**For Revision 2**: `[pending a fresh run]`

## Firmware Quirk Decision

- Quirk observed: `[yes/no]`
- If yes, RFC-0044 Appendix entry: `[link/section]`
- Root cause evidence: `[required if claimed]`
- Resolution/workaround: `[required if applied]`

## Sign-Off

- [ ] All fields above are complete.
- [ ] Evidence corresponds to the checksummed artifact named above.
- [ ] No result was inferred from QEMU.
- Human validator/signature: `[required]`
- Date: `[required]`
