# RFC-0045 Phase 3 — USB HID Boot-Protocol Keyboard Driver (WIP, NOT YET DONE)

## Status: structurally implemented, 3 real bugs found and fixed, real end-to-end determinism NOT yet achieved

This is an honest, non-final status report. Unlike every other phase report in this project's
history, Phase 3 is **not** being marked DONE here. The driver code is real, compiles, and has been
observed to run the full real device-enumeration + keystroke-read chain successfully multiple times
under real QEMU with a real `usb-kbd` device — but not reliably enough, in this session's own
testing environment, to meet this project's own evidentiary bar (repeatable, deterministic real
QEMU proof). The new smoke test (`systems_usb_hid_keyboard_driver_smoke.sh`) is present in the repo
but has **not** been wired into the CTest suite, specifically because it does not yet pass reliably.

## What was built

`stdlib/usb_xhci_policy.abas` extended with real USB device enumeration on top of the Phase 2 xHCI
controller driver:

- `XhciEnsureScratchDmaBuffer` / `XhciScratchDmaPhys` / `XhciScratchDmaMmio` — a real, separately
  `BootServices.AllocatePages`-backed DMA target buffer for descriptor reads and HID reports
  (replacing an earlier design flaw — see Bug 3 below).
- `XhciPollEvent`, `XhciRingDoorbell`, `XhciSubmitCommand`, `XhciEp0EnqueueTrb` — generic Event
  Ring / Command Ring / EP0 Transfer Ring primitives, reused by every higher-level operation below.
- `UsbXhci.EnableSlot`, `UsbXhci.AddressDevice` — real Enable Slot and Address Device Commands, a
  real Input Context (Input Control + Slot + EP0 Endpoint Context, Context Size confirmed 32 bytes
  via `HCCPARAMS1.CSZ`), a real Device Context registered in the DCBAA, a real EP0 Transfer Ring.
- `UsbXhci.Ep0ControlTransfer`, `UsbXhci.GetDescriptor`, `UsbXhci.Ep0NoDataRequest` — real 3-stage
  USB control transfers (Setup/Data/Status TRBs) for `GET_DESCRIPTOR` and no-data class/standard
  requests (`SET_CONFIGURATION`, `SET_PROTOCOL`).
- `UsbXhci.FindHidInterruptEndpoint` — real configuration-descriptor parsing (Configuration +
  Interface + HID + Endpoint descriptor chain) to locate the HID interrupt IN endpoint.
- `UsbXhci.ConfigureHidEndpoint` — real Configure Endpoint Command, a real second Transfer Ring for
  the interrupt endpoint, real xHCI Interval-field encoding (LS/FS formula, `floor(log2(bInterval))
  + 3`, computed via a real bit-shift loop since this backend has no `log()`).
- `UsbHidKeyboard.Init` — orchestrates the full chain: Enable Slot → Address Device →
  `GET_DESCRIPTOR`(Device, 8) → `GET_DESCRIPTOR`(Config, 9) → `GET_DESCRIPTOR`(Config, full) → parse
  → Configure Endpoint → `SET_CONFIGURATION` → `SET_PROTOCOL`(Boot Protocol).
- `UsbHidKeyboard.PollReport` — a real, **non-blocking** "is a report ready" check, matching
  `PS2Keyboard.PollScancode`'s own established sibling contract exactly (submits one Normal TRB the
  first time it's called, tracks the outstanding request in state so repeat calls never duplicate
  it, and does a single Event Ring check per call — callers poll it in their own loop).
- `UsbHidKeyboard.KeycodeToChar` — real USB HID Keyboard/Keypad Usage Page (0x07) translation, US
  QWERTY unshifted, fail-closed for anything undefined (the same scope decision as
  `PS2Keyboard.ScancodeToChar`).

New fixture: `tests/fixtures/usb-hid-keyboard-driver/usb-hid-keyboard-driver.abas` — full real
enumeration against `qemu-xhci` + `usb-kbd`, then a real injected keystroke read back through
`PollReport` and translated via `KeycodeToChar`.

## 3 real bugs found and fixed during development

1. **A real QEMU-crashing bit-position bug.** The Endpoint Context's real dword0 layout (cross-
   checked directly against QEMU's own parsing, `hw/usb/hcd-xhci.c`'s `xhci_init_epctx`) is Mult at
   bits 0–1, Max Primary Streams at bits 10–14, LSA at bit 15, Interval at bits 16–23. This driver
   originally placed Interval at bits 8–15 — a nonzero Interval value's own bits bled into bit 10,
   which QEMU reads as part of Max Primary Streams, making a plain non-streaming endpoint look
   stream-capable. The very first doorbell ring for that endpoint then hit a real QEMU-internal
   `assert(streamid != 0)` in `xhci_find_stream` and **aborted the whole QEMU process**. Found by
   reading QEMU's own source after the crash, not by spec inspection alone. Fixed by moving Interval
   to bits 16–23; Max Primary Streams/LSA are left at 0 (this driver's own real scope: one
   boot-protocol keyboard, no stream support).
2. **A real off-by-one in configuration-descriptor parsing.** `bInterfaceClass` lives at byte offset
   5 in a standard USB Interface Descriptor (0=bLength, 1=bDescriptorType, 2=bInterfaceNumber,
   3=bAlternateSetting, 4=bNumEndpoints, 5=bInterfaceClass, ...) — this driver originally read offset
   4 (`bNumEndpoints`, which for a boot keyboard reads as `1`), which never matches Class 3 (HID),
   so the HID interrupt endpoint was never recognized. Caught by a real byte-level dump of a real
   device's own configuration descriptor under QEMU, not by inspection.
