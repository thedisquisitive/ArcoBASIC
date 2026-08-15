# APS CR3 Cutover and GOP MMIO Alias

## Scope delivered

The APS cutover fixture now builds a fresh x86-64 hierarchy from `AllocatePages`, switches
to its CR3, and renders the contract card after the switch.

## Architecture decision

The firmware GOP physical BAR (`0x08000000` under QEMU) is not reused as a virtual address
after ownership transition. APS reserves a virtual alias window beginning at `0x20000000`
and installs 2 MiB leaf mappings to the aligned GOP physical range. Rendering uses an
`MMIOPTR` constructed from that APS virtual alias.

## Validation

The QEMU/OVMF run completed with `APS PRE`, `APS PAGE`, `APS TABLE OK`, and `APS BUILT`; the
post-CR3 rendering path completed without a page fault. The previous direct-BAR path faulted
at `CR2=0x08000000`. The alias path produced no `#PF` entries in the QEMU interrupt log.

## Limitation

The fixture currently maps a bounded 64-entry (128 MiB) alias window, sufficient for the GOP
framebuffer sizes exercised by the proof. Production VRD policy must derive the window length
from the framebuffer size and reserve the virtual range transactionally.
