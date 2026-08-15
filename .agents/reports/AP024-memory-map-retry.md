# Agent Packet 024 — bounded memory-map retry

`AcquireMemoryMap` now retries failed final `GetMemoryMap` calls up to three times. Each retry
allocates additional boot-services data headroom, reacquires the map, and increments a bounded
attempt counter. `ExitBootServicesSafe` refuses to proceed if all attempts fail.

The wrapper and handoff fixture compile through multi-function PE32+ lowering, and the UEFI binding
smoke passes.

This remains bootstrap code: failed retry allocations are intentionally left for the forthcoming
pool-lifetime manager, and the map descriptor buffer is not yet parsed after handoff.
