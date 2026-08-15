# Agent Packet 014 — freestanding internal calls

## Implemented

Fission now emits declared ArcoBASIC helper functions into the same PE32+ `.text` image as the
selected UEFI entry function. `CallValue` targets are validated against declared functions,
marshalled using Microsoft x64 register/stack argument locations, emitted as near `CALL rel32`, and
patched after all function bases are known. Declaration-only synthetic wrappers are omitted.

The generated reveal reports `INTERNAL_CALLS` and each resolved target.

## Proof

`arcology-os/tests/fixtures/graphics-substrate/graphics-substrate-call.abas` calls the five-
argument ArcoBASIC `FillMappedSurface` helper from `Main`. It produces a PE32+ image and passes the
substrate smoke test. The image was also booted under QEMU/OVMF and ran until its intentional
`CPU.HaltForever` endpoint (outer timeout `124`).

## Boundary

This is direct internal ArcoBASIC code generation; no C++ rendering policy or runtime helper is
introduced. Recursive calls are structurally supported by rel32 emission, subject to the existing
stack-frame and ABI limits.
