# RFC-0016: Physical Surface Integration

Status: Draft

Physical displays are ordinary `SURFACE` runtime handles owned by the graphics runtime. Applications
obtain the primary display through `GRAPHICS.PrimarySurface()`, bind it with the same
`GRAPHICS.Bind`/`PushSurface`/`PopSurface` API used for image surfaces, and never receive GOP
protocols, framebuffer addresses, stride, or pixel-format details.

`PrimarySurface` returns a `Borrowed` handle. `GRAPHICS.DestroySurface(primary)` is a safe no-op/
false result and cannot destroy runtime-owned display state. Memory-backed surfaces remain
`Returned`/caller-destroyable. A backend may implement the primary object with GOP, a display
driver, a compositor, a GPU swapchain, or a remote display without changing source behavior.

The hosted reference backend currently creates a deterministic 800×600 software primary surface.
The UEFI adapter must replace that allocation with a runtime-owned surface wrapping validated GOP
metadata and mapped framebuffer storage; that mapping remains below the public API.
