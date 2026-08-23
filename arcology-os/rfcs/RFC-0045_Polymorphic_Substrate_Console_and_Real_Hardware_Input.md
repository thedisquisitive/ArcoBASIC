# RFC-0045: Polymorphic Substrate Console — Real PS/2 and USB HID Keyboard Input

**RFC Number:** RFC-0045\
**Title:** Polymorphic Substrate Console — Real PS/2 and USB HID Keyboard Input\
**Status:** Draft (no implementation phase has begun)

**Category:** Platform / Drivers / Interactive Runtime

**Authors:** Arcology Project\
**Created:** 2026-08-22\
**Last Updated:** 2026-08-22\
**Supersedes:** None\
**Superseded By:** None

**Related Architecture:** Arcology Object Architecture; Polymorphic Substrate (APS)\
**Related RFCs:** RFC-0007 (ArcoBASIC Interactive Program Model — the consumer of this RFC's own
real input surface), RFC-0019 (Physical Region Database), RFC-0020 (Address Spaces and Virtual
Memory), the `aps-exception-entry` fixture family (real, already-proven `ExitBootServices` + own
CR3/GDT/IDT takeover this RFC builds directly on top of)

------------------------------------------------------------------------

# 1. Executive Summary

RFC-0007's first real increment (`arcology-seed-ready.abas`) proved a genuine, persistent,
keyboard-interactive `READY.` prompt — but it runs entirely inside UEFI Boot Services, using
`ConsoleIn`/`ConsoleOut` as firmware-provided protocols. It is not running *on* the Arcology
Polymorphic Substrate (APS); it is running *inside* firmware's own borrowed environment, same as
every fixture in this project before it.

APS itself already exists and is proven separately: the `aps-exception-entry` fixture family
genuinely calls `ExitBootServices`, then installs its own page tables (`CPU.WriteCR3`), its own
GDT/TSS, its own IDT, and proves real exception/fault recovery on top of all of it. That track and
RFC-0007's interactive prompt have never been unified.

A direct probe found why unification is not a simple recombination: `ConsoleIn.ReadKeyStroke`
called after `ExitBootServices` does not merely stop working the way `ConsoleOut.Write` is already
known to hang — it **crashes**, jumping into unrelated memory (`#UD` Invalid Opcode fault,
`RIP=0x000000000E0000`, a legacy BIOS shadow-memory address, definitely not this protocol's own
code). The Boot-Services-only driver behind the Simple Text protocols is torn down by
`ExitBootServices`; calling through its now-stale function pointer is undefined and, empirically,
fatal.

Under APS there is no firmware left to read a keyboard for the substrate — this RFC builds the
real thing: a raw PS/2 keyboard driver (port I/O, no firmware dependency) AND a real USB host
controller (xHCI) plus USB HID keyboard class driver, because PS/2-only input is known, from real
prior OS-development experience, to be unreliable on modern hardware — many laptops emulate PS/2
only through firmware/SMM tricks that do not survive a genuine hardware takeover, and any purely
USB-attached keyboard (external, or a laptop's own internal keyboard wired through USB rather than
a real i8042 controller) has no PS/2 path at all. Real, generic keyboard support on real modern
hardware needs both.

------------------------------------------------------------------------

# 2. Goals

1. Document, with real evidence, exactly why the existing UEFI keyboard binding cannot be reused
   after `ExitBootServices` (Section 4).
2. A real, raw PS/2 keyboard driver: port I/O scancode reading (ports `0x60`/`0x64`), a real
   Scan Code Set 1 (or 2, whichever this project's target QEMU/OVMF combination and real hardware
   actually emit — verified empirically, not assumed) to ASCII/Unicode translation table covering
   at least the same character set RFC-0007's own `READY.` prompt already needs (printable ASCII,
   Enter, Backspace).
