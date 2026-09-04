"""Blender-side translation layer for A3D import (RFC-0051 Section 8-11). Depends on bpy/mathutils
-- kept strictly separate from a3d_parser.py (which stays bpy-free), per RFC-0051 Section 22's own
recommended architecture:

    A3D Parser / Model -> Bridge-neutral translation layer -> Blender version adapter -> Blender API

This file is both the "translation layer" and the "version adapter" for now (a real, deliberate
scope call for a first bridge pass -- splitting them further only pays off once a SECOND Blender
version actually needs different handling, which hasn't happened yet).
"""

import bpy
import mathutils

from . import a3d_parser


class ImportReport:
    """RFC-0051 Section 21's own diagnostics requirement -- every import should produce a concise
    result summary, not silently succeed or fail. Collected as plain strings, shown in the
    operator's own report() call and available for inspection/testing."""

    def __init__(self):
        self.translated = []
        self.approximated = []
        self.warnings = []

    def note(self, message):
        self.translated.append(message)

    def warn(self, message):
        self.warnings.append(message)

    def summary(self):
        lines = []
        if self.translated:
            lines.append("Imported: " + "; ".join(self.translated))
        if self.warnings:
            lines.append("Warnings: " + "; ".join(self.warnings))
        return " | ".join(lines) if lines else "Nothing imported"


def _quat_field_to_blender(quat_field):
    """A3D stores quaternions as [x, y, z, w] (see A3DFormat.gd's own header comment on
    QuaternionOrder). Converts to Blender's (w, x, y, z) Quaternion constructor order AFTER
    applying the Y-up -> Z-up axis conversion -- order matters: convert axes first, then reorder
    components, since a3d_parser.quat_to_blender operates on the (x, y, z, w) tuple it was given."""
    x, y, z, w = a3d_parser.quat_to_blender(tuple(quat_field))
    return mathutils.Quaternion((w, x, y, z))


def _build_mesh(component, report):
    mesh_data = component.get("Mesh", {})
    vertices_a3d = mesh_data.get("Vertices", [])
    faces = mesh_data.get("Faces", [])
    if not vertices_a3d:
        return None

    vertices_blender = [a3d_parser.position_to_blender(v) for v in vertices_a3d]
    mesh = bpy.data.meshes.new(component.get("Name", "Component"))
    mesh.from_pydata(vertices_blender, [], faces)
    mesh.update()

    uvs = mesh_data.get("UVs")
    if uvs and len(uvs) == len(vertices_a3d):
        uv_layer = mesh.uv_layers.new(name="UVMap")
        for loop in mesh.loops:
            u, v = uvs[loop.vertex_index]
            uv_layer.data[loop.index].uv = (u, v)
        report.note("UVs")
    else:
        report.warn("no UVs on '%s' (only a never-modified Arco3D primitive keeps them today)" % component.get("Name", "?"))

    # Real per-vertex normals if the file has them (schema v2); otherwise fall back to Blender's
    # own smooth shading, matching visually what Arco3D itself falls back to when it has none.
    normals = mesh_data.get("Normals")
    applied_custom_normals = False
    if normals and len(normals) == len(vertices_a3d):
        try:
            converted = [a3d_parser.position_to_blender(n) for n in normals]
            mesh.normals_split_custom_set_from_vertices(converted)
            applied_custom_normals = True
        except (AttributeError, RuntimeError) as error:
            # Blender's custom-split-normals API has genuinely changed across versions (RFC-0051
            # Section 3.3's own warning that "Blender APIs evolve") -- degrade to plain smooth
            # shading rather than fail the whole import over a cosmetic normals detail.
            report.warn("custom normals API unavailable on this Blender version (%s) -- used smooth shading instead" % error)
    mesh.shade_smooth()
    if applied_custom_normals:
        report.note("normals")

    return mesh


