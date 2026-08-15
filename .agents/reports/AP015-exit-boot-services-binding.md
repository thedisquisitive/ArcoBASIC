# Agent Packet 015 — UEFI lifecycle binding slice

## Implemented

Added `UEFI.BootServices.ExitBootServices(imageHandle, mapKey)` to the shared UEFI binding registry
at the verified `EFI_BOOT_SERVICES.ExitBootServices` offset `0xE8`. It is available to ArcoBASIC
source through the normal typed service-table call path and lowers as an ordinary external ABI call.

## Validation

- Added `exit-boot-services.abas` binding fixture.
- UEFI binding smoke accepts the fixture.
- Unit tests verify the `0xE8` table offset.
- Existing runtime/compiler tests remain green.

## Explicit boundary

This does not falsely claim a complete handoff. A real call requires a valid `GetMemoryMap` key;
that memory-map acquisition and retry-on-`EFI_INVALID_PARAMETER` protocol is the next lifecycle
slice. The fixture intentionally uses key `0` and is not booted as a handoff test.
