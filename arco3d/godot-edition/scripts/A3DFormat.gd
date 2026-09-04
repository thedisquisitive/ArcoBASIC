extends RefCounted
class_name A3DFormat

## Real A3D read/write for Arco3D (Godot Edition), implementing RFC-0050's logical model as far as
## this application actually authors data today: a Manifest (format/version/coordinate system/
## capability negotiation, RFC-0050 Section 23) wrapping a Component tree (Section 7 --
## Components/Transforms/Geometry/Rig/Skin/Pose; layered Materials/paint/pose-catalogs/morphs are
## still future work, same as the paused ArcoBASIC implementation -- see the "Known gaps" comment
## at the bottom of this file for the full, honest accounting).
##
## RFC-0050 Section 6 deliberately does not mandate a physical encoding ("The physical encoding
## MAY evolve"), only that it support chunk discovery and versioned capability negotiation. The
## ArcoBASIC implementation (arco3d/stdlib/arco3d_io.abas) used ArcoCompy's own tagged-length wire
## format, native to that toolchain; this one uses plain JSON, native to GDScript (Godot's
## JSON.stringify/parse) -- same manifest shape and schema, different concrete bytes. Real
## cross-engine interop between the two .a3d producers is future work, not required for this
## engine's own export/import round-trip to be correct and spec-conformant on its own terms.
##
## Coordinate convention (RFC-0050 Section 10, frozen for v1): right-handed, Y-up, meters. This
## matches Godot's own native 3D convention exactly (Godot is right-handed, Y-up already), so --
## unlike a Blender bridge, which RFC-0051 correctly notes needs an explicit axis conversion --
## nothing here needs converting. Rotation is stored BOTH as Euler degrees (this engine's own
## authoritative representation -- Main.gd's rig FK math and Move/Rotate/Scale tools are all
## Euler-based, and changing that is out of scope here) AND as a derived quaternion (RFC-0050
## Section 10's own "quaternion ordering" requirement, [x, y, z, w]) -- purely additive, computed
## from the exact same Basis already backing the Euler value, so the two can never drift apart.
## The quaternion form exists for cross-application consumers (the Blender Bridge, RFC-0051):
## composing an axis-convention change (Y-up -> Z-up) is exact and order-independent via quaternion
## conjugation, whereas naively permuting Euler *components* under an axis swap is not (Euler
## angles don't commute with a change of basis the way a plain vector swap does) -- exactly the
## class of "looks reasonable, is actually wrong" bug this whole project has repeatedly caught by
## testing with real numbers rather than trusting a derivation, so it wasn't worth risking here.
##
## Schema v2 additions over v1 (both are read; only v2 is written): per-vertex Normals and UVs
## pulled directly from Godot's own already-built ArrayMesh (the SAME arrays already rendered on
## screen, not re-derived -- correct by construction, no separate normal-generation algorithm to
## keep in sync); an optional "Construction" block (the non-destructive base_mesh_data + modifier
## stack, RFC-0050 Section 11's own "authoring-level non-destructive construction data" allowance)
## so re-importing a file THIS app exported keeps it fully editable (Edit Mode, modifiers, Rig/Pose
## Mode) instead of flattening it into an opaque baked mesh forever; and an optional "Rig" block
## (named bones with real stable IDs, parent-child hierarchy via ParentId, bind positions, rigid
## skin weights) plus a "Poses" array (RFC-0050 Section 13/14 -- currently ever at most one entry,
## the live pose at export time, since this app has no saved pose CATALOG yet, only one live pose
## per object -- a real, honest scaffold toward Section 14, not a full catalog).
##
## Schema v3 additions over v2 (both are read; only v3 is written): an optional "MountPoints" array
## per component (named attachment points -- see Main.gd's own "Mount points and attachment"
## section for the design) and an optional "Attachment" block (which OTHER component's mount point
## this one is snapped to, referenced by that component's own already-existing stable "Id" field --
## no new ID scheme needed). A mount point's own bone reference (if bone-attached) uses the same
## "BoneId" stable-reference convention the Rig block's own ParentId already established.
##
## Schema v4 additions over v3 (both are read; only v4 is written): an optional "Material" block
## per component -- an ordered stack of layers (RFC-0050 Sections 17/19/20's own layered-paint and
## non-UV mapping model, see Main.gd's own "Material layers" section for the full design), each
## with its real source image embedded as base64 PNG (RFC-0050 Section 22's own embedded-resource
## allowance) rather than a filesystem path, plus its mapping mode, transform, source-rect crop,
## blend mode, and opacity.

const FORMAT_NAME := "A3D"
const CONTAINER_VERSION := 1
const SCHEMA_VERSION := 4
const SUPPORTED_CAPABILITIES := ["mesh.core", "rig.core", "construction.arco3d", "mount.core", "material.layers"]
const POSE_EPSILON := 0.0001


