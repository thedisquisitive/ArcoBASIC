# RFC-0014: Graphics Surface Binding

Status: Draft

## Abstract

Applications render through abstract `Surface` objects. A graphics backend owns the relationship
between surfaces and physical displays, GOP framebuffers, windows, textures, remote sessions, or
future compositors. Application code must not obtain framebuffer addresses or assume presentation
strategy.

## Binding contract

The graphics context has at most one active surface. `Bind(surface)` replaces the active binding;
`PushSurface(surface)` nests a binding and `PopSurface()` restores its caller. Binding never
transfers ownership. The implementation maintains this stack per execution context. An invalid or
destroyed surface is rejected and cannot receive writes.

All drawing operations use the active surface and apply its local `(0,0)` coordinate system and
clip region. Context-bound operations with no valid binding are no-ops/fail without touching
hardware. Explicit surface overloads remain available to the renderer implementation and hosted
tests, but application-facing code should use the bound context.

## Surface classes

The contract permits physical, window, image, texture, overlay, remote, and terminal surfaces.
Implementations may add classes without exposing their representation. The initial implementation
supports heap-backed image surfaces and a validated volatile UEFI framebuffer surface; the UEFI
adapter remains freestanding-only.

## Presentation and safety

Presentation is backend-owned. It may copy, composite, queue, or redraw damaged regions. The
application must not depend on immediate display updates, buffering count, monitor topology, or
GPU availability. The current software renderer requires matching dimensions and pixel formats
for `Present` and rejects unsupported GOP formats, null targets, invalid strides, and incompatible
blits.

## Language boundary

The current ArcoBASIC frontend does not yet have user-defined records or opaque heap handles. The
binding is therefore implemented at the systems renderer boundary first (`arco::graphics`). A
future ArcoBASIC binding shall expose opaque surface handles and context-bound methods without
exposing `Pixels`, physical addresses, GOP stride, or backend selection.
