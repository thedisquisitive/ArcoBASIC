# WP-032: "Arcology Seed READY." Physical Hardware Validation Package (Lenovo E15 Gen 2)

**Status:** READY FOR HUMAN EXECUTION — NOT YET VALIDATED
**Date prepared:** 2026-08-22

Do not mark this report complete from QEMU results. Fill every bracketed field during a physical
test and attach photographs using repository-relative paths. Matches `.agents/reports/
WP-030-render-and-halt-hardware-validation.md`'s own template and standing rule: no result here is
ever inferred from QEMU output, only from an actual person on an actual machine.

## What this validates

This is the FIRST validation package in this project for real KEYBOARD input and a genuinely
PERSISTENT (non-halting) program — every prior package (WP-026/029/030/031) proved firmware would
boot, render, and/or mount a real filesystem, then halt. This one proves RFC-0007's own real
`READY.` vision: the machine boots directly into a live, interactive prompt and STAYS there,
reading real keystrokes from a real physical keyboard and responding to them, exactly the way an
actual operating environment behaves — not a one-shot test that reports PASS/FAIL and stops.

**Target platform: Lenovo ThinkPad E15 Gen 2** (same machine WP-030/WP-031 were already run on).

**This is fundamentally a HANDS-ON test, not a watch-and-photograph one.** The tester needs to
physically type on the real keyboard and observe the real, live response — no QEMU proof can
substitute for confirming a real physical keyboard's own scancodes reach this binding correctly.

## Build Identity

- Git commit: `[fill in at physical test time]`
- Compiler/version: `ArcoFission 0.1.0`
- Image filename: `arcology-seed-ready-x86_64.img`
- Artifact directory: `arcology-os/dist/arcology-seed-ready/`
- EFI SHA-256: `3e4d2a4f9de60638d2b9966226beab36502d387064f4591e1e38648c072f3e07`
- Image SHA-256: `bf81cbf6b15cfb085d2e2daf264a280558a30ee03cc273bd348e7d3a9fa59627`
- Full checksums: `arcology-os/dist/arcology-seed-ready/SHA256SUMS`
- Media write command/tool: `[required]`

**Pre-physical-test sanity check already performed (QEMU, not a substitute for the checklist
below):** real keystrokes injected via QEMU's own human-monitor `sendkey` command (the same
scancode-delivery path a real keyboard driver uses) were correctly read back through
`ReadKeyStroke`, echoed via a real dynamically-constructed `ConsoleOut.Write` buffer, and dispatched
to the correct command handler. Full interactive sequence proven: `HELP` → the real command list;
`OT` → the real whimsical message; an unrecognized command → a real `?SYNTAX ERROR`, not a false
match; Backspace → real line editing (removes the last typed character both internally and
visually). 3x determinism confirmed. New harness `scripts/run/run-uefi-image-with-usb-gpu.sh`'s
sibling, `run-uefi-hello-with-keyboard.sh`, documents a real, empirically-found timing requirement:
injecting a keystroke immediately after QEMU's monitor socket appears silently drops it — the
guest's own PS/2 keyboard driver needs real time to become ready after that socket exists.

## Writing the image to a real USB stick

Same superfloppy-style whole-device FAT32 image WP-026/WP-030 use — **raw, byte-for-byte block
write only** (`dd`-equivalent), never a "burn/extract to filesystem" tool.

- **Linux/macOS**: `sudo dd if=arcology-seed-ready-x86_64.img of=/dev/sdX bs=4M status=progress conv=fsync && sync` — replace `/dev/sdX` with the REAL device node (confirm with `lsblk` first).
- **Windows**: Rufus in **DD Image mode**.
- **Any platform**: `sha256sum -c SHA256SUMS` before writing, and re-verify against the raw device after writing.

## Building this artifact

```
cd arcology-os
ArcoFission build tests/fixtures/arcology-seed-ready/arcology-seed-ready.abas \
    -o dist/arcology-seed-ready/BOOTX64.EFI --target uefi-x86_64
python3 scripts/build/build-arcology-hardware-image.py \
    dist/arcology-seed-ready/BOOTX64.EFI \
    dist/arcology-seed-ready/arcology-seed-ready-x86_64.img
cd dist/arcology-seed-ready
sha256sum BOOTX64.EFI arcology-seed-ready-x86_64.img > SHA256SUMS
```

## What you should see and do

1. The screen should show, on boot:
   ```
   ARCOLOGY SEED

   READY.
   ```
