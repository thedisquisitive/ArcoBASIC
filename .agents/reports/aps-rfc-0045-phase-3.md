# RFC-0045 Phase 3 — USB HID Boot-Protocol Keyboard Driver (DONE, QEMU-proven)

## Status: DONE. Real device enumeration + a real injected keystroke, reliably, deterministically.

This report was originally written as an honest WIP/not-done status after this driver's initial
development hit real, unresolved intermittent failures. The root cause was subsequently found and
fixed (see below); Phase 3 is now complete and QEMU-proven. `systems_usb_hid_keyboard_driver_smoke.sh`
passes reliably and is wired into the CTest suite.

## What was built

`stdlib/usb_xhci_policy.abas` extended with real USB device enumeration on top of the Phase 2 xHCI
controller driver:

- `XhciEnsureScratchDmaBuffer` / `XhciScratchDmaPhys` / `XhciScratchDmaMmio` — a real, separately
  `BootServices.AllocatePages`-backed DMA target buffer for descriptor reads and HID reports.
- `XhciPollEvent`, `XhciRingDoorbell`, `XhciSubmitCommand`, `XhciEp0EnqueueTrb` — generic Event
  Ring / Command Ring / EP0 Transfer Ring primitives.
- `UsbXhci.EnableSlot`, `UsbXhci.AddressDevice` — real Enable Slot and Address Device Commands, a
  real Input Context, a real Device Context registered in the DCBAA, a real EP0 Transfer Ring.
- `UsbXhci.Ep0ControlTransfer`, `UsbXhci.GetDescriptor`, `UsbXhci.Ep0NoDataRequest` — real 3-stage
  USB control transfers (Setup/Data/Status TRBs).
- `UsbXhci.FindHidInterruptEndpoint` — real configuration-descriptor parsing.
- `UsbXhci.ConfigureHidEndpoint` — real Configure Endpoint Command, a real second Transfer Ring for
  the interrupt endpoint, real xHCI Interval-field encoding.
- `UsbHidKeyboard.Init` — Enable Slot → Address Device → `GET_DESCRIPTOR`×3 → parse → Configure
  Endpoint → `SET_CONFIGURATION` → `SET_PROTOCOL`(Boot Protocol).
- `UsbHidKeyboard.PollReport` — a real, non-blocking "is a report ready" check, matching
  `PS2Keyboard.PollScancode`'s own sibling contract.
- `UsbHidKeyboard.KeycodeToChar` — real USB HID Keyboard/Keypad Usage Page (0x07) translation.
- `UsbXhci.DisconnectFirmwareDriver` — see "The real root cause" below.

Fixture: `tests/fixtures/usb-hid-keyboard-driver/usb-hid-keyboard-driver.abas`. Smoke test:
`tests/systems/systems_usb_hid_keyboard_driver_smoke.sh`, wired into CTest.

## 4 real bugs found and fixed

