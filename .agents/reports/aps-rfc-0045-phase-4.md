# RFC-0045 Phase 4 — Substrate Unification (DONE, QEMU-proven)

## Status: DONE.

A single fixture, `tests/fixtures/aps-arcology-seed-substrate/aps-arcology-seed-substrate.abas`,
runs a real `ExitBootServices` + APS's own proven CR3/GDT/IDT takeover (reusing
`aps-gdt-reload.abas`'s own sequence verbatim), then genuinely runs RFC-0007's own interactive
command loop (`HELP`/`OT`/`?SYNTAX ERROR`/backspace line editing), fed by BOTH real input paths
this RFC built — the Phase 1 PS/2 driver and the Phase 3 USB HID driver, polled together each
iteration via a new `PollAnyKey`, whichever produces a real character first.

Smoke test: `tests/systems/systems_aps_arcology_seed_substrate_smoke.sh`, wired into CTest.

## What changed from RFC-0007's own original fixture

`arcology-seed-ready.abas` (RFC-0007's first real increment) reads via
`ConsoleIn.ReadKeyStroke` and writes via `ConsoleOut.Write` — both firmware-hosted, both used
entirely before `ExitBootServices`. Neither is usable once the substrate genuinely takes over
(`ConsoleOut.Write` hangs post-exit, `ConsoleIn.ReadKeyStroke` outright crashes — RFC-0045
Section 4's own real finding). This fixture keeps RFC-0007's exact command logic (`HELP`, `OT`,
`?SYNTAX ERROR`, backspace, the same TEST-ONLY `EXIT`) but:
- reads keystrokes via `PollAnyKey` (PS/2 first, then USB HID if a keyboard was actually
  enumerated) instead of `ConsoleIn`;
- writes via the real serial port (the only real, proven post-exit output channel in this
  project's own history — `aps-gdt-reload.abas`, `ps2-keyboard-driver.abas`) instead of
  `ConsoleOut`. This is a real serial terminal session under QEMU, not a graphical console — an
  honestly-scoped substitution, documented as such; a GOP-based text console is real future work,
  not attempted here.
- uses a plain U8 line buffer instead of RFC-0007's own UTF-16 one, since serial output only ever
  needs single-byte ASCII.

## A real bug found and fixed: the identity map was too small

`aps-gdt-reload.abas`'s own minimal identity-mapped page tables (PML4 entry 0 → one PDPT → 512
2MB pages, covering the low 1GB) never needed to be bigger, because that fixture never touches any
real device MMIO after its own CR3 switch. This fixture does — `UsbHidKeyboard.PollReport`'s own
post-substrate polling loop keeps reading/writing the xHCI controller's real MMIO registers, and
that controller's own real MMIO BAR (assigned dynamically by QEMU/OVMF, not fixed) sits far
outside the low 1GB (observed at `~0xC000000000`, under PML4 entry 1, not entry 0). Under
firmware's own page tables (active up to the CR3 switch) this was never visible — UEFI's own
identity map already covers all real physical memory, MMIO BARs included.

Fixed with one additional 1GB page mapping, computed dynamically from the xHCI driver's own
already-discovered MMIO base (not hardcoded): a fourth allocated page serves as a second PDPT,
wired into whichever PML4 slot the BAR's real address falls under, with one populated 1GB-page
(PS-bit) entry covering the BAR's own 1GB-aligned region — the same PS-bit idiom the existing
2MB low-1GB map already used, one level higher. Guarded against `mmioBasePhys = 0` (no xHCI
controller present at all) — an earlier version of this fix, without the guard, silently
corrupted the existing low-1GB mapping in exactly that case, caught immediately by this fixture's
own PS/2-only negative-control run (`SUBSTRATE PDPT BAD`).

## Real proof, not simulated

- **Combined**: both PS/2 (QEMU's default machine keyboard) and USB HID (`qemu-xhci` +
  `usb-kbd`) present at once — `HELP`, `OT`, an unrecognized command (`zzz`, producing
  `?SYNTAX ERROR`), a backspace-corrected line (`hz<BS>elp` → `help`, proving real line editing,
  not just character echo), then the TEST-ONLY `EXIT`. All real, all correct, 3x determinism
  confirmed manually before the smoke test was written.
- **PS/2 alone**: no xHCI controller present at all (`SUBSTRATE USB NONE`) — the full `HELP`→
  `EXIT` flow still works correctly via PS/2 alone.
- **USB HID alone**: `-nodefaults` (no PS/2 keyboard at all) + `qemu-xhci`/`usb-kbd` — the full
  `HELP`→`EXIT` flow works correctly via USB HID alone.
- **Negative control**: no input device present at all — reaches `READY.` and genuinely just
  waits, no crash, no spurious dispatch.

All four scenarios are real, separately-launched QEMU boots (not one run reused), matching RFC-
0045 Section 7.3's own "negative control and repeated-run determinism matching every other real
proof in this project's history" mandate for Phase 4.

## Honest scope notes

- Output is a real serial terminal, not a graphical console — see above.
- `MEMORY.Read16` at a high 64-bit MMIO address still returns 0 (Phase 2's own documented
  compiler bug); the xHCI driver still works around it via `MmioReadField32`.
- The new 1GB MMIO mapping assumes the BAR's own PML4 index is never 0 in the way that would
  collide with the low-1GB map's own PDPT entry 0 in a way that matters (guarded, but not proven
  against every conceivable BAR placement) — real, working, and empirically confirmed against
  this project's own actual QEMU/OVMF environment, not exhaustively proven for all possible ones.
- Phase 5 (a new `WP-0xx` hardware validation package for the Lenovo ThinkPad E15 Gen 2) is the
  only remaining RFC-0045 phase, and is also explicitly noted in the RFC as "the first real-
  hardware test of which input path (PS/2, USB, or both) this specific laptop's own keyboard
  controller actually uses" — a genuinely open question this RFC cannot answer from QEMU alone.