def _import_component(component, collection, report, id_to_object):
    mesh = _build_mesh(component, report)
    if mesh is None:
        return None

    obj = bpy.data.objects.new(component.get("Name", "Component"), mesh)
    collection.objects.link(obj)

    transform = component.get("Transform", {})
    obj.location = a3d_parser.position_to_blender(transform.get("Position", [0, 0, 0]))
    quat_field = transform.get("Quaternion")
    if quat_field:
        obj.rotation_mode = 'QUATERNION'
        obj.rotation_quaternion = _quat_field_to_blender(quat_field)
    scale = transform.get("Scale", [1, 1, 1])
    # Scale has no handedness to worry about (a pure per-axis magnitude), but the AXIS each
    # magnitude applies to still needs the same X/Y/Z -> X/Z/Y remap positions/normals use.
    sx, sy, sz = scale
    obj.scale = (sx, sz, sy)

    stable_id = component.get("Id", -1)
    obj["arco3d_id"] = stable_id
    id_to_object[stable_id] = obj
    report.note("mesh '%s'" % obj.name)

    rig_data = component.get("Rig", {})
    if rig_data:
        _import_rig(component, obj, rig_data, collection, report)

    return obj


def _import_rig(component, mesh_obj, rig_data, collection, report):
    bone_dicts = rig_data.get("Bones", [])
    if not bone_dicts:
        return

    arm_data = bpy.data.armatures.new(mesh_obj.name + "_Armature")
    arm_obj = bpy.data.objects.new(mesh_obj.name + "_Armature", arm_data)
    collection.objects.link(arm_obj)
    arm_obj.location = mesh_obj.location
    arm_obj.rotation_mode = mesh_obj.rotation_mode
    if mesh_obj.rotation_mode == 'QUATERNION':
        arm_obj.rotation_quaternion = mesh_obj.rotation_quaternion
    arm_obj.scale = mesh_obj.scale

    id_to_name = {int(b.get("Id", i)): b.get("Name", "Bone%d" % i) for i, b in enumerate(bone_dicts)}

    bpy.context.view_layer.objects.active = arm_obj
    bpy.ops.object.mode_set(mode='EDIT')
    edit_bones = arm_data.edit_bones
    # First pass: create every bone with a real position (converted to Blender's axes) before any
    # parenting -- Blender requires the parent edit-bone to already exist.
    head_positions = {}
    for bone_dict in bone_dicts:
        name = bone_dict.get("Name", "Bone")
        head = a3d_parser.position_to_blender(bone_dict.get("BindPosition", [0, 0, 0]))
        head_positions[name] = mathutils.Vector(head)
        eb = edit_bones.new(name)
        eb.head = head
        # A small fixed-length stub along Blender's own +Z (this app's own "up" after conversion),
        # not toward the child -- deliberately, so every bone's REST ORIENTATION is a known, fixed
        # rotation relative to armature space (roll 0, pointing +Z) rather than one that varies
        # with wherever a child happens to sit. The real per-bone rotation is applied afterward via
        # pose_bone.matrix (see _apply_pose below), which corrects for whatever the rest
        # orientation actually is -- so this choice only affects the bone's on-screen OCTAHEDRAL
        # shape in Blender's viewport, never posing correctness (verified directly against a real
        # Armature-modifier deform, not assumed -- see project memory for the exact numbers).
        eb.tail = (head[0], head[1], head[2] + 0.1)
        eb.roll = 0.0

    for bone_dict in bone_dicts:
        parent_id = bone_dict.get("ParentId", -1)
        if parent_id is not None and int(parent_id) >= 0 and int(parent_id) in id_to_name:
            edit_bones[bone_dict.get("Name")].parent = edit_bones[id_to_name[int(parent_id)]]
    bpy.ops.object.mode_set(mode='OBJECT')

    # Parent the mesh to the armature with a real Armature modifier -- ordinary Blender rigging,
    # not a custom deform path, so the result behaves like any other rigged Blender character.
    mesh_obj.parent = arm_obj
    modifier = mesh_obj.modifiers.new(name="Armature", type='ARMATURE')
    modifier.object = arm_obj

    for bone_dict in bone_dicts:
        name = bone_dict.get("Name")
        group = mesh_obj.vertex_groups.new(name=name)
        bone_dict["_vertex_group"] = group

    bone_id_per_vertex = rig_data.get("BoneIdPerVertex", [])
    if bone_id_per_vertex:
        for vertex_index, bone_id in enumerate(bone_id_per_vertex):
            name = id_to_name.get(int(bone_id))
            if name is None:
                continue
            group = mesh_obj.vertex_groups.get(name)
            if group is not None:
                group.add([vertex_index], 1.0, 'REPLACE')
        report.note("rig '%s' (%d bones, rigid skin weights)" % (arm_obj.name, len(bone_dicts)))
    else:
        report.warn("rig '%s' imported with no skin weights (a modifier changed the vertex count at export time -- see A3DFormat.gd's own comment)" % arm_obj.name)

    poses = rig_data.get("Poses", [])
    if poses:
        _apply_pose(arm_obj, bone_dicts, poses[0], report)


