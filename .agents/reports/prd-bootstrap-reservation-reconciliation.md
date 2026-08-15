# PRD Bootstrap Reservation Reconciliation

## Status

Prepared as the next implementation gate for the Physical Hardware Readiness
packet.

## Current evidence

The baseline audit found that the APS CR3 fixture obtains page-table and
metadata buffers from Boot Services, but those pages are not yet represented as
explicit PRD reservations before allocator use. The framebuffer aperture is
also still represented only by fixture-local page-table entries.

## Implemented slice

`PRDReserveRange` now performs a checked reservation transaction against a free
record. It can split a larger free descriptor into prefix, reserved, and suffix
records, preserving the original provider/reason/flags on the remaining free
pieces and recording the bootstrap owner/purpose on the reserved piece. It
refuses malformed ranges, non-free backing, and capacity exhaustion before
committing.

## Remaining next change

The final memory-map handoff must retain a normalized list of consumed ranges
and call an owner/purpose-aware PRD reservation operation for the image, stack,
PRD/VRD storage, page tables, descriptor tables, diagnostics, retained firmware
tables, and GOP aperture. Allocation must be attempted only after these
reservations have committed.

No implementation in this report treats the reconciliation as complete.
