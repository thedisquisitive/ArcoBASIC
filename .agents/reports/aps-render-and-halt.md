# "Render and Halt" — Real GOP Bring-Up Test and Hardware Validation Package

## Scope delivered

RFC-0006's own real hardware bring-up track, extended for the first time from a text-only banner
(WP-026, still itself unexecuted) to a real graphics render. Directive: "start working toward
getting that working with a bootloader 'render and halt' test" on the user's own Lenovo ThinkPad
E15 Gen 2.

- New fixture `arcology-os/tests/fixtures/render-and-halt/render-and-halt.abas`: discovers the
  real GOP framebuffer (the same binding `gop-discovery.abas` already proves compiles correctly,
  RFC-0014), and fills the ENTIRE real screen — not a small fixed-size corner square — with a
  two-tone test card (a dark background field plus a bright rectangle covering the middle half of
  the screen in both dimensions). Width/Height/PixelsPerScanLine are all read from the real
  discovered mode, not hardcoded, so it renders correctly at whatever resolution the real display
  actually reports.
- A real self-verification step: after rendering, reads two real pixels back (one from the
  background field, one from the accent rectangle's own center) and confirms both match what was
  just written, before ever printing the completion marker — proving a real render happened, not
  just that GOP discovery succeeded.
- Dual-reports its result: `ConsoleOut.Write` text markers for a human watching the real screen
  (matching WP-026's own proven text-banner shape), and a serial marker for automated QEMU
  verification — matching `graphical-storage-tooling.abas`'s own established pattern for this exact
  situation (a real GOP device attached flips OVMF's own ConOut default to the graphics console
  instead of serial, which a headless harness cannot otherwise capture).
- New smoke test `systems_render_and_halt_smoke.sh`, registered in `arcology-os/cmake/
  Testing.cmake`.
- A real hardware artifact built and checksummed: `arcology-os/dist/render-and-halt/
  arcology-render-and-halt-x86_64.img`, using this project's own existing, unmodified,
  byte-reproducible FAT32 image-building tool (`build-arcology-hardware-image.py`).
- A new physical-hardware validation package, `.agents/reports/
  WP-030-render-and-halt-hardware-validation.md`, matching WP-026's own established template — the
  first one in this project prepared for a specific, named real machine (the user's own Lenovo
  ThinkPad E15 Gen 2) rather than an unspecified "designated test laptop."

## A real driver-path gap found and closed: USB boot is genuinely distinct from block-device boot

The user's own next directive ("I am planning on booting from USB stick, so we need to make sure
it can support that") surfaced a real gap in the proof so far: every QEMU check up to that point
used a plain block device (`-drive`, virtio-blk, or a bare IDE disk) to boot the built image --
never a real USB device. Real UEFI firmware routes USB boot media through its own USB host
controller plus Mass Storage Class driver stack, genuinely distinct from how it discovers a
SATA/NVMe/IDE disk; proving a fixture boots via a plain `-drive` does not prove it boots via USB
specifically.

Closed with a new, reusable harness: `arcology-os/scripts/run/run-uefi-image-with-usb-gpu.sh` --
a real `qemu-xhci` USB 3 host controller with a real `usb-storage` device attached to it, combined
with the same real `-vga std` GOP device the GOP-only proof already used (this fixture needs both
capabilities exercised together). Booting the ACTUAL wrapped image (the same `.img` file
`build-arcology-hardware-image.py` produces, the real dd-writable artifact) through this harness
confirmed OVMF's own boot-manager discovers it specifically AS a USB device (`"UEFI ... USB
HARDDRIVE"` in its own boot-entry description) and boots it correctly. Negative control and 3x
determinism confirmed the same way as the GOP-only check. The project's own smoke test
(`systems_render_and_halt_smoke.sh`) now exercises both paths -- the loose `.efi` via `-vga std`
alone, and the real wrapped image via real USB Mass Storage Class boot.

`WP-030-render-and-halt-hardware-validation.md` also gained a real "Writing the image to a real USB
stick" section with concrete, correct instructions (`dd`-equivalent raw block write, Rufus's own
DD Image mode on Windows, checksum verification before AND after writing) -- the most common way a
real-hardware USB boot test fails is a tool that "extracts" or "burns" the image into a filesystem
on the stick instead of writing it as a raw block image, which this format specifically requires.

## Validation

- Compiles cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF with a real `-vga std` GOP device attached**
  (`run-uefi-hello-with-gpu.sh`): the fixture discovers the framebuffer, fills the full screen at
  the real discovered resolution, reads two real pixels back and confirms both match, then reports
  `ARCOLOGY RENDER AND HALT DONE`. Passed on the first real attempt.
- Negative control: corrupting the expected accent-pixel comparison value produces a real,
  non-vacuous `ARCOLOGY RENDER AND HALT VFAC` failure marker, not a false pass.
- Deterministic across 3 repeated QEMU runs.
- The wrapped hardware image separately confirmed correct: FAT32 boot signature (`55 AA` at offset
  510) present, and the raw image boots correctly as a single removable-media image via
  `run-arcology-hardware-image.sh` (the same harness WP-026's own artifact uses).
- New smoke test `systems_render_and_halt_smoke` registered and passing via `ctest` (80.27s, now
  exercising both the GOP-only path (loose `.efi`, `-vga std`) and the real USB Mass Storage Class
  boot path (the actual wrapped image, `qemu-xhci`+`usb-storage`)).
- Full regression suite re-run clean: 88/88 passing.

## What this does and does not close

This closes the "render" half of "render and halt" for real, at least under QEMU — the actual
physical-hardware halt of RFC-0006's own remaining scope is still, honestly, `.agents/reports/
WP-026-hardware-validation-package.md` and now also `.agents/reports/
WP-030-render-and-halt-hardware-validation.md`: both READY FOR HUMAN EXECUTION, both NOT YET
VALIDATED. Nothing in this project has ever been confirmed to run on real hardware; this increment
adds a second, graphically-richer artifact to the queue rather than closing that gap itself — only
an actual person on the actual Lenovo E15 Gen 2 can do that.

## Documented scope reductions

1. **No attempt to interpret PixelFormat precisely.** The test card's own two colors (a neutral
   gray and a saturated two-channel bright value) were deliberately chosen to stay clearly a
   "deliberate render, not corruption" pattern regardless of whether the real panel's own channel
   order is RGB or BGR (RFC-0014's `PixelRedGreenBlueReserved8`/`PixelBlueGreenRedReserved8`) — the
   two tones just trade which hue looks which way, neither becomes invisible. A future increment
   that wants exact color fidelity would need to branch on `UEFI.GOP.PixelFormat(gop)` explicitly.
2. **Text markers assume a working ConsoleOut, not tested independently of GOP.** If GOP discovery
   fails cleanly (a legitimate, real possibility on unfamiliar firmware), this fixture still reports
   that via `ConsoleOut.Write` before halting — but if ConsoleOut itself were also broken on some
   real firmware, there is no lower-level fallback (e.g. unconditional serial output from the very
   first instruction) in this specific fixture. Real serial output only starts after `SerialInit()`
   runs, itself after the `ConsoleOut.Write("...START")` call.
