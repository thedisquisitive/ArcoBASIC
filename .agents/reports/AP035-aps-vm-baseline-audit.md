# AP035 — APS virtual-memory baseline audit

## Scope delivered

Audited the post-AP034 tree before beginning the APS memory/fault/interrupt/time packet.

## Current contracts

- PRD records are caller-owned `MMIOPTR` storage: count at byte 0, records at byte 8, 64 bytes each. Records contain base, page count, state, owner, provider, reason, and flags.
- VRD records are caller-owned `MMIOPTR` storage: count at byte 0, records at byte 8, 40 bytes each. Records contain virtual base, byte length, physical base, protection, and state.
- Address-space policy currently validates and lowers CR3/INVLPG mechanisms but does not yet materialize page tables.
- CR3 operations are explicit compiler mechanisms; mapping and address-space policy remain ArcoBASIC.
- `uefi_bootstrap.abas` acquires a fresh memory map and calls `ExitBootServices`, but the current proof fixtures do not yet construct an APS page-table root or continue after a CR3 cutover.

## Post-UEFI execution assumptions

The current PE32+ image uses firmware-provided translation and the compiler-generated stack/frame layout. The executable sections are page-aligned PE sections; the backend allocates a bounded stack frame in the image's active execution context. No persistent APS mapping of code, data, stack, PRD, VRD, or framebuffer exists yet. Therefore a safe CR3 switch cannot be claimed from the current baseline.

The framebuffer path currently obtains GOP metadata and uses validated MMIO mapping semantics, but preservation after a new CR3 root is not yet proven.

## Baseline validation

The existing compiler/runtime/unit and non-boot smoke tests passed through the currently runnable set, including `arcology_os_unit_tests`, fixed-width types, frontend/A-MIR, UEFI bindings, code generation, integer core, control flow, port I/O, memory address, GOP, runtime handles, PRD, VRD, and page-table mechanism smoke tests. QEMU boot tests remain long-running harness tests and were not treated as evidence of APS CR3 ownership.

## Gate result

The baseline confirms the required architectural gap: APS page-table geometry, ownership, physical access windows, hierarchy construction, and cutover are still unimplemented. The first implementation slice is therefore paging geometry policy in ArcoBASIC; no firmware descriptor is promoted into allocator state.