2. **Type `HELP` and press Enter.** You should see each character echo live as you type, then on
   Enter: `IMMEDIATE MODE COMMANDS: HELP  OT` followed by a fresh `READY.`.
3. **Type `OT` and press Enter.** You should see `THE WAGON HAS BROKEN DOWN.` followed by `READY.`.
4. **Type something not recognized (e.g. `XYZ`) and press Enter.** You should see `?SYNTAX ERROR`
   followed by `READY.` — NOT a false match to HELP or OT, and not a hang or crash.
5. **Type a command, then press Backspace a few times before pressing Enter** (e.g. type `HELQ`,
   backspace once, type `P`, press Enter). The backspace should visibly erase the last typed
   character on screen, and the corrected command should be recognized correctly (`HELP` in this
   example).
6. **Confirm the prompt never returns to a boot menu, resets, or otherwise stops responding** —
   it should keep accepting new commands indefinitely. There is no real "quit" — closing out the
   test means simply powering off the machine (this is the correct, expected end of a real
   `READY.` session, matching RFC-0007's own vision that there is no exit from Seed's interactive
   environment other than power-off, until Program Mode/RUN exist).

## Test Platform

- Laptop model: `Lenovo ThinkPad E15 Gen 2`
- CPU: `[required — reuse WP-030's own recorded value if unchanged]`
- Firmware vendor: `[required]`
- Firmware version/date: `[required]`
- UEFI mode enabled: `[required — yes/no]`
- Legacy/CSM state: `[required — enabled/disabled/unavailable]`
- Secure Boot state: `[required — enabled/disabled]`
- Removable-media make/model/capacity: `[required]`
- Keyboard used: `[required — the laptop's own built-in keyboard, or an external USB/PS2 keyboard; note which]`

## Checklist

- [ ] `sha256sum -c SHA256SUMS` passes before writing media.
- [ ] The selected block device was independently confirmed as removable.
- [ ] Firmware detects the media and loads `EFI/BOOT/BOOTX64.EFI`.
- [ ] `ARCOLOGY SEED` banner and initial `READY.` appear.
- [ ] Typed characters echo live on screen as they are typed (real keyboard input confirmed).
- [ ] `HELP` produces the real command list.
- [ ] `OT` produces the real whimsical message.
- [ ] An unrecognized command produces `?SYNTAX ERROR`, not a false match or a hang.
- [ ] Backspace visibly erases the last typed character and the corrected line is recognized
      correctly.
- [ ] The prompt keeps responding to new commands indefinitely (test for at least a few minutes of
      real interactive use, not just one command) — this is the FIRST real confirmation that
      anything in this project can run persistently on real hardware, not just for a fixed test
      duration.
- [ ] No unexpected reset, hang, or return to firmware at any point during the session.

## Failure Signatures to Watch For (not exhaustive — record whatever actually happens)

- The banner/`READY.` never appears at all — a real boot or `ConsoleOut` failure.
- Typed characters do not echo, or echo incorrectly (wrong characters, garbled) — would indicate a
  real, novel finding about this specific keyboard/firmware's own scancode-to-Unicode translation,
  worth its own RFC-0007 Appendix entry.
- `HELP`/`OT` are not recognized even when typed correctly — check for a stray leading/trailing
  character (e.g. an unexpected key repeat or a firmware-injected character) via the "typed
  characters echo live" checklist item above.
- The system stops responding to input after some period, or hangs — a real, first-of-its-kind
  finding about this fixture's own busy-poll loop's long-run behavior on real hardware (this
  fixture polls `ReadKeyStroke` continuously with zero delay between polls — a real, intentional
  simplification, not previously tested for more than a few seconds at a time under QEMU either).

## Observation

- Start time/timezone: `[required]`
- Exact observed behavior: `[required]`
- How long the interactive session was tested for: `[required]`
- Unexpected behavior: `[none or details]`
- Photograph paths: `[required]`
- Tester name/identifier: `[required]`

## Firmware Quirk Decision

- Quirk observed: `[yes/no]`
- If yes, RFC-0007 Appendix entry: `[link/section]`
- Root cause evidence: `[required if claimed]`
- Resolution/workaround: `[required if applied]`

## Sign-Off

- [ ] All fields above are complete.
- [ ] Evidence corresponds to the checksummed artifact named above.
- [ ] No result was inferred from QEMU.
- Human validator/signature: `[required]`
- Date: `[required]`