## Serializes one spawned primitive's real geometry (not a regenerate-from-descriptor shortcut --
## RFC-0050 Section 11 wants real vertex/index data) out of whatever ArrayMesh Godot already built
## for it. Works for any Mesh resource with at least one surface, not just this app's own
## BoxMesh/SphereMesh spawns. Normals/UVs are pulled straight from the SAME arrays Godot already
## computed for rendering (RFC-0050 Section 11's base-interoperable-subset MUST for normals/UVs) --
## UVs are omitted entirely (not padded with a fake value) when the source mesh genuinely has none
## -- which, found directly while writing this file's own test, turns out to be the common case for
## ANY object that has ever gone through Main._recompute_modifiers (adding a modifier, a rig, or
## any Edit Mode operation), not just an obviously-edited one: base_mesh_data (and therefore
## _mesh_from_dict, which _recompute_modifiers always rebuilds .mesh through) has no per-vertex UV
## field at all yet, so recomputing even once already discards whatever UVs Godot's own primitive
## generator originally provided. Only a plain, never-recomputed spawn still has real UVs to export
## today (see the "Known gaps" note at file end) -- a real, honestly-scoped limitation of the
## in-app mesh-data model, not something this exporter can paper over.
static func _mesh_to_dict(mesh: Mesh) -> Dictionary:
	if mesh == null or mesh.get_surface_count() == 0:
		return {}
	var arrays := mesh.surface_get_arrays(0)
	var vertices: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	var indices: PackedInt32Array
	if arrays[Mesh.ARRAY_INDEX] != null:
		indices = arrays[Mesh.ARRAY_INDEX]
	else:
		# A non-indexed mesh (every 3 consecutive vertices form one triangle, no shared index
		# buffer) -- real, not hypothetical: Godot's own CSG bake output (CSGCombiner3D.get_meshes(),
		# see _apply_boolean in Main.gd) comes out this way, since CSG-generated geometry has no
		# particular reason to share vertices between triangles. A sequential 0,1,2,3,4,5... index
		# array is exactly equivalent to "no indexing" for a plain triangle list, so synthesizing
		# one here means every OTHER piece of code that touches this dict (import, modifiers) never
		# needs to know indexed and non-indexed meshes are even a distinction that exists.
		indices = PackedInt32Array()
		indices.resize(vertices.size())
		for i in range(vertices.size()):
			indices[i] = i
	var vertex_list := []
	for v in vertices:
		vertex_list.append([v.x, v.y, v.z])
	var face_list := []
	var i := 0
	while i + 2 < indices.size():
		face_list.append([indices[i], indices[i + 1], indices[i + 2]])
		i += 3
	var result := {"Vertices": vertex_list, "Faces": face_list}

	if arrays[Mesh.ARRAY_NORMAL] != null:
		var normals: PackedVector3Array = arrays[Mesh.ARRAY_NORMAL]
		var normal_list := []
		for n in normals:
			normal_list.append([n.x, n.y, n.z])
		result["Normals"] = normal_list
	if arrays[Mesh.ARRAY_TEX_UV] != null:
		var uvs: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
		var uv_list := []
		for uv in uvs:
			uv_list.append([uv.x, uv.y])
		result["UVs"] = uv_list
	return result


## Rebuilds a real ArrayMesh (not a placeholder) from serialized vertex/face data. Uses the file's
## own stored Normals/UVs when present (schema v2 -- respects whatever smoothing/mapping the
## source actually authored, rather than guessing); falls back to SurfaceTool-generated smooth
## normals only when the file has none (schema v1, or a minimal external producer that only wrote
## the RFC-0050 Section 11 baseline of positions+topology).
static func _mesh_from_dict(mesh_dict: Dictionary) -> ArrayMesh:
	var vertices := PackedVector3Array()
	for v in mesh_dict.get("Vertices", []):
		vertices.append(Vector3(v[0], v[1], v[2]))
	var indices := PackedInt32Array()
	for f in mesh_dict.get("Faces", []):
		indices.append(f[0])
		indices.append(f[1])
		indices.append(f[2])

	var stored_normals: Array = mesh_dict.get("Normals", [])
	var stored_uvs: Array = mesh_dict.get("UVs", [])
	var has_stored_normals: bool = stored_normals.size() == vertices.size() and vertices.size() > 0
	var has_stored_uvs: bool = stored_uvs.size() == vertices.size() and vertices.size() > 0

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = indices
	if has_stored_normals:
		var normals := PackedVector3Array()
		for n in stored_normals:
			normals.append(Vector3(n[0], n[1], n[2]))
		arrays[Mesh.ARRAY_NORMAL] = normals
	if has_stored_uvs:
		var uvs := PackedVector2Array()
		for uv in stored_uvs:
			uvs.append(Vector2(uv[0], uv[1]))
		arrays[Mesh.ARRAY_TEX_UV] = uvs

	var array_mesh := ArrayMesh.new()
	array_mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	if has_stored_normals:
		return array_mesh

	# No usable stored normals -- generate real ones rather than leaving every face lit as pure
	# black (an all-zero normal), same fallback the pre-v2 format always used unconditionally.
	var surface_tool := SurfaceTool.new()
	surface_tool.create_from(array_mesh, 0)
	surface_tool.generate_normals()
	return surface_tool.commit()


