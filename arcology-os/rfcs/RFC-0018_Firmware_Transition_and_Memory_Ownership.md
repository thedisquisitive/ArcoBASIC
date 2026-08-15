# RFC-0018: Firmware Transition and Memory Ownership

Status: Draft

This RFC defines the permanent UEFI-to-Arcology ownership transition. Before
`ExitBootServices`, firmware owns allocation and descriptors; afterward, Arcology owns physical
memory, reservations, page tables, and allocation policy. Firmware descriptors are validated once,
translated into substrate-owned region metadata, and never consulted by the allocator again.

The bootstrap contract includes the map buffer, map size, descriptor size/version, and map key. The
final sequence is: acquire map, validate descriptors, reserve the Arcology image/metadata,
framebuffer, runtime services, ACPI, and MMIO regions, acquire the final map key, call
`ExitBootServices`, and continue using only Arcology-owned state.

Allocator policy belongs in ArcoBASIC. It must provide checked page allocation/release, alignment,
overflow and overlap rejection, fragmentation/coalescing behavior, protected metadata, and
deterministic diagnostics. The compiler supplies only privileged mechanisms (address operations,
memory access, barriers, and future page-table instructions). The RFC requires documented
bootstrap types, `MEMORY.Initialize/AllocatePages/ReleasePages/Reserve/IsReserved`, descriptor and
region diagnostics, and validation under QEMU/OVMF and real hardware.

The first implementation phase is descriptor validation and region translation. Persistent region
storage, reservation tracking, and a post-handoff allocator follow without changing the source
contract.
