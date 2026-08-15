# APS-Owned IDT

## Scope delivered

Added ArcoBASIC IDT construction policy for 256 entries. Architectural vectors
0–31 receive present interrupt gates, and vector 8 selects IST1. The IDTR
pseudo-descriptor covers the complete 4096-byte table.

`CPU.Breakpoint` is now a canonical hardware semantic lowering to `INT3`, ready
for the recoverable breakpoint proof.

## Remaining entry-ABI gate

The common exception entry stub is not implemented. The IDT therefore is not
loaded by the live fixture yet. An ordinary compiled function is not an
interrupt handler: the next work must normalize CPU error-code/no-error-code
frames, preserve registers, maintain stack alignment, and return with IRETQ.

