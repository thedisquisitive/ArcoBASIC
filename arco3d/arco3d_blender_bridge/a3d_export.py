"""Blender -> A3D export (RFC-0051 Section 20's "portable subset"): mesh geometry, transform,
armature, rigid skin weights, and the current pose. Deliberately smaller than the importer's own
coverage -- RFC-0051 Section 20 is explicit that "the exporter MUST NOT silently imply that
arbitrary Blender scenes can round-trip losslessly", so this stays honest about what it does not
attempt (materials, shape keys, constraints, multi-bone smooth weights) rather than guessing.
"""

import json
import math

import bpy
import mathutils

from . import a3d_parser


class ExportReport:
    def __init__(self):
        self.exported = []
        self.warnings = []

    def note(self, message):
        self.exported.append(message)

    def warn(self, message):
        self.warnings.append(message)

    def summary(self):
        lines = []
        if self.exported:
            lines.append("Exported: " + "; ".join(self.exported))
        if self.warnings:
            lines.append("Warnings: " + "; ".join(self.warnings))
        return " | ".join(lines) if lines else "Nothing exported"


def _find_armature(mesh_obj):
    if mesh_obj.parent is not None and mesh_obj.parent.type == 'ARMATURE':
        return mesh_obj.parent
    for modifier in mesh_obj.modifiers:
        if modifier.type == 'ARMATURE' and modifier.object is not None:
            return modifier.object
    return None


def _rig_dict_from_armature(arm_obj, mesh_obj, vertex_count, report):
    bones = list(arm_obj.data.bones)
    if not bones:
        return {}
    name_to_id = {bone.name: i for i, bone in enumerate(bones)}
    bone_dicts = []
    for i, bone in enumerate(bones):
        parent_id = name_to_id[bone.parent.name] if bone.parent is not None else -1
        head_a3d = a3d_parser.position_from_blender(tuple(bone.head_local))
        bone_dicts.append({
            "Id": i,
            "Name": bone.name,
            "ParentId": parent_id,
            "BindPosition": list(head_a3d),
        })

    # Rigid weights: this format has no multi-bone blending (see A3DFormat.gd's own "Known gaps"
    # note), so a Blender vertex with MORE than one nonzero-weight group loses information here --
    # take its single HIGHEST-weight group and warn, rather than silently picking one arbitrarily
    # or guessing an average that would not match either app's actual deformation.
    bone_id_per_vertex = [-1] * vertex_count
    multi_weight_count = 0
    unweighted_count = 0
    for vertex in mesh_obj.data.vertices:
        best_group = None
        best_weight = 0.0
        nonzero_groups = 0
        for group_element in vertex.groups:
            if group_element.weight > 0.0001:
                nonzero_groups += 1
                if group_element.weight > best_weight:
                    best_weight = group_element.weight
                    best_group = group_element.group
        if nonzero_groups > 1:
            multi_weight_count += 1
        if best_group is None:
            unweighted_count += 1
            continue
        group_name = mesh_obj.vertex_groups[best_group].name
        if group_name in name_to_id:
            bone_id_per_vertex[vertex.index] = name_to_id[group_name]
    if multi_weight_count:
        report.warn("%d vertex(es) had more than one weighted bone in Blender -- only the highest-weight bone was kept (this format has no smooth multi-bone blending yet)" % multi_weight_count)
    if unweighted_count:
        report.warn("%d vertex(es) had no armature weight in Blender -- left unweighted (bone 0 fallback on import)" % unweighted_count)

    result = {"Bones": bone_dicts}
    if all(b >= 0 for b in bone_id_per_vertex):
        result["BoneIdPerVertex"] = bone_id_per_vertex

    # Current pose: sparse, same convention as the importer -- only bones whose pose differs from
    # rest (beyond a small epsilon) are recorded, matching RFC-0050 Section 13's own "a pose MAY
    # contain only a subset of bones".
    bone_rotations = []
    for i, bone in enumerate(bones):
        pose_bone = arm_obj.pose.bones.get(bone.name)
        if pose_bone is None:
            continue
        local_quat = pose_bone.matrix_basis.to_quaternion()
        if abs(local_quat.angle) < 0.001:
            continue
        # pose_bone.matrix_basis is relative to the bone's own REST-LOCAL frame -- convert to the
        # same "pure world-space rotation about the bone's own head" representation this format
        # actually stores, using the exact inverse of the composition _apply_pose (a3d_import.py)
        # performs: desired_world = pose_matrix @ rest_matrix.inverted(), then strip the
        # translation (which _pivot_rotation_transform-style storage doesn't need -- BindPosition
        # already carries the head).
        rest_matrix = pose_bone.bone.matrix_local
        pose_matrix = pose_bone.matrix
        world_rotation_matrix = (pose_matrix @ rest_matrix.inverted()).to_3x3()
        world_quat_blender = world_rotation_matrix.to_quaternion()
        x, y, z, w = a3d_parser.quat_from_blender((world_quat_blender.x, world_quat_blender.y, world_quat_blender.z, world_quat_blender.w))
        a3d_quat = mathutils.Quaternion((w, x, y, z))
        euler = a3d_quat.to_euler('XYZ')
        bone_rotations.append({
            "BoneId": i,
            "RotationDegrees": [math.degrees(euler.x), math.degrees(euler.y), math.degrees(euler.z)],
            "Quaternion": [x, y, z, w],
        })
    if bone_rotations:
        result["Poses"] = [{"Id": 0, "Name": "Exported Pose", "BoneRotations": bone_rotations}]
    return result