3. **A real unsafe-assumption bug in the DMA target buffer.** The original design used the driver's
   own fixed low-memory scratch region directly as a DMA target for `GET_DESCRIPTOR` reads and HID
   reports, assuming (never verified) that its virtual address equals its physical address. This
   broke this driver's own established discipline (every other DMA structure — Command/Event rings,
   ERST, DCBAA, Input/Device Contexts, Transfer Rings — tracks a real
   `BootServices.AllocatePages`-returned physical address explicitly, never assuming virt==phys for
   a fixed address). The result: `GET_DESCRIPTOR` calls appeared to "hang" (no Transfer Event ever
   arrived) even though the command/transfer machinery itself was correct. Fixed by giving the
   scratch DMA buffer its own real allocated page via the same `XhciAllocPage` path as every other
   DMA structure in this file (`XhciEnsureScratchDmaBuffer`, lazily allocated once).

A closely related, smaller finding: the Transfer Event this driver waits for is always the Status
Stage TRB's own completion (IOC is deliberately only set there, not on the Data Stage TRB), so its
"TRB Transfer Length" field is always 0 by construction — not a real data byte count. Since this
driver's own `GetDescriptor` callers never over-request (8-byte device-descriptor probe, 9-byte
config-header probe, then the device's own reported `wTotalLength`), a real Success on the whole
3-stage transfer honestly implies the full requested length was transferred; this is used directly
instead of the (structurally meaningless) event field, a real, documented scope limitation rather
than a silent miscount.

## The real, NOT-yet-root-caused open issue

Across many real QEMU runs, individual commands and control transfers (Enable Slot, Address Device,
`GET_DESCRIPTOR`, Configure Endpoint — no single fixed step) intermittently never receive a
Command Completion / Transfer Event at all, even after a bounded doorbell re-ring retry (3 attempts)
and a generous per-attempt polling ceiling (20,000,000 iterations). This was investigated in real
depth:

- Cross-referenced the entire relevant path in QEMU's own source (`hw/usb/hcd-xhci.c`): doorbell
  dispatch (`xhci_doorbell_write`), command processing (`xhci_process_commands`,
  `xhci_ring_fetch`'s cycle-bit check), and event delivery (`xhci_event`, `xhci_write_event`,
  including its own ring-full/drop-event bounds checks) — no logic bug matching the observed
  behavior (a TRB that's cycle-correct by our own bookkeeping simply never being fetched) was found
  in either QEMU's code or this driver's own TRB/cycle-bit construction.
- A raw Event Ring dump on a real caught failure showed every prior event correctly typed and
  Success — the ring and cycle-bit bookkeeping were genuinely fine up to that point; the affected
  command's own event just never arrived, and a doorbell re-ring did not change that (ruling out a
  simple "missed the doorbell effect once" theory, since QEMU's `xhci_doorbell_write` for register 0
  processes the command ring synchronously within the same MMIO write).
- Running the identical fixture under `-icount shift=auto` (QEMU's deterministic-virtual-time mode,
  which removes real host-scheduling variance from the guest's own perspective) produced a large,
  reproducible reliability improvement for the enumeration path specifically — strong evidence this
  is host-CPU-contention-sensitive (this is a shared, loaded dev machine; `uptime` showed a
  load average of 1.2–1.7 on 4 cores throughout this investigation) rather than a pure logic bug.
  However, `-icount` did **not** make the keystroke-polling step (`PollReport`) reliable, and even
  under `-icount` the enumeration path was not 100% reliable (4/5, then failures again on a later
  run) — so contention is very likely a real contributing factor but not a complete explanation on
  its own.

Given the substantial effort already invested (ceiling increases, a bounded retry mechanism, a
non-blocking `PollReport` redesign matching `PS2Keyboard`'s own contract, `-icount` isolation
testing, and direct QEMU-source cross-referencing all failed to produce reliable determinism), this
was deliberately **not** pushed further open-endedly. It is reported here honestly as real, unfinished
work rather than declared complete.

## What's real and what's still open

**Real and proven**: every individual piece of the enumeration chain has been observed to succeed
against a real `qemu-xhci` + `usb-kbd` device in multiple independent runs, including a full,
correct pass all the way through `SET_PROTOCOL` and — at least once — a real injected keystroke
correctly read back and translated (`KEYOK x` for an injected `x`). The 3 bugs above are real,
confirmed, and fixed; none of them were guessed at, all were pinned down via direct evidence (a QEMU
crash with a stack-traceable assert, a byte-level descriptor dump, and a raw Event Ring dump).

**Still open**: real, repeatable, gate-worthy determinism has not been achieved in this session's own
test environment. `systems_usb_hid_keyboard_driver_smoke.sh` exists and is a real, usable diagnostic
tool (uses `-icount` and a 3-attempt whole-boot retry), but is intentionally **not** wired into the
CTest suite — adding a known-flaky test to the gate would be worse than leaving this phase visibly
unfinished.

## Recommended next steps (not yet directed by the user)

1. Try this exact fixture on a quieter machine (or explicitly `nice`/pin the QEMU process) to see if
   host contention alone explains the remaining gap once truly isolated.
2. Consider whether QEMU's own xHCI emulation has a real, upstream timing-sensitivity bug worth
   reporting, now that the relevant code paths are already identified in this report.
3. Investigate why `-icount` did not also stabilize `PollReport` specifically, since HID interrupt
   IN endpoint polling and command-ring polling share the same underlying Event Ring mechanism.
