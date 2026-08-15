# AP036 — x86-64 paging geometry policy

## Scope delivered

Added `arcology-os/stdlib/x86_64_paging_policy.abas` with the Generation-1 4 KiB paging constants and policy helpers for page offsets, checked page alignment, canonical-address/range validation, physical-address mask validation, and PML4/PDPT/PD/PT index extraction.

## Architecture decisions

- Generation-1 uses four-level paging and 512 entries per table.
- Canonicality follows 48-bit x86-64 virtual addresses: low half through `0x00007fff_ffffffff` and high half from `0xffff8000_00000000`.
- Physical addresses are restricted to the current 48-bit physical mask.
- Zero-length ranges and arithmetic overflow are invalid.
- Policy is expressed in ArcoBASIC; no compiler mapping policy was added.

## Validation

`systems_arco_basic_page_table_geometry_smoke` passed. The fixture reveals A-MIR shift/mask operations and builds a freestanding EFI artifact.

## Known limitation

The policy helpers are not yet connected to a page-table allocator or materialized hierarchy. Those are subsequent AP work packages and are required before any CR3 cutover can be considered safe.