def _component_dict_from_object(obj, report):
    if obj.type != 'MESH':
        return None
    mesh = obj.data
    vertices = [a3d_parser.position_from_blender(tuple(v.co)) for v in mesh.vertices]
    mesh.calc_loop_triangles()
    faces = [list(tri.vertices) for tri in mesh.loop_triangles]

    mesh_dict = {"Vertices": vertices, "Faces": faces}

    normals = [a3d_parser.position_from_blender(tuple(v.normal)) for v in mesh.vertices]
    mesh_dict["Normals"] = normals

    if mesh.uv_layers.active is not None:
        uv_layer = mesh.uv_layers.active
        uvs = [[0.0, 0.0] for _ in mesh.vertices]
        for loop in mesh.loops:
            uv = uv_layer.data[loop.index].uv
            uvs[loop.vertex_index] = [uv[0], uv[1]]
        mesh_dict["UVs"] = uvs
        report.note("UVs on '%s'" % obj.name)

    quat = obj.rotation_quaternion if obj.rotation_mode == 'QUATERNION' else obj.rotation_euler.to_quaternion()
    a3d_quat_xyzw = a3d_parser.quat_from_blender((quat.x, quat.y, quat.z, quat.w))
    a3d_quat = mathutils.Quaternion((a3d_quat_xyzw[3], a3d_quat_xyzw[0], a3d_quat_xyzw[1], a3d_quat_xyzw[2]))
    euler = a3d_quat.to_euler('XYZ')

    sx, sy, sz = obj.scale
    component = {
        "Id": -1,
        "Name": obj.name,
        "Transform": {
            "Position": list(a3d_parser.position_from_blender(tuple(obj.location))),
            "Rotation": [math.degrees(euler.x), math.degrees(euler.y), math.degrees(euler.z)],
            "Quaternion": list(a3d_quat_xyzw),
            "Scale": [sx, sz, sy],
        },
        "Mesh": mesh_dict,
        "Children": [],
    }
    report.note("mesh '%s' (%d vertices, %d faces)" % (obj.name, len(vertices), len(faces)))

    armature = _find_armature(obj)
    if armature is not None:
        rig_dict = _rig_dict_from_armature(armature, obj, len(vertices), report)
        if rig_dict:
            component["Rig"] = rig_dict
            report.note("rig '%s' (%d bones)" % (armature.name, len(rig_dict.get("Bones", []))))

    return component


def export_a3d(path, objects, asset_name, report):
    """Exports the given Blender objects (mesh objects; non-mesh objects are skipped with a
    warning) to an A3D v2 file at `path`. Returns nothing -- fills `report` and raises OSError on a
    real filesystem failure, matching a3d_parser's own "let real I/O errors surface, don't
    swallow them" convention."""
    children = []
    capability_flags = {"rig": False}
    for obj in objects:
        if obj.type != 'MESH':
            report.warn("skipped '%s' (only mesh objects are exported; %s is not supported yet)" % (obj.name, obj.type))
            continue
        component = _component_dict_from_object(obj, report)
        if component is not None:
            if "Rig" in component:
                capability_flags["rig"] = True
            children.append(component)

    optional_capabilities = []
    if capability_flags["rig"]:
        optional_capabilities.append("rig.core")

    manifest = {
        "Format": a3d_parser.FORMAT_NAME,
        "ContainerVersion": a3d_parser.CONTAINER_VERSION,
        "SchemaVersion": a3d_parser.SCHEMA_VERSION,
        "CoordinateSystem": {"Up": "Y", "Handedness": "Right", "Unit": "Meter", "QuaternionOrder": "XYZW"},
        "RequiredCapabilities": ["mesh.core"],
        "OptionalCapabilities": optional_capabilities,
        "Asset": {
            "Name": asset_name,
            "Root": {
                "Id": -1,
                "Name": "Scene",
                "Transform": {"Position": [0, 0, 0], "Rotation": [0, 0, 0], "Quaternion": [0, 0, 0, 1], "Scale": [1, 1, 1]},
                "Children": children,
            },
        },
    }

    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)

    if not children:
        report.warn("nothing exported -- select at least one mesh object")