def _apply_pose(arm_obj, bone_dicts, pose, report):
    """Applies one saved A3D pose to the armature's pose bones. Uses pose_bone.matrix (an
    ARMATURE-SPACE target, not the bone-local matrix_basis directly) precisely because a bone's
    REST orientation in Blender is generally NOT axis-aligned with armature space (its local Y
    axis points head-to-tail) -- setting matrix_basis directly would apply the desired rotation in
    the WRONG frame. The correct composition, verified against a real Armature-modifier mesh
    deform (not assumed): pose_bone.matrix = desired_world_space_transform @ rest_matrix, so that
    Blender's own real deformation formula (pose_matrix @ rest_matrix.inverted() @ vertex) reduces
    to exactly desired_world_space_transform @ vertex."""
    bone_rotations = pose.get("BoneRotations", [])
    id_to_name = {int(b.get("Id", i)): b.get("Name", "Bone%d" % i) for i, b in enumerate(bone_dicts)}
    id_to_bind = {int(b.get("Id", i)): b.get("BindPosition", [0, 0, 0]) for i, b in enumerate(bone_dicts)}
    posed_names = []
    for entry in bone_rotations:
        bone_id = int(entry.get("BoneId", -1))
        name = id_to_name.get(bone_id)
        if name is None or name not in arm_obj.pose.bones:
            continue
        pose_bone = arm_obj.pose.bones[name]
        head = mathutils.Vector(a3d_parser.position_to_blender(id_to_bind[bone_id]))
        quat = _quat_field_to_blender(entry.get("Quaternion", [0, 0, 0, 1]))
        desired_world = mathutils.Matrix.Translation(head) @ quat.to_matrix().to_4x4() @ mathutils.Matrix.Translation(-head)
        rest_matrix = pose_bone.bone.matrix_local
        pose_bone.matrix = desired_world @ rest_matrix
        posed_names.append(name)
    if posed_names:
        bpy.context.view_layer.update()
        report.note("pose '%s' (%d bone(s): %s)" % (pose.get("Name", "Pose"), len(posed_names), ", ".join(posed_names)))


def import_a3d(path, context):
    """Top-level entry point used by the Import operator. Returns an ImportReport. Raises
    a3d_parser.A3DError for a malformed file -- the operator layer is responsible for catching it
    and turning it into a user-facing bpy report, never a raw traceback."""
    manifest = a3d_parser.load_a3d(path)
    report = ImportReport()

    collection = bpy.data.collections.new(manifest.get("Asset", {}).get("Name", "Arco3D Asset"))
    context.scene.collection.children.link(collection)

    id_to_object = {}
    for component in a3d_parser.iter_components(manifest):
        _import_component(component, collection, report, id_to_object)

    if not id_to_object:
        report.warn("no mesh components found in this A3D file")
    return report
