# Physical Hardware Readiness Baseline Audit

## Scope delivered

This report freezes the pre-change state for the Physical Hardware Readiness
packet. It describes the current UEFI-to-APS transition, the physical regions
used by the bootstrap proof, and the places where the implementation still
depends on bootstrap shortcuts or QEMU-specific assumptions.

## Current transition

```text
UEFI entry
  -> GOP metadata captured
  -> Boot Services memory map acquired
  -> bootstrap buffers/page-table pages allocated by Boot Services
  -> final GetMemoryMap / ExitBootServices sequence
  -> fixture builds a small identity-oriented page hierarchy
  -> CR3 is switched to the fixture root
  -> framebuffer is accessed through a fixed virtual alias
  -> contract-card rendering continues
```

The current fixture does successfully continue after loading its newly built
CR3 under QEMU/OVMF. That is a useful machine-code and page-table proof, but it
is not yet a complete APS ownership transition: the firmware descriptor list is
not translated into the persistent PRD, and the active mappings are not yet
transactionally represented by VRD records.

## Bootstrap physical-region inventory

| Region | Current source | Current treatment | Post-transition authority | Gap |
|---|---|---|---|---|
| PE32+ image/code/data | UEFI-loaded image | Remains resident after `ExitBootServices` | Implicit page-table identity mapping | Not registered as a PRD reservation |
| Current bootstrap stack | Firmware-provided execution stack / current `RSP` | Used after CR3 switch | Implicit identity mapping | Bounds and ownership are not recorded |
| Root page-table hierarchy | `BootServices.AllocatePages` | Fixture allocates root/upper tables and writes entries | Fixture-local addresses | Not allocated through PRD; no table-page ownership records |
| PRD storage buffer | `BootServices.AllocatePages` | Reserved only by the fixture's allocation call | No permanent PRD record | The buffer is not populated by descriptor translation |
| VRD/bootstrap metadata | Pool/page allocations as used by fixture | Survives transition where referenced | No authoritative VRD adoption record | Lifetime and physical backing are implicit |
| GOP framebuffer aperture | GOP mode information | Mapped by a fixed alias window; QEMU proof uses `0x20000000` | Direct page-table entries | Alias size/base and cache policy are hard-coded; no PRD device record |
| GOP metadata/protocol structures | UEFI protocol pointers | Read before firmware exit | Historical values retained by fixture | No normalized, owned post-exit record |
| Firmware memory descriptors | `GetMemoryMap` buffer | Used to obtain the final map key | Historical only in current fixture | Not validated/translated into the permanent PRD |
| ACPI/firmware tables | Not yet part of the CR3 proof | No complete retained-table handoff | Firmware-dependent | Physical reservations and ownership are absent |
| Diagnostic buffers | UEFI console/serial/fixture buffers | Ad hoc | Mixed | No allocation-free post-fault diagnostic sink |

## Allocator and ownership findings

The current post-transition proof does not have a sole physical allocator. It
still relies on Boot Services allocations performed before `ExitBootServices`,
and the page-table construction path owns its returned addresses locally. The
PRD library has a record layout and basic split/release helpers, but the boot
fixture does not ingest the firmware descriptor set into those records before
general use.

Consequently, the following invariants are not yet established:

- every page already consumed by Arcology is reserved in PRD;
- every page-table page has an owner/purpose record;
- unknown firmware descriptor types are non-allocatable by default;
- no post-transition path can return a physical page without a PRD state change;
- allocator metadata cannot be returned as free memory;
- release validates owner and rejects double release.

## Virtual-memory findings

The fixture builds a minimal hierarchy sufficient for the QEMU continuation
proof. The framebuffer alias currently uses a fixed virtual window and fixed
page-directory placement (the known QEMU path uses a `0x20000000` alias and a
hard-coded directory index). The alias is effective for the current proof, but
it is not selected from GOP aperture size and is not created through a
transaction that commits PRD, VRD, and page-table state together.

The current VRD helpers provide record storage and basic add/protect/unmap
operations. They do not yet verify actual PTEs, reject all overlaps, roll back
partial page-table materialization, or adopt existing bootstrap mappings after
CR3 activation.

## Descriptor/fault state

The fixture does not yet install APS-owned GDT, TSS, or IDT state. It therefore
continues to rely on firmware descriptor tables after the CR3 switch. No
structured CR2/fault frame or allocation-free fatal diagnostic path is active.
The current QEMU success criterion is absence of a page fault, not recoverable
fault evidence.

## Diagnostics

UEFI console output is available before firmware exit and framebuffer output is
available after the alias is established. A durable post-exit diagnostic sink
that does not depend on the framebuffer is not yet guaranteed. COM1 support is
present in the project, but the current APS proof does not establish a complete
post-transition serial ledger and does not expose a machine-readable boot-stage
record.

## Baseline conclusion

The current tree proves **APS-created CR3 continuation under QEMU/OVMF**. It
does not yet prove physical-hardware readiness. The highest-risk gaps are:

1. complete firmware-descriptor translation and bootstrap reservation
   reconciliation;
2. removal of non-PRD physical allocation paths;
3. transactional VRD/page-table mapping with consistency verification;
4. APS-owned descriptor tables and fault survival;
5. dynamic GOP aperture mapping and explicit device cache policy;
6. diagnostics that survive framebuffer failure and physical firmware
   differences.

This report is the baseline for the subsequent packet work packages. No
allocator or mapping behavior is changed by this audit.

