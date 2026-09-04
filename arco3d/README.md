# Arco3D

Arco3D is a lightweight, laptop-first 3D asset construction, rigging, posing, surface-authoring,
and stylization application. It exists to remove high-friction preparation work from heavyweight
3D suites — Arco3D is not a Blender replacement, it's the fast on-ramp before Blender:

```
Block / Kitbash -> Non-destructive refinement -> Rig and pose -> Surface / paint / decals
    -> Stylize and preview -> Export A3D -> Blender -> Animation / fine tuning / render
```

## Status

**This ArcoBASIC implementation is paused.** It hit a real, unresolved interpreted-per-call
performance ceiling in the interactive tool (see "Making an asset and exporting it" below and
project memory) that survived a genuine 26x compiler-optimization fix, and the user chose to
pivot rather than keep chasing it. A parallel implementation on the Godot engine is now the active
track -- see [`godot-edition/`](godot-edition/README.md). Everything below this point describes the
paused ArcoBASIC track as it stood when work stopped; it's kept for reference (the object model, A3D
format design, and compiler bugs found along the way are all still real and still relevant), not
because it's the current plan.

**RFC-0050/RFC-0051 (A3D format + Blender Bridge) are real and implemented against the active
Godot track**: `godot-edition/scripts/A3DFormat.gd` produces a real, versioned A3D v2 file (real
per-vertex normals/UVs, a non-destructive Construction chunk, and a Rig/Pose chunk with stable
bone IDs and rigid skin weights), and [`arco3d_blender_bridge/`](arco3d_blender_bridge/README.md)
is a real, tested Blender add-on that imports it into a proper Blender Armature + rigged mesh and
can export back. See that directory's own README for the full detail, including the exact
coordinate-conversion math and a non-obvious Blender pose-bone API gotcha found and verified along
the way.

Tech direction (as originally chosen): Arco3D stays an ArcoBASIC application (like ArcoFlow and
the `games/` apps). The 3D viewport has real GPU primitives in `src/gui/glfw_backend.cpp` -- see
"The GPU viewport" below -- and there's a first interactive tool (`arco3d.abas`) built on top of
them; see "Making an asset and exporting it".

**Done:**

- Step 1 of RFC-0049's recommended build order, the canonical scene/asset object model, folded
  together with step 3 (primitives/transforms) since primitives are the object model's first real
  content. See `stdlib/arco3d_scene.abas`: `Vec3`/`Vec2` math, a `Transform`
  (position/Euler-rotation/scale), a triangulated `Mesh`/`Face`/`Component`/`Asset` object model
  with stable non-positional IDs, five primitive builders (block, wedge, cylinder, plane, sphere),
  and move/rotate/scale/duplicate on `Component`. Verified by `tests/scene_smoke.abas` --
  primitive vertex/face counts, transform accuracy, outward face-normal winding, and Clone()
  deep-copy independence.
- Step 4, the non-destructive Construction Stack itself (RFC-0049 Section 7.2). See
  `stdlib/arco3d_construction.abas`: `ConstructionStack` (ordered operations, non-destructive
  `Evaluate()`, explicit `Commit()`, `MoveOperation`/`RemoveOperationAt`), plus two real operations
  proving it end-to-end -- `MirrorOp` (reflect + optional seam weld, winding-corrected) and
  `ArrayOp` (linear repeat). `AttachConstructionStack`/`AddConstructionOp`/
  `RefreshComponentFromStack` wire a stack onto a `Component` (its `Stack` field starts `NULL`
  until attached -- see the file for why that field is untyped). Verified by
  `tests/construction_smoke.abas`, including an order-sensitivity check (Mirror-then-Array
  produces different geometry than Array-then-Mirror) and enable/disable/reorder/duplicate/commit.