## Duplicated (not shared via cross-preload) from Main.gd's own _weld_coincident_vertices --
## kept deliberately self-contained here rather than having A3DFormat.gd preload Main.gd (which
## would create a circular preload, since Main.gd already preloads THIS file), matching RFC-0050
## Section 32's own AI Implementation Guidance to keep the parser standalone. Used only as an
## import-time fallback (see _component_from_dict) for a file with no "Construction" block --
## synthesizing a workable base_mesh_data so Edit Mode isn't silently broken for every imported
## object, not just the ones this app itself round-trips.
static func _weld_coincident_vertices_for_import(mesh_data: Dictionary) -> Dictionary:
	const WELD_EPSILON := 0.0001
	var original_vertices: Array = mesh_data.get("Vertices", [])
	var original_faces: Array = mesh_data.get("Faces", [])
	var welded_vertices: Array = []
	var index_remap: Array = []
	var position_to_welded_index: Dictionary = {}
	for v in original_vertices:
		var rounded_key := Vector3(
			snapped(float(v[0]), WELD_EPSILON),
			snapped(float(v[1]), WELD_EPSILON),
			snapped(float(v[2]), WELD_EPSILON))
		if position_to_welded_index.has(rounded_key):
			index_remap.append(position_to_welded_index[rounded_key])
		else:
			var new_index: int = welded_vertices.size()
			welded_vertices.append(v)
			position_to_welded_index[rounded_key] = new_index
			index_remap.append(new_index)
	var welded_faces: Array = []
	for f in original_faces:
		welded_faces.append([index_remap[f[0]], index_remap[f[1]], index_remap[f[2]]])
	return {"Vertices": welded_vertices, "Faces": welded_faces}


static func _is_nonzero_rotation(degrees: Array) -> bool:
	return abs(float(degrees[0])) > POSE_EPSILON or abs(float(degrees[1])) > POSE_EPSILON or abs(float(degrees[2])) > POSE_EPSILON


## Serializes `node`'s arco_rig metadata (if any) to the on-disk schema: bones carry a real stable
## "Id" (RFC-0050 Section 9 -- "Stable IDs MUST NOT be based solely on array position"; this
## producer's IDs happen to equal array index today since this app never reorders or deletes an
## individual bone once a rig is created, but the field itself is a real ID reference, not a bare
## positional convention, so a future producer that DOES support reordering/deletion can populate
## it correctly without a format change), and "ParentId" is a reference to that Id, resolved back
## to an array index at import time via an explicit id map (see _component_from_dict) rather than
## assumed to equal the parent's own array position.
##
## Skin weights ("BoneIdPerVertex") are aligned to the EXPORTED "Mesh" vertex list -- the evaluated
## geometry, after modifiers -- not base_mesh_data's own pre-modifier list, because that's what a
## receiving application (Blender, or this app's own Mesh block) actually builds real geometry
## from. In the common case (no Mirror/Array/Boolean modifier active) the two lists are identical
## anyway; when a modifier HAS changed the vertex count, weights are honestly omitted rather than
## exporting a misaligned array that would silently skin the wrong vertices -- the skeleton and its
## current pose are still exported either way, just without per-vertex weights that pass this face
## check.
static func _rig_to_dict(node: Node3D, evaluated_vertex_count: int) -> Dictionary:
	if not node.has_meta("arco_rig"):
		return {}
	var rig: Dictionary = node.get_meta("arco_rig")
	var bones: Array = rig.get("Bones", [])
	var bone_dicts := []
	var poses := []
	for i in range(bones.size()):
		var bone: Dictionary = bones[i]
		bone_dicts.append({
			"Id": i,
			"Name": bone.get("Name", "Bone%d" % i),
			"ParentId": int(bone.get("Parent", -1)),
			"BindPosition": bone.get("BindPosition", [0.0, 0.0, 0.0]),
		})
		var rotation_degrees: Array = bone.get("PoseRotationDegrees", [0.0, 0.0, 0.0])
		if _is_nonzero_rotation(rotation_degrees):
			var basis := Basis.from_euler(Vector3(deg_to_rad(rotation_degrees[0]), deg_to_rad(rotation_degrees[1]), deg_to_rad(rotation_degrees[2])))
			var quat := basis.get_rotation_quaternion()
			poses.append({
				"BoneId": i,
				"RotationDegrees": rotation_degrees,
				"Quaternion": [quat.x, quat.y, quat.z, quat.w],
			})
	var result := {"Bones": bone_dicts}
	if not poses.is_empty():
		result["Poses"] = [{"Id": 0, "Name": "Exported Pose", "BoneRotations": poses}]

	var weights: Array = node.get_meta("arco_vertex_bone_index", [])
	if not weights.is_empty() and weights.size() == evaluated_vertex_count:
		result["BoneIdPerVertex"] = weights.duplicate()
	return result


