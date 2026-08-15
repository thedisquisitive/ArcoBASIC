# PRD Sole-Authority Gate

## Status

Not yet closed. This report records the gate and the remaining proof.

`PRDAllocateFirstFreeForOwner` now provides an ArcoBASIC policy entry point
that records owner and purpose metadata on allocated records. The legacy
three-argument allocator remains as a compatibility wrapper with owner `0`.
`PRDReleaseRegionOwned` rejects invalid indices, non-allocated records, and
owner mismatches before releasing a page range.

## Remaining acceptance work

- remove fixture-local page-table/physical bump allocation paths after the
  handoff;
- instrument every post-transition physical allocation and prove a PRD state
  transition;
- reserve all bootstrap pages before the first general allocation;
- add constrained synthetic exhaustion and double-release tests;
- wire the owner-aware allocator into page-table policy.

Until those checks pass, PRD is not claimed as the sole post-transition
authority.