3. A real USB host controller driver: xHCI (PCI enumeration to find the controller, MMIO BAR
   mapping, controller reset/initialization, command ring, event ring, device context base array,
   port enumeration and reset, control transfers for standard USB device enumeration).
4. A real USB HID keyboard class driver on top of the xHCI driver: HID report descriptor handling
   (or a fixed boot-protocol keyboard report format, whichever proves simpler and sufficient — see
   Section 7.2), interrupt transfers to read key state, translation to the same character set the
   PS/2 driver produces.
5. A single, real, non-halting fixture combining APS's own already-proven `ExitBootServices` +
   own-CR3/GDT/IDT takeover (reusing the exact proven sequence from `aps-gdt-reload.abas`) with
   BOTH input drivers feeding the SAME interactive loop RFC-0007's `arcology-seed-ready.abas`
   already established (`HELP`/`OT`/`?SYNTAX ERROR`/backspace editing), proven under real QEMU with
   BOTH a real emulated PS/2 keyboard AND a real emulated USB HID keyboard (QEMU supports
   `-device usb-kbd` attached to a real `qemu-xhci` controller, genuinely distinct emulation from
   the default i8042 PS/2 device), and ultimately on real physical hardware (the Lenovo ThinkPad
   E15 Gen 2), where the built-in keyboard's own real controller identity is not yet known and is
   itself a real finding this RFC's own hardware package will surface.

------------------------------------------------------------------------

# 3. Non-Goals

1. **Full USB stack generality.** This RFC's own USB work is scoped to exactly what a keyboard
   needs: device enumeration, control transfers, one interrupt IN endpoint. Mass storage, hubs
   beyond simple pass-through port enumeration, isochronous transfers, and USB 2.0/EHCI/UHCI
   fallback paths are explicitly out of scope — xHCI only, matching every real modern platform's
   own primary USB controller.
2. **Full HID generality.** Only the USB HID "boot protocol" keyboard report format (an 8-byte
   fixed report: modifier byte, reserved byte, 6 keycode bytes — a real, simple, near-universally
   supported USB HID sub-mode every USB keyboard implements specifically so BIOS/UEFI/bootloaders
   without a full HID report-descriptor parser can still read keystrokes) is required. Full HID
   report descriptor parsing for arbitrary devices is out of scope unless boot protocol proves
   insufficient.
3. **Mouse, hot-plug, or multi-keyboard support.** One keyboard, present at driver init time,
   assumed to stay present. Real hot-plug detection (a keyboard attached or removed mid-session) is
   future work.
4. **International keyboard layouts.** US QWERTY scancode-to-character mapping only, matching this
   project's own existing QEMU-testing convention (every prior keystroke-injection test in this
   project used unshifted US-layout key names).
5. **PS/2 mouse, PS/2 device hot-plug detection, or the full i8042 command set.** Only what reading
   keyboard scancodes requires.

------------------------------------------------------------------------

# 4. The Real Finding: ConsoleIn Does Not Survive ExitBootServices

A minimal probe (`ExitBootServices` via the exact, already-proven `AcquireMapAndExit` sequence
`aps-gdt-reload.abas` already uses, then a bounded polling loop calling
`systemTable.ConsoleIn.ReadKeyStroke`) crashed immediately on the first post-exit call:

```
!!!! X64 Exception Type - 06(#UD - Invalid Opcode)  CPU Apic ID - 00000000 !!!!
RIP  - 00000000000E0000, CS  - 0000000000000038, RFLAGS - 0000000000010046
```

`0x000000000E0000` is not this protocol's own code — it is the classic legacy BIOS/option-ROM
shadow region, the kind of address a stale/torn-down function pointer resolves to once whatever
memory it used to point into has been reclaimed or repurposed. This is consistent with, and a more
severe version of, this project's own already-documented finding that `ConsoleOut.Write` merely
*hangs* after `ExitBootServices` (see `aps-gdt-reload.abas`'s own exclusive use of raw
`SerialByte`-based port I/O for every diagnostic after its own `ExitBootServices` call — no
`ConsoleOut.Write` appears anywhere after that point in that file). Both `ConsoleIn` and `ConsoleOut`
are Simple Text protocols backed by Boot Services drivers; neither survives Boot Services ending.

