# UEFI Graphics and UI Foundation

The graphics foundation is a target-independent software renderer layered over a typed surface.
It keeps applications away from GOP framebuffer address arithmetic:

```text
GOP framebuffer -> Surface -> backbuffer -> primitives/text/UI -> Present -> framebuffer
```

The hosted implementation lives in `arcology-os/include/arco/graphics.hpp` and
`arcology-os/src/graphics/graphics.cpp`. It is deliberately usable with an ordinary heap-backed
surface, so clipping and layout can be tested without booting firmware.

## Surface and pixel formats

`Surface` carries `Width`, `Height`, `PixelsPerScanLine`, `PixelFormatValue`, and `Pixels`. The
`FromFramebuffer` constructor validates a non-null pointer, nonzero dimensions, a stride at least as
wide as the visible surface, and one of the two supported GOP 8-bit-per-color formats:

- `RedGreenBlueReserved8` (GOP `PixelRedGreenBlueReserved8BitPerColor`)
- `BlueGreenRedReserved8` (GOP `PixelBlueGreenRedReserved8BitPerColor`)

`CreateSurface` allocates a zeroed four-byte-per-pixel backbuffer and detects dimension/size
failures. Unsupported GOP bitmask formats fail explicitly; they are not guessed or silently packed.

## Drawing contract

`PutPixel`, `DrawLine`, `DrawRect`, `FillRect`, `DrawCircle`, and `FillCircle` all route through
surface bounds. Negative coordinates and partially offscreen rectangles are safe. `DrawText` uses
an embedded deterministic 5x7 bitmap glyph set with printable-ASCII fallback, newline handling,
metrics, and clipping. `Blit` clips source and destination rectangles and rejects incompatible
pixel formats. `Present` requires matching dimensions and formats before copying a backbuffer.

`Panel`, `Label`, `Button`, and `ProgressBar` are passive data objects. Their `Render` functions
compose the same primitives; they do not know about GOP or physical addresses. `Centered` and
`Align` provide parent-relative layout that scales with the active resolution.

## Surface binding

The Graphics Surface Binding RFC is implemented at the C++ renderer boundary by `Bind`,
`PushSurface`, `PopSurface`, `Unbind`, and `CurrentSurface`. The binding stack is thread-local and
does not transfer ownership. Context-bound overloads such as `FillRect(x, y, w, h, color)` and
`DrawText(x, y, text, color)` route to the active surface; no active or invalid surface means no
write. Nested rendering can therefore push an image/window surface and reliably restore its
caller. Each surface carries a lifetime token, so a binding whose surface has been destroyed is
discarded before use.

`arcology-os/examples/graphics_ui_reference.cpp` contains the intended static Arcology screen
composition: full-screen background, centered panel, title, menu rows, progress bar, and status.

## Current boundary

The current ArcoBASIC frontend has fixed-width values and compiler-recognized memory operations,
but does not yet expose user-defined records, heap-backed arrays, or a native object/handle ABI.
Consequently this packet's reusable renderer is currently a C++ systems library and reference
composition, not a claim that `GRAPHICS.Surface` can already be declared as an ArcoBASIC value.
The next binding work should expose an opaque surface handle plus constructors and methods while
keeping the renderer implementation unchanged. The UEFI adapter must construct a volatile surface
from `UEFI.GOP` metadata and `MEMORY.MapDevice`; normal application code should only receive the
surface/backbuffer handle and call renderer operations.

## Safety limitations

This is a software renderer. It does not implement alpha blending, bitmask GOP formats, input,
font files, filesystem assets, or a compositor. Real hardware validation still requires a UEFI
adapter that checks `LocateProtocol` status, null `Mode`/`Info`, framebuffer size/stride overflow,
and mapping attributes before invoking `FromFramebuffer`/`Present`.
