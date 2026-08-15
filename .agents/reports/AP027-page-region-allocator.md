# Agent Packet 027 — checked page-region allocator

Extended `uefi_memory_manager.abas` with two source-level policies:

- `FirstConventionalBase` finds the first conventional-memory descriptor.
- `AllocatePagesFromRegion` performs checked 4 KiB page-granular bump allocation within a region.

The allocator rejects zero-sized requests, out-of-range allocation indices, and requests that
exceed the region. AST, A-MIR, and freestanding PE32+ smoke validation pass.

This is a deterministic region allocator policy, not yet a concurrent free-list or page-table
manager. Those remain necessary for a complete post-UEFI runtime.
