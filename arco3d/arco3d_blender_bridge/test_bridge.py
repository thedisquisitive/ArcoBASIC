"""Real, headless functional test for the Arco3D Blender Bridge -- run with:

    blender --background --python test_bridge.py

Imports the REAL golden A3D fixtures Arco3D (Godot Edition)'s own headless test suite produces
(tests/test_headless.gd's "A3D v2 schema" section, /tmp/arco3d_godot_test_v2*.a3d) -- genuine
cross-application interop proof, not a synthetic fixture invented just for this test. Also
exercises the exporter and feeds its own output back through the importer (a real Blender-to-
Blender round trip) plus a structural re-validation via the bpy-free parser module directly.

Exits 0 with "ALL CHECKS PASSED" printed, 1 with "FAIL: ..." lines describing every failure.
"""

import math
import os
import sys

import bpy
import mathutils

# Import as a real package (arco3d_blender_bridge.*), not loose top-level modules -- so this test
# exercises the SAME relative-import code path (`from . import a3d_parser`) the real installed
# Blender add-on uses, not a different one that happens to work only under this test's own setup.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import arco3d_blender_bridge.a3d_parser as a3d_parser  # noqa: E402
import arco3d_blender_bridge.a3d_import as a3d_import  # noqa: E402
import arco3d_blender_bridge.a3d_export as a3d_export  # noqa: E402

failures = []


def check_true(condition, message):
    if not condition:
        failures.append(message)


def check_close(actual, expected, message, tol=1e-4):
    if abs(actual - expected) > tol:
        failures.append("%s (expected %s, got %s)" % (message, expected, actual))


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)


# --- Import the rigged/posed golden fixture from Arco3D (Godot Edition)'s own test suite --------
RIGGED_FIXTURE = "/tmp/arco3d_godot_test_v2.a3d"
UV_FIXTURE = "/tmp/arco3d_godot_test_v2_uv.a3d"

if not os.path.isfile(RIGGED_FIXTURE):
    print("FAIL: golden fixture %s does not exist -- run the Godot test suite first (scripts/run_tests.sh)" % RIGGED_FIXTURE)
    sys.exit(1)

reset_scene()
report = a3d_import.import_a3d(RIGGED_FIXTURE, bpy.context)
print("Import report:", report.summary())

mesh_objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']
armature_objects = [obj for obj in bpy.data.objects if obj.type == 'ARMATURE']
check_true(len(mesh_objects) == 1, "expected exactly 1 imported mesh object, got %d" % len(mesh_objects))
check_true(len(armature_objects) == 1, "expected exactly 1 imported armature, got %d" % len(armature_objects))

if armature_objects:
    arm_obj = armature_objects[0]
    bones = arm_obj.data.bones
    check_true(len(bones) == 17, "expected the full 17-bone humanoid preset, got %d" % len(bones))
    check_true(bones.get("Hips") is not None, "expected a bone named 'Hips'")
    check_true(bones.get("LeftShoulder") is not None, "expected a bone named 'LeftShoulder'")
    if bones.get("Hips") is not None:
        check_true(bones["Hips"].parent is None, "Hips should be the root bone (no parent)")
    if bones.get("LeftElbow") is not None and bones.get("LeftShoulder") is not None:
        check_true(bones["LeftElbow"].parent.name == "LeftShoulder", "LeftElbow's parent should be LeftShoulder")

if mesh_objects:
    mesh_obj = mesh_objects[0]
    check_true(len(mesh_obj.vertex_groups) == 17, "expected one vertex group per bone (17), got %d" % len(mesh_obj.vertex_groups))
    total_weighted = sum(1 for v in mesh_obj.data.vertices if len(v.groups) > 0)
    check_true(total_weighted == len(mesh_obj.data.vertices), "every vertex should have a real (rigid) weight assignment, got %d of %d" % (total_weighted, len(mesh_obj.data.vertices)))

    # Real FK proof, mirroring the exact technique verified against a real Armature-modifier
    # deform before this file was written (see project memory): read back the ACTUAL deformed mesh
    # via the evaluated depsgraph, not just inspect pose_bone rotation values.
    depsgraph = bpy.context.evaluated_depsgraph_get()
    eval_obj = mesh_obj.evaluated_get(depsgraph)
    eval_mesh = eval_obj.to_mesh()
    rest_positions = [tuple(v.co) for v in mesh_obj.data.vertices]
    posed_positions = [tuple(eval_mesh.vertices[i].co) for i in range(len(eval_mesh.vertices))]
    eval_obj.to_mesh_clear()

    left_shoulder_group = mesh_obj.vertex_groups.get("LeftShoulder")
    moved_count = 0
    unrelated_moved = False
    left_shoulder_related_names = set()
    if armature_objects:
        # Walk the bone hierarchy to find every descendant of LeftShoulder (including itself),
        # the same "related vs. unrelated" classification the Godot test itself uses.
        def collect_descendants(bone, out):
            out.add(bone.name)
            for child in bone.children:
                collect_descendants(child, out)
        ls_bone = armature_objects[0].data.bones.get("LeftShoulder")
        if ls_bone is not None:
            collect_descendants(ls_bone, left_shoulder_related_names)

    for i, vertex in enumerate(mesh_obj.data.vertices):
        group_names = {mesh_obj.vertex_groups[g.group].name for g in vertex.groups}
        moved = any(abs(rest_positions[i][c] - posed_positions[i][c]) > 1e-5 for c in range(3))
        is_related = bool(group_names & left_shoulder_related_names)
        if is_related and moved:
            moved_count += 1
        elif (not is_related) and moved:
            unrelated_moved = True
    check_true(moved_count > 0, "at least one vertex weighted to LeftShoulder or a descendant should have visibly moved once the pose was applied")
    check_true(not unrelated_moved, "no vertex weighted to an unrelated bone should have moved")
    print("FK check: %d related vertices moved, unrelated moved = %s" % (moved_count, unrelated_moved))


