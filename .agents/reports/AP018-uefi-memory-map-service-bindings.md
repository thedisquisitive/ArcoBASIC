# Agent Packet 018 — UEFI memory-map service bindings

Added verified `EFI_BOOT_SERVICES` entries for `GetMemoryMap` (`0x38`), `AllocatePool` (`0x40`),
and `FreePool` (`0x48`). ArcoBASIC can type-check and lower the raw service calls, including the
five-argument stack-marshalling path used by `GetMemoryMap`.

Validation includes binding smoke fixtures and the existing compiler/runtime test suite.

This is deliberately not a complete handoff: pointer-to-pointer output buffers, descriptor sizing,
retry on `EFI_BUFFER_TOO_SMALL`, and valid map-key propagation still require a typed ArcoBASIC
bootstrap wrapper before `ExitBootServices` is safe on real hardware.