- Steps 6-7, rig and pose representation. See `stdlib/arco3d_rig.abas`: a real `Quaternion`
  (slerp-capable, unlike `Component.Transform`'s Euler angles -- a deliberate, separate choice, see
  the file header) plus `Bone`/`Rig` (parent-index hierarchy, bind pose, world-transform
  composition up the chain), `RigPoseState` (the live/current per-bone transforms a future posing
  UI would mutate, with `CapturePose()`/`ApplyPose()`), a sparse `Pose` (only the bones it actually
  specifies) and `PoseCatalog`, and `InterpolatePoses()` (translation/scale lerp + quaternion
  slerp, restricted to bones present in both poses). Verified by `tests/rig_smoke.abas` --
  known-angle rotation/composition/slerp correctness, hierarchy composition through a rotated
  parent, and partial-pose apply/interpolate semantics.

- Step 5, A3D serialization -- a real "minimum subset" (geometry/hierarchy/transform; no rig/pose/
  materials/style/construction-stack yet). See `stdlib/arco3d_io.abas`: `ExportA3D`/`ImportA3D`,
  built on `stdlib/compy.abas`'s `ArcoCompy` -- an existing, already-tested, plain-text generic
  ArcoBASIC value serializer, not a hand-rolled parser -- wrapped in an A3D manifest (format/
  version/capability fields per RFC-0050 Section 23). Coordinate convention frozen for v1:
  right-handed, Y-up, meters, matching `arco3d_scene.abas`'s own internal convention exactly (no
  conversion needed yet -- the eventual Blender Bridge will need one, since Blender is Z-up).
  Verified by `tests/a3d_io_smoke.abas`: single-asset and component-hierarchy round-trip (geometry,
  transform, and that restored values are real, working class instances, not inert data), export-
  captures-a-snapshot (mutating the source after export doesn't affect the file), and clean
  rejection of a missing file, a corrupt container, the wrong format tag, an unsupported required
  capability, and a future container version.
- Rig/pose data on an Asset, as a real (optional-capability) A3D extension: `Asset.Rig` and
  `Asset.PoseCatalogs` (untyped fields in `arco3d_scene.abas`, same reasoning as
  `Component.Stack`), advertised in the manifest's `OptionalCapabilities` only when actually
  present (`rig.core`/`pose.catalog`, matching RFC-0050 Section 29's own example manifest).
  Verified by `tests/a3d_rig_io_smoke.abas`: bone hierarchy/bind-pose/pose-catalog round-trip
  alongside real geometry in the same file, and that restored `Rig`/`Quaternion`/`Pose` instances
  are still genuinely working objects (`WorldBindTransform()`, `RotateVector()`, etc. all still
  compute correctly on the restored data, not just hold inert fields). Finding a real
  compiler bug along the way (see below): the importing script needs `#IMPORT "arco3d_rig"`
  itself for these types to restore as working instances, not just data -- see
  `arco3d_io.abas`'s own `ExportA3D` comment.
- A real GPU 3D viewport, and a first interactive tool built on it. See "The GPU viewport" and
  "Making an asset and exporting it" below -- `GUI.Clear3D`/`GUI.Triangle3D` (depth-tested,
  perspective-projected, real primitives from `arco3d_scene.abas` composited under the existing 2D
  HUD) and `arco3d.abas` (spawn/select/move/export via keyboard, driving the exact same
  `arco3d_io.abas` export path as the code example below).

**Not started:** skin weights / mesh deformation from a posed rig, IK chains, joint constraints,
pose sequences, Extrude/Inset/Bevel/Subdivision (need real edge/half-edge topology the current
`Mesh` doesn't have), the viewport/command system, materials/paint, style preview, and the Blender
Bridge (RFC-0051 -- a separate Python/Blender-addon deliverable; A3D export existing is necessary
for it but not sufficient). See
[`19. AI Implementation Guidance`](rfcs/RFC-0049_Arco3D_Application_and_Authoring_Model.md#19-ai-implementation-guidance)
in RFC-0049 for the full recommended order.

### Running things

Arco3D is self-contained under this directory -- no need to `cd` to the repo root, remember a
top-level `build/` path, or set any environment variable:

```sh
arco3d/scripts/test.sh
```

Run from anywhere (it locates itself; builds everything on first run). Each test runs as a real,
standalone compiled ELF64 executable -- `arco3d/build/scene_smoke`, `arco3d/build/construction_smoke`,
`arco3d/build/rig_smoke` -- with the ArcoBASIC bytecode and a bytecode VM embedded directly in the
binary. **No arcosh, no ArcoFission, no ArcoBASIC toolchain of any kind needs to be installed to
run one of these afterward** -- confirmed by running one with a completely empty environment
(`env -i arco3d/build/scene_smoke`); `ldd` shows only ordinary system libraries (libstdc++, libm,
libgcc_s, libc). Run one directly the same way:

```sh
arco3d/build/scene_smoke
```

Building explicitly (idempotent, safe to rerun after pulling changes to the language toolchain --
also builds this repo's own `arcosh`/`ArcoFission` first if either is missing):

```sh
arco3d/build.sh
```

**Performance: this builds against `build-release/` (`CMAKE_BUILD_TYPE=Release`, `-O3`), not the
repo's plain `build/`.** A compiled capsule embeds the bytecode VM as compiled-in C++ code (see
"This *is* a real standalone compiled capsule" below), so the capsule's own runtime speed is set
entirely by what optimization level `arco_runtime_core`/`arco_compiler_core` were built with when
`ArcoFission` linked it in -- ArcoFission's own build type is irrelevant to this. Measured directly
on one machine, same benchmark script, same machine: 2m15s unoptimized vs. 5.1s at `-O3` -- a ~26x
difference. `arcoflow/build.sh` and `games/build.sh` default to `build-release/` for the same
reason. Set `REPO_BUILD_DIR=/path/to/build` (or `ARCOFISSION=/path/to/it` for the other two) if you
specifically need an unoptimized, more-debuggable capsule for troubleshooting the compiler itself.

This *is* a real standalone compiled capsule, the same way `arcoflow/build.sh` produces
`arcoflow/build/arcoflow` via `ArcoFission native` -- getting here took fixing five real, distinct
bugs in the shared ArcoFission bytecode compiler/VM (unrelated to arco3d's own code, all fixed at
the source, not routed around): see "Five real compiler bugs" below. `arco3d/build.sh` also
rebuilds the lean/no-GUI/no-libcurl runtime capsules link against by default
(`ArcoFissionCapsuleCoreProbe`) -- a real `EXCLUDE_FROM_ALL` CMake target ordinary builds never
touch, which is exactly what made the ExitTheProgram fix below appear not to work at first.

Why CWD matters for a `.abas` *source* file (not a compiled capsule, which needs none of this):
`#IMPORT` in this toolchain resolves relative to the **process's current directory** (not the
importing file's own directory -- confirmed directly against the runtime, see
`import_candidates()` in `src/runtime/runtime.cpp`). Arco3D's own files import each other by bare
module name (`#IMPORT "arco3d_scene"`, not a path), exactly like the top-level repo's own
`#IMPORT "gui"` resolves via `<repo-root>/stdlib/gui.abas` when run from the repo root -- the same
mechanism, one level down. `arco3d/build.sh` handles this `cd` internally when compiling; it only
matters if you run a `.abas` file directly through `arco3d/build/arcosh` (still built as a
convenience for ad-hoc interactive use) yourself.

### The GPU viewport

`GUI.Clear3D(window, eyeX, eyeY, eyeZ, targetX, targetY, targetZ, upX, upY, upZ, fovYDegrees,
near, far, bgR, bgG, bgB)` and `GUI.Triangle3D(window, x1, y1, z1, x2, y2, z2, x3, y3, z3, r, g, b,
[a])` are two new real GPU primitives added to `src/gui/glfw_backend.cpp` this phase (declared in
`include/arco/gui.hpp`, stubbed to throw on the web/canvas and headless/stub backends since neither
supports a real 3D pipeline). They're deliberately minimal and fixed-function (`glFrustum` +
a hand-built look-at matrix + `glBegin(GL_TRIANGLES)`/`glVertex3d`, matching the rest of this
project's existing legacy-OpenGL-2.1 GUI backend, not a shader pipeline) -- exactly enough to draw
lit, depth-tested, camera-projected triangles from an `arco3d_scene.abas` `Mesh`, with the existing
2D Cairo canvas still composited on top as a HUD overlay (`Clear3D` also clears the canvas to fully
transparent; `Present` blends it over the 3D scene with premultiplied-alpha `GL_ONE,
GL_ONE_MINUS_SRC_ALPHA`, correct for Cairo's own ARGB32 format). Real primitives render for many
frames with zero errors in this environment, though pixel-level visual verification wasn't possible
here (see the git history / session notes for why -- no working screenshot tool against this
sandbox's GLFW window); the math itself is the standard, well-known perspective/look-at formulas,
not novel.

### Making an asset and exporting it

The real way to do this now is the interactive tool, built by `build.sh` as a genuine standalone
capsule (`arco3d/build/arco3d`) alongside the test capsules -- no `arcosh` needed to run it:

```sh
arco3d/build/arco3d
```

(needs a real desktop session -- it checks `GUI.Available()` and exits cleanly if there isn't one).
It can also be run interpreted, straight from source, the same way as any `.abas` script:

```sh
cd arco3d
build/arcosh arco3d.abas
```
`B` spawns a block, `N` spawns a sphere, `Tab` cycles selection, arrow keys + Page Up/Down move the
selected object, Delete/Backspace removes it, `A`/`D`/`W`/`S`/`Q`/`E` orbit/pitch/zoom the camera,
and `X` exports the whole scene to `arco3d_builder_output.a3d` in the current directory via the
exact same `ExportA3D` call the script example below uses (a flat list of spawned objects wrapped
under one synthetic root `Component` -- real component hierarchy/kitbashing is future work, see
Status above). This is a genuinely first, minimal increment of RFC-0049's BUILD workspace, not the
full authoring tool the RFC describes -- no gizmos, no undo, no non-destructive Construction Stack
wiring yet. The spawn/move/export workflow itself was validated headlessly (calling the same
underlying `CreateBlock`/`CreateSphere`/`SetPosition`/`Translate`/`ExportA3D` sequence the key
handlers use, then re-importing and checking positions and mesh data survive) since live GUI key
injection isn't available in every environment this runs in; live interactive use is expected to
work identically since it drives the identical code path.

You can also skip the GUI entirely and script an asset directly against `stdlib/arco3d_scene.abas`
and `stdlib/arco3d_io.abas`:

```basic
#IMPORT "arco3d_io"   ' pulls in arco3d_scene transitively -- Asset/Component/CreateBlock/etc.
                       ' come along with it, no separate #IMPORT "arco3d_scene" needed.

asset = Asset("MyFirstAsset")
root = CreateBlock(2, 1, 3)
root.SetPosition(0, 0.5, 0)
asset.Root = root

ExportA3D(asset, "my_first_asset.a3d")
```

Run it with `arco3d/build/arcosh my_script.abas` from inside `arco3d/`. Either way you get a
plain-text `.a3d` file (inspect it in any editor -- it's ArcoCompy's tagged-length format,
documented in `stdlib/compy.abas`'s own header). **This does not open in Blender yet** -- there is
no A3D importer on the Blender side (RFC-0051 is unstarted; it's a separate Python/Blender-addon
deliverable that would need to understand this same file). What exists today is a real, tested,
round-tripping *Arco3D-to-Arco3D* interchange file -- `ImportA3D("my_first_asset.a3d")` reads it
back into a genuine `Asset`/`Component`/`Mesh` object graph, proven in `tests/a3d_io_smoke.abas`.

### Five real compiler bugs found along the way -- fixed upstream, not worked around

All found while building `arco3d_scene.abas`/`arco3d_rig.abas`/`arco3d_construction.abas` and then
trying to get them running as real compiled capsules, all fixed at the source
(`src/frontend/lexer.cpp`, `src/frontend/parser.cpp`, `src/runtime/runtime.cpp`,
`src/compiler/fission.cpp`) rather than patched around in ArcoBASIC code. Whole project test suite
(`ctest`, core + ArcoFission/AMIR/freestanding-substrate coverage, 98/98) still green after every
fix; see git history on those files for the actual change and reasoning.

**1. Calling a method whose receiver is itself a dotted-member access, or the result of another
call, used to fail** -- either as a parse error or as a bogus `unknown method: A.B.C` runtime
error:

```basic
' All of these now work (previously failed):
result = someObject.SomeField.SomeMethod()
result = Vec3(1, 2, 3).Plus(other)
result = someObject.MethodOne().MethodTwo()
```

Two distinct root causes, both fixed:

- `someObject.SomeField.SomeMethod()`-shaped calls (a dotted variable *name*, fused into one
  token by the lexer) were split into receiver/method on the FIRST dot, so `method` ended up as
  the garbled `"SomeField.SomeMethod"` instead of just `"SomeMethod"`. Fixed by splitting on the
  LAST dot for actual dispatch, while a namespace-style call like `UEFI.GOP.Discover(...)` still
  needs the OLD first-dot split for its own unrelated pattern-matching (and for
  `src/compiler/fission.cpp`'s AMIR lowering) -- so `MethodCallExpr` now carries both splits, one
  per consumer, rather than changing what either one means.
- `someObject.MethodOne().MethodTwo()`-shaped calls (chaining off an actual call/index result, not
  a plain name) failed at the LEXER level: `.` had no token rule at all except as a decimal point
  or as an identifier-fusion continuation character, so a bare `.` immediately after `)`/`]` was
  "unexpected character". Fixed by giving `.` a real standalone token and a generic postfix
  `expr.member`/`expr.method(...)` parse rule that wraps whatever expression precedes it.

**2. A typed `FUNCTION ... AS ClassName` return annotation used to reject a `NULL` return
outright**, even though a typed `AS ClassName` *field* already allowed `NULL` freely (the
documented behavior for a typed field with no initializer):

```basic
FUNCTION MaybeFind(...) AS Pose
    ...
    RETURN NULL   ' now fine (previously: "MaybeFind should return Pose")
END FUNCTION
```

`enforce_return_type` was the one return-type/field-type check in the runtime missing the
`!value.is_null()` exemption every other one already had (`ensure_field_assignment_type`,
`make_reference_to`, `make_reference`, `set_reference_value`) -- brought in line with those.
`arco3d_rig.abas`'s `Pose.FindTransform()`/`PoseCatalog.FindPoseByName()` no longer need to leave
their return type unannotated to work around this; left that way for now since it costs nothing
and the annotation added no real information a caller couldn't already infer.

**3. An ordinary method call on a class-typed parameter was misclassified as a freestanding/UEFI
`CALL_EXTERNAL` call** (a raw external-struct-field-chain call, meaningless for an ordinary
ArcoBASIC object) **and crashed with `bytecode VM does not implement opcode yet: CALL_EXTERNAL`**
-- the bug that originally motivated all of this. Two compounding causes:

- `lower_call()`'s CALL_EXTERNAL heuristic treated *any* parameter (`has_parameter(function, name)`)
  as an external-call receiver, with no regard for its actual type -- correct only for the one case
  it was built for (a UEFI-typed handle parameter with no explicit `AS UEFI.X` annotation), wrong
  for the overwhelmingly more common case of an ordinary typed method/function parameter. Fixed,
  across a few rounds chasing real regressions in both directions (a genuinely UEFI-typed *local
  variable*, not just a parameter, needing to stay external; a genuinely *untyped* parameter --
  found via `arco3d.abas`'s own `SpawnAt(newComponent, index)` -- needing to stay ordinary), to the
  final rule: a parameter is only treated as an external receiver when it's **typed** and that type
  is **not a declared ArcoBASIC class** (covers the real UEFI-handle-parameter case), or when a
  variable's declared type is UEFI-prefixed regardless of whether it's a parameter or a local
  (covers the real `LET blockIo AS UEFI.BlockIoProtocol = ...` case) -- a genuinely untyped
  parameter now correctly defaults to ordinary dispatch instead. This also explains why `SELF`
  needed its own special-case exclusion from the start: `SELF` is never given a type annotation, so
  it's exactly this "untyped parameter" shape.
- Once that was fixed, a second, more specific bug surfaced: `lower_class()` never populated
  `types_` for a **method's own parameters** the way `lower_function()` already did for plain
  `FUNCTION`s -- so any method whose body called a method on *its own* typed parameter
  (`FUNCTION ApplyToPoint(localPoint AS Vec3) ... localPoint.ScaledBy(...)`) still hit the same
  CALL_EXTERNAL misclassification purely because the parameter's type was invisible at that point,
  even though it was declared correctly. Fixed by having `lower_class()` seed `types_` from method
  parameters exactly like `lower_function()` already does.

**4. `SHARED` class fields/methods (RFC docs/classes.md) had no persistent storage at all in the
compiled bytecode pipeline** -- `ArcoId.NextId = ArcoId.NextId + 1` (this project's own stable-ID
counter) either crashed outright (`undefined bytecode local: ArcoId`) or silently reset to its
initial value on every call from a different function than the one that last wrote it, because a
plain `STORE`/`LOAD` is scoped to one function's own call frame, and `lower_class()`'s own
`.__new` deliberately gives a `SHARED` field no per-instance storage at all (correctly -- that's
what `SHARED` means). Fixed by backing every `SHARED` field with a real persistent slot in
`Runtime`'s own `globals_` map, through the exact same `Runtime.SetGlobal`/`Runtime.GetGlobal`
primitive `apply_script_global_scoping()` already uses to give ordinary script-scope variables
cross-function visibility -- the same underlying gap (a bytecode VM function has no visibility
into any state outside its own frame by default), just needed for a different, previously-uncovered
case.

Also fixed along the way, once the above unblocked `arco3d_rig.abas` specifically:

- **`ExitTheProgram()`/`Exit()`/`ExitProgram()` were registered only by ArcoSH's own shell
  builtins**, not the core `Runtime`, so a compiled capsule failed with `unknown host function:
  exittheprogram` even though the underlying `ExitSignal` mechanism was already a core, non-shell
  type. Promoted the registration into `Runtime`'s own constructor (available to the interpreter,
  ArcoFission-hosted code, and native capsules alike), and taught `run_bytecode`/
  `run_bytecode_binary`/`compile_run` to catch `ExitSignal` and actually terminate the process with
  the requested code (flushing whatever output had already accumulated first) instead of reporting
  a clean exit as a generic failure.
- **A `CONSTRUCTOR`/method parameter default value that isn't a simple literal** -- e.g.
  `CONSTRUCTOR(position AS Vec3 = Vec3(0, 0, 0))`, used throughout `arco3d_rig.abas`'s
  `BoneTransform` -- crashed with a bare `stod` (libstdc++'s `std::invalid_argument` message for
  `std::stod`) because the bytecode compiler's default-value handling (`parse_constant_value`)
  only ever understood plain literals, and fell through to blindly parsing anything else as a
  number. Fixed by recognizing the `Identifier(literal, literal, ...)` shape a constructor-call
  default actually renders as, resolving it against the already-compiled target function at
  prepare time, and evaluating it fresh (via the VM's own existing recursive function-call
  primitive) every time the default actually fires -- not computed once and shared, so mutating one
  caller's default-constructed instance can never alias another's.

**5. The bytecode VM's own instance-method dispatch had the identical first-dot bug the tree-
walking interpreter's `MethodCallExpr` did (bug 1 above), independently** -- found by
`a3d_rig_io_smoke.abas`'s `restoredRoot.LocalMesh.VertexCount()` failing with `unknown host
function: restoredRoot.LocalMesh.VertexCount` as a *compiled capsule* while working fine
interpreted. The two are separate C++ implementations (`src/frontend/parser.cpp`'s tree-walking
`eval()` vs. `src/compiler/fission.cpp`'s bytecode `CallValue` dispatch), so fixing one never
touched the other. Same root cause, same fix shape: split on every dot instead of just the first,
walking any intermediate segments as field accesses (reusing `Runtime::get_member`, the same
helper bug 1's fix introduced) to reach the real receiver the final segment's method dispatches
against.

## Layout

- `stdlib/arco3d_scene.abas` — the canonical scene/asset object model (see Status above).
- `stdlib/arco3d_construction.abas` — the non-destructive Construction Stack (depends on the file
  above).
- `stdlib/arco3d_rig.abas` — quaternion math, rig/bone hierarchy, and pose/pose-catalog
  representation (depends on `arco3d_scene.abas` only, independent of the construction stack).
- `stdlib/arco3d_io.abas` — A3D export/import (depends on `arco3d_scene.abas` and the shared
  `stdlib/compy.abas`).
- `tests/scene_smoke.abas`, `tests/construction_smoke.abas`, `tests/rig_smoke.abas`,
  `tests/a3d_io_smoke.abas`, `tests/a3d_rig_io_smoke.abas` — self-checking smoke tests.
- `arco3d.abas` — the first interactive asset-creation tool: spawn/select/move/delete primitives in
  a real GPU viewport, export to `.a3d` (see "Making an asset and exporting it" above). Run
  directly with `arco3d/build/arcosh arco3d.abas`; not part of `build.sh`'s per-test capsule set.
- `build.sh` — compiles a real standalone capsule per test into `build/`, plus an `arcosh` symlink
  for ad-hoc interactive use (see "Running things" above).
- `build/` — gitignored; `build.sh`'s output (one native capsule per test, `build/arcosh`).
- `scripts/test.sh` — runs every compiled capsule under `build/`, rebuilding first if needed.
- `rfcs/` — the three RFCs below.

## RFCs

- [RFC-0049: Arco3D Application and Authoring Model](rfcs/RFC-0049_Arco3D_Application_and_Authoring_Model.md)
  — the application itself: block-first modeling, non-destructive construction stack, kitbashing,
  rigging/posing, layer-based paint/material authoring, stylized rendering, and the laptop-only
  (keyboard + touchpad) interaction baseline.
- [RFC-0050: A3D and A3M Asset and Material Interchange](rfcs/RFC-0050_A3D_and_A3M_Asset_and_Material_Interchange.md)
  — the portable file formats: `.a3d` (asset: geometry, rig, poses, materials, style metadata) and
  `.a3m` (reusable material package: channels, layers, masks, generated maps).
- [RFC-0051: Arco3D Blender Bridge](rfcs/RFC-0051_Arco3D_Blender_Bridge.md)
  — the Blender add-on that imports/exports A3D/A3M, with pose catalogs as the primary feature
  (saved Arco3D poses becoming real Blender animation-blocking building blocks).

These follow the [Arcology RFC Standard](../arcology-os/rfcs/RFC-0000_RFC_Process.md) (RFC-0000).
The three documents are mutually cross-referenced (`Related RFCs`); read RFC-0049 first for the
overall shape, then RFC-0050 for the interchange contract, then RFC-0051 for how Blender fits in.

## Why a separate project

Arco3D's canonical asset model is deliberately independent of both ArcoBASIC/Arcology OS internals
and of Blender — the A3D/A3M interchange boundary is the only contract either side depends on. It
lives as its own top-level folder (sibling to `arcology-os/`, `arcoflow/`, `arcology-commons/`) for
the same reason those do: a clear boundary, even though all are first-class parts of Arcology.
