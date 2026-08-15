# RFC-0015: Runtime Object Handles

Status: Draft

Runtime Object Handles are opaque, identity-bearing values for resources whose representation must
not become part of the ArcoBASIC language ABI. Graphics surfaces, windows, images, fonts, files,
sockets, timers, and future OS resources use the same lifetime model.

## Contract

Applications treat a handle as an indivisible typed value. They may assign it, pass it to library
APIs, compare it for identity, or compare it with that type's null value. They may not access
fields, perform arithmetic, cast it to a pointer, map it, or construct it manually. Creation and
destruction are library operations with documented ownership; copying a handle does not copy its
resource.

Native implementations use a slot plus generation token internally. The published `RuntimeHandle`
type is opaque to source code. `RuntimeHandleTable` validates both type and generation, rejects
stale/destroyed values, and makes repeated destruction fail safely. The representation may change
to indices, capabilities, IDs, or GPU tokens without changing source compatibility.

## Current implementation

`include/arco/runtime_handles.hpp` provides the generation-checked native table. `Value` carries
handles as a distinct storage alternative and equality compares identity. The hosted runtime
recognizes `SURFACE`, `WINDOW`, `IMAGE`, `FONT`, `FILE`, `DIRECTORY`, `TIMER`, `THREAD`, `MUTEX`,
`SOCKET`, and `RANDOM` as opaque types; null is accepted and non-null handles must carry the matching
type. Explicit `RANDOM` handles contain deterministic PCG32 state as specified by RFC-0021.

The graphics Surface Binding implementation is the first consumer at the native renderer boundary.
ArcoBASIC library constructors and an opaque handle ABI for direct `GRAPHICS.CreateSurface` calls
are now wired through the hosted runtime reference implementation:

```basic
LET surface AS SURFACE = GRAPHICS.CreateSurface(32, 24)
GRAPHICS.Bind(surface)
GRAPHICS.Clear(COLOR.Magenta())
GRAPHICS.DestroySurface(surface)
```

`CreateSurface` is `Returned`, binding/stack operations are `Borrowed`, and destruction is
`Consumed`. Surface internals remain hidden; UEFI-native construction/presentation is a separate
backend adapter.
