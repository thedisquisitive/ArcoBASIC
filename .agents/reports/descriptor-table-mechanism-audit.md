# Descriptor-Table Mechanism Audit

## Scope delivered

Added the first missing fault-evidence machine primitive: `CPU.ReadCR2()`.
It is represented as canonical `READCR2` A-MIR, rejected from ordinary hosted
execution through the existing systems-profile rules, and lowered to the exact
x86-64 sequence `0F 20 D0` (`MOV RAX, CR2`).

The descriptor-table mechanism set now also includes `CPU.LoadGDT`,
`CPU.LoadIDT`, and `CPU.LoadTaskRegister`, lowered respectively to `LGDT [RAX]`,
`LIDT [RAX]`, and `LTR AX` after the policy supplies a descriptor address or
selector value.

## Validation

- exact encoder unit test added;
- A-MIR reveal smoke added;
- x86-64 reveal byte smoke added;
- exact GDT/IDT/TR encoder and reveal coverage added;
- existing C++ unit suite passes.

## Remaining descriptor work

`LGDT`, `LIDT`, `LTR`, normalized exception entry stubs, and APS-owned GDT/TSS/
IDT policy are still outstanding. This report intentionally does not claim
fault containment or hardware interrupt readiness.