**Implication**: any I/O the substrate needs after `ExitBootServices` — output AND input — must be
done at a lower level than UEFI protocols. Serial output already has a real, proven, working
low-level path (`SerialByte`/`PORT.WriteByte`, used throughout the `aps-exception-entry` family).
Keyboard input has no equivalent yet — this RFC builds it.

------------------------------------------------------------------------

# 5. Requirement: A Real PS/2 Keyboard Driver

## 5.1 Why build this even though USB is also required

Real, immediate value independent of the USB work: QEMU's default `-M pc` machine type includes a
real i8042 PS/2 controller, and every keystroke-injection test this project has already built
(RFC-0007's own `run-uefi-hello-with-keyboard.sh`) already goes through it by default (no explicit
USB keyboard device was ever attached) — meaning a PS/2 driver is immediately, fully testable with
existing infrastructure, and gives the substrate SOME real input path while the much larger xHCI
work is still in progress. It is also genuinely simpler: a handful of port I/O reads plus a fixed
translation table, no PCI enumeration, no MMIO, no ring buffers.

## 5.2 Real mechanism

- Port `0x64` (status/command register): bit 0 (Output Buffer Full) indicates a scancode byte is
  ready to read from port `0x60`.
- Port `0x60` (data register): read to consume the pending scancode byte.
- Scan Code Set to use: verified empirically against real QEMU/OVMF output before being assumed
  (Section 7.1's own mandate) — most PC-compatible keyboard controllers default to Scan Code Set 1
  (XT) at the i8042-to-host boundary regardless of what the physical keyboard itself speaks
  internally, since the i8042 controller itself performs translation; this MUST be confirmed with a
  real probe reading known keys before the translation table is written, not assumed from
  general PC hardware knowledge alone.
- Break codes (key release) in Set 1 are the make code with the high bit set (make code `+ 0x80`);
  this driver only needs make codes (key press) for RFC-0007's own line-input model, but must
  correctly discard break codes rather than misinterpreting them as new keystrokes.

## 5.3 Real stdlib surface

- `PS2Keyboard.Init() AS BOOL` — real controller self-test/enable sequence (verified against a real
  reference before being assumed, matching Section 7.1).
- `PS2Keyboard.PollScancode() AS U32` — returns 0 if no scancode pending, otherwise the raw scancode
  byte (including the break-code high bit, so the caller can distinguish make/break).
- `PS2Keyboard.ScancodeToChar(scancode AS U32) AS U16` — translates a real Set 1 make code to an
  ASCII/Unicode character matching RFC-0007's own existing character set (0 if the scancode has no
  printable/control-character mapping this driver defines).

------------------------------------------------------------------------

# 6. Requirement: A Real USB Host Controller (xHCI) and HID Keyboard Driver

## 6.1 Real mechanism, xHCI

1. **Discovery**: PCI configuration space enumeration to find a device with Class `0x0C`
   (Serial Bus Controller), Subclass `0x03` (USB), Programming Interface `0x30` (xHCI) — real PCI
   config-space port I/O (`0xCF8`/`0xCFC`) or, if this project's own existing UEFI-boot-time
   context still has it available, `PciIo` protocol discovery before `ExitBootServices` to locate
   the controller's own MMIO BAR up front (verify which is actually simpler/more reliable
   empirically, per Section 7.1 — this is exactly the kind of design-constraint question earlier
   RFCs in this project have found answers to only by testing directly rather than assuming).
2. **MMIO BAR mapping**: the xHCI controller's Capability Registers, Operational Registers,
   Runtime Registers, and Doorbell Registers all live in one MMIO region located via the discovered
   BAR — reusing `MEMORY.MapDevice`/`ADDRESS.MMIO`, the same mechanism this project's own GOP
   framebuffer access already uses.