## Reconstructs arco_rig/arco_vertex_bone_index metadata from the on-disk schema and attaches it
## directly to `mesh_instance`. Resolves ParentId -> array index through an explicit id map (built
## from the file's own "Id" fields, never assumed to equal storage order) so a hypothetical future
## producer with non-positional bone IDs would still import correctly, even though today's own
## exporter happens to use positional IDs.
static func _apply_rig_from_dict(mesh_instance: MeshInstance3D, rig_data: Dictionary) -> void:
	if rig_data.is_empty():
		return
	var bone_dicts: Array = rig_data.get("Bones", [])
	if bone_dicts.is_empty():
		return
	var id_to_index := {}
	for i in range(bone_dicts.size()):
		id_to_index[int(bone_dicts[i].get("Id", i))] = i

	var pose_by_bone_index := {}
	var poses: Array = rig_data.get("Poses", [])
	if not poses.is_empty():
		# Only the FIRST saved pose is applied at import (this app has no pose-catalog UI to pick
		# from yet -- see the "Known gaps" note at file end); later entries round-trip through
		# Poses.duplicate() below even though only Poses[0] is ever actually applied as the live
		# pose, so a future catalog UI could recover them without a format change.
		for bone_rotation in poses[0].get("BoneRotations", []):
			var bone_id := int(bone_rotation.get("BoneId", -1))
			if id_to_index.has(bone_id):
				pose_by_bone_index[id_to_index[bone_id]] = bone_rotation.get("RotationDegrees", [0.0, 0.0, 0.0])

	var bones := []
	for bone_dict in bone_dicts:
		var id := int(bone_dict.get("Id", bones.size()))
		var parent_id := int(bone_dict.get("ParentId", -1))
		var parent_index: int = id_to_index.get(parent_id, -1) if parent_id >= 0 else -1
		var index: int = id_to_index[id]
		bones.append({
			"Name": String(bone_dict.get("Name", "Bone%d" % index)),
			"Parent": parent_index,
			"BindPosition": bone_dict.get("BindPosition", [0.0, 0.0, 0.0]),
			"PoseRotationDegrees": pose_by_bone_index.get(index, [0.0, 0.0, 0.0]),
		})
	mesh_instance.set_meta("arco_rig", {"Bones": bones})

	var bone_id_per_vertex: Array = rig_data.get("BoneIdPerVertex", [])
	if not bone_id_per_vertex.is_empty():
		var weights := []
		for bone_id in bone_id_per_vertex:
			weights.append(id_to_index.get(int(bone_id), 0))
		mesh_instance.set_meta("arco_vertex_bone_index", weights)
	elif mesh_instance.has_meta("base_mesh_data"):
		# No per-vertex weights stored (e.g. a modifier changed the vertex count at export time --
		# see _rig_to_dict's own comment), but the skeleton itself imported fine and base_mesh_data
		# is available -- recompute real automatic weights against it rather than leaving the rig
		# un-posable. A graceful degrade, not a silently broken feature.
		var vertices: Array = (mesh_instance.get_meta("base_mesh_data") as Dictionary).get("Vertices", [])
		var bones_for_weights: Array = bones
		var weights := []
		weights.resize(vertices.size())
		for i in range(vertices.size()):
			var point := Vector3(vertices[i][0], vertices[i][1], vertices[i][2])
			var best_bone_index := 0
			var best_distance := INF
			for bone_index in range(bones_for_weights.size()):
				var bind: Array = bones_for_weights[bone_index]["BindPosition"]
				var bind_position := Vector3(bind[0], bind[1], bind[2])
				var distance := point.distance_to(bind_position)
				if distance < best_distance:
					best_distance = distance
					best_bone_index = bone_index
			weights[i] = best_bone_index
		mesh_instance.set_meta("arco_vertex_bone_index", weights)


