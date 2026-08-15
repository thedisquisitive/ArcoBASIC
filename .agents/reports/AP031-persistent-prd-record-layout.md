# Agent Packet 031 — persistent PRD record layout

Added a persistent, caller-owned PRD buffer layout in ArcoBASIC. `PRDInitialize`, `PRDRegionCount`,
`PRDAddRegion`, and `PRDRegionState` manage a fixed 64-byte record format containing base, pages,
state, owner, provider, reason, and flags. Firmware descriptors are not consulted by these
operations; the buffer is the substrate-owned database input.

The PRD runtime smoke test validates typed memory reads/writes, record indexing, and freestanding
PE32+ generation. A future language storage phase can replace the buffer encoding without changing
the allocator-facing policy APIs.
