# RFC-0045 — Real GOP On-Screen Terminal for the Substrate (DONE, QEMU-proven)

## Status: DONE.

Before starting Phase 5 (real hardware validation), the user asked to hold off: the Phase 4
substrate fixture's only post-`ExitBootServices` output was the real serial port, which a real
laptop doesn't expose without extra hardware — Phase 5 as originally scoped would have been
untestable on the user's own machine. This increment adds a real, second output channel: a
GOP-framebuffer text terminal, so the same `READY.` session is genuinely visible on the laptop's
own screen.

`aps-arcology-seed-substrate.abas` (RFC-0045 Phase 4's own fixture) extended in place; smoke test
`systems_aps_arcology_seed_substrate_smoke.sh` extended with a real screendump-based visual check.

## What was built

- **A real embedded 8x8 bitmap font** for ASCII 32–126 (95 glyphs), rasterized *offline* from a
  real monospace TrueType font (`FreeMonoBold.ttf`, via Python/Pillow) rather than hand-authored —
  each glyph packed into one `U64` (one byte per row, `bit7`=leftmost pixel), written by a
  generated `WriteFontData()`. This is a different approach from `aps-cr3-cutover.abas`'s own
  earlier hand-picked 19-letter banner font (`GlyphRow`/`GlyphPixel`/`DrawGlyph`), which doesn't
  cover the full character set this terminal needs (lowercase, digits, punctuation).
- **`DrawChar`**: draws one glyph at a given pixel origin, scaled 2x (16x16 real screen pixels per
  character), always painting the full cell background too — this doubles as the terminal's own
  real per-cell erase primitive.
- **A real terminal state machine** (`TermPutChar`): newline advances a row and wraps to the top
  (clearing the screen) once past the bottom; backspace moves back one cell and redraws it blank;
  anything outside the embedded font's own 32–126 range is silently dropped, matching this
  project's established fail-closed discipline for undefined input.
- **`EchoChar`**: the new combined sink used everywhere this fixture used to call `SerialByte`
  directly — writes to both the real serial port (this project's own established, QEMU-testable
  channel) and the new on-screen terminal, from the same call sites, no special-casing needed.
- **Real GOP discovery** (`UEFI.GOP.Discover`/`Width`/`Height`/`PixelsPerScanLine`/
  `FrameBufferBase`), done pre-`ExitBootServices` alongside the existing PS/2 + USB HID setup, and
  allowed to fail closed (`gopOk` stays 0) if no GOP device exists — a serial-only session (this
  project's own prior, already-proven fallback) remains genuinely correct in that case.

## The real finding, and its fix

Extending `MapExtra1GbRegion` (the dynamic 1GB identity-map helper first built for the xHCI
controller's own MMIO BAR) to *also* cover the GOP framebuffer's own real base address, since it
too sits far outside the low-1GB range the substrate's minimal page tables otherwise provide, and
also needs to stay reachable after the CR3 switch. This is the same class of finding as before,
generalized instead of duplicated: the helper now also handles two real regions coincidentally
sharing the same top-level page-table slot (reuses the earlier region's own PDPT instead of
silently orphaning it), a case the xHCI-only version never needed to consider.

Page table allocation grew from 4 pages (root/PDPT0/PD0/one spare PDPT) to 5 (a second spare PDPT,
one per real region). The terminal itself is only marked ready (`TermReadyAddress`) *after* the
CR3 switch and GDT/IDT activation complete — not before — since only then is the framebuffer's own
identity mapping guaranteed active under the new page tables.

## Real proof, not simulated

Re-ran the full existing Phase 4 proof suite (combined PS/2+USB, PS/2 alone, USB HID alone,
negative control) — all still pass unchanged, confirming the new terminal code is a real additive
capability, not a regression, and correctly no-ops when no GOP device is present (this project's
own headless CI configuration, `-vga none -display none`, has no GOP device at all).

Added a genuinely new proof: boot with a real GOP-capable display (`-vga std`), inject a real
`HELP` keystroke sequence via QEMU's own `sendkey`, take a real QEMU `screendump` (an actual P6 PPM
capture of the live framebuffer, not a synthetic render), and run a new, dependency-free (Python
stdlib only, no Pillow requirement in CI) pixel-region checker
(`arcology-os/scripts/run/check_ppm_text_region.py`) confirming real lit pixels in the top-left
region where `ARCOLOGY SEED`/`READY.` are always drawn first. Also manually inspected a real
screenshot at this stage (not just the automated pixel-fraction check) — `ARCOLOGY SEED`, `READY.`,
the typed `help`, and the resulting `IMMEDIATE MODE COMMANDS: HELP  OT` response are all clearly,
correctly legible.

Determinism: the smoke test's own GOP scenario was run twice standalone, producing the *exact
same* bright-pixel count both times (2764/30000). Full regression suite: 96/96.

## Honest scope notes

- 2x-scaled 8x8 glyphs, no line-height gap between rows (tight but legible — confirmed visually,
  not just by pixel-fraction count).
- No scrolling in the usual sense — once the cursor passes the bottom row, the whole screen clears
  and the cursor resets to the top-left, rather than scrolling existing lines up. A real, simple,
  documented choice, not a silent gap.
- `check_ppm_text_region.py`'s own pixel check is a coarse "something real was drawn here"
  signal (a lit-pixel-fraction threshold), not per-glyph OCR — deliberately simple and dependency-
  free rather than exhaustively precise.
- Phase 5 (real hardware validation on the Lenovo ThinkPad E15 Gen 2) is next, now that there's a
  real way to see the result on the laptop's own screen.
