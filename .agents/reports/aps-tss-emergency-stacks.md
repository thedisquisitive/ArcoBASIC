# APS TSS and Emergency Stacks

## Scope delivered

Added ArcoBASIC policy for a zeroed 104-byte x86-64 TSS, RSP0, IST1, I/O-map
limit, and the corresponding 16-byte available-TSS descriptor. IST1 is reserved
for the double-fault path.

## Remaining ownership work

The actual emergency stack must still be allocated through PRD, guarded and
recorded in VRD, then its top supplied to `BuildTSS`. `LTR` must not be executed
until that reservation and mapping are verified.

