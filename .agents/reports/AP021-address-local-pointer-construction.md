# Agent Packet 021 — explicit local pointer construction

Added `ADDRESS.Local(variable)` to the freestanding address semantics. It emits a typed `PTR`
pointing at the variable's spill slot using `LEA [RSP+offset]`; it does not expose raw stack-frame
layout to source code. This provides the explicit pointer-to-pointer construction needed by raw UEFI
output-buffer calls such as `LocateProtocol` and `GetMemoryMap`.

The `pointer-to-pointer.abas` fixture passes a pointer to a local output slot into
`BootServices.LocateProtocol`, and the UEFI binding smoke verifies its x86-64 lowering.

This is the construction primitive, not yet a complete memory-map allocator. Buffer sizing and
allocation remain the next handoff slice.
