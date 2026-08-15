# Agent Packet 034 — Address-space hardware mechanisms

## Delivered

- Added exact x86-64 encoder primitives for `MOV RAX, CR3`, `MOV CR3, RAX`, and `INVLPG [RAX]`.
- Added canonical ArcoBASIC/A-MIR operations `CPU.ReadCR3`, `CPU.WriteCR3`, and `CPU.InvalidatePage`.
- Added `arcology-os/stdlib/address_space_policy.abas`, keeping address-space switching and page invalidation policy in ArcoBASIC.
- Added a freestanding reveal smoke test covering A-MIR and exact machine bytes.

## Verification

- `systems_arco_basic_virtual_region_smoke` — passed.
- `systems_arco_basic_page_table_smoke` — passed.
- `arcology_os_tests` — passed, including exact byte assertions.

## Boundary

These are privileged compiler mechanisms only. No page-table allocation, mapping policy, CR3 validation, or scheduler policy was moved into C++; those remain the responsibility of the ArcoBASIC virtual-memory layer. Executing the fixture directly would require a valid substrate-owned page-table root and is intentionally not a QEMU boot test yet.
