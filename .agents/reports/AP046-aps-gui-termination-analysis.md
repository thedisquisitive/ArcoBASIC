# APS GUI QEMU Termination Analysis

## Reported symptom

The GTK QEMU window disappeared after being launched from the automation shell.

## Changes in the APS fixture

The fixture now:

1. Allocates a fresh CR3 root and page-directory hierarchy through UEFI `AllocatePages`.
2. Builds identity mappings for bootstrap code, stack, and data.
3. Reserves a virtual alias window beginning at `0x20000000`.
4. Maps the GOP framebuffer's aligned physical 2 MiB windows into that alias.
5. Switches to the APS CR3.
6. Renders the contract through `ADDRESS.MMIO(ADDRESS.Virtual(0x20000000))`.
7. Enters `CPU.HaltForever`.

The alias was introduced because direct post-CR3 access to the firmware GOP physical
address (`0x08000000` in QEMU) produced a guest page fault. The alias path completed
headless QEMU runs without `#PF` entries in the QEMU interrupt log.

## Reproduction evidence

Running GTK QEMU synchronously with `DISPLAY=:1` and a five-second timeout leaves QEMU
running until the timeout sends SIGTERM (`rc=124`). No guest page-fault records are
produced. The generated EFI image does not request shutdown or reboot; after rendering it
deliberately halts in the APS proof loop.

The background launch used `nohup` from the tool session. That session reaps child
processes when the command returns, which explains the disappearing window. This is a
launcher-lifecycle termination, not evidence that the guest crashed.

## Correct launch

Keep QEMU attached to a terminal/session:

```sh
DISPLAY=:1 qemu-system-x86_64 \
  -bios /usr/share/ovmf/OVMF.fd \
  -drive file=fat:rw:<EFI_ROOT>,format=raw \
  -net none -vga std -display gtk,gl=off \
  -monitor none -no-reboot
```

Do not background it through a short-lived automation shell if the display must remain
open.

## Remaining engineering limitation

The current fixture maps a conservative 64-entry (128 MiB) alias span. Production VRD
policy must size and reserve the alias transactionally from `FrameBufferSize`, and the
proof should eventually install an APS-owned IDT before enabling any interrupt path.
