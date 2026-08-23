# WP-033: APS Arcology Seed Substrate Physical Hardware Validation Package (Lenovo E15 Gen 2)

**Status:** READY FOR HUMAN EXECUTION — NOT YET VALIDATED
**Date prepared:** 2026-08-23

Do not mark this report complete from QEMU results. Fill every bracketed field during a physical
test and attach photographs using repository-relative paths. Matches `.agents/reports/
WP-032-arcology-seed-ready-hardware-validation.md`'s own template and standing rule: no result
here is ever inferred from QEMU output, only from an actual person on an actual machine.

## What this validates

This is RFC-0045 Phase 5: the FIRST physical-hardware test of the fully unified Polymorphic
Substrate — a real `ExitBootServices` call, APS's own CR3/GDT/IDT takeover (no more firmware
underneath at all), and RFC-0007's own real interactive `READY.` command loop, fed by BOTH real
input paths this RFC built (a real PS/2 driver and a real USB HID boot-protocol keyboard driver,
polled together) and rendered on a real GOP-framebuffer text terminal (a real embedded bitmap
font, not firmware's own console — `ConsoleOut` is provably dead by this point).

This is also explicitly the first real-hardware test of **which input path this specific laptop's
own keyboard controller actually uses** — a genuinely open question no amount of QEMU testing can
answer (WP-032 tested RFC-0007's `READY.` prompt via `ConsoleIn`, still firmware-hosted at that
point; this package tests the real, firmware-independent PS/2 and USB HID drivers directly).

**This is fundamentally a HANDS-ON test.** The tester needs to physically type on the real
keyboard (the laptop's own built-in one first, then ideally a real external USB keyboard too, to
test both input paths independently) and observe the real, live response on the real screen.

## Build Identity

- Git commit: `[fill in at physical test time]`
- Compiler/version: `ArcoFission 0.1.0`
- Image filename: `aps-arcology-seed-substrate-x86_64.img`
- Artifact directory: `arcology-os/dist/aps-arcology-seed-substrate/`
- EFI SHA-256: `2c7c6fd4893eb2c59889c7c9c315e7bbfa66fd28e01b87e889482f55f13f773f`
- Image SHA-256: `79b057223a6a0bca87a4663fa427f12a5703ec14a064112959ae7d1f16c673b8`
- Full checksums: `arcology-os/dist/aps-arcology-seed-substrate/SHA256SUMS`
- Media write command/tool: `sudo dd if=aps-arcology-seed-substrate-x86_64.img of=/dev/sda bs=4M status=progress conv=fsync && sync` — write already performed this session, verified byte-for-byte via a raw-device checksum readback (first 64MiB) immediately after.

**Pre-physical-test sanity check already performed (QEMU, not a substitute for the checklist
below):** the exact built `.img` file (not a synthetic vvfat directory) was booted directly under
`qemu-system-x86_64` with a real `qemu-xhci`+`usb-kbd` device and a real GOP display (`-vga std`).
Real injected keystrokes (QEMU's own `sendkey`) were correctly read through the real substrate —
past a real `ExitBootServices`, under APS's own CR3/GDT/IDT — and both echoed to serial AND drawn
onto a real captured framebuffer screenshot (`ARCOLOGY SEED`, `READY.`, the typed `help`, and the
resulting `IMMEDIATE MODE COMMANDS: HELP  OT` response all clearly legible, confirmed by direct
visual inspection of the screenshot, not just an automated pixel count). Full combined-path,
PS/2-only, USB-HID-only, and negative-control scenarios all proven separately; 96/96 regression
suite. See `.agents/reports/aps-rfc-0045-phase-4.md` and
`.agents/reports/aps-rfc-0045-phase4-gop-terminal.md` for full detail.

## Writing the image to a real USB stick

Same superfloppy-style whole-device FAT32 image WP-026/WP-030/WP-032 use — **raw, byte-for-byte
block write only** (`dd`-equivalent), never a "burn/extract to filesystem" tool.

- **Linux/macOS**: `sudo dd if=aps-arcology-seed-substrate-x86_64.img of=/dev/sdX bs=4M status=progress conv=fsync && sync` — replace `/dev/sdX` with the REAL device node (confirm with `lsblk` first).
- **Windows**: Rufus in **DD Image mode**.
- **Any platform**: `sha256sum -c SHA256SUMS` before writing, and re-verify against the raw device after writing.

## Building this artifact

```
cd arcology-os
ArcoFission build tests/fixtures/aps-arcology-seed-substrate/aps-arcology-seed-substrate.abas \
    -o dist/aps-arcology-seed-substrate/BOOTX64.EFI --target uefi-x86_64 --entry Main
python3 scripts/build/build-arcology-hardware-image.py \
    dist/aps-arcology-seed-substrate/BOOTX64.EFI \
    dist/aps-arcology-seed-substrate/aps-arcology-seed-substrate-x86_64.img
cd dist/aps-arcology-seed-substrate
sha256sum BOOTX64.EFI aps-arcology-seed-substrate-x86_64.img > SHA256SUMS
```

## What you should see and do

1. The screen should show real boot-progress text (firmware-hosted, via `ConsoleOut`, still normal
   at this point): `SUBSTRATE PRE`, `SUBSTRATE USB READY` or `SUBSTRATE USB NONE`, `SUBSTRATE GOP
   READY` or `SUBSTRATE GOP NONE`, `SUBSTRATE TABLE OK`. If `GOP NONE` appears, stop — this
   package can't be visually validated without a working GOP device; report it as a real finding
   instead of proceeding blind.
2. The screen should then show, drawn by the fixture's OWN real on-screen terminal (not
   firmware's console — this is the real substrate-owned rendering path):
   ```
   ARCOLOGY SEED

   READY.
   ```
3. **Type `HELP` and press Enter, using the laptop's own built-in keyboard.** You should see each
   character echo live as you type, then on Enter: `IMMEDIATE MODE COMMANDS: HELP  OT` followed by
   a fresh `READY.`.
4. **Type `OT` and press Enter.** You should see `THE WAGON HAS BROKEN DOWN.` followed by `READY.`.
5. **Type something not recognized (e.g. `XYZ`) and press Enter.** You should see `?SYNTAX ERROR`
   followed by `READY.`.
6. **Type a command, then press Backspace a few times before pressing Enter** (e.g. type `HELQ`,
   backspace once, type `P`, press Enter). The backspace should visibly erase the last typed
   character on screen, and the corrected command should be recognized correctly.
7. **If you have a real external USB keyboard, unplug the laptop's ability to rely on it isn't
   possible, but DO test typing on a real external USB keyboard too** (plugged into a real USB
   port) and confirm it ALSO works — this is the real, open question this package exists to
   answer: does this laptop's own keyboard controller present as PS/2, USB HID, or does the
   built-in keyboard use one path and an external one use the other?
8. **Confirm the prompt never returns to a boot menu, resets, or otherwise stops responding** — it
   should keep accepting new commands indefinitely, running entirely under this project's own
   substrate with zero firmware services remaining.
9. There is no real "quit" from this session other than power-off.

## Test Platform

- Laptop model: `Lenovo ThinkPad E15 Gen 2`
- CPU: `[required — reuse WP-030's own recorded value if unchanged]`
- Firmware vendor: `[required]`
- Firmware version/date: `[required]`
- UEFI mode enabled: `[required — yes/no]`
- Legacy/CSM state: `[required — enabled/disabled/unavailable]`
- Secure Boot state: `[required — enabled/disabled]`
- Removable-media make/model/capacity: `[required]`
- Keyboard(s) used: `[required — built-in, and separately any external USB keyboard tested; note results for each]`

## Checklist

- [ ] `sha256sum -c SHA256SUMS` passes before writing media.
- [ ] The selected block device was independently confirmed as removable.
- [ ] Firmware detects the media and loads `EFI/BOOT/BOOTX64.EFI`.
- [ ] Real boot-progress markers (`SUBSTRATE PRE`/`USB READY or NONE`/`GOP READY or NONE`/`TABLE
      OK`) appear on screen before the substrate takeover.
- [ ] `GOP READY` was reported (if not, the rest of this checklist can't be completed visually —
      report as a real finding and stop).
- [ ] `ARCOLOGY SEED` banner and initial `READY.` appear, drawn by the substrate's own real
      on-screen terminal.
- [ ] Typed characters echo live on screen as they are typed, using the laptop's own built-in
      keyboard.
- [ ] `HELP` produces the real command list.
- [ ] `OT` produces the real whimsical message.
- [ ] An unrecognized command produces `?SYNTAX ERROR`, not a false match or a hang.
- [ ] Backspace visibly erases the last typed character and the corrected line is recognized
      correctly.
- [ ] (If available) a real external USB keyboard was also tested and its own result recorded.
- [ ] The prompt keeps responding to new commands indefinitely (test for at least a few minutes of
      real interactive use).
- [ ] No unexpected reset, hang, or return to firmware at any point during the session.

## Failure Signatures to Watch For (not exhaustive — record whatever actually happens)

- `SUBSTRATE GOP NONE` — this specific laptop's real GPU/display isn't being exposed as a GOP
  device the way QEMU's `-vga std` was, or the mode this firmware selects isn't one this fixture's
  own simple `UEFI.GOP.Discover` call picks up correctly. A real, first-of-its-kind finding if it
  happens — QEMU alone can't predict real GPU/firmware GOP behavior.
- The screen goes black/frozen right after `SUBSTRATE TABLE OK` and never shows `SUBSTRATE LIVE`
  or the banner — a real crash/hang during `ExitBootServices` or the CR3/GDT/IDT takeover on real
  hardware, which QEMU never reproduced.
- The banner appears but garbled, or at the wrong screen position/scale — a real finding about
  this laptop's own actual GOP resolution/stride differing from QEMU's assumptions in a way this
  fixture's own dynamic `Width \ TermCellPx()` sizing doesn't handle correctly.
- Typed characters do not echo, or echo incorrectly — check which input path is actually in use
  (built-in keyboard could be PS/2 OR USB HID on this hardware, unlike QEMU where it's always
  known); a real, novel finding either way.
- The built-in keyboard doesn't work but a real external USB keyboard does, or vice versa — this
  is itself the real, valuable finding this package exists to surface, not a failure.
- The system stops responding to input after some period, or hangs — a real finding about the
  combined `PollAnyKey` busy-poll loop's long-run behavior on real hardware.

## Observation

- Start time/timezone: `[required]`
- Exact observed behavior: `[required]`
- How long the interactive session was tested for: `[required]`
- Which input path(s) actually worked (built-in keyboard, external USB keyboard, both, neither):
  `[required]`
- Unexpected behavior: `[none or details]`
- Photograph paths: `[required]`
- Tester name/identifier: `[required]`

## Firmware Quirk Decision

- Quirk observed: `[yes/no]`
- If yes, RFC-0045 Appendix/follow-up entry: `[link/section]`
- Root cause evidence: `[required if claimed]`
- Resolution/workaround: `[required if applied]`

## Sign-Off

- [ ] All fields above are complete.
- [ ] Evidence corresponds to the checksummed artifact named above.
- [ ] No result was inferred from QEMU.
- Human validator/signature: `[required]`
- Date: `[required]`
