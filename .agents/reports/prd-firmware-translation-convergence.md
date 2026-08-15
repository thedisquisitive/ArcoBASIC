# PRD Firmware Translation Convergence

## Scope delivered

Added `prd_firmware_translation.abas`, which consumes UEFI descriptor records
once and translates firmware types into Arcology PRD states and flags. Free
conventional/persistent memory, runtime memory, device apertures, ACPI classes,
and unknown/reserved classes no longer need to be interpreted by allocator
policy as raw UEFI type numbers.

`PRDAddRegion` now validates page alignment, checked range end arithmetic, and
rejects overlap with an existing PRD record.

## Architecture decisions

- UEFI type numbers are bootstrap input only.
- Conventional (type 7) and persistent (type 14) memory begin `FREE`.
- Runtime types (5/6) are protected as `RUNTIME`.
- MMIO types (11/12) are protected as `DEVICE`.
- All other types default to `RESERVED`.
- Unknown or malformed ranges fail closed.

## Files added

- `arcology-os/stdlib/prd_firmware_translation.abas`
- `arcology-os/tests/fixtures/physical-hardware-readiness/prd-translation.abas`
- `arcology-os/tests/systems/systems_arco_basic_prd_translation_smoke.sh`

## Files modified

- `arcology-os/stdlib/physical_region_database.abas`
- `arcology-os/stdlib/physical_region_database_runtime.abas`

## Tests and validation

The translation fixture is accepted by the shared frontend and its A-MIR
contains a typed call to `PRDTranslateFirmwareMap`. The existing persistent PRD
freestanding smoke now also builds successfully after the runtime file was
made self-contained.

## Known limitations / intentional deferrals

The current fixture has not yet wired this translator into the final
`GetMemoryMap`/`ExitBootServices` handoff. It also does not yet normalize and
reserve every bootstrap allocation (image, stack, tables, diagnostics) before
general allocation. Those are the next reconciliation steps.

