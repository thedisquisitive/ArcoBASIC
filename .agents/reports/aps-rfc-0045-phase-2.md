# RFC-0045 Phase 2: Real xHCI Host Controller Driver

**Status:** Delivered and QEMU-proven, independently of any device/keyboard enumeration.

## What was built

`stdlib/usb_xhci_policy.abas`: real xHCI (USB 3 Host Controller) driver using raw PCI config space
access (ports `0xCF8`/`0xCFC`) and real MMIO register access — no UEFI protocol dependency for the
controller-programming path itself.

- **PCI discovery**: a real bus/device/function scan (Class `0x0C`, Subclass `0x03`, ProgIf `0x30`)
  finds the controller; Memory Space + Bus Master enabled in the PCI Command register.
- **MMIO mapping**: real BAR0/BAR1 decode (confirmed empirically as a real 64-bit BAR against
  QEMU's `qemu-xhci`), Capability Registers read (CAPLENGTH, MaxSlots, MaxPorts).
- **Controller reset**: real `USBCMD.HCRST`, with `USBSTS.CNR` confirmed cleared before touching
  anything else.
- **Ring/DCBAA setup**: a real Command Ring (with a real Link TRB and Toggle Cycle bit, correctly
  wrapping the ring), a real Event Ring plus Event Ring Segment Table, and the Device Context Base
  Address Array — `CRCR`/`DCBAAP`/`ERSTSZ`/`ERSTBA`/`ERDP`/`CONFIG` all programmed per spec. Every
  DMA-visible structure allocated via a real `BootServices.AllocatePages` call, matching
  `aps-gdt-reload.abas`'s own established precedent for APS-owned physical memory.
- **Controller start**: real `USBCMD.RS` set, `USBSTS.HCH` confirmed cleared.
- **Port enumeration and reset**: real `PORTSC` reads distinguishing connected from unconnected
  ports (`CCS`), and a real port reset (`PR` set, `PRC` confirmed asserted, `PED` confirmed set).

## Two real bugs found and fixed during this phase's own development

1. **A real compiler bug** (not fixed, documented and worked around): `MEMORY.Read16(ADDRESS.
   Offset(ptr, N))` returns 0 instead of the real value when `ptr` holds a HIGH 64-bit address
   (confirmed at a real xHCI MMIO base, `0xC000000000`) — `MEMORY.Read32`/`Read8` through the exact
   same pattern at the exact same address are unaffected, and `MEMORY.Read16` at a LOW address is
   also unaffected. Root cause traced into `src/compiler/fission.cpp`'s `normalize()`/
   `width_bits()`/`load_value()` but not conclusively pinned to one exact instruction — not on
   RFC-0045's own critical path to keep investigating further. This driver reads every
   16-bit-or-narrower hardware field via a real 32-bit-aligned `MEMORY.Read32` plus `SHR`/`AND`
   instead, never `MEMORY.Read16`, on any MMIO register. Worth a dedicated follow-up outside this
   RFC's own scope.

2. **A real, intermittent functional bug**, caught specifically by a smoke test's own repeated-run
   harness after passing 3/3 manual tests: `UsbXhci.ResetPort`'s original "preserve bits 31:16,
   zero the rest" mask (`AND 0xFFFF0000`) before writing `PR` also zeroed bit 9 (`PP`, Port Power —
   a real RW bit living in the LOWER 16 bits), powering the port off as an unintended side effect
   of every reset attempt. QEMU's own xHCI emulation tolerated this most of the time, which is
   exactly why manual testing missed it. Fixed by preserving only the specific bits that must
   survive a `PR` write (`PLS` bits 8:5, `PP` bit 9, `PIC` bits 15:14 — mask `0xC3E0`) and
   explicitly zeroing everything else, including every RW1C status-change bit and the read-only
   `CCS`/`PED`/`OCA` bits, rather than blindly copying forward whatever the register happened to
   contain.

A third, related issue was found and fixed during the SAME investigation: the busy-wait polling
loops (`Reset`, `Start`, `ResetPort`) checked their condition but never actually broke out of the
`WHILE` loop early — they kept iterating to the full ceiling regardless, just skipping the body
once satisfied. Harmless at a small ceiling with a cheap loop body, but raising the ceiling for
extra timing margin and adding `CPU.Pause` (a real, deliberate fix for CPU contention under `-jN`
parallel load, matching this session's own earlier harness-speedup lesson) together caused even a
SUCCESSFUL reset to blow past a real QEMU test harness's own timeout. Fixed using this project's
own established early-exit idiom (jump the loop counter straight to its own ceiling the instant the
condition is met, since this backend has no real `BREAK`/`EXIT WHILE` statement).

## Validation

Real, first-attempt success (after the bug fixes above) across the full Phase 2 chain, proven with
a real `qemu-xhci` controller and a real `usb-kbd` device attached: PCI discovery → MMIO mapping →
reset (CNR cleared) → ring/DCBAA setup → controller start (HCH cleared) → port enumeration
(correctly distinguishing the connected keyboard's own port, `0x00000E03`/CCS=1, from 7 unconnected
ports, `0x000002A0`/CCS=0) → port reset (PRC confirmed, PED confirmed). Negative control: with NO
USB device attached, all 8 ports honestly report unconnected, and the fixture correctly reports "no
device" rather than a false positive. New smoke test `systems_xhci_controller_init_smoke`
(structural compile check, real positive proof, real negative control, 3x determinism) — run a
total of 4 additional times standalone after the bug fixes (12 total QEMU boots) with zero
failures, specifically to rebuild confidence after the earlier intermittent bug.

## What this does not close

RFC-0045 Phase 3 (real USB HID boot-protocol keyboard driver on top of this controller) — this
phase proves the controller itself works, not that a keyboard's own keystrokes can be read yet.
Real hardware validation is also still open — this driver has never run on the Lenovo E15 Gen 2 or
any physical machine; RFC-0045's own Phase 5 remains the vehicle for that.
