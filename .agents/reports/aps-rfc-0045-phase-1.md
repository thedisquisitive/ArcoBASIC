# RFC-0045 Phase 1: Real PS/2 Keyboard Driver

**Status:** Delivered and QEMU-proven, both before and after `ExitBootServices`.

## Why this phase

RFC-0007's `arcology-seed-ready.abas` proved a genuine, persistent interactive `READY.` prompt —
but entirely inside UEFI Boot Services. A direct probe (built to answer the user's own question,
"is this running on the polymorphic substrate?") found `ConsoleIn.ReadKeyStroke` crashes, not
merely hangs, after `ExitBootServices` (`#UD` Invalid Opcode fault, `RIP=0x000000000E0000` — a
stale function pointer into torn-down Boot Services driver memory). The substrate has no keyboard
input path of its own without a driver that does not depend on any UEFI protocol at all.

## What was built

`stdlib/ps2_keyboard_policy.abas`: raw port I/O (`0x60` data, `0x64` status/command) keyboard
driver, US QWERTY, unshifted Scan Code Set 1. `PS2Keyboard.Init()` (real, minimal, standard i8042
sequence: disable port 2 in case a mouse is present, flush stale bytes, enable port 1),
`PS2Keyboard.PollScancode()` (returns 0 or the real raw byte), `PS2Keyboard.ScancodeToChar()`
(real translation table, fail-closed — 0 for anything undefined, including break codes and
modifier keys).

**The scan code set was verified empirically before the table was written, not assumed** (RFC-0045
Section 7.1's own mandate): a standalone probe read raw bytes from port `0x60` while a real 'a'
keystroke was injected via QEMU's monitor. Result: `SC=1E` (make) then `SC=9E` (make + 0x80,
break) — exactly Scan Code Set 1's own known encoding for 'a', confirming the i8042 controller's
standard legacy-translation behavior is active under this QEMU/OVMF combination, as expected but
not assumed.

## Validation

New fixture `ps2-keyboard-driver.abas`: reads one real injected keystroke via the driver BEFORE
`ExitBootServices` (fast iteration, `ConsoleOut` still safe for a dual-report), calls the exact
same proven `AcquireMapAndExit` sequence `aps-gdt-reload.abas` already established, then reads a
SECOND real injected keystroke via the SAME driver AFTER `ExitBootServices` — the real proof this
phase exists for. First attempt succeeded cleanly on both sides:

```
PS2DRIVER PRE-EXIT GOT KEY
P1=x
EXOK
P2=y
DONE
```

3x determinism confirmed with three different key pairs (`q`/`z`, `j`/`m`, `5`/`0`) — proving the
translation table generally, not one coincidentally-correct key. New smoke test
`systems_ps2_keyboard_driver_smoke`, registered in `Testing.cmake`. Full regression suite: 93
tests total after this phase.

## What this does not close

Real hardware validation — this driver has never run on the Lenovo E15 Gen 2 or any physical
machine. Per the user's own direct, experience-based concern, PS/2 alone is known to be unreliable
on modern hardware (much of it emulated only through firmware/SMM tricks that may not survive a
real hardware takeover, and any purely USB-attached keyboard has no PS/2 path at all) — RFC-0045
Phases 2-3 (real xHCI host controller + USB HID keyboard driver) are required before Phase 4
(substrate unification with RFC-0007's interactive loop) can honestly claim generic keyboard
support, matching RFC-0045 Section 8's own explicit dependency ("Phase 4 depends on Phase 1 AND
Phase 3"). Shift/Ctrl/Alt modifier tracking and non-US keyboard layouts are also out of scope,
named explicitly in RFC-0045 Non-Goals 4.