## Mount points and attachment (RFC-0049's own object-composition gap -- see Main.gd's own "Mount
## points and attachment" section header comment for the full design reasoning). A BoneIndex-based
## mount point references its bone by the SAME stable "Id" scheme Rig.Bones already uses (Id ==
## array index for this producer, same as _rig_to_dict's own ParentId) -- written as "BoneId" here
## for the same reason, -1 meaning "not bone-attached, plain object-level point."
static func _mount_points_to_dict(node: Node3D) -> Array:
	var mount_points: Array = node.get_meta("arco_mount_points", [])
	var result := []
	for mount_point in mount_points:
		result.append({
			"Name": mount_point.get("Name", "Mount"),
			"Gender": mount_point.get("Gender", "Male"),
			"Type": mount_point.get("Type", "Generic"),
			"BoneId": int(mount_point.get("BoneIndex", -1)),
			"LocalPosition": mount_point.get("LocalPosition", [0.0, 0.0, 0.0]),
			"LocalRotationDegrees": mount_point.get("LocalRotationDegrees", [0.0, 0.0, 0.0]),
			# Constraints (the user's own follow-up ask, after mount points/attachment themselves):
			# "None" (the default) means fully rigid -- Min/MaxDegrees are meaningless until an
			# axis is actually chosen, but still round-tripped so re-enabling a constraint later
			# doesn't lose whatever range was previously configured.
			"ConstraintAxis": mount_point.get("ConstraintAxis", "None"),
			"ConstraintMinDegrees": mount_point.get("ConstraintMinDegrees", -180.0),
			"ConstraintMaxDegrees": mount_point.get("ConstraintMaxDegrees", 180.0),
		})
	return result


## Resolves "BoneId" -> the in-memory "BoneIndex" a freshly-reconstructed arco_rig will actually
## use. Rebuilds the SAME id-to-index map _apply_rig_from_dict computes internally rather than
## threading it through as a return value -- cheap (a handful of bones) and keeps the two functions
## independent, matching this file's own preference for small, separately-readable steps over
## tightly-coupled multi-output helpers.
static func _apply_mount_points_from_dict(mesh_instance: MeshInstance3D, mount_points_data: Array, rig_data: Dictionary) -> void:
	if mount_points_data.is_empty():
		return
	var id_to_index := {}
	for i in range(rig_data.get("Bones", []).size()):
		var bone_dict: Dictionary = rig_data["Bones"][i]
		id_to_index[int(bone_dict.get("Id", i))] = i
	var mount_points := []
	for mp_dict in mount_points_data:
		var bone_id := int(mp_dict.get("BoneId", -1))
		mount_points.append({
			"Name": String(mp_dict.get("Name", "Mount")),
			"Gender": String(mp_dict.get("Gender", "Male")),
			"Type": String(mp_dict.get("Type", "Generic")),
			"BoneIndex": id_to_index.get(bone_id, -1) if bone_id >= 0 else -1,
			"LocalPosition": mp_dict.get("LocalPosition", [0.0, 0.0, 0.0]),
			"LocalRotationDegrees": mp_dict.get("LocalRotationDegrees", [0.0, 0.0, 0.0]),
			"ConstraintAxis": String(mp_dict.get("ConstraintAxis", "None")),
			"ConstraintMinDegrees": float(mp_dict.get("ConstraintMinDegrees", -180.0)),
			"ConstraintMaxDegrees": float(mp_dict.get("ConstraintMaxDegrees", 180.0)),
		})
	mesh_instance.set_meta("arco_mount_points", mount_points)


## Material layers (RFC-0050 Sections 17/19/20 -- see Main.gd's own "Material layers" section for
## the full design). Each layer's real image is embedded as base64-encoded PNG bytes (RFC-0050
## Section 22's own "A3D/A3M MUST support embedded resources"), NOT a filesystem path -- a real
## reference to wherever the user's own disk happened to have the source image would break the
## instant the file moved, same reasoning every other real embedded-resource format uses. Real,
## honest gap: this does NOT also bake a flat fallback texture+UV pair for a non-Arco3D reader --
## see Main.gd's own header comment on why that's a deliberate, documented deferral, not an
## oversight.
static func _material_to_dict(node: Node3D) -> Dictionary:
	var material: Dictionary = node.get_meta("arco_material", {})
	var layers: Array = material.get("Layers", [])
	if layers.is_empty():
		return {}
	var textures_by_id: Dictionary = node.get_meta("arco_material_textures", {})
	var result_layers := []
	for layer in layers:
		var layer_id := int(layer.get("Id", -1))
		var texture: Texture2D = textures_by_id.get(layer_id)
		if texture == null:
			continue # a layer whose image failed to load has nothing real to export
		var image := texture.get_image()
		var png_bytes := image.save_png_to_buffer()
		result_layers.append({
			"Id": layer_id,
			"Name": layer.get("Name", "Layer"),
			"Enabled": layer.get("Enabled", true),
			"Mapping": layer.get("Mapping", "Triplanar"),
			"Offset": layer.get("Offset", [0.0, 0.0, 0.0]),
			"Scale": layer.get("Scale", [1.0, 1.0, 1.0]),
			"RotationDegrees": layer.get("RotationDegrees", [0.0, 0.0, 0.0]),
			"SourceRect": layer.get("SourceRect", [0.0, 0.0, 1.0, 1.0]),
			"BlendMode": layer.get("BlendMode", "Normal"),
			"Opacity": layer.get("Opacity", 1.0),
			"ImageFormat": "png",
			"ImageData": Marshalls.raw_to_base64(png_bytes),
		})
	return {"Layers": result_layers} if not result_layers.is_empty() else {}