1. **A QEMU-crashing Endpoint Context bit-position bug.** The real dword0 layout (cross-checked
   against QEMU's own `xhci_init_epctx`, `hw/usb/hcd-xhci.c`) is Mult at bits 0–1, Max Primary
   Streams at bits 10–14, LSA at bit 15, Interval at bits 16–23. This driver originally placed
   Interval at bits 8–15 — its own bits bled into bit 10 (Max Primary Streams), making a plain
   endpoint look stream-capable and hitting a real `assert(streamid != 0)` in QEMU's
   `xhci_find_stream`, **aborting the whole QEMU process**. Found by reading QEMU's own source
   after the crash. Fixed by moving Interval to bits 16–23.
2. **An off-by-one in configuration-descriptor parsing.** `bInterfaceClass` lives at byte offset 5
   in a standard USB Interface Descriptor; this driver originally read offset 4 (`bNumEndpoints`).
   Caught by a real byte-level dump of a real device's own configuration descriptor.
3. **An unsafe DMA-buffer design** assuming virt==phys for a fixed low-memory scratch region
   instead of a real `BootServices.AllocatePages`-backed page. Fixed via
   `XhciEnsureScratchDmaBuffer`, matching every other DMA structure in this driver.
4. **The real root cause of the remaining intermittent failures (see below): a firmware/guest
   driver ownership conflict over the xHCI controller.**

## The real root cause of the intermittent failures — found and fixed

After the first 3 bugs were fixed, commands and transfers still intermittently never received a
completion event, at no fixed step. Extensive investigation (ceiling increases, a doorbell-retry
mechanism, `-icount` isolation, direct reading of QEMU's entire relevant source path) narrowed but
did not explain it. The user directed continuing rather than accepting this as a known limitation.

**Enabling QEMU's own xHCI trace events** (`-d trace:usb_xhci_doorbell_write,trace:usb_xhci_fetch_trb,
trace:usb_xhci_queue_event,...`) found it directly: on a captured failure, immediately after this
driver's own `CR_CONFIGURE_ENDPOINT` command was correctly processed by QEMU
(`usb_xhci_slot_configure`, `usb_xhci_ep_enable` both fired), **no completion event was ever
queued** — instead the trace showed an endless sequence of `usb_xhci_doorbell_write off 0x0000,
val 0x00000000` (the Command Ring doorbell) each immediately followed by a `TRB_RESERVED` fetch,
with **no corresponding call anywhere in this driver's own source**. Something other than this
driver was ringing the command doorbell.

The explanation: **OVMF's own native XHCI driver stays bound to this real PCI device** (it is a
real device firmware discovers and binds a driver to during its own boot-time driver-connection
pass, for USB keyboard/boot support in the UEFI shell) even though this project's driver talks to
the same controller directly via raw PCI config space and MMIO, entirely outside any UEFI
protocol. Both drivers were racing for the same Command Ring — a real firmware-vs-guest-driver
ownership conflict, not a logic bug in this driver's own TRB/cycle-bit construction (which the
earlier QEMU source review had already, correctly, found no fault in).

**The fix**: `UsbXhci.DisconnectFirmwareDriver`, called at the very start of `UsbXhci.MapMmio`
(before any register on the controller is touched):
1. Builds the real `EFI_PCI_IO_PROTOCOL` GUID and calls `BootServices.LocateHandle` (ByProtocol)
   to enumerate every PCI I/O handle firmware knows about.
2. For each, calls `HandleProtocol` to get the real `EFI_PCI_IO_PROTOCOL` interface, then its
   `GetLocation` method to read the real Segment/Bus/Device/Function it represents.
3. Compares that against the same Bus/Device/Function this driver's own raw PCI config-space scan
   (`UsbXhci.DiscoverPci`) already found for the xHCI controller.
4. On a match, calls `BootServices.DisconnectController` on that handle — forcing UEFI's own
   driver stack off the controller before this driver starts reconfiguring it.

This required two new real UEFI bindings (`arcology-os/include/arco/uefi_bindings.hpp`):
`EFI_BOOT_SERVICES.DisconnectController` (offset `0x110`) and a new `UEFI.PciIoProtocol` type
exposing `GetLocation` (offset `0x70`) — both real, spec-derived offsets, following this project's
own established `LocateHandle`/`HandleProtocol` pattern from RFC-0044's multi-handle Block I/O
enumeration.

**Verification**: re-running with the same trace events after the fix showed `CR_CONFIGURE_ENDPOINT`
immediately followed by its own `ER_COMMAND_COMPLETE` event — no more spurious doorbell writes, no
more phantom polling. 10 consecutive full real QEMU runs (enumeration + a real injected keystroke,
including a negative control with no device attached) all passed cleanly. The smoke test itself
(3 real keystrokes across 3 separate runs, plus a negative control) passes reliably, including
under real `ctest -j4` parallel load on this machine — the fixture's own report-poll ceiling needed
raising (2,000,000 → 20,000,000) to keep real margin under contention, the same class of finding as
[[qemu_harness_streaming_speedup]], but the underlying enumeration itself is now fully reliable.

## A separate, unrelated finding surfaced during this investigation

While verifying no regression was introduced, `systems_ps2_keyboard_driver_smoke` (RFC-0045 Phase 1,
completely separate PS/2 code, untouched this session) was found to fail consistently in repeated
runs on this machine. Confirmed via a controlled test (fully reverting this session's own
`uefi_bindings.hpp` changes and rebuilding the compiler) that this is **not** caused by this
session's work — the PS/2 test fails identically with or without these changes. This is a real,
pre-existing issue, not yet investigated further; flagged here rather than silently ignored.

## Remaining honest scope notes

- `MEMORY.Read16` at a high 64-bit MMIO address still returns 0 (the Phase 2-documented compiler
  bug); this driver still works around it via `MmioReadField32` everywhere. Not fixed, out of
  Phase 3's own scope.
- US QWERTY unshifted keycode translation only (matches `PS2Keyboard.ScancodeToChar`'s own scope).
- One HID interrupt endpoint, no streams, no hot-plug — matches RFC-0045's own stated Non-Goals.
