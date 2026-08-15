# Agent Packet 013 — Microsoft x64 stack argument lowering

## Implemented

The Fission x86-64 backend now supports incoming 5th-and-later integer/pointer parameters and
reserves a non-overlapping outgoing stack area for external calls. Incoming arguments are loaded
from `entry RSP + 40 + 8*n` after the prologue and stored in the normal spill frame. External calls
with stack arguments place them at the required `RSP+32` outgoing positions and preserve the
resolved indirect-call target in `R11` while loading those values.

## Validation

- `FillMappedSurface` now uses an explicit fifth `color AS U32` parameter.
- X86 reveal contains the stack load (`48 8b 84 24 ...`) and the substrate smoke passes.
- Port-I/O, runtime-handle, memory-address, GOP, runtime, and resource-registry tests pass.
- The port-I/O smoke harness now flattens formatted hexdumps before byte matching, avoiding
  display-line boundary false negatives.

## Remaining boundary

Internal freestanding function calls and multi-function PE emission remain separate work. This
patch extends the established Microsoft x64 ABI without introducing a compiler-only substrate API.
