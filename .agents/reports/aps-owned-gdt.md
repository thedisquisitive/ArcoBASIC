# APS-Owned GDT

## Scope delivered

`descriptor_table_policy.abas` now constructs a minimal long-mode GDT in
ArcoBASIC: null, 64-bit code, long-mode data, and an available 64-bit TSS
descriptor pair. The pseudo-descriptor has a 39-byte limit and is consumable by
the canonical `CPU.LoadGDT` mechanism.

The descriptor values follow Intel's long-mode segment/system descriptor
layout. Compiler involvement is limited to `LGDT [RAX]` encoding.

## Validation

The policy builds freestanding and the encoder/reveal suite checks exact LGDT
bytes.

## Remaining activation gate

The live CR3 fixture does not load this GDT yet. Segment-selector reload and a
valid exception entry path must be present before replacing the firmware GDT in
the integrated proof.