3. **Controller initialization**: reset (`USBCMD.HCRST`), wait for `USBSTS.CNR` to clear, allocate
   and register the Device Context Base Address Array, allocate and register a Command Ring,
   allocate and register (via an Event Ring Segment Table) an Event Ring, enable the controller
   (`USBCMD.RS`).
4. **Port enumeration**: read `PORTSC` registers for each real root hub port, detect connected
   devices (`CCS` bit), issue a port reset, wait for the port to report enabled.
5. **Device enumeration**: issue an `Enable Slot` command via the Command Ring, allocate an input
   context, address the device (`Address Device` command using real, standard USB control transfers
   — `GET_DESCRIPTOR` for the device descriptor, then the configuration descriptor to find the HID
   interface and its interrupt IN endpoint).
6. **Endpoint configuration**: configure the discovered interrupt IN endpoint via a `Configure
   Endpoint` command, matching the real descriptor-reported max packet size and polling interval.

## 6.2 Real mechanism, USB HID boot-protocol keyboard

- `SET_PROTOCOL` control request (or rely on the device's own default boot-protocol state — verify
  which real devices actually need explicitly, per Section 7.1) to ensure the fixed 8-byte report
  format.
- Poll the interrupt IN endpoint (via the xHCI Event Ring reporting Transfer Events) for new 8-byte
  HID boot-protocol reports: byte 0 = modifier bitmask (Ctrl/Shift/Alt/GUI, left and right), byte 1
  = reserved, bytes 2-7 = up to 6 simultaneously-pressed USB HID keycodes (0 = no key in that slot).
- Translate USB HID keycodes (a real, standard, different numbering from PS/2 scancodes) to the
  SAME character set `PS2Keyboard.ScancodeToChar`'s own output uses, so the interactive loop this
  RFC feeds does not need to know or care which physical input path a keystroke came from.

## 6.3 Real stdlib surface

- `UsbXhci.Discover(systemTable AS UEFI.SystemTable) AS BOOL` — real PCI/MMIO discovery, called
  BEFORE `ExitBootServices` if that proves the more reliable discovery path (Section 7.1).
- `UsbXhci.Init() AS BOOL` — real controller reset/ring setup, safe to call after
  `ExitBootServices` (uses only the MMIO region already discovered/mapped, no Boot Services calls).
- `UsbXhci.EnumerateKeyboard() AS BOOL` — real port scan, device address, HID interface discovery,
  endpoint configuration. Returns FALSE (fail-closed, matching this project's own established
  convention) if no USB HID keyboard is found, rather than a partially-configured state.
