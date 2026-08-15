# Graphics substrate in ArcoBASIC

`arcology-os/stdlib/graphics_substrate.abas` is the beginning of the freestanding graphics
implementation. Its framebuffer loops are written in ArcoBASIC and use only the systems primitives
already defined by the language: typed addresses, `ADDRESS.Offset`, volatile `MEMORY.Write32`,
existing control flow, and `CPU.MemoryBarrier`.

C++ remains responsible for the compiler, PE/UEFI image writer, handle ABI plumbing, and primitive
instruction lowering. It must not become the implementation of substrate policy or drawing
algorithms. The next step is to compile these library functions as part of a freestanding module
and expose their results through opaque `SURFACE` handles.

The current functions require validated mapped framebuffer metadata as arguments. They are not yet
the final public API: clipping, pixel-format conversion, resource records, and primary-surface
construction will be moved behind the ArcoBASIC surface library as the freestanding call ABI grows.
The lowering now supports incoming stack-passed parameters, so `FillMappedSurface` accepts an
explicit fifth `color AS U32` and homes it from the Microsoft x64 shadow-space/stack area.
Declared ArcoBASIC helpers can now be called directly from a UEFI entry function; the companion
`graphics-substrate-call.abas` fixture proves multi-function PE32+ emission.

`arcology-os/stdlib/graphics_primitives.abas` adds reusable `PutPixel` and `FillRectClipped`
implementations in ArcoBASIC. Clipping, address calculation, and every pixel write remain source
code; only the typed MMIO primitive is compiler-provided.

`arcology-os/stdlib/graphics_color.abas` now provides canonical RGB and BGR channel packing in
ArcoBASIC, keeping pixel-format policy outside the compiler and renderer implementation.

Freestanding GOP consumers guard both the discovered protocol and current mode before accessing
framebuffer metadata. Failure follows the deterministic halt path until a status-aware firmware
error/reporting layer is added.
