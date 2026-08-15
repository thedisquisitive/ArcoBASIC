# Agent Packet 032 — stateful PRD operations

The persistent ArcoBASIC PRD buffer now supports state transitions:

- `PRDReserveRegion` marks free records reserved.
- `PRDAllocateFirstFree` performs first-fit allocation and appends a split allocated record.
- `PRDReleaseRegion` returns allocated records to the free state and rejects double release.
- `PRDCoalescePair` merges adjacent free records and retires the second record.

All operations use the PRD buffer as authoritative state and preserve the fixed record metadata
layout. The PRD runtime smoke test passes through freestanding PE32+ generation.