- `UsbHidKeyboard.PollReport() AS U32` — returns 0 if no new report, otherwise a real USB HID
  keycode (first newly-pressed key found in the latest report, matching `PS2Keyboard.
  PollScancode`'s own single-key-per-call contract for a uniform caller-side loop).
- `UsbHidKeyboard.KeycodeToChar(keycode AS U32) AS U16` — same character-set contract as
  `PS2Keyboard.ScancodeToChar`.

------------------------------------------------------------------------

# 7. AI Implementation Guidance

## 7.1 Required boundaries

- Every register offset, bit position, and command encoding (i8042 status bits, xHCI Capability/
  Operational/Runtime register layout, TRB — Transfer Request Block — formats, HID boot-protocol
  report layout) MUST be verified against a real, cited reference (Intel's own xHCI specification
  for the USB work, matching this project's own established EDK2-citation discipline for every
  UEFI binding) — not assumed from general recollection, and cross-checked against real captured
  QEMU behavior via a probe BEFORE being relied on in a real fixture, exactly as this RFC's own
  Section 4 finding was itself confirmed empirically rather than assumed.
- A structural (AST/A-MIR/X86_64) smoke test proves each new binding/driver function compiles
  correctly BEFORE any real QEMU proof is attempted, matching every other increment in this
  project's history.
- Real QEMU proof for the PS/2 driver MUST use real injected keystrokes (this project's own
  existing `run-uefi-hello-with-keyboard.sh` mechanism already does this via the default i8042
  device). Real QEMU proof for the USB driver MUST use a REAL, DISTINCT device topology — `-device
  qemu-xhci` plus `-device usb-kbd` — not the same PS/2 path repurposed, and MUST confirm the two
  input paths are genuinely independent (e.g., a keystroke sent while ONLY the USB keyboard is
  attached must still be read correctly, proving the xHCI path itself works, not merely that the
  combined fixture happens to also still see PS/2 input).

## 7.2 No silent scope reduction without saying so

If the USB HID boot-protocol format proves insufficient for the real QEMU `usb-kbd` device (e.g.,
if it does not honor `SET_PROTOCOL` the way assumed, or reports keys in a shape this RFC's own
Section 6.2 did not anticipate), state the finding and the revised approach explicitly in that
phase's own report — matching RFC-0044 Section 7.2's own precedent (which itself found and
honestly documented a real discovery-order surprise rather than silently working around it).

## 7.3 Mandatory acceptance evidence

- Phase 1 (PS/2 driver): a real fixture reads a real injected keystroke via raw port I/O alone (no
  `ConsoleIn` involved at all) and correctly translates it to the expected character — proven both
  BEFORE `ExitBootServices` (simpler, faster iteration) and AFTER (the real target environment).
- Phase 2 (xHCI driver): real controller discovery, reset, and initialization proven independently
  of keyboard enumeration first (e.g., confirm `USBSTS.CNR` clears, confirm a real port reports a
  connected device) before attempting full device enumeration — isolating controller-level bugs
  from device-level ones.
- Phase 3 (USB HID keyboard driver): a real fixture reads a real injected keystroke via the xHCI +
  HID path alone (PS/2 driver not involved, or explicitly proven absent/irrelevant) and correctly
  translates it to the expected character.
- Phase 4 (substrate + combined input): a single fixture, post-`ExitBootServices`, running under
  APS's own CR3/GDT/IDT (reusing `aps-gdt-reload.abas`'s own proven sequence), genuinely running
  RFC-0007's `HELP`/`OT`/`?SYNTAX ERROR`/backspace command loop, accepting real keystrokes from
  EITHER input path, with negative control and repeated-run determinism matching every other real
  proof in this project's history.
- Phase 5 (hardware package): a new `WP-0xx` package for the Lenovo ThinkPad E15 Gen 2 — this is
  also the first real-hardware test of which input path (PS/2, USB, or both) this specific laptop's
  own keyboard controller actually uses, itself a genuinely open, real question this RFC cannot
  answer from QEMU alone.

------------------------------------------------------------------------

# 8. Implementation Phases

- **Phase 1 — Real PS/2 Keyboard Driver** (Section 5). Real port I/O scancode reading + real
  translation table, proven under QEMU with real injected keystrokes, both before and after a real
  `ExitBootServices` call.
- **Phase 2 — Real xHCI Host Controller Driver** (Section 6.1). Real PCI/MMIO discovery,
  initialization, port/device enumeration, proven independently of keyboard-specific behavior.
- **Phase 3 — Real USB HID Keyboard Driver** (Section 6.2). Real boot-protocol report reading on
  top of Phase 2, proven with a real injected keystroke via a genuinely distinct QEMU USB keyboard
  device.
- **Phase 4 — Substrate Unification**. `ExitBootServices` + APS's own proven CR3/GDT/IDT takeover +
  RFC-0007's real interactive loop, fed by BOTH input paths from Phases 1 and 3.
- **Phase 5 — Hardware Validation Package**. A new `WP-0xx` package for the Lenovo ThinkPad E15
  Gen 2.

