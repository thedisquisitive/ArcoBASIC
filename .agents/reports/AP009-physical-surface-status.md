# Physical Surface Integration — Status

## Delivered

- Runtime-owned, non-destroyable primary `SURFACE` handle.
- `GRAPHICS.PrimarySurface()` public library constructor.
- Primary surface participates in the normal binding stack and drawing APIs.
- `GRAPHICS.DestroySurface(primary)` rejects ownership transfer safely.
- Public ArcoBASIC proof uses `PrimarySurface`, `Bind`, `Clear`, `FillRect`, and `DrawText` only.
- A-MIR marks primary construction `Borrowed`, drawing/bind operations `Borrowed`, and destruction
  `Consumed`; returned image surfaces are marked `Returned`.
- Equality/null behavior and stale/double-destroy validation remain covered.

## Backend boundary

The hosted reference backend supplies an 800×600 software primary. A freestanding UEFI backend still
needs to construct this runtime-owned object from validated GOP discovery and mapped framebuffer
memory. No application fixture references GOP or framebuffer addresses.

## Validation

`systems_runtime_handle_abi_smoke` passes the positive primary-surface fixture, stale-handle negative
fixture, and A-MIR ownership checks. Arcology OS unit, GOP, memory, and port-I/O focused regressions
also pass.
