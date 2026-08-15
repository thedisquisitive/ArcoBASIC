# Agent Packet 030 — RFC-0019 region-state policy

Checked in RFC-0019 and Agent Packet 019. Added `physical_region_allocator.abas` with source-level
region-state policies for reservation eligibility, split validity, adjacent-free coalescing, and
explicit overflow/overlap invariants. The current scalar API preserves the contract while the
language record/array representation is still being designed.

`systems_arco_basic_region_allocator_smoke` validates AST, A-MIR, and freestanding PE32+ emission.