Phase 1 has no dependency on Phases 2-3 and delivers real, immediate value alone. Phase 3 depends
on Phase 2. Phase 4 depends on Phase 1 AND Phase 3 (both real input paths must exist before the
combined fixture can honestly claim to support either). Phase 5 depends on Phase 4.

Given Phase 2-3's own real complexity (Section 1's own explicit acknowledgment that this is a
larger, higher-risk undertaking than anything else in this project's history), Phase 1 is scoped to
complete and be reported on independently before Phase 2 begins, rather than being treated as a
single combined increment.

------------------------------------------------------------------------

# 9. Revision History

| Version | Date | Summary |
|---------|------|---------|
| 0.1 | 2026-08-22 | Initial draft. Written directly after discovering `ConsoleIn.ReadKeyStroke` crashes (not merely hangs) after `ExitBootServices`, and after the user's own real prior OS-development experience that PS/2-only keyboard support is unreliable on modern hardware — explicitly requested generic USB HID keyboard support alongside PS/2, not as a replacement for it. Five phases: real PS/2 driver, real xHCI host controller driver, real USB HID keyboard driver, substrate unification with RFC-0007's own interactive loop, and a new hardware validation package. Status: Draft; no implementation phase has begun. |
| 0.2 | 2026-08-22 | Phase 1 (real PS/2 driver) delivered and QEMU-proven both before and after `ExitBootServices` (commit `60c4e01`). Phase 2 (real xHCI host controller driver) delivered and QEMU-proven independently of any device/keyboard enumeration: real PCI discovery, MMIO mapping, controller reset (`USBSTS.CNR` cleared), Command/Event ring + DCBAA setup, controller start (`USBSTS.HCH` cleared), and real port enumeration/reset — proven against a real `qemu-xhci` controller with a real `usb-kbd` device attached, with a real negative control (no device attached, all ports honestly report unconnected). Two real bugs found and fixed during Phase 2's own development: a real, intermittent `PORTSC` write bug (an earlier "preserve upper 16 bits" mask inadvertently cleared Port Power, `PP`, on every reset attempt — QEMU tolerated this most of the time, which is why it passed manual testing 3/3 before a smoke test's own repeated-run harness caught it) and a real compiler bug (`MEMORY.Read16` returns 0 at a high 64-bit MMIO address; documented and worked around via 32-bit-aligned reads, not fixed — out of this RFC's own critical path). See `.agents/reports/aps-rfc-0045-phase-2.md` for full detail. Phases 3-5 remain open. |
| 0.3 | 2026-08-22 | Phase 3 (USB HID boot-protocol keyboard driver) attempted: structurally complete (real device enumeration, `GET_DESCRIPTOR`, Configure Endpoint, `SET_CONFIGURATION`/`SET_PROTOCOL`, a non-blocking `PollReport` matching `PS2Keyboard.PollScancode`'s own contract, real HID Usage Page keycode translation) with 3 real bugs found and fixed (a QEMU-crashing Endpoint Context bit-position bug, an off-by-one interface-class descriptor offset, an unsafe DMA-buffer virt==phys assumption) — but Section 7.3's own mandatory acceptance evidence (a real fixture reliably reading a real injected keystroke) was **not** achieved deterministically in this session's own test environment, despite direct QEMU-source cross-referencing, a bounded retry mechanism, and `-icount` isolation testing pointing at (but not conclusively confirming) host CPU contention as a real contributing factor. Phase 3 is explicitly **not** marked done; see `.agents/reports/aps-rfc-0045-phase-3.md` for the full, honest account. The new smoke test exists but is deliberately not wired into the CTest gate. Phases 4-5 remain blocked on Phase 3's own real completion. |
| 0.4 | 2026-08-23 | Phase 3's real root cause found and fixed: QEMU's own xHCI trace events (`-d trace:usb_xhci_*`) showed the Command Ring doorbell being rung repeatedly with no corresponding call anywhere in this driver, immediately after `CR_CONFIGURE_ENDPOINT` was correctly processed but before its own completion event was ever queued — OVMF's own native XHCI driver stays bound to the same real PCI device this driver also drives directly via raw PCI/MMIO, and both were racing for the same Command Ring. Fixed with a new `UsbXhci.DisconnectFirmwareDriver` (two new real UEFI bindings, `EFI_BOOT_SERVICES.DisconnectController` and `UEFI.PciIoProtocol.GetLocation`, following RFC-0044's own multi-handle enumeration pattern), called at the start of `UsbXhci.MapMmio`, forcing UEFI's own driver stack off the controller first. Verified via the same trace events (`CR_CONFIGURE_ENDPOINT` now immediately followed by its own completion event, no more spurious doorbell writes) and 10 consecutive clean real QEMU runs. Phase 3 is now DONE — `systems_usb_hid_keyboard_driver_smoke` passes reliably (including under real `ctest -j4` parallel load) and is wired into the CTest suite. Phase 4 (substrate unification) is next. |
| 0.5 | 2026-08-23 | Phase 4 (substrate unification) delivered and QEMU-proven: `aps-arcology-seed-substrate.abas` runs a real `ExitBootServices` + APS's own CR3/GDT/IDT takeover (`aps-gdt-reload.abas`'s own proven sequence, reused verbatim) then genuinely runs RFC-0007's own `HELP`/`OT`/`?SYNTAX ERROR`/backspace command loop, fed by BOTH the Phase 1 PS/2 and Phase 3 USB HID drivers via a new `PollAnyKey`. Output moves to the real serial port (`ConsoleOut` hangs post-exit; a real, documented substitution, not a graphical console). A real bug found and fixed: `aps-gdt-reload.abas`'s own minimal identity map only covers the low 1GB, which was never a problem there (it touches no device MMIO after its own CR3 switch) but is here — the xHCI controller's own real MMIO BAR sits far outside that range, and `UsbHidKeyboard.PollReport` needs to keep reading it post-substrate. Fixed with one additional, dynamically-computed 1GB page mapping (guarded against the BAR being absent, which an earlier version of the fix got wrong and a real PS/2-only negative-control run caught immediately). Proven with 4 real, separately-launched QEMU scenarios: combined PS/2+USB, PS/2 alone, USB HID alone, and a negative control with no input device at all — all real, all passing, wired into CTest as `systems_aps_arcology_seed_substrate_smoke`. Full detail in `.agents/reports/aps-rfc-0045-phase-4.md`. Phase 5 (hardware validation package) is the only phase left. |
| 0.6 | 2026-08-23 | Before starting Phase 5, the user flagged (and chose to fix rather than accept) a real practical gap: the substrate's only post-exit output was serial, which the user's own laptop can't expose without extra hardware, making Phase 5 untestable as scoped. Added a real second output channel: a GOP-framebuffer text terminal (an embedded 8x8 bitmap font rasterized offline from a real monospace TrueType font, packed as 95 `U64` glyphs; a new `EchoChar` sink writes to both serial and the new terminal from every existing call site). The same `MapExtra1GbRegion` 1GB-identity-map fix from Phase 4 was generalized (not duplicated) to also cover the GOP framebuffer's own real base address, including a real edge case Phase 4 alone never needed: two real regions coincidentally sharing the same top-level page-table slot. Proven with a real QEMU screendump (an actual live-framebuffer P6 PPM capture) plus a new, dependency-free pixel-region checker (`check_ppm_text_region.py`) confirming real rendered text, cross-checked by manual visual inspection of the captured screenshot; all of Phase 4's own existing proofs re-ran unchanged. Full detail in `.agents/reports/aps-rfc-0045-phase4-gop-terminal.md`. Phase 5 is next, now genuinely observable on the user's own screen. |
