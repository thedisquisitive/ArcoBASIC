# Arco3D Blender Bridge

Real Blender add-on implementing RFC-0051 against the RFC-0050 A3D v2 schema Arco3D (Godot
Edition) now produces (`godot-edition/scripts/A3DFormat.gd`). Imports/exports geometry, rigs, skin
weights, and the current pose through `File > Import/Export > Arcology 3D (.a3d)`.

## Install

1. Zip this whole directory (`arco3d_blender_bridge/`) or copy it directly into Blender's own
   addons folder (e.g. `~/.config/blender/<version>/scripts/addons/`).
2. In Blender: Edit > Preferences > Add-ons > Install... (if using a zip), then enable
   "Arco3D Bridge (.a3d)" in the add-on list.
3. `File > Import > Arcology 3D (.a3d)` / `File > Export > Arcology 3D (.a3d)` now appear.

Tested against Blender 4.3.2. `bl_info["blender"]` declares a 4.2+ minimum (the classic add-on
model this uses -- not the newer Extensions Platform manifest.toml format -- keeps working there;
see this repo's own `__init__.py` header comment for why that choice was made deliberately).

## Architecture (RFC-0051 Section 22)

```
a3d_parser.py   -- standalone, bpy-free parser/validator + the Y-up<->Z-up axis conversion math.
                   Loadable and testable with a plain `python3` interpreter.
a3d_import.py   -- Blender-side translation: builds real Mesh/Object/Armature/VertexGroup data
                   from a parsed manifest.
a3d_export.py   -- Blender-side translation the other way: Blender objects -> A3D v2 (the
                   "portable subset", not a claim of lossless round-trip -- see its own header).
__init__.py     -- bl_info + the two File-menu operators. The only file that touches Blender's UI
                   registration API.
```

`a3d_parser.py` has zero Blender dependency on purpose (RFC-0051 Section 22's own recommended
"A3D Parser / Model" -> "Bridge-neutral translation layer" -> "Blender version adapter" split) --
run its own axis-conversion math through a plain interpreter any time without needing Blender
installed at all:

```sh
python3 -c "import a3d_parser as p; print(p.position_to_blender((1,2,3)))"
```

## Coordinate conversion

A3D is right-handed, Y-up (same as Godot and glTF). Blender is right-handed, Z-up. The conversion
is the exact same one Blender's own built-in glTF importer performs (a real, precedented mapping,
not a novel derivation) -- rotate +90 degrees about the X axis:

```
blender.x =  a3d.x
blender.y = -a3d.z
blender.z =  a3d.y
```

Orientations (object transforms, bone pose rotations) are converted via quaternion conjugation by
the equivalent change-of-basis quaternion, NOT by permuting Euler angle components directly --
Euler components don't commute with a change of basis the way a plain vector swap does, which is
exactly the kind of "looks reasonable, is actually wrong" bug this whole project has repeatedly
caught by testing with real numbers rather than trusting a derivation. Both the position and
quaternion conversions (and their exact inverses, used by the exporter) are verified directly with
concrete numbers in this add-on's own test -- see `test_bridge.py` and project memory for the exact
values checked.

## Posing bones correctly (a real, non-obvious Blender API detail)

A bone's REST orientation in Blender is generally NOT axis-aligned with armature space (its local Y
axis points head-to-tail, with a "roll" for the other two axes) -- so setting
`pose_bone.matrix_basis` directly with a world-space rotation would apply it in the WRONG frame.
The correct approach, verified against a real Armature-modifier mesh deformation (not assumed):

```python
pose_bone.matrix = desired_world_space_transform @ pose_bone.bone.matrix_local
```

`pose_bone.matrix` is an ARMATURE-SPACE target; Blender's real deformation formula is
`pose_matrix @ rest_matrix.inverted() @ vertex`, so composing the desired transform with the rest
matrix before assigning makes that formula reduce to exactly `desired_world_space_transform @
vertex` -- matching this whole format's own forward-kinematics semantics (a pure rotation around
the bone's own head) exactly. See `a3d_import.py`'s `_apply_pose` for the real code and its own
comment for the full derivation.

## Testing

```sh
blender --background --python test_bridge.py
```

Real, not synthetic-only: imports the GOLDEN fixtures Arco3D (Godot Edition)'s own headless test
suite produces (`/tmp/arco3d_godot_test_v2*.a3d`, written by `godot-edition/tests/test_headless.gd`'s
own "A3D v2 schema" section) -- genuine cross-application interop proof, not a fixture invented just
for this test. Run the Godot suite first if those files don't exist yet:

```sh
cd ../godot-edition && ./scripts/run_tests.sh
```

Covers: rig/bone-hierarchy import, rigid skin-weight assignment, a real FK pose check via the
ACTUAL evaluated (depsgraph-deformed) mesh -- not just inspecting pose_bone rotation values --
confirming a posed bone's own descendants move and unrelated bones' vertices don't; UV import for a
never-modified primitive; a full Blender export -> reimport round trip; and structural
re-validation of an exported file through the bpy-free parser directly.

Also manually confirmed bidirectional interop the other direction: a file this add-on exports was
fed back into Arco3D (Godot Edition)'s own `A3DFormat.import_asset` and imported with the exact
same bone hierarchy, bind positions (correctly converted back to Y-up), and vertex weights intact.

## Known, real, honestly-scoped gaps (not silently missing)

- **Materials**: not implemented. Arco3D itself has no material-authoring UI yet (see
  `A3DFormat.gd`'s own "Known gaps" note) -- there is nothing real to translate either direction.
- **Pose catalogs**: only the ONE live pose Arco3D exports is imported/applied; this is not yet a
  browsable multi-pose catalog or Blender pose-asset library entry (RFC-0051 Section 11's own
  "primary Bridge feature" -- a real, explicit gap against the RFC's fuller vision, deferred
  because Arco3D has no multi-pose catalog to export yet either).
- **Multi-bone smooth skin weights**: this format only ever stores rigid (single-bone) weights.
  Exporting a Blender mesh with soft multi-group blending keeps only the single highest-weight
  group per vertex and reports how many vertices lost information this way -- a real, reported
  approximation (RFC-0051 Section 21), not a silent one.
- **Component hierarchy/attachment points, decals, layered materials, morphs, stylization
  metadata**: none of this exists in Arco3D yet, so there's nothing to bridge.
- **Custom split normals**: attempted via `mesh.normals_split_custom_set_from_vertices`, wrapped in
  a try/except that falls back to plain smooth shading if a future Blender version's API has moved
  on (RFC-0051 Section 3.3's own explicit warning that Blender APIs evolve).
