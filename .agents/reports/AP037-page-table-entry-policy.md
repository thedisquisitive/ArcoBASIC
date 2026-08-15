# AP037 — page-table entry policy and bootstrap allocation conflict

## Scope investigated

Added the first APS CR3-cutover fixture and attempted to obtain three table pages before `ExitBootServices`.

## Evidence

- The fixture boots under QEMU/OVMF and emits `APS PRE`.
- It does not reach the post-allocation marker, and no CR3-active marker is observed.
- The generated call uses the expected `EFI_BOOT_SERVICES.AllocatePool` table slot (`0x40`) and Microsoft x64 argument registers, but the service does not produce a usable continuation in this fixture.

## Resolution status

The minimal `AllocatePool` and `AllocatePages` fixtures return successfully under QEMU/OVMF, proving the external-call ABI. The cutover fixture now obtains page-aligned physical table and PRD storage through `AllocatePages`, constructs a fresh identity hierarchy, preserves the live stack mapping, switches CR3, verifies it with `CPU.ReadCR3`, and continues into framebuffer writes.

## Intentional bootstrap limitation

The current proof still uses firmware allocation to bootstrap the first PRD/table pages before `ExitBootServices`; the PRD translation and allocator must now take ownership of all remaining conventional regions from the final memory map. The hierarchy currently identity-maps the first 4 GiB and the live stack region; it is not yet the final VRD-derived global layout.
