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

This revision also adds the real "Arcology Boot Screen" — a System Assembly View dashboard
(`docs/arcology-boot-screen-concept.md`) driven by this fixture's own real boot milestones (PS/2
init, USB HID enumeration, GOP discovery, identity-map build, `ExitBootServices`, CR3 switch,
GDT/IDT load, terminal handoff), not simulated/fake progress. This is also the first real-hardware
proof of the fix for the freeze this specific laptop hit at "SUBSTRATE TABLE OK" during this
package's own first physical run (root-caused to the identity map not covering wherever firmware
actually loaded the running image/stack on real hardware; fixed by identity-mapping the full 512GB
PML4-slot-0 range instead of just the low 1GB).

**This is fundamentally a HANDS-ON test.** The tester needs to physically type on the real
keyboard (the laptop's own built-in one first, then ideally a real external USB keyboard too, to
test both input paths independently) and observe the real, live response on the real screen.

## Build Identity

- Git commit: `0a42e34` (real hardware fix — the terminal's own font was too illegible to
  distinguish "hi" from "ni"; now a dedicated 8x14 font — on top of `bb62338`'s RPM zero-init fix,
  `0cecd37`'s Program Mode increment, `102a051`'s font-scale fix, `02b2762`'s dashboard, and
  `0cd99c8`'s CR3 fix)
- Compiler/version: `ArcoFission 0.1.0`
- Image filename: `aps-arcology-seed-substrate-x86_64.img`
- Artifact directory: `arcology-os/dist/aps-arcology-seed-substrate/`
- EFI SHA-256: `03a37937a622904c3d7ccc55ecb753748a577a52f01ebe2aebcf27380a59de43`
- Image SHA-256: `1bbb69ee165d8664f78598c4630055e18b0aff301ba1546b86a6d6ac34a2ff51`
- Full checksums: `arcology-os/dist/aps-arcology-seed-substrate/SHA256SUMS`
- Media write command/tool: `sudo dd if=aps-arcology-seed-substrate-x86_64.img of=/dev/sda bs=4M status=progress conv=fsync && sync` — write performed this session, verified byte-for-byte via a raw-device checksum readback (first 64MiB) immediately after.

## Round 3 result: TWO real bugs found on real hardware, both now fixed (not yet re-validated)

**Bug 1**: the user's own first physical test of Program Mode: `10 PRINT "HELLO"` then `RUN`
printed nothing and never returned to `READY.` on its own — only a real injected `ESC` broke out,
reporting a nonsensical `BREAK IN 3206755423`. Root cause: `RpmCountAddress` and
`KeyModifierStateAddress` were both read before any code path was guaranteed to have written
them — invisible under QEMU (whose own VM RAM always starts zeroed) but real on the user's physical
RAM, which had genuine leftover garbage there. Confirmed by direct reproduction (a throwaway build
that deliberately poisons `RpmCountAddress` under QEMU reproduced the identical symptom shape),
then confirmed fixed the same way. Commit `bb62338`.

**Bug 2**: after Bug 1's fix, `RUN`/`GOTO` worked correctly, but the user reported the font was
still bad — `PRINT "hi"` rendered indistinguishably from "ni". Root cause: the shared 8x8 dashboard
font gave lowercase ascenders (h/b/d/k/l) only ONE pixel of headroom, decoded and confirmed directly
from the stored bitmap. Fixed with a dedicated 8x14 terminal-only font (the dashboard's own 8x8
font and layout are untouched). Commit `0a42e34`.

See RFC-0007 Section 15's own two "real bug found on real hardware, Round 3" writeups for full
detail on each. This image (commit `0a42e34`) is the first build with BOTH fixes — Round 4 is the
real test.

## Round 3 addendum: RFC-0007 Program Mode is now on this image

This build adds real `10 PRINT "..."` / `20 GOTO ...` / `RUN` / `LIST` / `NEW` / `CLEAR` — see
`arcology-os/rfcs/RFC-0007_ArcoBASIC_Interactive_Program_Model.md` Section 15 for the full writeup.
Not yet covered by this report's own checklist below (written before Program Mode existed), but
worth trying opportunistically during Round 3:

- Type `10 PRINT "HELLO"` then `RUN` — the double-quote requires a real Shift keystroke
  (Shift+apostrophe), the first real-hardware test of Shift support in this project at all.
- Type `10 PRINT "LOOP"`, `20 GOTO 10`, `RUN`, then press `ESC` — should print `LOOP` repeatedly
  then `BREAK IN 20` and return to `READY.`. Tests the real ESC keystroke on real hardware for the
  first time too.
- `LIST` after typing a couple of lines should echo them back.

If Shift or ESC don't work on the real keyboard, that's a genuinely new, valuable finding, not a
failure of this checklist.

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

**This revision's own additional sanity check:** the same `.img` file was paused mid-boot via a
real QEMU monitor `stop` immediately after a serial milestone marker, then `screendump`-captured,
confirming the Arcology Boot Screen dashboard itself actually renders (header, UEFI/APS boxes,
subsystem panels with real per-panel state text, a partially-filled segmented assembly bar, and a
real populated event feed) — not just that the fixture reports `GOP READY` without ever touching
the framebuffer. A real, found-and-fixed bug from this same check: firmware's own `ConsoleOut` text
cursor kept advancing independently of the dashboard's own drawing, and two of this fixture's
pre-`ExitBootServices` diagnostic markers (`SUBSTRATE USB READY`/`NONE`, `SUBSTRATE TABLE OK`) were
landing glyphs directly on top of already-painted dashboard panels; both are now suppressed once
the dashboard is live (the same milestones are already shown, labeled, in the event feed instead).
96/96 regression suite still green after the fix.

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

1. Briefly, real boot-progress text via firmware's own console: `SUBSTRATE PRE`, then `SUBSTRATE
   GOP READY` or `SUBSTRATE GOP NONE`. If `GOP NONE` appears, stop — this package can't be visually
   validated without a working GOP device; report it as a real finding instead of proceeding blind.
2. The moment GOP is confirmed, the screen should switch to the real **Arcology Boot Screen** — a
   dark dashboard with an `ARCOLOGY` / `SYSTEM ASSEMBLY` header, a `UEFI` box transitioning to an
   `APS` box, six subsystem panels (`MEMORY`, `ADDRESS SPACE`, `I/O`, `INPUT`, `DISPLAY`,
   `CONSOLE`) that visibly change state (dim `PENDING` → cyan `INITIALIZING` → bright `ACTIVE`) as
   each real subsystem comes online, a segmented assembly bar filling left to right, and an event
   feed logging each real milestone (`GOP`, `PS2`, `HID`, `MEM`, `BS`, `CR3`, `CPU`, `TERM`). At the
   real `ExitBootServices` → CR3-switch moment, the `APS` box should turn magenta and the link to
   the `UEFI` box should visibly sever — this is the real, symbolic authority-transition moment the
   dashboard exists to show. **This entire sequence should complete in well under a second** — it
   is not meant to be read frame-by-frame in real time, only confirmed as a coherent, non-garbled
   flash before the terminal handoff. A photo or short video is the most useful record here.
3. The screen should then hand off to the fixture's own real on-screen terminal (not firmware's
   console, not the dashboard — this is the real substrate-owned text rendering path, replacing the
   dashboard entirely):
   ```
   ARCOLOGY SEED

   READY.
   ```
5. **Type `HELP` and press Enter, using the laptop's own built-in keyboard.** You should see each
   character echo live as you type, then on Enter: `IMMEDIATE MODE COMMANDS: HELP  OT` followed by
   a fresh `READY.`.
6. **Type `OT` and press Enter.** You should see `THE WAGON HAS BROKEN DOWN.` followed by `READY.`.
7. **Type something not recognized (e.g. `XYZ`) and press Enter.** You should see `?SYNTAX ERROR`
   followed by `READY.`.
8. **Type a command, then press Backspace a few times before pressing Enter** (e.g. type `HELQ`,
   backspace once, type `P`, press Enter). The backspace should visibly erase the last typed
   character on screen, and the corrected command should be recognized correctly.
9. **If you have a real external USB keyboard, unplug the laptop's ability to rely on it isn't
   possible, but DO test typing on a real external USB keyboard too** (plugged into a real USB
   port) and confirm it ALSO works — this is the real, open question this package exists to
   answer: does this laptop's own keyboard controller present as PS/2, USB HID, or does the
   built-in keyboard use one path and an external one use the other?
10. **Confirm the prompt never returns to a boot menu, resets, or otherwise stops responding** — it
    should keep accepting new commands indefinitely, running entirely under this project's own
    substrate with zero firmware services remaining.
11. There is no real "quit" from this session other than power-off.

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
- [ ] `SUBSTRATE PRE` and `SUBSTRATE GOP READY` (or `GOP NONE`) appear briefly via firmware's own
      console before the substrate takeover. If `GOP NONE`, the rest of this checklist can't be
      completed visually — report as a real finding and stop.
- [ ] The Arcology Boot Screen dashboard appears — header, UEFI/APS boxes, subsystem panels
      changing state, assembly bar filling, event feed populating — as a coherent, non-garbled
      flash (it completes in well under a second; a photo/video is more useful than an eyeball
      read).
- [ ] The `APS` box visibly turns magenta and severs its link to `UEFI` at the authority-transition
      moment (the real `ExitBootServices` → CR3-switch instant).
- [ ] `ARCOLOGY SEED` banner and initial `READY.` appear, drawn by the substrate's own real
      on-screen terminal, replacing the dashboard entirely.
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
- The screen freezes partway through the dashboard (or on a blank/dark screen) and never reaches
  the `ARCOLOGY SEED` banner — a real crash/hang during `ExitBootServices` or the CR3/GDT/IDT
  takeover on real hardware, which QEMU never reproduced. **This exact laptop hit exactly this
  freeze once already**, root-caused via the (now-removed) diagnostic checkpoint blocks to
  `CPU.WriteCR3(root)` — the identity map only covered the low 1GB, and this machine loads the
  running image/stack somewhere QEMU never does. Fixed by identity-mapping the full 512GB
  PML4-slot-0 range. This package is the first real test of whether that fix actually holds; if the
  freeze recurs, it's a genuinely new finding, not a repeat of the known one.
- The banner appears but garbled, or at the wrong screen position/scale — a real finding about
  this laptop's own actual GOP resolution/stride differing from QEMU's assumptions in a way this
  fixture's own dynamic `Width \ TermCellPx()` sizing doesn't handle correctly. **This exact
  category of finding already happened once**, though not "garbled" — the terminal's text rendered
  correctly positioned but at a fixed, too-small pixel size ("nearly impossible to read") on this
  real panel. Fixed by computing the glyph scale from the real detected GOP width instead of a
  hardcoded constant (see RFC-0045 revision 0.9). Not yet re-validated on real hardware.
- Typed characters do not echo, or echo incorrectly — check which input path is actually in use
  (built-in keyboard could be PS/2 OR USB HID on this hardware, unlike QEMU where it's always
  known); a real, novel finding either way.
- The built-in keyboard doesn't work but a real external USB keyboard does, or vice versa — this
  is itself the real, valuable finding this package exists to surface, not a failure.
- The system stops responding to input after some period, or hangs — a real finding about the
  combined `PollAnyKey` busy-poll loop's long-run behavior on real hardware.

## Observation

**Round 1 (commit `02b2762`, before the font-scale fix) — partial, real, physical:**
- Exact observed behavior: reached `ARCOLOGY SEED` / `READY.`; `HELP` and `OT` both accepted and
  produced correct responses. The boot dashboard rendered and was described as looking "nice and
  unique," visible for roughly 4 seconds. The terminal's own text (the `READY.` prompt and command
  output, a separate rendering path from the dashboard) was reported "nearly impossible to read" —
  the real finding fixed in RFC-0045 revision 0.9 (font-scale fix), not yet re-validated.
- Start time/timezone, exact session duration, backspace-editing check, external USB keyboard
  check, and photograph paths: not yet recorded — `[still required]`.
- Which input path(s) actually worked: not yet distinguished (built-in keyboard was used; whether
  this laptop presents it as PS/2 or USB HID is still an open question) — `[still required]`.
- Tester name/identifier: `[still required]`.

A full Round 2 (this artifact, after the font-scale fix) is needed to complete the checklist below
and this section's remaining required fields.

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