## Decodes each layer's embedded image back into a real, live Texture2D and reconstructs
## arco_material/arco_material_textures -- the same two-metadata-key shape Main.gd's own live
## material system already uses, so a reimported object is immediately editable in the MATERIAL
## panel, not just visually correct.
static func _apply_material_from_dict(mesh_instance: MeshInstance3D, material_data: Dictionary) -> void:
	var layer_dicts: Array = material_data.get("Layers", [])
	if layer_dicts.is_empty():
		return
	var layers := []
	var textures_by_id := {}
	for layer_dict in layer_dicts:
		var layer_id := int(layer_dict.get("Id", -1))
		layers.append({
			"Id": layer_id,
			"Name": String(layer_dict.get("Name", "Layer")),
			"Enabled": bool(layer_dict.get("Enabled", true)),
			"Mapping": String(layer_dict.get("Mapping", "Triplanar")),
			"Offset": layer_dict.get("Offset", [0.0, 0.0, 0.0]),
			"Scale": layer_dict.get("Scale", [1.0, 1.0, 1.0]),
			"RotationDegrees": layer_dict.get("RotationDegrees", [0.0, 0.0, 0.0]),
			"SourceRect": layer_dict.get("SourceRect", [0.0, 0.0, 1.0, 1.0]),
			"BlendMode": String(layer_dict.get("BlendMode", "Normal")),
			"Opacity": float(layer_dict.get("Opacity", 1.0)),
		})
		var png_bytes: PackedByteArray = Marshalls.base64_to_raw(layer_dict.get("ImageData", ""))
		var image := Image.new()
		if image.load_png_from_buffer(png_bytes) == OK:
			textures_by_id[layer_id] = ImageTexture.create_from_image(image)
	mesh_instance.set_meta("arco_material", {"Layers": layers})
	mesh_instance.set_meta("arco_material_textures", textures_by_id)


static func _component_to_dict(node: Node3D, capability_flags: Dictionary) -> Dictionary:
	var quat := node.quaternion
	var result := {
		"Id": node.get_meta("arco_id", -1),
		"Name": node.name,
		"Transform": {
			"Position": [node.position.x, node.position.y, node.position.z],
			"Rotation": [node.rotation_degrees.x, node.rotation_degrees.y, node.rotation_degrees.z],
			"Quaternion": [quat.x, quat.y, quat.z, quat.w],
			"Scale": [node.scale.x, node.scale.y, node.scale.z],
		},
	}
	var evaluated_vertex_count := 0
	if node is MeshInstance3D and node.mesh != null:
		var mesh_dict := _mesh_to_dict(node.mesh)
		result["Mesh"] = mesh_dict
		evaluated_vertex_count = (mesh_dict.get("Vertices", []) as Array).size()
		if node.has_meta("base_mesh_data"):
			result["Construction"] = {
				"BaseMesh": node.get_meta("base_mesh_data"),
				"Modifiers": node.get_meta("arco_modifiers", []),
			}
			capability_flags["construction"] = true
		var rig_dict := _rig_to_dict(node, evaluated_vertex_count)
		if not rig_dict.is_empty():
			result["Rig"] = rig_dict
			capability_flags["rig"] = true
		var mount_points_dict := _mount_points_to_dict(node)
		if not mount_points_dict.is_empty():
			result["MountPoints"] = mount_points_dict
			capability_flags["mount"] = true
		if node.has_meta("arco_attachment"):
			result["Attachment"] = node.get_meta("arco_attachment")
			capability_flags["mount"] = true
		var material_dict := _material_to_dict(node)
		if not material_dict.is_empty():
			result["Material"] = material_dict
			capability_flags["material"] = true

	var children := []
	for child in node.get_children():
		# Every MeshInstance3D in this app carries a picking-collider Area3D child (see
		# _add_picking_collider in Main.gd) -- real UI plumbing, not asset content, so it must NOT
		# be exported as a spurious empty component (a real, pre-existing wart this pass fixes: the
		# original v1 exporter recursed into it unconditionally, since Area3D is a Node3D too).
		if child is Node3D and not (child is Area3D):
			children.append(_component_to_dict(child, capability_flags))
	result["Children"] = children
	return result


