# ARCADE Testing

Packet Section 36 requires ARCADE to be testable below the GUI level, across six categories. This
document adopts them and notes what exists to build on today.

## 36.1 Model Tests

Stable IDs, serialization, property updates, state inheritance, binding graph, layout
relationships — all against the Application Model once it exists (`application-model.md`). None of
this exists yet; there is no Application Model to test.

## 36.2 Round-Trip Tests

```text
Source → Model → Source
Surface edit → Model → Source → Model
Flow edit → Model → Source/Flow reload
```

No-op round trips must be deterministic (byte-identical output for unmodified input) — see
`source-roundtrip.md`. This is directly testable the moment Level 1 constructs exist (Phase C/D),
well before Surface/Flow editing UI does: a test can construct an Application Model programmatically
and assert its regenerated Source matches a fixture, with no GUI involved at all.

## 36.3 Layout Tests

Test layout results numerically and structurally, not only via screenshots. Depends on whatever
layout primitives Surface ends up using (see `surface.md`'s open question about `PresentationNode`'s
current `x`/`y`/`width`/`height`-only shape).

## 36.4 Renderer Parity Tests

Verify Surface preview and runtime use the same rendering/layout path. Because ARCADE's Surface is
architected to drive the real `arcoui::Runtime` directly (not a parallel approximation — see
`surface.md`), this category should largely fall out of the architecture rather than need bespoke
comparison infrastructure; still worth an explicit test once Surface exists; e.g. rendering the
same Application Model both through ARCADE's Surface view and through a plain compiled/run ArcoUI
program and comparing the resulting `PresentationNode` trees.

## 36.5 Runtime Protocol Tests

Attach, identify, inspect, modify-safe-property, reject-unsafe-modification — against whatever
Phase H's runtime inspection channel turns out to be (currently nonexistent; see
`runtime-inspection.md`).

## 36.6 UI Smoke Tests

Basic editor startup, project loading, view switching, build/run, and selection mapping. This
project already has an established pattern for exactly this kind of automation (a dependency-free
CDP-style browser-driving toolkit for one project, `xdotool`-driven X11 automation used repeatedly
this session for Arconaut) — reuse the `xdotool`-driven approach for ARCADE's own Linux desktop
smoke tests once there is a shell to smoke-test (Phase B), rather than inventing a third automation
approach.

## Existing project test conventions to follow

This repository's C++ unit tests use a plain-assert style (e.g. `tests/unit/arcoui_core_tests.cpp`,
which already tests `arcoui::Runtime` directly — Model Tests for ARCADE's own Application Model
should follow the same convention once it exists, and may sit alongside or extend this file rather
than invent a new test harness). Integration/smoke tests are plain shell scripts registered via
`ctest` (see `cmake/Testing.cmake`'s `arco_add_script_test` pattern). ARCADE's own tests should
follow both conventions rather than introduce a third test framework into the repository.

## Status

No ARCADE tests exist yet — there is no ARCADE code to test. This document records the categories
and conventions to follow once Phase C produces the first testable Application Model code.
