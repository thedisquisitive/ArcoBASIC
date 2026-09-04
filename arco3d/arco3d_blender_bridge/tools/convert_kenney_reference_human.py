"""One-time conversion script: imports a CC0 Kenney "Blocky Characters" model (real download, see
CREDITS below) into Blender, joins its 6 separate rigid mesh parts (head/torso/2 arms/2 legs -- no
built-in armature, confirmed directly by inspecting the imported glTF) into ONE mesh object, and
exports it through this repo's own arco3d_blender_bridge exporter to a geometry-only A3D file.

This is a REAL exercise of the Blender Bridge's export path on a genuinely external, non-Arco3D-
authored mesh -- not just the Blender-round-trip test fixture. A second step
(tools/rig_and_export_arco_archer.gd, run inside Godot) imports that geometry-only file, adds the
real Humanoid rig via Arco3D's own API, adds the Bow prop, and produces the final
content/ArcoArcher.a3d.

Run with:
    blender --background --python arco3d_blender_bridge/tools/convert_kenney_reference_human.py

Source: Kenney "Blocky Characters" (https://kenney.nl/assets/blocky-characters), CC0
(https://creativecommons.org/publicdomain/zero/1.0/) -- free for personal/educational/commercial
use, credit appreciated but not required. See content/CREDITS.md for the full attribution.
"""

import os
import sys

import bpy

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, REPO_ROOT)
import arco3d_blender_bridge.a3d_export as a3d_export  # noqa: E402

SOURCE_GLB = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else None
if not SOURCE_GLB:
    raise SystemExit("usage: blender --background --python convert_kenney_reference_human.py -- <path-to-character.glb>")

OUTPUT_PATH = os.path.join(REPO_ROOT, "godot-edition", "content", "ArcoArcher_reference_body.a3d")

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=SOURCE_GLB)

mesh_objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']
if not mesh_objects:
    raise SystemExit("no mesh objects found in the imported GLB")
print("Imported %d mesh parts: %s" % (len(mesh_objects), [o.name for o in mesh_objects]))

# Join all parts into one real mesh object -- Arco3D's own rig system rigs ONE mesh's own vertex
# list, not a multi-object assembly (see A3DFormat.gd's own "no object hierarchy" note). Blender's
# join bakes each part's real WORLD transform into the combined mesh's vertex data, so the parts'
# original relative placement (already correct, professionally modeled) is preserved exactly.
bpy.context.view_layer.objects.active = mesh_objects[0]
for obj in mesh_objects:
    obj.select_set(True)
bpy.ops.object.join()
combined = bpy.context.view_layer.objects.active
combined.name = "ArcoArcherReferenceBody"

# Rescale to roughly match this app's own existing scale convention (spawned primitives ~1 unit,
# GRID_SNAP=0.5) -- the source model is authored in its own unit scale (whole figure ~2.7 Blender-Z
# units tall); this rescales to ~1.75 units tall and re-centers so feet sit at Z=0 (Blender's own
# up axis pre-conversion), matching every other object already in this app's scenes.
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
min_z = min(v.co.z for v in combined.data.vertices)
max_z = max(v.co.z for v in combined.data.vertices)
height = max_z - min_z
target_height = 1.75
scale_factor = target_height / height if height > 0 else 1.0
for v in combined.data.vertices:
    v.co.z -= min_z
    v.co *= scale_factor
combined.data.update()

print("Rescaled: source height %.3f -> %.3f (scale factor %.4f)" % (height, target_height, scale_factor))
print("Combined mesh: %d vertices, %d polygons" % (len(combined.data.vertices), len(combined.data.polygons)))

report = a3d_export.ExportReport()
a3d_export.export_a3d(OUTPUT_PATH, [combined], "ArcoArcherReferenceBody", report)
print("Export report:", report.summary())
print("Exported", OUTPUT_PATH)
