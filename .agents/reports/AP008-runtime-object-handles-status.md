# Runtime Object Handles — Implementation Status

## Delivered

- Distinct `Value` storage for opaque runtime handles.
- Identity equality and non-inspecting string representation.
- Generation-checked slot table with typed validation and safe repeated destruction.
- Runtime-owned handle table access through `Runtime::object_handles()`.
- Hosted type recognition for `SURFACE`, `WINDOW`, `IMAGE`, `FONT`, `FILE`, `DIRECTORY`, `TIMER`,
  `THREAD`, `MUTEX`, and `SOCKET`; null values are accepted for each type.
- Graphics surface binding remains compatible with the handle model and does not expose framebuffer
  addresses to application-facing APIs.
- RFC and implementation documentation.

## Runtime Library ABI milestone

The reference hosted ABI is now wired through ordinary library calls:

- `GRAPHICS.CreateSurface(width, height [, pixelFormat)` returns a typed `SURFACE` handle.
- `GRAPHICS.Bind`, `PushSurface`, `PopSurface`, and `DestroySurface` validate handles through the
  runtime table.
- `COLOR.RGB`, `COLOR.RGBA`, and basic predefined colors provide library-owned color values.
- `GRAPHICS.Clear` renders through the active bound surface.
- A positive ArcoBASIC fixture and a destroyed-handle negative fixture exercise the boundary.

UEFI-native surface construction/presentation and compiler/A-MIR ownership annotations remain
future work; the hosted ABI is intentionally the reference implementation.

## Validation

The project builds successfully after the ABI changes. The dedicated runtime-handle ABI smoke test
passes, including stale/double-destroy rejection. Existing hosted test execution also contains a
pre-existing environment-sensitive color assertion (`forces colors for external grep`); that failure
is unrelated to handle compilation and should be isolated in a follow-up test cleanup.
