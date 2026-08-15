# RFC-0020: Address Spaces and Virtual Memory

Status: Draft — Foundational

RFC-0020 defines substrate-owned address spaces, virtual-region records, mappings, page-table
ownership, and the compiler/runtime boundary after RFC-0018 and RFC-0019. Physical Regions remain
the sole physical backing authority; Address Spaces own root page tables and virtual-region policy.

ArcoBASIC owns `CreateAddressSpace`, `DestroyAddressSpace`, `Map`, `Unmap`, `Protect`, and address
space diagnostics. The compiler supplies only mechanisms such as CR3 access, `INVLPG`, cache/TLB
hooks, and page-table entry encoding. Every mapping is represented in a persistent Virtual Region
Database with virtual base, length, physical backing, protection, owner/provider, sharing, and state.

The first implementation phase is source-level virtual-region validation and persistent record
storage. Page-table construction and CR3 activation require a subsequent hardware-mechanism packet.