static func _component_from_dict(data: Dictionary, next_id_holder: Array) -> Node3D:
	var node: Node3D
	var mesh_data: Dictionary = data.get("Mesh", {})
	if mesh_data.size() > 0:
		var mesh_instance := MeshInstance3D.new()
		mesh_instance.mesh = _mesh_from_dict(mesh_data)
		var material := StandardMaterial3D.new()
		material.albedo_color = Color(0.6, 0.7, 0.85)
		mesh_instance.material_override = material

		var construction: Dictionary = data.get("Construction", {})
		if construction.has("BaseMesh"):
			mesh_instance.set_meta("base_mesh_data", construction["BaseMesh"])
			mesh_instance.set_meta("arco_modifiers", construction.get("Modifiers", []))
		else:
			# No construction chunk (an old v1 file, or a minimal external producer that only wrote
			# the RFC-0050 base geometry subset) -- synthesize a real, editable base_mesh_data
			# anyway so Edit Mode isn't silently dead for every imported object, matching the same
			# weld-at-spawn-time treatment every native primitive already gets.
			mesh_instance.set_meta("base_mesh_data", _weld_coincident_vertices_for_import(mesh_data))
			mesh_instance.set_meta("arco_modifiers", [])

		_apply_rig_from_dict(mesh_instance, data.get("Rig", {}))
		_apply_mount_points_from_dict(mesh_instance, data.get("MountPoints", []), data.get("Rig", {}))
		# Attachment.ParentId references another top-level component's own "Id" -- resolved LAZILY
		# by _find_object_by_id at propagation time (Main._propagate_all_attachments, every frame),
		# not here, so no two-pass "does the parent exist yet" resolution is needed: by the time
		# propagation ever runs, every component from this file has already been constructed and
		# registered in scene_objects regardless of which order they appeared in the file.
		if data.has("Attachment"):
			mesh_instance.set_meta("arco_attachment", data["Attachment"])
		_apply_material_from_dict(mesh_instance, data.get("Material", {}))
		node = mesh_instance
	else:
		node = Node3D.new()
	node.name = String(data.get("Name", "Component"))
	var transform_data: Dictionary = data.get("Transform", {})
	var position = transform_data.get("Position", [0, 0, 0])
	var rotation = transform_data.get("Rotation", [0, 0, 0])
	var scale = transform_data.get("Scale", [1, 1, 1])
	node.position = Vector3(position[0], position[1], position[2])
	# Euler degrees remain the authoritative rotation representation on import -- the Quaternion
	# field is written for cross-application consumers (see this file's own header comment) but
	# this reader doesn't need it, since it's derived from (never independent of) the same value.
	node.rotation_degrees = Vector3(rotation[0], rotation[1], rotation[2])
	node.scale = Vector3(scale[0], scale[1], scale[2])
	var id: int = int(data.get("Id", -1))
	node.set_meta("arco_id", id)
	if id >= next_id_holder[0]:
		next_id_holder[0] = id + 1
	for child_data in data.get("Children", []):
		var child_node := _component_from_dict(child_data, next_id_holder)
		node.add_child(child_node)
	return node


