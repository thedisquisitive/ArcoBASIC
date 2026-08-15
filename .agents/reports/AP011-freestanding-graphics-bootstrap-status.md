# Freestanding Graphics Bootstrap — Status

## Started

- `GRAPHICS.PrimarySurface()` is recognized by the Fission frontend and lowers to the existing
  freestanding GOP discovery sequence.
- The opaque `SURFACE` value is represented internally by the discovered protocol pointer for this
  bootstrap slice; application source does not see that representation.
- `GRAPHICS.Bind(surface)` lowers to a reserved per-function binding slot.
- A minimal UEFI fixture builds successfully to PE32+ and proves the generated path contains GOP
  discovery plus surface binding.

## Not yet complete

`GRAPHICS.Clear`, `FillRect`, `DrawText`, and runtime resource registration are still hosted
runtime calls in the freestanding path. The next implementation step is to lower these operations
to freestanding software-rendering loops over the bound GOP surface, then boot the fixture under
QEMU/OVMF and on physical UEFI hardware. No hardware-complete claim is made yet.