# --- Import the never-recomputed primitive fixture and confirm real UVs survive -----------------
if os.path.isfile(UV_FIXTURE):
    reset_scene()
    uv_report = a3d_import.import_a3d(UV_FIXTURE, bpy.context)
    uv_mesh_objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']
    check_true(len(uv_mesh_objects) == 1, "UV fixture should import exactly 1 mesh object")
    if uv_mesh_objects:
        check_true(len(uv_mesh_objects[0].data.uv_layers) == 1, "the never-recomputed primitive's real UVs should have imported as a UV layer")
    print("UV fixture import report:", uv_report.summary())
else:
    print("(skipping UV fixture check -- %s not present)" % UV_FIXTURE)


# --- Export round-trip: build a small real rig in Blender, export, reimport, verify -------------
reset_scene()
arm_data = bpy.data.armatures.new("ExportTestArmature")
arm_obj = bpy.data.objects.new("ExportTestArmature", arm_data)
bpy.context.scene.collection.objects.link(arm_obj)
bpy.context.view_layer.objects.active = arm_obj
bpy.ops.object.mode_set(mode='EDIT')
eb = arm_data.edit_bones
root_bone = eb.new("Root")
root_bone.head = (0, 0, 0)
root_bone.tail = (0, 0, 0.3)
child_bone = eb.new("Child")
child_bone.head = (0, 0, 1.0)
child_bone.tail = (0, 0, 1.3)
child_bone.parent = root_bone
bpy.ops.object.mode_set(mode='OBJECT')

mesh = bpy.data.meshes.new("ExportTestMesh")
mesh.from_pydata(
    [(-0.5, -0.5, 0.0), (0.5, -0.5, 0.0), (0.5, 0.5, 0.0), (-0.5, 0.5, 0.0),
     (-0.5, -0.5, 2.0), (0.5, -0.5, 2.0), (0.5, 0.5, 2.0), (-0.5, 0.5, 2.0)],
    [],
    [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7)],
)
mesh.update()
mesh_obj = bpy.data.objects.new("ExportTestMesh", mesh)
bpy.context.scene.collection.objects.link(mesh_obj)
vg_root = mesh_obj.vertex_groups.new(name="Root")
vg_root.add([0, 1, 2, 3], 1.0, 'REPLACE')
vg_child = mesh_obj.vertex_groups.new(name="Child")
vg_child.add([4, 5, 6, 7], 1.0, 'REPLACE')
mesh_obj.parent = arm_obj
modifier = mesh_obj.modifiers.new(name="Armature", type='ARMATURE')
modifier.object = arm_obj

export_path = "/tmp/arco3d_blender_test_export.a3d"
export_report = a3d_export.ExportReport()
a3d_export.export_a3d(export_path, [mesh_obj], "BlenderExportTest", export_report)
print("Export report:", export_report.summary())
check_true(os.path.isfile(export_path), "export should produce a real file on disk")

# Re-validate structurally through the bpy-free parser (proves the file is genuinely well-formed
# A3D, not just something this same session's live bpy objects happen to produce).
manifest = a3d_parser.load_a3d(export_path)
check_true(manifest.get("SchemaVersion") == a3d_parser.SCHEMA_VERSION, "exported file should declare the current SCHEMA_VERSION")
components = list(a3d_parser.iter_components(manifest))
check_true(len(components) == 1, "expected exactly 1 exported component")
if components:
    check_true(len(components[0].get("Mesh", {}).get("Vertices", [])) == 8, "exported mesh should have all 8 vertices")
    check_true("Rig" in components[0], "exported component should carry a Rig block")
    if "Rig" in components[0]:
        rig = components[0]["Rig"]
        check_true(len(rig.get("Bones", [])) == 2, "exported rig should have 2 bones")
        check_true(rig.get("BoneIdPerVertex") is not None, "exported rig should carry skin weights (every vertex was weighted)")

# Full Blender-to-Blender round trip: reimport what was just exported.
reset_scene()
reimport_report = a3d_import.import_a3d(export_path, bpy.context)
print("Reimport report:", reimport_report.summary())
reimported_meshes = [obj for obj in bpy.data.objects if obj.type == 'MESH']
reimported_armatures = [obj for obj in bpy.data.objects if obj.type == 'ARMATURE']
check_true(len(reimported_meshes) == 1, "round-tripped file should reimport exactly 1 mesh")
check_true(len(reimported_armatures) == 1, "round-tripped file should reimport exactly 1 armature")
if reimported_armatures:
    check_true(len(reimported_armatures[0].data.bones) == 2, "round-tripped armature should have 2 bones")


if failures:
    for failure in failures:
        print("FAIL: " + failure)
    sys.exit(1)
else:
    print("ALL CHECKS PASSED")
    sys.exit(0)
