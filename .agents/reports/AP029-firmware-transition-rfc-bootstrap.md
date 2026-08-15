# Agent Packet 029 — RFC-0018 bootstrap and region policy

Checked in RFC-0018 and Agent Packet 018 as the authoritative firmware-transition contract.
Added `physical_region_database.abas` with ArcoBASIC implementations for checked region-end
calculation, descriptor geometry validation, and overlap rejection. These policies cover page
alignment, descriptor-size minimums, page-count/address overflow, and protected-region collision
checks.

`systems_arco_basic_region_policy_smoke` validates AST, A-MIR, and freestanding PE32+ generation.
Persistent region storage and full allocator state remain the next implementation phase of the
packet.