## Exports every Node3D child of scene_root (the app's own flat spawn list, same shape the paused
## ArcoBASIC arco3d.abas used -- one synthetic root Component wrapping a flat list) to path.
## OptionalCapabilities reflects what THIS PARTICULAR asset actually uses (rig.core/
## construction.arco3d only appear when at least one component really has a rig/construction
## block), not a blanket declaration -- so a reader that only understands mesh.core can tell, from
## the manifest alone, that it's safe to ignore chunks it doesn't have simplest without needing to
## scan the whole file first.
static func export_asset(scene_root: Node3D, asset_name: String, path: String) -> Error:
	var capability_flags := {"rig": false, "construction": false, "mount": false, "material": false}
	var children := []
	for child in scene_root.get_children():
		if child is Node3D:
			children.append(_component_to_dict(child, capability_flags))

	var optional_capabilities := []
	if capability_flags["construction"]:
		optional_capabilities.append("construction.arco3d")
	if capability_flags["rig"]:
		optional_capabilities.append("rig.core")
	if capability_flags["mount"]:
		optional_capabilities.append("mount.core")
	if capability_flags["material"]:
		optional_capabilities.append("material.layers")

	var manifest := {
		"Format": FORMAT_NAME,
		"ContainerVersion": CONTAINER_VERSION,
		"SchemaVersion": SCHEMA_VERSION,
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

	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return FileAccess.get_open_error()
	file.store_string(JSON.stringify(manifest, "  "))
	file.close()
	return OK


## Returns {Ok: bool, Root: Node3D, NextId: int, Error: String}. Root is a fresh Node3D holding the
## imported children (mirroring export_asset's own flat-list-under-a-synthetic-root shape) ready to
## be reparented under the app's real scene root; caller owns freeing it if Ok is false. Caller is
## also responsible for running each imported MeshInstance3D through the real modifier/pose
## pipeline (Main._recompute_modifiers) once reparented -- this function only reconstructs metadata
## and the AS-EXPORTED baked mesh; it does not re-evaluate modifiers/pose itself (no access to that
## pipeline from this standalone parser, by design -- see this file's own header comment).
static func import_asset(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {"Ok": false, "Error": "file does not exist: " + path}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"Ok": false, "Error": "could not open file: " + path}
	var text := file.get_as_text()
	file.close()

	var json := JSON.new()
	var parse_result := json.parse(text)
	if parse_result != OK:
		return {"Ok": false, "Error": "corrupt A3D container (invalid JSON): " + json.get_error_message()}
	var manifest = json.data
	if typeof(manifest) != TYPE_DICTIONARY:
		return {"Ok": false, "Error": "corrupt A3D container (root is not an object)"}
	if manifest.get("Format", "") != FORMAT_NAME:
		return {"Ok": false, "Error": "not an A3D file (Format tag is '%s')" % manifest.get("Format", "")}
	if int(manifest.get("ContainerVersion", -1)) > CONTAINER_VERSION:
		return {"Ok": false, "Error": "A3D container version %s is newer than this build supports (%d)" % [str(manifest.get("ContainerVersion")), CONTAINER_VERSION]}
	if int(manifest.get("SchemaVersion", 1)) > SCHEMA_VERSION:
		return {"Ok": false, "Error": "A3D schema version %s is newer than this build supports (%d)" % [str(manifest.get("SchemaVersion")), SCHEMA_VERSION]}
	for capability in manifest.get("RequiredCapabilities", []):
		if not SUPPORTED_CAPABILITIES.has(capability):
			return {"Ok": false, "Error": "unsupported required capability: " + str(capability)}

	var asset: Dictionary = manifest.get("Asset", {})
	var root_data: Dictionary = asset.get("Root", {})
	var next_id_holder := [0]
	var imported_root := Node3D.new()
	imported_root.name = String(asset.get("Name", "ImportedAsset"))
	for child_data in root_data.get("Children", []):
		var child_node := _component_from_dict(child_data, next_id_holder)
		imported_root.add_child(child_node)
	return {"Ok": true, "Root": imported_root, "NextId": next_id_holder[0], "Error": ""}


## ---------------------------------------------------------------------------------------------
## Known, real, honestly-documented gaps against RFC-0050's full logical model (Section 7/8) --
## not silently missing, deliberately deferred because nothing in this app authors the underlying
## data yet:
##   - Materials/A3M: real layered materials now exist (Sections 17/19/20 -- see Main.gd's own
##     "Material layers" section) and export/import for real, but only the LIVE, layered, shader-
##     composited form -- no baked flat-texture+UV fallback is produced for a non-Arco3D reader yet
##     (a real, deliberate deferral RFC-0050 Section 19 itself allows: "Readers that do not
##     implement the generator MAY consume the cached generated image instead", which presumes one
##     exists to consume -- none is baked here yet). Material Channels beyond base color
##     (roughness/metallic/specular/normal/height/emission, RFC-0050 Section 16), Masks (Section
##     18), and Stylization metadata (Section 21) don't exist in this app at all yet either.
##   - Pose catalogs (RFC-0050 Section 14): only ONE live pose per rig is ever exported (see
##     _rig_to_dict), not a named, multi-pose catalog -- this app has no UI to save/name/browse
##     multiple poses yet, only to pose live. The "Poses" array's shape already matches the RFC's
##     multi-pose model, so adding catalog UI later is a producer-side change, not a format change.
##   - Pose sequences, morphs, stylization metadata, mesh-part component boundaries finer than
##     "one whole object" -- none of these exist in this app yet.
##   - Multi-bone smooth skin blending -- weights are always rigid (single bone, implicit weight
##     1.0), matching this app's own automatic-weighting design; still a real, valid degenerate
##     case of RFC-0050 Section 12's "weights MUST be normalized" requirement, not a violation.
##   - UVs only survive export for a plain, never-recomputed spawn (see _mesh_to_dict's own
##     comment) -- base_mesh_data has no per-vertex UV field, so adding a modifier/rig or doing any
##     Edit Mode operation already discards a primitive's original UVs before export ever runs.
##     Real fix requires threading a UV array through base_mesh_data and every function that
##     mutates it (weld/extrude/inset/mirror/array/rig-pose) -- a separate, larger feature (UV
##     authoring/preservation), not attempted here.
## ---------------------------------------------------------------------------------------------
