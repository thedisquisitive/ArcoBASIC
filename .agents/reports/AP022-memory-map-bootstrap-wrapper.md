# Agent Packet 022 — ArcoBASIC memory-map bootstrap wrapper

Added `arcology-os/stdlib/uefi_bootstrap.abas` with `AcquireMemoryMap(systemTable)`. The wrapper:

1. probes `GetMemoryMap` using explicit `ADDRESS.Local` output pointers;
2. rejects an empty map result;
3. allocates boot-services data through `AllocatePool` with 64 KiB headroom;
4. reacquires the map and returns the firmware map key;
5. returns zero on allocation or final acquisition failure.

The checked-in fixture compiles through multi-function PE32+ lowering and is covered by the UEFI
binding smoke test.

This is not yet a complete handoff loop: descriptor parsing, pool cleanup, and retry after a map-key
invalidation remain required before calling `ExitBootServices` on a production boot path.
