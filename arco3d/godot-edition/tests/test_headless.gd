extends SceneTree

## Headless functional test for Arco3D (Godot Edition) -- exercises the real Main.gd logic
## (spawn/select/move/delete/export/import) the same way a human would via keyboard/UI, without
## needing a real display. Run with:
##   godot --headless --script tests/test_headless.gd
## Exits 0 with "ALL CHECKS PASSED" on success, 1 with a description of the first failure.

const MainScript = preload("res://scripts/Main.gd")
const A3DFormat = preload("res://scripts/A3DFormat.gd")

var failures: Array[String] = []


func check_true(condition: bool, message: String) -> void:
	if not condition:
		failures.append(message)


func check_close(actual: float, expected: float, message: String) -> void:
	if abs(actual - expected) > 0.0001:
		failures.append("%s (expected %s, got %s)" % [message, str(expected), str(actual)])


func _initialize() -> void:
	var main := MainScript.new()
	get_root().add_child(main)
	main._ready()
	# A real frame needs to pass before Camera3D.current registration (and other in-tree-only
	# state) is actually established -- calling _ready() manually above doesn't drive the engine's
	# own per-frame processing, and unproject_position() below needs that to have happened. Same
	# underlying class of issue as the earlier "not inside tree" bugs this test caught.
	await process_frame

	# --- spawn ---
	main.spawn_block()
	main.spawn_sphere()
	check_true(main.scene_objects.size() == 2, "expected 2 spawned objects")
	check_true(main.selected_index == 1, "expected the just-spawned sphere (index 1) to be selected")
	check_close(main.scene_objects[0].position.x, 0.0, "block should spawn at x=0")
	check_close(main.scene_objects[1].position.x, 2.0, "sphere should spawn at x=2 (auto-offset by spawn order)")

	var block_faces: int = A3DFormat._mesh_to_dict(main.scene_objects[0].mesh).get("Faces", []).size()
	check_true(block_faces == 12, "block should have 12 triangular faces, got %d" % block_faces)

	# --- select + move ---
	main._select(0)
	check_true(main.selected_index == 0, "expected selection to move to index 0")
	main._move_selected(Vector3(0, 1.5, 0))
	check_close(main.scene_objects[0].position.y, 1.5, "selected block should move up by 1.5")

	# --- tab-cycle ---
	main._cycle_selection()
	check_true(main.selected_index == 1, "Tab should cycle selection from 0 to 1")

	# --- export ---
	var export_path := "/tmp/arco3d_godot_test_output.a3d"
	var export_error := A3DFormat.export_asset(main.scene_root, "GodotTestScene", export_path)
	check_true(export_error == OK, "export_asset should succeed, got error %d" % export_error)
	check_true(FileAccess.file_exists(export_path), "exported file should exist on disk")

	# --- import into a fresh scene root and verify round-trip ---
	var result := A3DFormat.import_asset(export_path)
	check_true(result.get("Ok", false), "import_asset should succeed: %s" % str(result.get("Error", "")))
	if result.get("Ok", false):
		var imported_root: Node3D = result["Root"]
		var children := imported_root.get_children()
		check_true(children.size() == 2, "expected 2 imported children, got %d" % children.size())
		if children.size() == 2:
			check_close(children[0].position.x, 0.0, "imported block x")
			check_close(children[0].position.y, 1.5, "imported block y (must survive the earlier move)")
			check_close(children[1].position.x, 2.0, "imported sphere x")
			var imported_block_faces: int = A3DFormat._mesh_to_dict(children[0].mesh).get("Faces", []).size()
			check_true(imported_block_faces == 12, "imported block should still have 12 faces, got %d" % imported_block_faces)
		imported_root.queue_free()

	# --- malformed input handling ---
	var missing_result := A3DFormat.import_asset("/tmp/arco3d_godot_test_does_not_exist.a3d")
	check_true(not missing_result.get("Ok", true), "importing a missing file should fail cleanly, not throw")

	var bad_file := FileAccess.open("/tmp/arco3d_godot_test_corrupt.a3d", FileAccess.WRITE)
	bad_file.store_string("not json at all {{{")
	bad_file.close()
	var corrupt_result := A3DFormat.import_asset("/tmp/arco3d_godot_test_corrupt.a3d")
	check_true(not corrupt_result.get("Ok", true), "importing a corrupt file should fail cleanly, not throw")

	# --- selection handles (TinkerCad-style height/rotate/scale handles) ---
	main._select(0)
	check_true(main.selection_handles != null, "selecting an object should build its handles")
	check_true(main.scale_handle_nodes.size() == 4, "expected 4 scale corner handles, got %d" % main.scale_handle_nodes.size())
	var block_aabb: AABB = main.scene_objects[0].mesh.get_aabb()
	var expected_top_y: float = main.scene_objects[0].position.y + block_aabb.size.y * main.scene_objects[0].scale.y / 2.0 + main.HANDLE_OFFSET
	check_close(main.height_handle_node.position.y, expected_top_y, "height handle should sit above the block's actual top")
	check_true(main.face_handle_nodes.size() == 4, "expected 4 extrude/face handles (+-X, +-Z), got %d" % main.face_handle_nodes.size())

	# --- extrude (FACE drag): pull the +X face out by exactly 1 world unit ---
	# Precise, deterministic test of the actual math (not just that a raycast can find a handle,
	# which needs real hardware/physics ticks to verify meaningfully) -- computes the EXACT screen
	# position a 1-unit move along +X would project to, using the same camera the app itself uses,
	# then feeds that straight into _update_face_drag and checks the resulting size/position.
	var block := main.scene_objects[0]
	var start_scale_x: float = block.scale.x
	var start_position: Vector3 = block.position
	var start_half_extent: float = block_aabb.size.x * start_scale_x / 2.0
	var drag_start_screen := main.camera.unproject_position(block.position + Vector3(start_half_extent, 0, 0))
	main._begin_handle_drag({"mode": main.DragMode.FACE, "target": block, "face_axis": 0, "face_sign": 1.0}, drag_start_screen)
	var one_unit_further_screen := main.camera.unproject_position(block.position + Vector3(start_half_extent + 1.0, 0, 0))
	main._update_face_drag(one_unit_further_screen)
	# Expected new half-extent is start_half_extent + 1.0 (0.5 + 1.0 = 1.5), so new full size 3.0
	# and scale.x 3.0/1.0 = 3.0; the anchor (opposite, -X face) stays fixed, so center shifts by
	# the change in half-extent, +1.0. Tolerance is looser than check_close's default here on
	# purpose: _project_drag_onto_axis is a linear screen-space approximation of a perspective
	# projection, which is only exactly linear in the limit of small drags -- real interactive use
	# only ever applies many small per-frame deltas (a few pixels each), where this error is
	# negligible, but this test's single synthetic 1-unit jump is much larger than any real mouse
	# delta between frames, so it exercises the approximation's worst case on purpose.
	var scale_error := absf(block.scale.x - 3.0)
	check_true(scale_error < 0.15, "extruding +X by 1 unit should grow scale.x to ~3.0, got %s (perspective-projection approximation error should stay under 0.15)" % str(block.scale.x))
	var position_error := absf(block.position.x - (start_position.x + 1.0))
	check_true(position_error < 0.15, "extruding +X by 1 unit should shift center by ~+1.0 (opposite face stays fixed), got %s" % str(block.position.x))
	check_close(block.position.z, start_position.z, "extruding +X must not move the object along Z")
	main._end_drag()

	# --- new primitives: Cylinder/Cone/Wedge ---
	main.spawn_cylinder()
	var cylinder: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	check_true(cylinder.mesh is CylinderMesh, "spawn_cylinder should produce a CylinderMesh")

	main.spawn_cone()
	var cone: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	check_true(cone.mesh is CylinderMesh and (cone.mesh as CylinderMesh).top_radius == 0.0, "spawn_cone should produce a CylinderMesh with a zero top radius")

	main.spawn_wedge()
	var wedge: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	var wedge_faces: int = A3DFormat._mesh_to_dict(wedge.mesh).get("Faces", []).size()
	check_true(wedge_faces == 8, "wedge should have 8 triangular faces (a right-triangular prism), got %d" % wedge_faces)
	# Real round-trip check on the hand-derived winding/geometry, not just a face count -- export
	# and reimport the wedge alone and confirm the vertex positions survive exactly.
	var wedge_export_path := "/tmp/arco3d_godot_test_wedge.a3d"
	var wedge_root := Node3D.new()
	wedge_root.add_child(wedge.duplicate())
	check_true(A3DFormat.export_asset(wedge_root, "WedgeTest", wedge_export_path) == OK, "wedge export should succeed")
	var wedge_import_result := A3DFormat.import_asset(wedge_export_path)
	check_true(wedge_import_result.get("Ok", false), "wedge import should succeed")
	if wedge_import_result.get("Ok", false):
		var imported_wedge: MeshInstance3D = wedge_import_result["Root"].get_child(0)
		var imported_wedge_verts: Array = A3DFormat._mesh_to_dict(imported_wedge.mesh).get("Vertices", [])
		check_true(imported_wedge_verts.size() == 6, "wedge should round-trip with all 6 vertices, got %d" % imported_wedge_verts.size())
		wedge_import_result["Root"].queue_free()
	wedge_root.queue_free()

	# --- duplicate ---
	var objects_before_duplicate := main.scene_objects.size()
	main._select(0) # the original block
	var original_block := main.scene_objects[0]
	var duplicated_block := main._duplicate_selected()
	check_true(duplicated_block != null, "duplicating the selected object should return the new copy")
	check_true(main.scene_objects.size() == objects_before_duplicate + 1, "duplicate should add exactly one object")
	check_true(main.selected_index == main.scene_objects.size() - 1, "the new duplicate should become the selection")
	check_close(duplicated_block.position.x, original_block.position.x + main.GRID_SNAP * 2.0, "duplicate should default-offset from the original")
	check_true(int(duplicated_block.get_meta("arco_id")) != int(original_block.get_meta("arco_id")), "duplicate must get its own stable id, not share the original's")
	# The exact bug this function's own header comment warns about: duplicate()'s copied Area3D
	# child metadata still pointing at the ORIGINAL node unless explicitly fixed up.
	for child in duplicated_block.get_children():
		if child is Area3D and child.has_meta("arco_target"):
			check_true(child.get_meta("arco_target") == duplicated_block, "duplicated object's picking collider must target itself, not the original")

	# --- modifier stack: Mirror ---
	# Verified against get_meta("computed_mesh_data") -- Main's own pre-rebuild source-of-truth
	# dict for whatever's currently displayed -- rather than by re-extracting arrays from the
	# actual rebuilt .mesh via A3DFormat._mesh_to_dict(). Real, confirmed reason: _mesh_from_dict's
	# SurfaceTool-based rebuild pipeline welds coincident vertex positions (correct, expected
	# behavior for real mesh construction -- shared vertices are exactly what a real mesh should
	# have), so a mirrored *symmetric* box's two vertex sets legitimately collapse back down to the
	# same 8 unique positions once rebuilt, which is correct mesh output but makes raw
	# post-rebuild vertex counts meaningless as a way to verify the MODIFIER math itself. Face
	# count is unaffected by welding and stays a reliable check either way.
	main.spawn_block()
	var mirror_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	var base_data: Dictionary = mirror_target.get_meta("base_mesh_data")
	var base_vertex_count: int = (base_data["Vertices"] as Array).size()
	var base_face_count: int = (base_data["Faces"] as Array).size()
	await main._add_modifier(mirror_target, {"type": main.ModifierOp.MIRROR, "axis": "X", "enabled": true})
	var mirrored_data: Dictionary = mirror_target.get_meta("computed_mesh_data")
	check_true((mirrored_data["Vertices"] as Array).size() == base_vertex_count * 2, "Mirror should double the raw vertex count (pre-rebuild)")
	check_true((mirrored_data["Faces"] as Array).size() == base_face_count * 2, "Mirror should double the face count")
	# Disabling should cleanly recover the ORIGINAL base geometry -- the whole point of this being
	# non-destructive rather than baked.
	await main._toggle_modifier(mirror_target, 0)
	var disabled_data: Dictionary = mirror_target.get_meta("computed_mesh_data")
	check_true((disabled_data["Vertices"] as Array).size() == base_vertex_count, "disabling Mirror should fully restore the un-mirrored vertex count")

	# --- modifier stack: Array ---
	main.spawn_block()
	var array_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	var array_base_data: Dictionary = array_target.get_meta("base_mesh_data")
	var array_base_vertex_count: int = (array_base_data["Vertices"] as Array).size()
	await main._add_modifier(array_target, {"type": main.ModifierOp.ARRAY, "count": 3, "offset": Vector3(2, 0, 0), "enabled": true})
	var arrayed_data: Dictionary = array_target.get_meta("computed_mesh_data")
	check_true((arrayed_data["Vertices"] as Array).size() == array_base_vertex_count * 3, "Array ×3 should triple the raw vertex count (pre-rebuild)")
	# The third copy should sit +4 units from the original along X (2 copies * 2.0 offset). Checked
	# against the REBUILT mesh's own AABB here (not raw vertex indexing into the pre-rebuild dict)
	# since that's a welding-independent, real-world-meaningful measurement of the actual result.
	var arrayed_aabb: AABB = array_target.mesh.get_aabb()
	check_close(arrayed_aabb.size.x, 5.0, "3 copies of a 1-unit block offset by 2 units each should span 5 units total (1 + 2 + 2)")

	# --- modifier stack: Boolean (real CSG subtraction, awaited through its 1-frame bake) ---
	main.spawn_block()
	var boolean_base: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	boolean_base.position = Vector3(50, 0, 0) # keep well clear of every other object in the scene
	main.spawn_sphere()
	var boolean_cutter: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	boolean_cutter.position = boolean_base.position # overlapping, so the subtraction actually cuts something
	var pre_boolean_data: Dictionary = boolean_base.get_meta("base_mesh_data")
	var pre_boolean_face_count: int = (pre_boolean_data["Faces"] as Array).size()
	await main._add_modifier(boolean_base, {"type": main.ModifierOp.BOOLEAN, "operation": CSGShape3D.OPERATION_SUBTRACTION, "target_id": int(boolean_cutter.get_meta("arco_id")), "enabled": true})
	var post_boolean_data: Dictionary = boolean_base.get_meta("computed_mesh_data")
	var post_boolean_face_count: int = (post_boolean_data["Faces"] as Array).size()
	check_true(post_boolean_face_count > 0, "Boolean subtraction should produce real, non-empty geometry")
	check_true(post_boolean_face_count != pre_boolean_face_count, "Boolean subtraction should actually change the geometry when the cutter genuinely overlaps")
	check_true(not boolean_cutter.visible, "the Boolean operand should hide itself once consumed by the modifier (Blender's own convention)")
	# Removing the modifier should both restore the original block AND un-hide the operand.
	await main._remove_modifier(boolean_base, 0)
	var restored_data: Dictionary = boolean_base.get_meta("computed_mesh_data")
	check_true((restored_data["Faces"] as Array).size() == pre_boolean_face_count, "removing the Boolean modifier should restore the pre-cut face count")
	check_true(boolean_cutter.visible, "removing the Boolean modifier should restore the operand's visibility")

	# --- Apply Modifiers: bake a Boolean-built kitbash into a real, fresh, riggable base mesh ---
	# The real reason this exists: a Boolean modifier's CSG rebake produces an entirely new vertex
	# ordering with no correspondence to the ORIGINAL base_mesh_data indices, so a rig computed
	# against pre-Boolean base_mesh_data would garble deformation once the bake actually runs
	# (unlike Mirror/Array, which at least preserve the original indices for their first N
	# vertices). Re-adds the SAME cutter (now un-hidden and reusable after the removal above).
	await main._add_modifier(boolean_base, {"type": main.ModifierOp.BOOLEAN, "operation": CSGShape3D.OPERATION_SUBTRACTION, "target_id": int(boolean_cutter.get_meta("arco_id")), "enabled": true})
	var pre_apply_computed: Dictionary = (boolean_base.get_meta("computed_mesh_data") as Dictionary).duplicate(true)
	var pre_apply_vertex_count: int = (pre_apply_computed["Vertices"] as Array).size()
	var applied := await main._apply_modifiers_permanently(boolean_base)
	check_true(applied, "_apply_modifiers_permanently should succeed on an object with real modifiers and no rig")
	check_true((main._get_modifiers(boolean_base) as Array).is_empty(), "applying should clear the modifier stack")
	var post_apply_base: Dictionary = boolean_base.get_meta("base_mesh_data")
	# Welding can only ever REDUCE the vertex count (merging exact-duplicate positions) -- never
	# increase it or drop below what the CSG bake actually produced minus real duplicates, so this
	# is a real, meaningful bound, not a loose "something happened" check.
	check_true((post_apply_base["Vertices"] as Array).size() <= pre_apply_vertex_count, "the applied base mesh's vertex count should be at most the pre-apply baked count (welding only ever merges)")
	check_true((post_apply_base["Vertices"] as Array).size() > 0, "the applied base mesh should have real geometry, not be empty")
	var post_apply_computed: Dictionary = boolean_base.get_meta("computed_mesh_data")
	check_true((post_apply_computed["Faces"] as Array).size() == (pre_apply_computed["Faces"] as Array).size(), "applying modifiers should not visually change the geometry -- same face count immediately after")

	# A SECOND apply with an empty modifier list should refuse cleanly, not error or double-bake.
	check_true(await main._apply_modifiers_permanently(boolean_base) == false, "applying with no modifiers left on the stack should refuse")

	# Now this baked mesh can be rigged as ONE coherent object -- weights should cover every real
	# vertex of the COMBINED (post-Boolean) mesh, not just whatever the pre-Boolean block alone had.
	main._select(main.scene_objects.find(boolean_base))
	await main._add_humanoid_rig()
	var post_rig_weights: Array = boolean_base.get_meta("arco_vertex_bone_index", [])
	check_true(post_rig_weights.size() == (post_apply_base["Vertices"] as Array).size(), "rigging after Apply Modifiers should weight every vertex of the COMBINED baked mesh")

	# Applying modifiers on an object that ALREADY has a rig must be refused -- it would silently
	# invalidate weights computed against the base_mesh_data that's about to be replaced. Add a
	# real modifier back first so this actually exercises the RIG guard specifically, not just the
	# already-proven "empty modifier list" guard from a moment ago.
	await main._add_modifier(boolean_base, {"type": main.ModifierOp.MIRROR, "axis": "X", "enabled": true})
	check_true(await main._apply_modifiers_permanently(boolean_base) == false, "applying modifiers must be refused once a rig already exists, even with a real modifier present")

	# --- Mount points and attachment: "a specific system for mount points... automatic parenting"
	# (the user's own explicit ask) -- object-level (vehicle/turret) case first ---
	main.spawn_block()
	var vehicle: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	vehicle.position = Vector3(80, 0, 0) # keep well clear of every other object in the scene
	main._select(main.scene_objects.find(vehicle))
	main._add_mount_point(vehicle, "Male")
	var vehicle_mounts: Array = vehicle.get_meta("arco_mount_points")
	check_true(vehicle_mounts.size() == 1, "adding a mount point should append exactly one entry")
	check_true(vehicle_mounts[0]["Gender"] == "Male", "the vehicle's mount point should be Male as requested")
	vehicle_mounts[0]["Type"] = "Turret"
	vehicle_mounts[0]["LocalPosition"] = [0.0, 0.6, 0.0] # on the roof
	var vehicle_mount_name: String = vehicle_mounts[0]["Name"]

	main.spawn_cylinder()
	var turret: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	turret.position = Vector3(200, 5, 0) # nowhere near the vehicle -- attach should move it there
	main._select(main.scene_objects.find(turret))
	main._add_mount_point(turret, "Female")
	var turret_mounts: Array = turret.get_meta("arco_mount_points")
	turret_mounts[0]["Type"] = "Turret"
	var turret_mount_name: String = turret_mounts[0]["Name"]

	# Real validation, not just bookkeeping: Male-to-Male AND Female-to-Female must both be
	# refused -- checked as two SEPARATE cases (not just one and assuming symmetry), since the
	# user asked directly whether the male/female counterparting was "properly" done.
	check_true(main._validate_mount_attachment(vehicle_mounts[0], vehicle_mounts[0]).is_empty() == false, "attaching a mount point to another of the SAME gender (Male-Male) must be refused")
	check_true(main._validate_mount_attachment(turret_mounts[0], turret_mounts[0]).is_empty() == false, "attaching a mount point to another of the SAME gender (Female-Female) must be refused")
	# Mismatched Type must also be refused, even with opposite genders.
	var mismatched_type_mount := {"Name": "X", "Gender": "Female", "Type": "Hand"}
	check_true(main._validate_mount_attachment(mismatched_type_mount, vehicle_mounts[0]).is_empty() == false, "attaching mismatched Types (Hand vs Turret) must be refused even with opposite genders")

	# A real gap found by directly re-checking this after the user's own question ("are the
	# mounting systems male-female counterparted properly?"): _add_mount_point's own `gender`
	# parameter was never validated to actually BE "Male" or "Female" -- the two UI buttons always
	# pass a hardcoded literal, so they were never at risk, but ArcoBASIC's own ADD_MOUNT_POINT
	# command passes user-authored script text straight through. Two mount points created with
	# garbage genders like "Foo"/"Bar" would have satisfied _validate_mount_attachment's own
	# `child_gender == parent_gender` inequality test and been allowed to attach, silently
	# defeating the whole real-connector-pairing guarantee. Verified fixed: _add_mount_point now
	# refuses anything else and reports it via its own return value, not just a status message.
	var mount_count_before_invalid := main._get_mount_points(vehicle).size()
	check_true(main._add_mount_point(vehicle, "Foo") == false, "_add_mount_point must refuse a gender that isn't exactly 'Male' or 'Female'")
	check_true(main._get_mount_points(vehicle).size() == mount_count_before_invalid, "a refused _add_mount_point call must NOT append a broken/half-created mount point")
	check_true(main._add_mount_point(vehicle, "male") == false, "_add_mount_point must refuse a case-mismatched gender too -- exactly 'Male'/'Female', not a loose match")

	# Same real check through the ACTUAL ArcoBASIC dispatch path, not just the underlying function
	# directly -- confirms the exact scenario the gap description above describes (a script
	# supplying an invalid gender) is really, fully blocked end-to-end, including that no mount
	# handle gets registered for the caller to (uselessly) reference afterward.
	var script_mount_handles: Dictionary = {}
	var script_material_handles: Dictionary = {}
	var script_spawn_handles: Dictionary = {0: vehicle}
	main._process_script_command("ADD_MOUNT_POINT 0 Bogus", script_spawn_handles, script_mount_handles, script_material_handles)
	check_true(script_mount_handles.is_empty(), "the ArcoBASIC ADD_MOUNT_POINT command must not register a handle when the gender argument is invalid")
	check_true(main._get_mount_points(vehicle).size() == mount_count_before_invalid, "the ArcoBASIC ADD_MOUNT_POINT command must not create a mount point when the gender argument is invalid")

	var attach_ok := main._attach_object(turret, turret_mount_name, vehicle, vehicle_mount_name)
	check_true(attach_ok, "a valid opposite-gender, matching-type attachment should succeed")
	check_true(turret.has_meta("arco_attachment"), "a successfully attached object should carry arco_attachment metadata")
	# Real, precise expected position: the vehicle's own mount point sits at local (0, 0.6, 0), so
	# with the vehicle at world (80,0,0) and no rotation, the turret should land at exactly (80, 0.6, 0).
	check_close(turret.position.x, 80.0, "attached turret should snap to the vehicle's mount point X")
	check_close(turret.position.y, 0.6, "attached turret should snap to the vehicle's mount point Y")
	check_close(turret.position.z, 0.0, "attached turret should snap to the vehicle's mount point Z")

	# Live propagation: moving/rotating the PARENT should carry the attached child along -- the
	# actual real-time behavior this whole feature is for, not just a one-time snap.
	vehicle.position += Vector3(10.0, 0.0, 5.0)
	main._propagate_all_attachments()
	check_close(turret.position.x, 90.0, "moving the vehicle should carry the attached turret along (X)")
	check_close(turret.position.z, 5.0, "moving the vehicle should carry the attached turret along (Z)")
	vehicle.rotation_degrees.y = 90.0
	main._propagate_all_attachments()
	# The mount point sits at local (0, 0.6, 0) -- directly above the vehicle's own origin, so a
	# pure Y rotation shouldn't move it at all (rotating a point ON the rotation axis is a no-op) --
	# a real, precise check that orientation composes correctly, not just position.
	check_close(turret.position.x, 90.0, "a Y-axis vehicle rotation shouldn't move a mount point sitting ON that same axis (X)")
	check_close(turret.position.z, 5.0, "a Y-axis vehicle rotation shouldn't move a mount point sitting ON that same axis (Z)")
	vehicle.rotation_degrees.y = 0.0
	main._propagate_all_attachments()

	# Cycle safety: attaching the vehicle (now the turret's own parent) back onto the turret must
	# be refused, not silently accepted into an oscillating loop.
	main._add_mount_point(vehicle, "Female")
	var vehicle_mounts_2: Array = vehicle.get_meta("arco_mount_points")
	vehicle_mounts_2[1]["Type"] = "Turret"
	check_true(main._would_create_attachment_cycle(vehicle, turret), "attaching the vehicle onto its own child (the turret) should be detected as a cycle")

	# Detach: the child should stop following, staying exactly where it last was.
	main._detach_object(turret)
	check_true(not turret.has_meta("arco_attachment"), "detaching should remove the attachment metadata")
	var turret_position_after_detach: Vector3 = turret.position
	vehicle.position += Vector3(50.0, 0.0, 0.0)
	main._propagate_all_attachments()
	check_close(turret.position.x, turret_position_after_detach.x, "a detached object must NOT keep following its former parent")

	# Removing a mount point should detach anything still using it (re-attach first to set this up).
	main._attach_object(turret, turret_mount_name, vehicle, vehicle_mount_name)
	check_true(turret.has_meta("arco_attachment"), "re-attach for the removal test should succeed")
	var vehicle_mount_index := main._find_mount_point_index(vehicle, vehicle_mount_name)
	main._remove_mount_point(vehicle, vehicle_mount_index)
	check_true(not turret.has_meta("arco_attachment"), "removing a mount point should detach anything that was using it")

	# --- Import merge ("kit parts"): "I plan on making a bunch of kit parts so I can rapidly make
	# new creations without needing to model everything every time" -- the user's own explicit ask,
	# right after confirming mount points/attachment were real. The plain "Import .a3d..." path
	# always REPLACES the whole scene (confirmed directly by reading _on_import_path_chosen before
	# building any of this), which makes a reusable parts library impossible -- you could never
	# bring a separately-saved arm/leg/engine into a scene that already has a body in it. This tests
	# the real merge path (_on_import_merge_path_chosen / _finish_import / the ArcoBASIC IMPORT
	# command), and specifically the id-rebase it needs: every export session numbers its own
	# objects starting from 0, so two independently-modeled kit parts are extremely likely to reuse
	# the exact same low arco_id values -- left unrebased, that collision would make
	# _find_object_by_id resolve an Attachment's ParentId (a plain linear scan, first match wins) to
	# the WRONG object the very first time it happened. Constructed here as a DELIBERATE, EXACT
	# collision rather than left to chance, so this test actually proves the fix, not just that
	# nothing broke by coincidence.
	var kit_root := main.spawn_block()
	kit_root.position = Vector3(400, 0, 0) # well clear of every other object already in the scene
	main._select(main.scene_objects.find(kit_root))
	main._add_mount_point(kit_root, "Female")
	var kit_root_mount_name: String = main._get_mount_points(kit_root).back()["Name"]

	var kit_child := main.spawn_cylinder()
	kit_child.position = Vector3(500, 0, 0)
	main._select(main.scene_objects.find(kit_child))
	main._add_mount_point(kit_child, "Male")
	var kit_child_mount_name: String = main._get_mount_points(kit_child).back()["Name"]
	check_true(main._attach_object(kit_child, kit_child_mount_name, kit_root, kit_root_mount_name), "the standalone kit file's own two objects should attach to each other before export")

	# Export just these two objects to their own standalone file, standing in for a SEPARATE
	# session's own kit part -- export_asset walks whatever Node3D it's given, not main.scene_root
	# specifically, so a throwaway root here is a legitimate independent "other file".
	var kit_scene_root := Node3D.new()
	main.scene_root.remove_child(kit_root)
	main.scene_root.remove_child(kit_child)
	kit_scene_root.add_child(kit_root)
	kit_scene_root.add_child(kit_child)
	main.scene_objects.erase(kit_root)
	main.scene_objects.erase(kit_child)
	var kit_path := "/tmp/arco3d_godot_test_kit_part.a3d"
	check_true(A3DFormat.export_asset(kit_scene_root, "KitPart", kit_path) == OK, "the standalone kit part export should succeed")
	kit_scene_root.queue_free()

	# Force a deliberate, exact id collision -- WITHOUT winding next_arco_id backward, which would
	# corrupt every OTHER test section sharing this same live `main` instance for the rest of the
	# suite. Instead, spawn a real object the normal way (a real, currently-unique id) and rewrite
	# the exported kit file's own Id to match it exactly -- a genuine, real collision at whatever
	# point the id space actually is right now, not a hand-picked small number.
	var colliding_object := main.spawn_block()
	colliding_object.position = Vector3(600, 0, 0)
	var colliding_id := int(colliding_object.get_meta("arco_id"))

	var kit_file_read := FileAccess.open(kit_path, FileAccess.READ)
	var kit_manifest = JSON.parse_string(kit_file_read.get_as_text())
	kit_file_read.close()
	var kit_children: Array = kit_manifest["Asset"]["Root"]["Children"]
	check_true(kit_children.size() == 2, "the standalone kit file should contain exactly the 2 exported objects")
	var kit_root_original_id := int(kit_root.get_meta("arco_id"))
	var kit_child_original_id := int(kit_child.get_meta("arco_id"))
	for c in kit_children:
		if int(c.get("Id", -1)) == kit_root_original_id:
			c["Id"] = colliding_id
		else:
			c["Attachment"]["ParentId"] = colliding_id
	var kit_file_write := FileAccess.open(kit_path, FileAccess.WRITE)
	kit_file_write.store_string(JSON.stringify(kit_manifest, "  "))
	kit_file_write.close()

	var id_offset_at_merge := main.next_arco_id # captured BEFORE the merge, same as _finish_import's own id_offset
	var scene_object_count_before_merge := main.scene_objects.size()

	await main._on_import_merge_path_chosen(kit_path)

	check_true(main.scene_objects.size() == scene_object_count_before_merge + 2, "merge-import should ADD the kit file's 2 objects, not replace the scene")
	check_true(main.scene_objects.has(colliding_object), "merge-import must leave everything already in the scene alone")
	var merged_root: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 2]
	var merged_child: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	check_true(int(colliding_object.get_meta("arco_id")) == colliding_id, "the pre-existing object's own id must be untouched by the merge")
	check_true(int(merged_root.get_meta("arco_id")) == colliding_id + id_offset_at_merge, "the imported root's colliding Id should be rebased by the pre-import next_arco_id offset")
	check_true(int(merged_child.get_meta("arco_id")) == kit_child_original_id + id_offset_at_merge, "the imported child's own Id should be rebased by the same offset")
	check_true(merged_child.has_meta("arco_attachment"), "the imported attachment metadata should survive the merge")
	var merged_attachment: Dictionary = merged_child.get_meta("arco_attachment")
	check_true(int(merged_attachment.get("ParentId", -1)) == colliding_id + id_offset_at_merge, "the imported Attachment's ParentId (originally the colliding id) must be rebased too, so it still points at the imported root and NOT the pre-existing object that really owns the un-rebased id")
	# The real, behavioral proof, not just a numeric assertion: id-based lookup must resolve each id
	# to the DISTINCT, correct object -- this is exactly the ambiguity an unrebased import would have
	# silently produced (both a real scene object and an imported one claiming the same id).
	check_true(main._find_object_by_id(colliding_id) == colliding_object, "the colliding id must still resolve to the pre-existing object, not the imported one")
	check_true(main._find_object_by_id(colliding_id + id_offset_at_merge) == merged_root, "the rebased id must resolve to the imported root")
	var expected_next_id_from_file := maxi(colliding_id, kit_child_original_id) + 1 # kit_root's Id became colliding_id; kit_child's own Id was left unchanged
	check_true(main.next_arco_id == id_offset_at_merge + expected_next_id_from_file, "next_arco_id should advance to the pre-import offset plus the file's own NextId (max original Id + 1)")

	# The ArcoBASIC side of the same feature: IMPORT <path> / ImportKitPart, dispatched exactly like
	# a real automation script would use it -- the actual thing that makes a kit-parts LIBRARY
	# practical (script "attach today's chosen turret/arm/leg" rather than clicking through a dialog
	# every time).
	var kit_spawn_handles: Dictionary = {}
	var kit_mount_handles: Dictionary = {}
	var kit_material_handles: Dictionary = {}
	var scene_object_count_before_script_import := main.scene_objects.size()
	await main._process_script_command("IMPORT " + kit_path, kit_spawn_handles, kit_mount_handles, kit_material_handles)
	check_true(main.scene_objects.size() == scene_object_count_before_script_import + 2, "the ArcoBASIC IMPORT command should merge both of the kit file's own objects into the live scene")
	check_true(kit_spawn_handles.size() == 1, "IMPORT should register exactly one script handle (the file's first top-level object), matching the documented one-handle-per-call scoping limit")
	check_true(kit_spawn_handles.get(0) is MeshInstance3D, "the registered IMPORT handle should resolve to a real object")

	# --- Mount point constraints: "rotation axis, degrees allowed" (the user's own direct
	# follow-up ask, right after mount points/attachment themselves landed) -- a real turret
	# traversing on its own vehicle mount, not just a fully rigid weld. Fresh objects, not reusing
	# `vehicle`/`turret` above, so this section doesn't depend on their residual post-mutation state.
	main.spawn_block()
	var turret_base: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	turret_base.position = Vector3(160, 0, 0)
	main._select(main.scene_objects.find(turret_base))
	main._add_mount_point(turret_base, "Male")
	var turret_base_mounts: Array = turret_base.get_meta("arco_mount_points")
	turret_base_mounts[0]["Type"] = "Turret"
	turret_base_mounts[0]["LocalPosition"] = [0.0, 0.5, 0.0]
	turret_base_mounts[0]["ConstraintAxis"] = "Y"
	turret_base_mounts[0]["ConstraintMinDegrees"] = -45.0
	turret_base_mounts[0]["ConstraintMaxDegrees"] = 90.0
	var turret_base_mount_name: String = turret_base_mounts[0]["Name"]

	main.spawn_cylinder()
	var gun: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	gun.position = Vector3(500, 5, 0) # nowhere near the turret base -- attach should move it there
	main._select(main.scene_objects.find(gun))
	main._add_mount_point(gun, "Female")
	var gun_mounts: Array = gun.get_meta("arco_mount_points")
	gun_mounts[0]["Type"] = "Turret" # LocalPosition left at its own default [0,0,0] deliberately --
	var gun_mount_name: String = gun_mounts[0]["Name"] # the gun's own mount point IS its own origin

	check_true(main._attach_object(gun, gun_mount_name, turret_base, turret_base_mount_name), "constrained attachment should still succeed like any other")
	var gun_position_at_rest: Vector3 = gun.position
	check_close(gun_position_at_rest.x, 160.0, "the gun should still snap exactly onto the mount point at JointAngleDegrees=0 (X)")
	check_close(gun_position_at_rest.y, 0.5, "the gun should still snap exactly onto the mount point at JointAngleDegrees=0 (Y)")
	check_close(gun.rotation_degrees.y, 0.0, "the gun should start unrotated at JointAngleDegrees=0")

	# Real rotation: since the gun's own mount point sits exactly at its own origin, the origin
	# itself doesn't move under a pure rotation about that same point (correct, not a bug -- a
	# point ON the rotation axis pivot never moves) -- what SHOULD change is the gun's own
	# ORIENTATION, verified directly against Godot's own Euler decomposition of a pure single-axis
	# Y rotation (unambiguous, no compound-rotation-order concern for this simple case).
	main._set_attachment_joint_angle(gun, 90.0)
	check_close(gun.position.x, gun_position_at_rest.x, "rotating a constrained joint should not move the gun's own origin, since it sits exactly on the pivot (X)")
	check_close(gun.position.z, gun_position_at_rest.z, "rotating a constrained joint should not move the gun's own origin, since it sits exactly on the pivot (Z)")
	check_close(gun.rotation_degrees.y, 90.0, "setting the joint angle to 90 should rotate the gun's own orientation by exactly 90 degrees around Y")

	# Real clamping, not just a UI hint: requesting well past the allowed range should be clamped.
	main._set_attachment_joint_angle(gun, 999.0)
	check_close((gun.get_meta("arco_attachment") as Dictionary)["JointAngleDegrees"], 90.0, "requesting an angle past ConstraintMaxDegrees should clamp to the max, not store the raw value")
	main._set_attachment_joint_angle(gun, -999.0)
	check_close((gun.get_meta("arco_attachment") as Dictionary)["JointAngleDegrees"], -45.0, "requesting an angle past ConstraintMinDegrees should clamp to the min, not store the raw value")

	# Real regression proof: an UNCONSTRAINED attachment (ConstraintAxis "None", the default) must
	# stay fully rigid even if something -- e.g. a stale JointAngleDegrees left over from an old
	# file -- is sitting in its own attachment data. This is the exact backward-compatibility
	# guarantee the whole constraints feature depends on: every mount point that existed before it
	# keeps behaving identically.
	main._set_attachment_joint_angle(gun, -45.0) # back to a known state
	turret_base_mounts[0]["ConstraintAxis"] = "None"
	(gun.get_meta("arco_attachment") as Dictionary)["JointAngleDegrees"] = 45.0 # simulate stale data
	main._propagate_attachments_from(turret_base)
	check_close(gun.rotation_degrees.y, 0.0, "an unconstrained (ConstraintAxis None) mount must stay fully rigid (zero rotation) regardless of any stored JointAngleDegrees")

	# --- A3D round trip: constraint fields + JointAngleDegrees survive export/import ---
	turret_base_mounts[0]["ConstraintAxis"] = "Y" # restore for the round-trip check
	main._set_attachment_joint_angle(gun, 30.0)
	var constraint_export_root := Node3D.new()
	var constraint_export_path := "/tmp/arco3d_godot_test_constraint.a3d"
	constraint_export_root.add_child(turret_base.duplicate())
	constraint_export_root.add_child(gun.duplicate())
	check_true(A3DFormat.export_asset(constraint_export_root, "ConstraintTest", constraint_export_path) == OK, "constraint export should succeed")
	var constraint_import_result := A3DFormat.import_asset(constraint_export_path)
	check_true(constraint_import_result.get("Ok", false), "constraint import should succeed: %s" % str(constraint_import_result.get("Error", "")))
	if constraint_import_result.get("Ok", false):
		var constraint_import_root: Node3D = constraint_import_result["Root"]
		var imported_turret_base: MeshInstance3D = null
		var imported_gun: MeshInstance3D = null
		for child in constraint_import_root.get_children():
			if child.has_meta("arco_mount_points") and (child.get_meta("arco_mount_points") as Array).size() > 0 and not child.has_meta("arco_attachment"):
				imported_turret_base = child
			elif child.has_meta("arco_attachment"):
				imported_gun = child
		check_true(imported_turret_base != null and imported_gun != null, "both the turret base and the gun should round-trip")
		if imported_turret_base != null:
			var imported_mount: Dictionary = (imported_turret_base.get_meta("arco_mount_points") as Array)[0]
			check_true(imported_mount["ConstraintAxis"] == "Y", "the round-tripped mount point should keep its ConstraintAxis")
			check_close(float(imported_mount["ConstraintMinDegrees"]), -45.0, "the round-tripped mount point should keep its ConstraintMinDegrees")
			check_close(float(imported_mount["ConstraintMaxDegrees"]), 90.0, "the round-tripped mount point should keep its ConstraintMaxDegrees")
		if imported_gun != null:
			check_close(float((imported_gun.get_meta("arco_attachment") as Dictionary)["JointAngleDegrees"]), 30.0, "the round-tripped attachment should keep its JointAngleDegrees")
		constraint_import_root.queue_free()
	constraint_export_root.queue_free()

	# --- Mount points: bone-attached case (the "weapon hand hold" ask specifically) ---
	main.spawn_block()
	var character: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	character.position = Vector3(120, 0, 0)
	main._select(main.scene_objects.find(character))
	await main._add_humanoid_rig()
	var character_bones: Array = (character.get_meta("arco_rig") as Dictionary)["Bones"]
	var right_hand_index := 10 # RightHand, per HUMANOID_RIG_PRESET
	main._toggle_pose_mode() # real Pose Mode entry, not just poking the state variables directly
	main.selected_rig_joint_index = right_hand_index # same effect as Shift+Tab-cycling to it
	main._add_mount_point(character, "Female")
	var character_mounts: Array = character.get_meta("arco_mount_points")
	var hand_mount: Dictionary = character_mounts[character_mounts.size() - 1]
	check_true(int(hand_mount["BoneIndex"]) == right_hand_index, "a mount point added while RightHand is selected in Pose Mode should attach to that bone")
	hand_mount["Type"] = "Hand"
	var hand_mount_name: String = hand_mount["Name"]
	main._toggle_pose_mode() # back to Select, don't leave test state in a mode later sections don't expect

	main.spawn_cylinder()
	var weapon: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	weapon.position = Vector3(500, 5, 0)
	main._select(main.scene_objects.find(weapon))
	main._add_mount_point(weapon, "Male")
	var weapon_mounts: Array = weapon.get_meta("arco_mount_points")
	weapon_mounts[0]["Type"] = "Hand"
	var weapon_mount_name: String = weapon_mounts[0]["Name"]

	check_true(main._attach_object(weapon, weapon_mount_name, character, hand_mount_name), "weapon should attach to the character's hand mount point")
	var hand_world_before_pose: Vector3 = weapon.position

	# The real point of a BONE-attached mount point: posing the arm should carry the weapon along,
	# exactly like it carries the character's own weighted vertices -- verified against the exact
	# same _world_pose_transform math the rest of this app's own FK already uses, not eyeballed.
	character_bones[8]["PoseRotationDegrees"] = [0.0, 0.0, 45.0] # RightShoulder
	await main._recompute_modifiers(character)
	main._propagate_all_attachments()
	var expected_hand_local: Vector3 = main._world_pose_transform(character_bones, right_hand_index) * main._vec3_from_data(character_bones[right_hand_index]["BindPosition"])
	var expected_hand_world: Vector3 = character.transform * expected_hand_local
	check_close(weapon.position.x, expected_hand_world.x, "posing the arm should carry the attached weapon to the hand's real posed world position (X)")
	check_close(weapon.position.y, expected_hand_world.y, "posing the arm should carry the attached weapon to the hand's real posed world position (Y)")
	check_close(weapon.position.z, expected_hand_world.z, "posing the arm should carry the attached weapon to the hand's real posed world position (Z)")
	check_true(weapon.position.x != hand_world_before_pose.x or weapon.position.y != hand_world_before_pose.y or weapon.position.z != hand_world_before_pose.z, "the weapon should have actually visibly moved when the arm was posed, not stayed frozen at the bind pose")

	# --- A3D round trip: mount points + attachment survive export/import ---
	var mount_export_root := Node3D.new()
	var mount_export_path := "/tmp/arco3d_godot_test_mount.a3d"
	mount_export_root.add_child(character.duplicate())
	mount_export_root.add_child(weapon.duplicate())
	check_true(A3DFormat.export_asset(mount_export_root, "MountTest", mount_export_path) == OK, "mount point export should succeed")
	var mount_import_result := A3DFormat.import_asset(mount_export_path)
	check_true(mount_import_result.get("Ok", false), "mount point import should succeed: %s" % str(mount_import_result.get("Error", "")))
	if mount_import_result.get("Ok", false):
		var mount_import_root: Node3D = mount_import_result["Root"]
		var imported_character: MeshInstance3D = null
		var imported_weapon: MeshInstance3D = null
		for child in mount_import_root.get_children():
			if child.has_meta("arco_rig"):
				imported_character = child
			else:
				imported_weapon = child
		check_true(imported_character != null and imported_weapon != null, "both the character and the weapon should round-trip")
		if imported_character != null:
			var imported_mounts: Array = imported_character.get_meta("arco_mount_points", [])
			check_true(imported_mounts.size() == 1, "the character's own hand mount point should round-trip")
			if not imported_mounts.is_empty():
				check_true(int(imported_mounts[0]["BoneIndex"]) == right_hand_index, "the round-tripped mount point's BoneId->BoneIndex resolution should still point at RightHand")
		if imported_weapon != null:
			check_true(imported_weapon.has_meta("arco_attachment"), "the weapon's attachment should round-trip")
		mount_import_root.queue_free()
	mount_export_root.queue_free()

	# --- Material layers: "a layered smart material system... paint/apply image and slide it
	# around... add decals... use parts of a texture... instead of unwrapping it" (the user's own
	# explicit ask, referencing their own RFC-0050/A3M design) ---
	# Real test images, not placeholders -- two small, distinct, solid-color PNGs written to real
	# files on disk, exactly the way a user's own "+ Add Layer..." file picker would load them.
	var red_image := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	red_image.fill(Color(1, 0, 0, 1))
	var red_image_path := "/tmp/arco3d_test_material_red.png"
	red_image.save_png(red_image_path)
	var blue_image := Image.create(4, 4, false, Image.FORMAT_RGBA8)
	blue_image.fill(Color(0, 0, 1, 1))
	var blue_image_path := "/tmp/arco3d_test_material_blue.png"
	blue_image.save_png(blue_image_path)

	main.spawn_block()
	var material_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	main._select(main.scene_objects.find(material_target))

	# A material-less object must still get the ORIGINAL flat StandardMaterial3D -- this whole
	# feature is purely additive, and this is the real regression proof for that claim.
	check_true(material_target.material_override is StandardMaterial3D, "an object with no material layers should keep the original flat StandardMaterial3D")

	main._add_material_layer(material_target, red_image_path)
	check_true(main._get_material_layers(material_target).size() == 1, "adding a layer should append exactly one entry")
	check_true(material_target.material_override is ShaderMaterial, "an object with a real material layer should get the real layered ShaderMaterial")
	var shader_material: ShaderMaterial = material_target.material_override
	check_true(shader_material.shader == main.LAYERED_MATERIAL_SHADER, "the assigned material should use the real layered_material.gdshader, not a different/default shader")

	var enabled_param: Array = shader_material.get_shader_parameter("layer_enabled")
	check_true(enabled_param[0] == true, "the first (and only) layer should be enabled in the shader's own uniform array")
	for i in range(1, main.MAX_MATERIAL_LAYERS):
		check_true(enabled_param[i] == false, "unused layer slot %d should be disabled, not garbage/leftover state" % i)
	var mapping_param: Array = shader_material.get_shader_parameter("layer_mapping")
	check_true(int(mapping_param[0]) == main.MATERIAL_MAPPING_NAMES.find("Triplanar"), "a freshly added layer should default to Triplanar mapping")

	# Add a second layer and verify real per-layer transform math ("slide it around") actually
	# reaches the shader -- not just that SOME uniform got set.
	main._add_material_layer(material_target, blue_image_path)
	check_true(main._get_material_layers(material_target).size() == 2, "adding a second layer should append exactly one more entry")
	var second_layer: Dictionary = main._get_material_layers(material_target)[1]
	second_layer["Offset"] = [1.0, 2.0, 3.0]
	main._apply_object_material(material_target, true)
	var transform_param: Array = shader_material.get_shader_parameter("layer_inverse_transform")
	var expected_inverse: Transform3D = main._material_layer_transform(second_layer).affine_inverse()
	check_close(transform_param[1].origin.x, expected_inverse.origin.x, "a layer's Offset should reach the shader as the exact inverse-transform origin (X)")
	check_close(transform_param[1].origin.y, expected_inverse.origin.y, "a layer's Offset should reach the shader as the exact inverse-transform origin (Y)")
	check_close(transform_param[1].origin.z, expected_inverse.origin.z, "a layer's Offset should reach the shader as the exact inverse-transform origin (Z)")

	# Selection highlighting must still work for a material-bearing object -- via the shader's own
	# uniform, not a material swap (which would have clobbered the real layer data).
	main._select(main.scene_objects.find(material_target))
	check_true(shader_material.get_shader_parameter("selection_highlight") == true, "the selected material-bearing object's shader should have selection_highlight enabled")
	main.spawn_block() # select something else
	check_true(shader_material.get_shader_parameter("selection_highlight") == false, "a no-longer-selected material-bearing object's shader should have selection_highlight disabled")
	main._select(main.scene_objects.find(material_target))

	# Reordering: moving layer 1 up should swap it with layer 0.
	var first_layer_name_before: String = main._get_material_layers(material_target)[0]["Name"]
	var second_layer_name_before: String = main._get_material_layers(material_target)[1]["Name"]
	main._move_material_layer(material_target, 1, -1)
	check_true(main._get_material_layers(material_target)[0]["Name"] == second_layer_name_before, "moving layer 1 up should swap it into slot 0")
	check_true(main._get_material_layers(material_target)[1]["Name"] == first_layer_name_before, "moving layer 1 up should swap the original slot-0 layer down to slot 1")

	# Real cap enforcement: MAX_MATERIAL_LAYERS is a real hard limit, not just a UI suggestion.
	main._add_material_layer(material_target, red_image_path)
	main._add_material_layer(material_target, red_image_path)
	check_true(main._get_material_layers(material_target).size() == main.MAX_MATERIAL_LAYERS, "should be able to fill up to exactly MAX_MATERIAL_LAYERS")
	main._add_material_layer(material_target, red_image_path)
	check_true(main._get_material_layers(material_target).size() == main.MAX_MATERIAL_LAYERS, "adding a layer past MAX_MATERIAL_LAYERS should be refused, not silently overflow")

	# Remove: should drop back to the original 2 layers and free the removed one's own texture.
	while main._get_material_layers(material_target).size() > 2:
		main._remove_material_layer(material_target, main._get_material_layers(material_target).size() - 1)
	check_true(main._get_material_layers(material_target).size() == 2, "removing the extra layers should leave exactly the original 2")

	# A real, failing image path should refuse cleanly, not add a broken/textureless layer.
	main._add_material_layer(material_target, "/tmp/arco3d_test_material_does_not_exist.png")
	check_true(main._get_material_layers(material_target).size() == 2, "attempting to add a layer from a nonexistent image path should be refused, not add a broken entry")

	# --- A3D round trip: real embedded image bytes + all layer parameters survive export/import ---
	var material_export_root := Node3D.new()
	var material_export_path := "/tmp/arco3d_godot_test_material.a3d"
	material_export_root.add_child(material_target.duplicate())
	check_true(A3DFormat.export_asset(material_export_root, "MaterialTest", material_export_path) == OK, "material export should succeed")
	# Confirm the actual embedded bytes are on disk, not just that SOME file was produced.
	var material_file := FileAccess.open(material_export_path, FileAccess.READ)
	var material_json := JSON.new()
	material_json.parse(material_file.get_as_text())
	material_file.close()
	var material_manifest: Dictionary = material_json.data
	check_true((material_manifest.get("OptionalCapabilities", []) as Array).has("material.layers"), "manifest should declare material.layers since this asset really has one")
	var material_component: Dictionary = ((material_manifest["Asset"] as Dictionary)["Root"] as Dictionary)["Children"][0]
	check_true(material_component.has("Material"), "exported component should carry a real Material block")
	check_true((material_component["Material"]["Layers"] as Array).size() == 2, "exported Material block should have exactly 2 layers")
	check_true(not String(material_component["Material"]["Layers"][0]["ImageData"]).is_empty(), "each exported layer should carry real, non-empty embedded image data")

	var material_import_result := A3DFormat.import_asset(material_export_path)
	check_true(material_import_result.get("Ok", false), "material import should succeed: %s" % str(material_import_result.get("Error", "")))
	if material_import_result.get("Ok", false):
		var material_import_root: Node3D = material_import_result["Root"]
		var imported_material_object: MeshInstance3D = material_import_root.get_child(0)
		var imported_layers: Array = main._get_material_layers(imported_material_object)
		check_true(imported_layers.size() == 2, "the round-tripped object should still have exactly 2 layers")
		var imported_textures: Dictionary = imported_material_object.get_meta("arco_material_textures", {})
		check_true(imported_textures.size() == 2, "the round-tripped object should have exactly 2 real decoded textures")
		if not imported_layers.is_empty():
			var imported_texture: ImageTexture = imported_textures.get(int(imported_layers[0]["Id"]))
			check_true(imported_texture != null, "the round-tripped first layer should have a real, non-null decoded texture")
			if imported_texture != null:
				var imported_pixel: Color = imported_texture.get_image().get_pixel(0, 0)
				# The two source images were pure blue and pure red -- after the reorder above, slot
				# 0 should be whichever one was blue, a real, exact pixel-level round-trip check, not
				# just "some image decoded to something."
				check_true(imported_pixel.b > 0.9 and imported_pixel.r < 0.1, "the round-tripped image's actual pixel data should match the original blue source image exactly, got %s" % imported_pixel)
		main._apply_object_material(imported_material_object, false) # proves a reimported material is immediately usable, not just data
		check_true(imported_material_object.material_override is ShaderMaterial, "a reimported material-bearing object should be immediately usable as a real ShaderMaterial")
		material_import_root.queue_free()
	material_export_root.queue_free()

	# --- ArcoBASIC automation script (real end-to-end: runs a real arcosh subprocess, not a mock) ---
	var arcosh_path := main._find_arcosh_path()
	check_true(not arcosh_path.is_empty(), "arcosh should be found (arco3d/build/arcosh, this repo's own dev-time convention)")
	if not arcosh_path.is_empty():
		var objects_before_script := main.scene_objects.size()
		# grid_of_blocks.abas writes a relative path ("arco3d_scripted_grid.a3d"), which resolves
		# against the subprocess's own CWD -- set to scripting/ itself by _on_run_script_chosen's
		# shell wrapper (see that function's own comment on why: ArcoBASIC's #IMPORT needs it).
		var scripted_export_path := ProjectSettings.globalize_path("res://scripting/arco3d_scripted_grid.a3d")
		if FileAccess.file_exists(scripted_export_path):
			DirAccess.remove_absolute(scripted_export_path)
		var script_path := ProjectSettings.globalize_path("res://scripting/examples/grid_of_blocks.abas")
		await main._on_run_script_chosen(script_path)
		# The example script spawns 9 blocks (a 3x3 grid) + 1 sphere with a Mirror modifier, then
		# exports. All three real, separate effects checked -- not just "did something get added".
		check_true(main.scene_objects.size() == objects_before_script + 10, "grid_of_blocks.abas should spawn exactly 10 objects (9 blocks + 1 sphere), got %d new" % (main.scene_objects.size() - objects_before_script))
		# Identifying this object by .name (or by .mesh's class, once AddMirror below has replaced
		# it with a rebuilt ArrayMesh -- real, by-design behavior of _recompute_modifiers, not a
		# bug) both turned out unreliable here: by this point in a long cumulative test run,
		# several earlier sections have already spawned their own same-named "Sphere"/"Block"
		# objects into the same scene_root, and Godot auto-renames colliding sibling names --
		# harmless in real use, but it means neither name nor mesh class is a robust way for THIS
		# test to confirm "the script's last spawn was really the sphere". The modifier-count check
		# right below is the actually meaningful assertion: it confirms AddMirror's handle
		# correctly resolved to whatever object was really spawned last.
		var scripted_sphere: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
		check_true(main._get_modifiers(scripted_sphere).size() == 1, "the script's AddMirror call should have added exactly one modifier")
		check_true(FileAccess.file_exists(scripted_export_path), "the script's ExportScene call should have written a real file")
		if FileAccess.file_exists(scripted_export_path):
			var reimport := A3DFormat.import_asset(scripted_export_path)
			check_true(reimport.get("Ok", false), "the scripted export should itself be a valid, re-importable .a3d file")
			if reimport.get("Ok", false):
				reimport["Root"].queue_free()
			DirAccess.remove_absolute(scripted_export_path) # don't leave a test artifact in the source tree

	# --- ArcoBASIC automation: Rig/Mount/Material commands (the user's own direct question, "is
	# this all being connected to the ArcoBASIC layer?" -- answered honestly at the time: no, only
	# the original 8 commands existed) -- real end-to-end, same real arcosh subprocess as above,
	# not a mock, and not just calling _process_script_command directly (that would only prove the
	# GODOT side's own dispatch logic, not that arco3d_api.abas is even valid, running ArcoBASIC).
	if not arcosh_path.is_empty():
		var rig_mount_material_image_path := "/tmp/arco3d_test_material_for_script.png"
		var script_test_image := Image.create(2, 2, false, Image.FORMAT_RGBA8)
		script_test_image.fill(Color(0, 1, 0, 1))
		script_test_image.save_png(rig_mount_material_image_path)

		var objects_before_rmm_script := main.scene_objects.size()
		var rmm_export_path := ProjectSettings.globalize_path("res://scripting/arco3d_scripted_rig_mount_material.a3d")
		if FileAccess.file_exists(rmm_export_path):
			DirAccess.remove_absolute(rmm_export_path)
		var rmm_script_path := ProjectSettings.globalize_path("res://scripting/examples/rig_mount_material_demo.abas")
		await main._on_run_script_chosen(rmm_script_path)

		check_true(main.scene_objects.size() == objects_before_rmm_script + 2, "rig_mount_material_demo.abas should spawn exactly 2 objects (character + prop)")
		var scripted_character: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 2]
		var scripted_prop: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]

		# Rig + pose: AddHumanoidRig + SetPose(character, 5, 30, 0, 0).
		check_true(scripted_character.has_meta("arco_rig"), "AddHumanoidRig should have attached a real rig")
		if scripted_character.has_meta("arco_rig"):
			var scripted_bones: Array = (scripted_character.get_meta("arco_rig") as Dictionary)["Bones"]
			check_true(scripted_bones.size() == main.HUMANOID_RIG_PRESET.size(), "the scripted rig should be the full Humanoid preset")
			check_close(float(scripted_bones[5]["PoseRotationDegrees"][0]), 30.0, "SetPose should have posed LeftShoulder (bone 5) at exactly 30 degrees around X")

		# Mount points + type: AddMountPoint + SetMountType on both objects.
		var scripted_character_mounts: Array = main._get_mount_points(scripted_character)
		var scripted_prop_mounts: Array = main._get_mount_points(scripted_prop)
		check_true(scripted_character_mounts.size() == 1 and scripted_character_mounts[0]["Type"] == "Hand", "the character's mount point should exist with Type set to 'Hand'")
		check_true(scripted_prop_mounts.size() == 1 and scripted_prop_mounts[0]["Type"] == "Hand", "the prop's mount point should exist with Type set to 'Hand'")

		# Constraint: SetMountConstraint(characterMount, "Y", -45, 90) -- on the CHARACTER's own
		# mount, the real PARENT side of the Attach call below, since a mount's own constraint
		# describes what an attached child is allowed to do, not a property of the child itself.
		check_true(scripted_character_mounts[0]["ConstraintAxis"] == "Y", "SetMountConstraint should have set the character's own mount ConstraintAxis to Y")
		check_close(float(scripted_character_mounts[0]["ConstraintMinDegrees"]), -45.0, "SetMountConstraint should have set ConstraintMinDegrees")
		check_close(float(scripted_character_mounts[0]["ConstraintMaxDegrees"]), 90.0, "SetMountConstraint should have set ConstraintMaxDegrees")

		# Attachment: Attach(propMount, characterMount) -- the prop should now be a real, live-
		# tracked child of the character's own mount point.
		check_true(scripted_prop.has_meta("arco_attachment"), "Attach should have made the prop a real attached child of the character")
		if scripted_prop.has_meta("arco_attachment"):
			var scripted_attachment: Dictionary = scripted_prop.get_meta("arco_attachment")
			check_true(int(scripted_attachment.get("ParentId", -1)) == int(scripted_character.get_meta("arco_id")), "the prop's attachment should point at the character as its real parent")
			check_close(float(scripted_attachment.get("JointAngleDegrees", 0.0)), 20.0, "SetJointAngle should have stored exactly 20 degrees (within the -45..90 constraint, so unclamped)")
			# Real, visible proof the constraint isn't just stored data -- the prop's own actual
			# orientation should reflect a real 20-degree rotation around Y, the same Euler-
			# decomposition check this session's own UI-level constraint tests already used.
			check_close(scripted_prop.rotation_degrees.y, 20.0, "the prop's own real orientation should reflect the 20-degree Y rotation SetJointAngle applied, not just the stored attachment data")

		# Material layer: AddMaterialLayer + SetLayerMapping/Transform/Crop/Blend, all on the
		# character. Real pixel-level proof the image actually loaded, not just "a layer exists".
		var scripted_layers: Array = main._get_material_layers(scripted_character)
		check_true(scripted_layers.size() == 1, "AddMaterialLayer should have added exactly one real layer")
		if not scripted_layers.is_empty():
			check_true(scripted_layers[0]["Mapping"] == "Box", "SetLayerMapping should have set the layer's own Mapping to Box")
			check_true(scripted_layers[0]["BlendMode"] == "Normal", "SetLayerBlend should have set the layer's own BlendMode")
			check_close(float(scripted_layers[0]["Opacity"]), 1.0, "SetLayerBlend should have set the layer's own Opacity")
			var scripted_textures: Dictionary = scripted_character.get_meta("arco_material_textures", {})
			var scripted_texture: ImageTexture = scripted_textures.get(int(scripted_layers[0]["Id"]))
			check_true(scripted_texture != null, "the scripted material layer should have a real, loaded texture, not a failed/empty one")
			if scripted_texture != null:
				var scripted_pixel: Color = scripted_texture.get_image().get_pixel(0, 0)
				check_true(scripted_pixel.g > 0.9 and scripted_pixel.r < 0.1, "the scripted layer's real texture should match the actual green test image, got %s" % scripted_pixel)
		check_true(scripted_character.material_override is ShaderMaterial, "a scripted material layer should produce the same real ShaderMaterial the UI path does")

		check_true(FileAccess.file_exists(rmm_export_path), "the script's own ExportScene call should have written a real file")
		if FileAccess.file_exists(rmm_export_path):
			DirAccess.remove_absolute(rmm_export_path)
		DirAccess.remove_absolute(rig_mount_material_image_path)

	# --- Real, confirmed handle-placement bug fixed while building vertex editing ---
	# Transform3D.basis columns already carry the object's own scale (confirmed directly: a
	# scale=(2,1,1) node's basis.x has length 2, not 1) -- several existing handle-position
	# calculations multiplied a basis column by an ALSO-separately-scaled half_extent, applying
	# scale TWICE. Verified fixed here with exact expected math, not just "some value changed".
	main.spawn_block()
	var scale_bug_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	scale_bug_target.position = Vector3(100, 0, 0) # clear of every other object in this long test
	scale_bug_target.scale = Vector3(2, 1, 1)
	main._select(main.scene_objects.size() - 1)
	var scale_bug_aabb: AABB = scale_bug_target.mesh.get_aabb()
	var expected_move_x: float = scale_bug_target.position.x + scale_bug_aabb.size.x / 2.0 * scale_bug_target.scale.x + main.HANDLE_OFFSET
	check_close(main.move_x_handle_node.position.x, expected_move_x, "move-X handle should sit at the real scaled surface plus a FIXED offset, not scale doubled")

	# --- Vertex editing (Edit Mode): the actual "PS1 quality geometry... no external mouse" ask ---
	main.spawn_block()
	var edit_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	var edit_target_index := main.scene_objects.size() - 1
	main._select(edit_target_index)
	check_true(main.current_tool == main.ToolMode.SELECT, "should start in Select tool before entering Edit Mode")

	main._toggle_edit_mode()
	check_true(main.current_tool == main.ToolMode.EDIT_VERTICES, "Tab should enter Edit Mode for a real MeshInstance3D with base_mesh_data")
	var block_base_data: Dictionary = edit_target.get_meta("base_mesh_data")
	var block_vertex_count: int = (block_base_data["Vertices"] as Array).size()
	check_true(main.vertex_handle_nodes.size() == block_vertex_count, "Edit Mode should show exactly one handle per real vertex, got %d for %d vertices" % [main.vertex_handle_nodes.size(), block_vertex_count])

	# Keyboard-only vertex selection + nudge -- the actual no-external-mouse requirement.
	main._cycle_selected_vertex()
	check_true(main.selected_vertex_index == 0, "Shift+Tab in Edit Mode should select the first vertex")
	var vertices_before_nudge: Array = block_base_data["Vertices"].duplicate(true)
	# +2 (not +1) deliberately moves this corner well PAST the block's own opposite bound (+0.5)
	# -- a smaller nudge risks landing exactly on another already-existing corner's own coordinate
	# (a real trap this test hit once already: BoxMesh's 8 corners share a lot of coincident X/Y/Z
	# values, so a +1 move from -0.5 landed exactly on +0.5, leaving the AABB unchanged and making
	# the "did this visibly grow the mesh" check below meaningless by coincidence, not by a bug).
	await main._nudge_selected_vertex(Vector3(2, 0, 0))
	var vertex_after_nudge: Array = block_base_data["Vertices"][0]
	var vertex_before_nudge: Array = vertices_before_nudge[0]
	check_close(float(vertex_after_nudge[0]), float(vertex_before_nudge[0]) + 2.0, "nudging the selected vertex +X should move exactly that vertex's local X by +2 (object is unrotated/unscaled here)")
	check_close(float(vertex_after_nudge[1]), float(vertex_before_nudge[1]), "nudging along X must not move Y")
	check_close(float(vertex_after_nudge[2]), float(vertex_before_nudge[2]), "nudging along X must not move Z")

	# The displayed mesh must actually reflect the edit -- not just the underlying data dict. Also
	# real, direct proof that base_mesh_data's own vertices are now WELDED (one shared vertex per
	# geometric corner, not the 3 separate per-face duplicates BoxMesh itself produces) -- editing
	# only ONE array entry visibly moves the WHOLE corner (all faces meeting there), instead of
	# tearing the mesh open by moving just one of several duplicate corner points.
	check_true(block_vertex_count == 8, "a Block's welded base_mesh_data should have exactly 8 unique corners, not BoxMesh's own raw 24 (3 duplicates per corner, one per adjacent face)")
	var edited_mesh_aabb: AABB = edit_target.mesh.get_aabb()
	check_true(edited_mesh_aabb.size.x > scale_bug_aabb.size.x, "moving a corner vertex outward should visibly grow the displayed mesh's own bounding box")

	# Cycling to a second vertex and nudging it must NOT disturb the first vertex's own edit --
	# real proof each vertex is independently addressable, not all aliasing the same data.
	main._cycle_selected_vertex()
	check_true(main.selected_vertex_index == 1, "Shift+Tab should advance to the next vertex")
	await main._nudge_selected_vertex(Vector3(0, 5, 0))
	var vertex0_after_second_edit: Array = block_base_data["Vertices"][0]
	check_close(float(vertex0_after_second_edit[0]), float(vertex_after_nudge[0]), "editing vertex 1 must not disturb vertex 0's own already-applied edit")

	# --- Extrude (the actual "add extrude" ask) ---
	# Fresh block, well clear of every other object in this long cumulative test. Edit Mode is
	# ALREADY active from the vertex-editing section above and was never explicitly exited --
	# _select() below just moves the SAME open edit session onto this new object (a real,
	# once-real test-structure mistake found here: an earlier draft called _toggle_edit_mode()
	# again at this point, which actually EXITED edit mode instead of entering it, since it was
	# already active -- _toggle_edit_mode() only checks whether edit mode is currently active
	# globally, not which object was active when it was entered).
	main.spawn_block()
	var extrude_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	extrude_target.position = Vector3(200, 0, 0)
	main._select(main.scene_objects.size() - 1)
	check_true(main.current_tool == main.ToolMode.EDIT_VERTICES, "Edit Mode should still be active from the vertex-editing section above")
	var extrude_base_data: Dictionary = extrude_target.get_meta("base_mesh_data")
	var faces_before_extrude: int = (extrude_base_data["Faces"] as Array).size()
	var vertices_before_extrude: int = (extrude_base_data["Vertices"] as Array).size()
	main._cycle_selected_mesh_face()
	check_true(main.selected_mesh_face_index == 0, "Shift+Tab with no vertex selected should select the first mesh face")
	var aabb_before_extrude: AABB = extrude_target.mesh.get_aabb()

	await main._apply_extrude(main.DEFAULT_EXTRUDE_DISTANCE)
	check_true((extrude_base_data["Vertices"] as Array).size() == vertices_before_extrude + 3, "extrude should add exactly 3 new cap vertices")
	check_true((extrude_base_data["Faces"] as Array).size() == faces_before_extrude + 6, "extrude should add exactly 6 new side-wall triangles (the original face slot is reused for the new cap, not counted twice)")
	var aabb_after_extrude: AABB = extrude_target.mesh.get_aabb()
	check_true(aabb_after_extrude.size.length() > aabb_before_extrude.size.length(), "extruding a face outward should visibly grow the mesh's own bounding box")
	check_true(main.selected_mesh_face_index == 0, "the extruded face's own selection should survive the handle rebuild (same array index, now the new cap)")

	# --- Inset ---
	var faces_before_inset: int = (extrude_base_data["Faces"] as Array).size()
	var vertices_before_inset: int = (extrude_base_data["Vertices"] as Array).size()
	await main._apply_inset(main.DEFAULT_INSET_RATIO)
	check_true((extrude_base_data["Vertices"] as Array).size() == vertices_before_inset + 3, "inset should add exactly 3 new inner vertices")
	check_true((extrude_base_data["Faces"] as Array).size() == faces_before_inset + 6, "inset should add exactly 6 new ring triangles")

	# --- Delete face ---
	var faces_before_face_delete: int = (extrude_base_data["Faces"] as Array).size()
	await main._delete_edit_selection()
	check_true((extrude_base_data["Faces"] as Array).size() == faces_before_face_delete - 1, "deleting the selected face should remove exactly one face")
	check_true(main.selected_mesh_face_index == -1, "deleting a face should clear the selection")

	# --- Delete vertex (real re-indexing check, not just a count) ---
	main._cycle_selected_vertex()
	var deleted_vertex_index: int = main.selected_vertex_index
	var vertex_count_before_vertex_delete: int = (extrude_base_data["Vertices"] as Array).size()
	# A face referencing an index AFTER the one being deleted, to directly verify re-indexing --
	# find any surviving face with an index > deleted_vertex_index before the delete.
	var face_with_higher_index: Array = []
	for f in (extrude_base_data["Faces"] as Array):
		if f[0] != deleted_vertex_index and f[1] != deleted_vertex_index and f[2] != deleted_vertex_index:
			for idx in f:
				if int(idx) > deleted_vertex_index:
					face_with_higher_index = f.duplicate()
					break
		if not face_with_higher_index.is_empty():
			break
	await main._delete_edit_selection()
	check_true((extrude_base_data["Vertices"] as Array).size() == vertex_count_before_vertex_delete - 1, "deleting the selected vertex should remove exactly one vertex")
	check_true(main.selected_vertex_index == -1, "deleting a vertex should clear the selection")
	if not face_with_higher_index.is_empty():
		var still_present := false
		for f in (extrude_base_data["Faces"] as Array):
			for idx in f:
				if int(idx) < 0 or int(idx) >= (extrude_base_data["Vertices"] as Array).size():
					still_present = true # a real, direct proof re-indexing produced an in-bounds index
		check_true(not still_present, "every remaining face index must stay in-bounds after re-indexing around the deleted vertex")

	# --- Refuse to delete down to zero faces (a real guard, not a crash) ---
	main.spawn_block()
	var guard_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	guard_target.position = Vector3(300, 0, 0)
	# Strip it down to a single triangle directly, to reach the guarded edge case without 11 clicks.
	var guard_base_data: Dictionary = guard_target.get_meta("base_mesh_data")
	guard_base_data["Faces"] = [guard_base_data["Faces"][0]]
	check_true(main._delete_mesh_face(guard_base_data, 0) == false, "deleting the last remaining face must be refused, not leave a zero-face mesh")
	check_true((guard_base_data["Faces"] as Array).size() == 1, "the refused delete must leave the face count unchanged")

	# --- Edge selection, Cut, and Bevel: "we need a place to switch selection type between
	# face/edge/vertex for editing... loop cuts and a knife tool... bevelling, corner to rounded
	# corner" -- the user's own explicit ask. Edit Mode is still active from the section above.
	main.spawn_block()
	var edge_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	edge_target.position = Vector3(3, 0, 0) # small offset, still well within the camera's fixed 8-unit orbit distance -- needed below for a REAL raycast, unlike every other object in this file which just uses a big offset for pure spatial isolation
	main._select(main.scene_objects.size() - 1)
	check_true(main.current_tool == main.ToolMode.EDIT_VERTICES, "Edit Mode should still be active for the edge/bevel section")
	var edge_base_data: Dictionary = edge_target.get_meta("base_mesh_data")
	var edge_list := main._compute_edges(edge_base_data)
	check_true(edge_list.size() == 18, "a welded cube (8 verts, 12 triangles) should have exactly 18 unique edges (12 real cube edges + 6 per-quad-face diagonals -- Euler's formula V-E+F=2 confirms: 8-18+12=2), got %d" % edge_list.size())
	check_true(main.edge_handle_nodes.size() == 18, "Edit Mode should show exactly one handle per real edge")

	# Selection-mode switch: only ONE handle set is actually visible/clickable at a time.
	check_true(main.edit_select_mode == main.EditSelectionMode.VERTEX, "should start in Vertex selection mode by default")
	check_true(main.vertex_handle_nodes[0].visible, "vertex handles should be visible in Vertex selection mode")
	check_true(not main.edge_handle_nodes[0].visible, "edge handles should be hidden while in Vertex selection mode")
	main._set_edit_selection_mode(main.EditSelectionMode.EDGE)
	check_true(main.edge_handle_nodes[0].visible, "edge handles should become visible after switching to Edge selection mode")
	check_true(not main.vertex_handle_nodes[0].visible, "vertex handles should hide once Edge selection mode is active")
	check_true(not main.mesh_face_handle_nodes[0].visible, "face handles should also stay hidden in Edge selection mode")

	# Keyboard-only edge selection (Shift+Tab), matching vertex/face's own existing convention.
	main._cycle_selected_edge()
	check_true(main.selected_edge_index == 0, "Shift+Tab in Edge selection mode should select the first edge")

	# A REAL, previously-existing bug found and fixed while building this: a real mouse click (via
	# the ACTUAL _raycast_handle_at -> _begin_handle_drag path, not the direct hand-built-dict
	# shortcut every other test in this file uses) on a vertex/edge/face/rig/mount handle used to
	# always resolve to index -1, since _raycast_handle_at only ever forwarded "face_axis"/
	# "face_sign" and silently dropped every OTHER handle kind's own extra meta -- see that
	# function's own comment. Proven fixed here via the real raycast entry point end-to-end.
	main.camera_target = edge_target.position
	main._update_camera()
	await physics_frame # a freshly-built collision shape needs one physics step to become raycastable
	var raycast_edge_screen: Vector2 = main.camera.unproject_position(main.edge_handle_nodes[0].position)
	var raycast_edge_info := main._raycast_handle_at(raycast_edge_screen)
	check_true(raycast_edge_info.get("mode") == main.DragMode.EDGE, "a real raycast at an edge handle's own screen position should actually hit an edge handle")
	check_true(raycast_edge_info.get("target") == edge_target, "the real raycast should resolve to the correct target object")
	check_true(raycast_edge_info.has("edge_a") and raycast_edge_info.has("edge_b") and raycast_edge_info.has("edge_index"), "the real raycast path must forward an edge handle's own edge_a/edge_b/edge_index meta, not just face_axis/face_sign")
	main._set_edit_selection_mode(main.EditSelectionMode.VERTEX)
	var raycast_vertex_screen: Vector2 = main.camera.unproject_position(main.vertex_handle_nodes[0].position)
	var raycast_vertex_info := main._raycast_handle_at(raycast_vertex_screen)
	check_true(raycast_vertex_info.get("mode") == main.DragMode.VERTEX, "a real raycast at a vertex handle's own screen position should actually hit a vertex handle")
	check_true(raycast_vertex_info.has("vertex_index"), "the real raycast path must forward a vertex handle's own vertex_index meta too -- this exact gap silently broke real mouse-driven vertex/face/rig/mount editing before this fix, not just edges")
	main._set_edit_selection_mode(main.EditSelectionMode.EDGE)
	main._cycle_selected_edge()
	check_true(main.selected_edge_index == 0, "Shift+Tab in Edge selection mode should select the first edge")

	# --- Cut Edge (the real "loop cut" primitive -- see _cut_edge's own header comment for the
	# honest single-edge-subdivide scoping, not a full quad-ring-propagating loop cut) ---
	var faces_before_cut: int = (edge_base_data["Faces"] as Array).size()
	var vertices_before_cut: int = (edge_base_data["Vertices"] as Array).size()
	var cut_edge_entry: Dictionary = edge_list[0]
	check_true(cut_edge_entry["faces"].size() == 2, "an interior cube edge should be shared by exactly 2 triangles")
	var cut_aabb_before: AABB = edge_target.mesh.get_aabb()
	await main._apply_cut_edge()
	check_true((edge_base_data["Vertices"] as Array).size() == vertices_before_cut + 1, "cutting an interior edge should add exactly 1 new midpoint vertex")
	check_true((edge_base_data["Faces"] as Array).size() == faces_before_cut + 2, "cutting an interior edge (2 adjacent triangles, each split into 2) should add exactly 2 net faces")
	check_true(main.selected_edge_index == -1, "cutting an edge should clear the edge selection -- the original edge no longer exists as a single edge")
	var cut_aabb_after: AABB = edge_target.mesh.get_aabb()
	check_true(cut_aabb_after.size.is_equal_approx(cut_aabb_before.size), "cutting an edge is a pure subdivision -- it must not change the mesh's own visible shape/bounding box")

	# --- Delete Edge ---
	var edge_list_before_delete := main._compute_edges(edge_base_data)
	var delete_target_edge: Dictionary = edge_list_before_delete[0]
	var faces_before_edge_delete: int = (edge_base_data["Faces"] as Array).size()
	var expected_faces_removed: int = delete_target_edge["faces"].size()
	check_true(main._delete_edge(edge_base_data, delete_target_edge["a"], delete_target_edge["b"]), "deleting a real, currently-existing edge should succeed")
	check_true((edge_base_data["Faces"] as Array).size() == faces_before_edge_delete - expected_faces_removed, "deleting an edge should remove exactly the faces that shared it, no more and no fewer")

	# --- Bevel Vertex ("corner to rounded corner") -- a fresh block, since the delete above already
	# perforated edge_target's own mesh in a way that would make the neighbor-ring math below harder
	# to state cleanly by hand.
	main.spawn_block()
	var bevel_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	bevel_target.position = Vector3(3, 0, 2)
	main._select(main.scene_objects.size() - 1)
	var bevel_base_data: Dictionary = bevel_target.get_meta("base_mesh_data")
	var bevel_vertex_index := 0
	var incident_face_count := 0
	for f in (bevel_base_data["Faces"] as Array):
		if f[0] == bevel_vertex_index or f[1] == bevel_vertex_index or f[2] == bevel_vertex_index:
			incident_face_count += 1
	var vertices_before_bevel: int = (bevel_base_data["Vertices"] as Array).size()
	var faces_before_bevel: int = (bevel_base_data["Faces"] as Array).size()
	check_true(main._bevel_vertex(bevel_base_data, bevel_vertex_index, main.DEFAULT_BEVEL_RATIO), "bevelling a real interior cube corner should succeed")
	var k := incident_face_count
	check_true((bevel_base_data["Vertices"] as Array).size() == vertices_before_bevel + (k - 1), "bevelling should replace the 1 old corner vertex with %d new ones (net +%d)" % [k, k - 1])
	check_true((bevel_base_data["Faces"] as Array).size() == faces_before_bevel + (2 * k - 2), "bevelling should replace the %d old corner faces with a %d-face ring plus a (%d-2)-triangle cap (net +%d)" % [k, 2 * k, k, 2 * k - 2])
	# Real, direct proof the OLD corner vertex is really gone (reindexed away), not just orphaned --
	# same in-bounds check _delete_vertex's own earlier test already uses.
	var bevel_vertex_count: int = (bevel_base_data["Vertices"] as Array).size()
	var bevel_indices_in_bounds := true
	for f in (bevel_base_data["Faces"] as Array):
		for idx in f:
			if int(idx) < 0 or int(idx) >= bevel_vertex_count:
				bevel_indices_in_bounds = false
	check_true(bevel_indices_in_bounds, "every face index must stay in-bounds after bevel's own reindex")

	# Refuses a bogus request rather than guessing -- an out-of-range vertex index.
	check_true(main._bevel_vertex(bevel_base_data, 9999, main.DEFAULT_BEVEL_RATIO) == false, "bevelling an out-of-range vertex index must be refused, not crash")

	# --- Bevel Vertex, EXTERIOR/boundary case: real, direct pushback from the user after the first
	# version of this feature only supported an INTERIOR (fully-enclosed) vertex -- "We need to do
	# interior, exterior, and what the fuck ever... it's just the minimum level of 'this app can
	# churn these assets out way faster than Blender can'". Delete one of a corner's own faces to
	# turn it into a real boundary vertex, then bevel IT -- this used to be refused outright.
	main.spawn_block()
	var ext_bevel_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	ext_bevel_target.position = Vector3(3, 0, 4)
	main._select(main.scene_objects.size() - 1)
	var ext_bevel_data: Dictionary = ext_bevel_target.get_meta("base_mesh_data")
	var ext_bevel_vertex := 0
	var ext_incident_before: Array = []
	for i in range((ext_bevel_data["Faces"] as Array).size()):
		var f: Array = ext_bevel_data["Faces"][i]
		if f[0] == ext_bevel_vertex or f[1] == ext_bevel_vertex or f[2] == ext_bevel_vertex:
			ext_incident_before.append(i)
	check_true(ext_incident_before.size() >= 3, "a fresh cube corner should start with several incident faces, to make this a meaningful boundary test")
	check_true(main._delete_mesh_face(ext_bevel_data, ext_incident_before[0]), "deleting one of the corner's own faces should succeed, opening a real boundary there")
	var ext_m: int = ext_incident_before.size() - 1 # the corner's own remaining incident face count, AFTER the deletion above
	var ext_vertices_before: int = (ext_bevel_data["Vertices"] as Array).size()
	var ext_faces_before: int = (ext_bevel_data["Faces"] as Array).size()
	check_true(main._bevel_vertex(ext_bevel_data, ext_bevel_vertex, main.DEFAULT_BEVEL_RATIO), "bevelling a real EXTERIOR/boundary vertex must now succeed, not be refused")
	check_true((ext_bevel_data["Vertices"] as Array).size() == ext_vertices_before + ext_m, "an OPEN (boundary) bevel of a %d-face corner should net +%d vertices (one MORE distinct neighbor than an interior bevel of the same size gets, since the fan doesn't loop back to reuse its own start)" % [ext_m, ext_m])
	check_true((ext_bevel_data["Faces"] as Array).size() == ext_faces_before + ext_m, "an OPEN (boundary) bevel of a %d-face corner should net +%d faces (side geometry only, no closing cap -- there was never a face there to begin with)" % [ext_m, ext_m])

	# --- Bevel Vertex, non-manifold refusal: a real "bowtie" vertex (two structurally disjoint
	# triangle fans sharing only one point) has no single, unambiguous ring to walk -- must still be
	# refused outright, not guessed at, even though real boundary vertices in general now work.
	var bowtie_data := {
		"Vertices": [[0.0, 0.0, 0.0], [1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [-1.0, 0.0, 0.0], [0.0, -1.0, 0.0]],
		"Faces": [[0, 1, 2], [0, 3, 4]],
	}
	check_true(main._bevel_vertex(bowtie_data, 0, main.DEFAULT_BEVEL_RATIO) == false, "bevelling a real non-manifold bowtie vertex must be refused, not guess at one arbitrary fan")

	# --- Join and Bridge: "I model a hand by itself. I have arm asset, I chop the hand off it and
	# replace it with my new hand. The mesh needs to correctly make that connection, without a bunch
	# of shitty geometry or a bunch of adjust->select->bridge gap, etc" -- the user's own explicit
	# ask, tested end-to-end with a real, fully controlled, axis-aligned setup: two blocks, each with
	# one whole face deleted (a real 4-vertex hole), positioned with a real 1-unit gap between their
	# two facing holes -- Bridge To... must Join them into one object AND connect the seam with
	# correctly-wound geometry, not just "some new faces that don't crash".
	main.spawn_block()
	var bridge_a: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	bridge_a.position = Vector3(10, 0, 0)
	main._select(main.scene_objects.size() - 1)
	var bridge_a_data: Dictionary = bridge_a.get_meta("base_mesh_data")
	var plus_x_faces_a: Array = []
	for i in range((bridge_a_data["Faces"] as Array).size()):
		var f: Array = bridge_a_data["Faces"][i]
		var all_at_plus_x := true
		for idx in f:
			if not is_equal_approx(float((bridge_a_data["Vertices"] as Array)[idx][0]), 0.5):
				all_at_plus_x = false
		if all_at_plus_x:
			plus_x_faces_a.append(i)
	check_true(plus_x_faces_a.size() == 2, "a block's +X face should be exactly 2 triangles before deletion")
	plus_x_faces_a.sort()
	plus_x_faces_a.reverse() # delete from the end so earlier indices in the list stay valid
	for i in plus_x_faces_a:
		main._delete_mesh_face(bridge_a_data, i)
	await main._recompute_modifiers(bridge_a) # keeps computed_mesh_data (what Join actually reads) in sync with the direct base_mesh_data edit above

	main.spawn_block()
	var bridge_b: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	bridge_b.position = Vector3(12, 0, 0) # a real 1-unit gap between bridge_a's hole (world x=10.5) and bridge_b's hole (world x=11.5)
	main._select(main.scene_objects.size() - 1)
	var bridge_b_data: Dictionary = bridge_b.get_meta("base_mesh_data")
	var minus_x_faces_b: Array = []
	for i in range((bridge_b_data["Faces"] as Array).size()):
		var f: Array = bridge_b_data["Faces"][i]
		var all_at_minus_x := true
		for idx in f:
			if not is_equal_approx(float((bridge_b_data["Vertices"] as Array)[idx][0]), -0.5):
				all_at_minus_x = false
		if all_at_minus_x:
			minus_x_faces_b.append(i)
	check_true(minus_x_faces_b.size() == 2, "a block's -X face should be exactly 2 triangles before deletion")
	minus_x_faces_b.sort()
	minus_x_faces_b.reverse()
	for i in minus_x_faces_b:
		main._delete_mesh_face(bridge_b_data, i)
	await main._recompute_modifiers(bridge_b)

	var bridge_a_edges := main._compute_edges(bridge_a_data)
	var bridge_a_boundary_edge: Dictionary = {}
	for e in bridge_a_edges:
		if e["faces"].size() == 1:
			bridge_a_boundary_edge = e
			break
	check_true(not bridge_a_boundary_edge.is_empty(), "bridge_a should have a real boundary edge after deleting its +X face")
	var loop_a_check := main._trace_boundary_loop(bridge_a_data, bridge_a_boundary_edge["a"], bridge_a_boundary_edge["b"])
	check_true(loop_a_check.size() == 4, "the opened +X face hole should trace to a clean 4-vertex boundary loop, got %d" % loop_a_check.size())

	check_true(main._mark_bridge_start_edge(bridge_a, bridge_a_boundary_edge["a"], bridge_a_boundary_edge["b"]), "marking a real boundary edge as Bridge Start should succeed")
	check_true(main.bridge_start.get("object") == bridge_a, "Bridge Start should record the correct object")

	var bridge_b_edges := main._compute_edges(bridge_b_data)
	var bridge_b_boundary_edge: Dictionary = {}
	for e in bridge_b_edges:
		if e["faces"].size() == 1:
			bridge_b_boundary_edge = e
			break
	check_true(not bridge_b_boundary_edge.is_empty(), "bridge_b should have a real boundary edge after deleting its -X face")

	var faces_before_bridge_a: int = (bridge_a_data["Faces"] as Array).size()
	var faces_before_bridge_b: int = (bridge_b_data["Faces"] as Array).size()
	var scene_count_before_bridge: int = main.scene_objects.size()

	check_true(await main._bridge_object_edges_to(bridge_b, bridge_b_boundary_edge["a"], bridge_b_boundary_edge["b"]), "Bridge To... should succeed for two matching 4-vertex loops")

	check_true(main.scene_objects.size() == scene_count_before_bridge - 1, "Bridge should JOIN the two objects into one, reducing the scene object count by exactly 1")
	check_true(not main.scene_objects.has(bridge_b), "the bridged-away object should no longer be in the scene")
	check_true(main.bridge_start.is_empty(), "a successful bridge should clear the pending bridge_start")

	var merged_faces: Array = bridge_a_data["Faces"]
	check_true(merged_faces.size() == faces_before_bridge_a + faces_before_bridge_b + 8, "bridging a 4-vertex loop should add exactly 8 new triangles (4 quads), on top of both objects' own already-existing faces")

	# The real, direct geometric proof this isn't "a bunch of shitty geometry": the combined mesh
	# should now be fully closed/watertight -- every edge shared by EXACTLY 2 triangles, not left
	# with any stray boundary edges (the tell-tale sign of a bad bridge: gaps, overlaps, or a missed
	# connection).
	var merged_edges := main._compute_edges(bridge_a_data)
	var boundary_edges_remaining := 0
	for e in merged_edges:
		if e["faces"].size() != 2:
			boundary_edges_remaining += 1
	check_true(boundary_edges_remaining == 0, "after a correct bridge, the combined mesh should be fully closed/watertight -- every edge shared by exactly 2 triangles, no leftover boundary, got %d leftover" % boundary_edges_remaining)

	# Real, precise winding proof, not just "it looks closed": find the specific new bridge face
	# directly connecting the two matching ++Y+Z corners (one from each original hole) and confirm
	# its own normal actually points AWAY from the tube's own central axis (positive Y AND Z), not
	# inward -- exactly the kind of real cross-product check this project's own past Extrude/Inset
	# winding bugs were caught with, applied here to a brand new operation rather than assumed correct.
	var top_right_corner_a_world := Vector3(10.5, 0.5, 0.5)
	var top_right_corner_b_world := Vector3(11.5, 0.5, 0.5)
	var top_right_local_a: Vector3 = bridge_a.transform.affine_inverse() * top_right_corner_a_world
	var top_right_local_b: Vector3 = bridge_a.transform.affine_inverse() * top_right_corner_b_world
	var top_right_vertex_a := -1
	var top_right_vertex_b := -1
	for i in range((bridge_a_data["Vertices"] as Array).size()):
		var v: Array = bridge_a_data["Vertices"][i]
		var p := Vector3(v[0], v[1], v[2])
		if p.distance_to(top_right_local_a) < 0.01:
			top_right_vertex_a = i
		if p.distance_to(top_right_local_b) < 0.01:
			top_right_vertex_b = i
	check_true(top_right_vertex_a >= 0, "should be able to find bridge_a's own ++corner vertex after the merge")
	check_true(top_right_vertex_b >= 0, "should be able to find bridge_b's own corresponding ++corner vertex after the merge")

	var outward_face: Array = []
	for f in merged_faces:
		if f.has(top_right_vertex_a) and f.has(top_right_vertex_b):
			outward_face = f
			break
	check_true(not outward_face.is_empty(), "a real bridge face directly connecting the two matching ++corners should exist")
	if not outward_face.is_empty():
		var fp0: Vector3 = main._vec3_from_data(bridge_a_data["Vertices"][outward_face[0]])
		var fp1: Vector3 = main._vec3_from_data(bridge_a_data["Vertices"][outward_face[1]])
		var fp2: Vector3 = main._vec3_from_data(bridge_a_data["Vertices"][outward_face[2]])
		var face_normal: Vector3 = (fp1 - fp0).cross(fp2 - fp0).normalized()
		# This specific triangle is one of the tube's 4 real FLAT side walls (not a diagonal corner
		# facet -- a real, initially-wrong assumption this test itself first had, corrected after
		# actually checking the real geometry instead of guessing): it lies entirely in EITHER the
		# Y=+0.5 plane or the Z=+0.5 plane (whichever axis _trace_boundary_loop's own traversal
		# happened to step along first from this corner), so its outward normal must be purely
		# +Y or purely +Z -- never negative (that would mean the wall points INTO the tube) and
		# never diagonal (that would mean this face isn't actually flat/axis-aligned as it should be).
		var is_outward_y := face_normal.y > 0.9 and absf(face_normal.z) < 0.1
		var is_outward_z := face_normal.z > 0.9 and absf(face_normal.y) < 0.1
		check_true(is_outward_y or is_outward_z, "the bridge face touching the ++Y+Z corner should be a flat side wall with a purely +Y or purely +Z outward normal, got %s -- a real, precise proof the winding self-correction actually worked, not just 'no crash'" % str(face_normal))

	# The original bridge_a boundary edge no longer exists as a clean boundary loop -- the successful
	# bridge above fully consumed it (the mesh is now watertight, confirmed above) -- re-marking it
	# must fail, not silently succeed on stale data.
	check_true(main._mark_bridge_start_edge(bridge_a, bridge_a_boundary_edge["a"], bridge_a_boundary_edge["b"]) == false, "re-marking a now-fully-closed edge as a boundary loop must fail")

	# Unequal loop counts must be refused with an actionable message, not guessed at: a real
	# 3-vertex hole (one single triangle removed) bridged against a real 4-vertex hole (a whole
	# quad face removed).
	main.spawn_block()
	var bridge_c: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	bridge_c.position = Vector3(20, 0, 0)
	main._select(main.scene_objects.size() - 1)
	var bridge_c_data: Dictionary = bridge_c.get_meta("base_mesh_data")
	var minus_x_faces_c: Array = []
	for i in range((bridge_c_data["Faces"] as Array).size()):
		var f: Array = bridge_c_data["Faces"][i]
		var all_at_minus_x := true
		for idx in f:
			if not is_equal_approx(float((bridge_c_data["Vertices"] as Array)[idx][0]), -0.5):
				all_at_minus_x = false
		if all_at_minus_x:
			minus_x_faces_c.append(i)
	minus_x_faces_c.sort()
	minus_x_faces_c.reverse()
	main._delete_mesh_face(bridge_c_data, minus_x_faces_c[0]) # only ONE of the 2 triangles -- a real 3-vertex hole, not 4
	await main._recompute_modifiers(bridge_c)
	var bridge_c_edges := main._compute_edges(bridge_c_data)
	var bridge_c_boundary_edge: Dictionary = {}
	for e in bridge_c_edges:
		if e["faces"].size() == 1:
			bridge_c_boundary_edge = e
			break
	check_true(not bridge_c_boundary_edge.is_empty(), "bridge_c should have a real boundary edge after deleting one triangle of its -X face")
	var loop_c_check := main._trace_boundary_loop(bridge_c_data, bridge_c_boundary_edge["a"], bridge_c_boundary_edge["b"])
	check_true(loop_c_check.size() == 3, "removing just one triangle should open a clean 3-vertex boundary loop, got %d" % loop_c_check.size())

	main.spawn_block()
	var bridge_d: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	bridge_d.position = Vector3(22, 0, 0)
	main._select(main.scene_objects.size() - 1)
	var bridge_d_data: Dictionary = bridge_d.get_meta("base_mesh_data")
	var minus_x_faces_d: Array = []
	for i in range((bridge_d_data["Faces"] as Array).size()):
		var f: Array = bridge_d_data["Faces"][i]
		var all_at_minus_x := true
		for idx in f:
			if not is_equal_approx(float((bridge_d_data["Vertices"] as Array)[idx][0]), -0.5):
				all_at_minus_x = false
		if all_at_minus_x:
			minus_x_faces_d.append(i)
	minus_x_faces_d.sort()
	minus_x_faces_d.reverse()
	for i in minus_x_faces_d:
		main._delete_mesh_face(bridge_d_data, i) # both triangles -- a real 4-vertex hole
	await main._recompute_modifiers(bridge_d)
	var bridge_d_edges := main._compute_edges(bridge_d_data)
	var bridge_d_boundary_edge: Dictionary = {}
	for e in bridge_d_edges:
		if e["faces"].size() == 1:
			bridge_d_boundary_edge = e
			break
	check_true(not bridge_d_boundary_edge.is_empty(), "bridge_d should have a real boundary edge after deleting its whole -X face")

	check_true(main._mark_bridge_start_edge(bridge_c, bridge_c_boundary_edge["a"], bridge_c_boundary_edge["b"]), "marking bridge_c's own real 3-vertex boundary loop should succeed on its own")
	var scene_count_before_mismatch: int = main.scene_objects.size()
	check_true(await main._bridge_object_edges_to(bridge_d, bridge_d_boundary_edge["a"], bridge_d_boundary_edge["b"]) == false, "bridging a 3-vertex loop to a 4-vertex loop must be refused, not guess at an ambiguous mapping")
	check_true(main.scene_objects.size() == scene_count_before_mismatch, "a refused bridge (mismatched loop sizes) must NOT join the two objects either -- no partial/half-applied side effect")

	main._cancel_bridge_start()
	check_true(main.bridge_start.is_empty(), "Cancel Bridge should clear any pending bridge_start")

	# Tab again exits Edit Mode back to Select, matching Blender's own toggle convention.
	main._toggle_edit_mode()
	check_true(main.current_tool == main.ToolMode.SELECT, "Tab a second time should exit Edit Mode")

	# --- Rigging (humans/animals ask): real preset skeleton, adjustable joints, real FK posing ---
	main.spawn_block()
	var rig_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	rig_target.position = Vector3(400, 0, 0)
	main._select(main.scene_objects.size() - 1)

	# Guarded: Rig/Pose Mode should refuse to activate before a rig exists (no silent zero-handle
	# state that LOOKS like it worked but did nothing).
	main._toggle_rig_mode()
	check_true(main.current_tool != main.ToolMode.RIG, "Rig Mode should refuse to activate before any rig exists")

	await main._add_humanoid_rig()
	check_true(rig_target.has_meta("arco_rig"), "Add Humanoid Rig should attach real rig metadata")
	var rig: Dictionary = rig_target.get_meta("arco_rig")
	var bones: Array = rig["Bones"]
	check_true(bones.size() == main.HUMANOID_RIG_PRESET.size(), "the rig should have exactly as many bones as the preset defines, got %d" % bones.size())
	check_true(int(bones[0]["Parent"]) == -1, "the first bone (Hips) should be a root with no parent")
	check_true(rig_target.has_meta("arco_vertex_bone_index"), "adding a rig should compute real automatic weights")
	var weights: Array = rig_target.get_meta("arco_vertex_bone_index")
	var rig_base_vertex_count: int = ((rig_target.get_meta("base_mesh_data") as Dictionary)["Vertices"] as Array).size()
	check_true(weights.size() == rig_base_vertex_count, "every real vertex should get a weight assignment, got %d weights for %d vertices" % [weights.size(), rig_base_vertex_count])

	# Auto-fit: Hips (offset y=0.50 of a unit-cube AABB spanning -0.5..0.5) should land at world
	# origin's own Y, not some arbitrary/default position -- a real, precise check, not "some rig
	# got attached".
	var hips_bind: Array = bones[0]["BindPosition"]
	check_close(float(hips_bind[1]), 0.0, "Hips should auto-fit to the vertical CENTER of the object's own bounding box")
	var head_bind: Array = bones[4]["BindPosition"]
	check_close(float(head_bind[1]), 0.5, "Head should auto-fit to the TOP of the object's own bounding box")

	# Now Rig Mode should activate normally.
	main._toggle_rig_mode()
	check_true(main.current_tool == main.ToolMode.RIG, "Rig Mode should activate once a rig exists")
	check_true(main.rig_joint_handle_nodes.size() == bones.size(), "Rig Mode should show exactly one handle per bone joint")

	# --- Real FK posing: rotate a bone, verify its own + its descendants' weighted vertices move,
	# and verify UNRELATED bones' vertices do NOT move at all. This is the exact math verified by
	# hand via direct computation before ever writing this test (see project memory) -- rotating
	# LeftShoulder (index 5) by 90 degrees around Y should carry LeftElbow's (its child, index 6)
	# own weighted vertices along with it.
	main._toggle_pose_mode()
	check_true(main.current_tool == main.ToolMode.POSE, "Pose Mode should activate once a rig exists")
	var pre_pose_data: Dictionary = (rig_target.get_meta("base_mesh_data") as Dictionary).duplicate(true)
	bones[5]["PoseRotationDegrees"] = [0.0, 90.0, 0.0]
	await main._recompute_modifiers(rig_target)
	var posed_data: Dictionary = rig_target.get_meta("computed_mesh_data")
	var any_vertex_moved := false
	var any_unrelated_vertex_moved := false
	for i in range(weights.size()):
		var bone_index: int = weights[i]
		var moved: bool = main._vec3_from_data(pre_pose_data["Vertices"][i]) != main._vec3_from_data(posed_data["Vertices"][i])
		# "Related" means bone_index IS 5 (LeftShoulder) or a descendant of it in the chain --
		# walking up from bone_index to the root and checking whether 5 appears along the way.
		var is_descendant_of_shoulder := false
		var walk: int = bone_index
		while walk >= 0:
			if walk == 5:
				is_descendant_of_shoulder = true
				break
			walk = int(bones[walk]["Parent"])
		if is_descendant_of_shoulder:
			if moved:
				any_vertex_moved = true
		elif moved:
			any_unrelated_vertex_moved = true
	check_true(any_vertex_moved, "rotating LeftShoulder should visibly move at least one vertex weighted to it or a descendant bone")
	check_true(not any_unrelated_vertex_moved, "rotating LeftShoulder must NOT move any vertex weighted to an unrelated bone")

	# --- Rig Mode joint adjustment should recompute weights, not just move the joint ---
	var weights_before_joint_move: Array = weights.duplicate()
	bones[0]["BindPosition"] = [0.0, 0.3, 0.0] # drag Hips upward, toward the chest region
	main._compute_automatic_rig_weights(rig_target)
	var weights_after_joint_move: Array = rig_target.get_meta("arco_vertex_bone_index")
	check_true(weights_after_joint_move.size() == weights_before_joint_move.size(), "moving a joint should not change the vertex count")

	main._toggle_pose_mode()
	check_true(main.current_tool == main.ToolMode.SELECT, "Pose Mode should toggle back off the same way Edit Mode does")

	# --- Quadruped rig: same code path, a different DATA TABLE (see QUADRUPED_RIG_PRESET's own
	# comment) -- covers the "and animals" half of the user's own explicit ask, and proves the
	# generic rig machinery isn't secretly humanoid-shaped anywhere.
	main.spawn_block()
	var quad_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	main._select(main.scene_objects.size() - 1)
	await main._add_quadruped_rig()
	check_true(quad_target.has_meta("arco_rig"), "Add Quadruped Rig should attach real rig metadata")
	var quad_bones: Array = (quad_target.get_meta("arco_rig") as Dictionary)["Bones"]
	check_true(quad_bones.size() == main.QUADRUPED_RIG_PRESET.size(), "the quadruped rig should have exactly as many bones as its own preset defines, got %d" % quad_bones.size())
	check_true(int(quad_bones[0]["Parent"]) == -1, "Hips should still be the quadruped's own root, same convention as the humanoid preset")
	# Every parent index must refer to an EARLIER bone (no forward references) -- a real structural
	# guarantee _world_pose_transform's own parent-chain walk depends on being acyclic, not just
	# assumed correct by eyeballing the preset table above.
	var quad_parent_chain_valid := true
	for i in range(quad_bones.size()):
		var parent_index: int = quad_bones[i]["Parent"]
		if parent_index >= i:
			quad_parent_chain_valid = false
	check_true(quad_parent_chain_valid, "every quadruped bone's parent must be an earlier bone in the array (or -1), never itself or a later one")

	# Real FK posing check, same pattern as the humanoid one above but exercising a DIFFERENT limb
	# (FrontLeftShoulder, a front leg) to prove this isn't humanoid-specific math -- exact expected
	# value hand-derived and confirmed via a direct probe before ever writing this assertion (see
	# project memory): rotating FrontLeftShoulder 45 degrees around X should move the paw at the end
	# of that leg's own chain to a precise, non-guessed position.
	var quad_weights: Array = quad_target.get_meta("arco_vertex_bone_index")
	var front_left_shoulder_index := 7
	var front_left_paw_index := 9
	quad_bones[front_left_shoulder_index]["PoseRotationDegrees"] = [45.0, 0.0, 0.0]
	await main._recompute_modifiers(quad_target)
	var quad_computed: Dictionary = quad_target.get_meta("computed_mesh_data")
	var quad_base: Dictionary = (quad_target.get_meta("base_mesh_data") as Dictionary).duplicate(true)
	var paw_vertex_index := -1
	for i in range(quad_weights.size()):
		if int(quad_weights[i]) == front_left_paw_index:
			paw_vertex_index = i
			break
	check_true(paw_vertex_index >= 0, "at least one vertex should be weighted to FrontLeftPaw for this check to mean anything")
	if paw_vertex_index >= 0:
		var pivot: Vector3 = main._vec3_from_data(quad_bones[front_left_shoulder_index]["BindPosition"])
		var before: Vector3 = main._vec3_from_data(quad_base["Vertices"][paw_vertex_index])
		var angle := deg_to_rad(45.0)
		var expected_y: float = pivot.y + (before.y - pivot.y) * cos(angle) - (before.z - pivot.z) * sin(angle)
		var expected_z: float = pivot.z + (before.y - pivot.y) * sin(angle) + (before.z - pivot.z) * cos(angle)
		var after: Vector3 = main._vec3_from_data(quad_computed["Vertices"][paw_vertex_index])
		check_close(after.y, expected_y, "FrontLeftPaw's posed Y should match the hand-derived rotate-around-FrontLeftShoulder's-own-pivot formula")
		check_close(after.z, expected_z, "FrontLeftPaw's posed Z should match the hand-derived rotate-around-FrontLeftShoulder's-own-pivot formula")
		check_close(after.x, before.x, "rotating around the X axis should leave this vertex's own X coordinate untouched")

	# --- Pivot rig: the "vehicle rigid-pivot rigging" ask, deliberately the degenerate ONE-bone
	# case of the exact same machinery (see PIVOT_RIG_PRESET's own comment) -- a wheel or door
	# rotating as one rigid piece around a single point, with zero new transform math needed.
	main.spawn_cylinder()
	var wheel_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	main._select(main.scene_objects.size() - 1)
	await main._add_pivot_rig()
	check_true(wheel_target.has_meta("arco_rig"), "Add Pivot Rig should attach real rig metadata")
	var pivot_bones: Array = (wheel_target.get_meta("arco_rig") as Dictionary)["Bones"]
	check_true(pivot_bones.size() == 1, "a pivot rig should have exactly one bone, not a whole skeleton")
	var pivot_bind: Vector3 = main._vec3_from_data(pivot_bones[0]["BindPosition"])
	check_close(pivot_bind.x, 0.0, "the auto-fit pivot should land on a symmetric primitive's own local origin (X)")
	check_close(pivot_bind.y, 0.0, "the auto-fit pivot should land on a symmetric primitive's own local origin (Y)")
	check_close(pivot_bind.z, 0.0, "the auto-fit pivot should land on a symmetric primitive's own local origin (Z)")
	var pivot_weights: Array = wheel_target.get_meta("arco_vertex_bone_index")
	var all_weighted_to_pivot := true
	for w in pivot_weights:
		if int(w) != 0:
			all_weighted_to_pivot = false
	check_true(all_weighted_to_pivot, "every vertex should be weighted to the single pivot bone -- the whole object rotates as one rigid piece")

	# Rotate the whole wheel 90 degrees around Z and verify against the plain, textbook 2D rotation
	# formula (no pivot offset to account for here, since the pivot sits at the origin) -- exact
	# match confirmed via a direct probe before writing this assertion, not assumed.
	pivot_bones[0]["PoseRotationDegrees"] = [0.0, 0.0, 90.0]
	await main._recompute_modifiers(wheel_target)
	var wheel_base: Dictionary = (wheel_target.get_meta("base_mesh_data") as Dictionary).duplicate(true)
	var wheel_computed: Dictionary = wheel_target.get_meta("computed_mesh_data")
	var wheel_before: Vector3 = main._vec3_from_data(wheel_base["Vertices"][0])
	var wheel_after: Vector3 = main._vec3_from_data(wheel_computed["Vertices"][0])
	check_close(wheel_after.x, -wheel_before.y, "rotating the pivot rig 90 degrees around Z should map x' = -y (textbook rotation about the origin)")
	check_close(wheel_after.y, wheel_before.x, "rotating the pivot rig 90 degrees around Z should map y' = x (textbook rotation about the origin)")
	check_close(wheel_after.z, wheel_before.z, "rotating around Z should leave Z untouched")

	# --- A3D v2 schema: real round-trip of Normals/UVs/Construction/Rig/Pose, not just Vertices/
	# Faces (the "flesh out the A3D format properly" ask) ---

	# UVs first, on a PLAIN, never-recomputed primitive -- a real, honest boundary found while
	# writing this very test, not assumed: base_mesh_data (and therefore _mesh_from_dict, which
	# _recompute_modifiers always rebuilds .mesh through) has no UV field at all yet, so ANY object
	# that has ever gone through _recompute_modifiers -- adding a modifier, a rig, or any Edit Mode
	# operation -- already loses its original UVs today, before this A3D exporter even runs. This
	# exporter is honest about that: it exports whatever the LIVE mesh actually has, so a plain,
	# untouched spawn keeps its real Godot-generated UVs, while a rigged/modified/edited object
	# does not, matching the app's own real current behavior rather than papering over the gap.
	main.spawn_block()
	var a3d_uv_source: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	var a3d_uv_source_arrays := a3d_uv_source.mesh.surface_get_arrays(0)
	check_true(a3d_uv_source_arrays[Mesh.ARRAY_TEX_UV] != null, "a freshly spawned, never-recomputed block's own ArrayMesh should have real UVs available to export (sanity check on the test's own setup)")
	var a3d_uv_root := Node3D.new()
	var a3d_uv_export_path := "/tmp/arco3d_godot_test_v2_uv.a3d"
	a3d_uv_root.add_child(a3d_uv_source.duplicate())
	check_true(A3DFormat.export_asset(a3d_uv_root, "V2UvTest", a3d_uv_export_path) == OK, "A3D v2 UV export should succeed")
	var a3d_uv_reimport := A3DFormat.import_asset(a3d_uv_export_path)
	if a3d_uv_reimport.get("Ok", false):
		var a3d_uv_imported: MeshInstance3D = a3d_uv_reimport["Root"].get_child(0)
		var a3d_uv_imported_arrays := a3d_uv_imported.mesh.surface_get_arrays(0)
		check_true(a3d_uv_imported_arrays[Mesh.ARRAY_TEX_UV] != null, "a never-recomputed primitive's real UVs should survive a full export/import round-trip")
		a3d_uv_reimport["Root"].queue_free()
	a3d_uv_root.queue_free()

	# Now Construction/Rig/Pose, on an object that HAS gone through _recompute_modifiers (adding a
	# rig always does) -- Normals still round-trip here (they're regenerated fresh by
	# _mesh_from_dict's SurfaceTool fallback every time regardless of UVs), but UVs correctly won't,
	# per the real gap just described above.
	main.spawn_block()
	var a3d_source: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	main._select(main.scene_objects.size() - 1)
	await main._add_humanoid_rig()
	var a3d_bones: Array = (a3d_source.get_meta("arco_rig") as Dictionary)["Bones"]
	a3d_bones[5]["PoseRotationDegrees"] = [0.0, 90.0, 0.0] # LeftShoulder, same real pose used above
	await main._recompute_modifiers(a3d_source)

	var a3d_v2_root := Node3D.new()
	var a3d_v2_export_path := "/tmp/arco3d_godot_test_v2.a3d"
	a3d_v2_root.add_child(a3d_source.duplicate())
	check_true(A3DFormat.export_asset(a3d_v2_root, "V2SchemaTest", a3d_v2_export_path) == OK, "A3D v2 export should succeed")

	# Inspect the raw JSON manifest directly (not just re-parsed through import_asset) to confirm
	# the physical file really contains what this whole increment is actually about -- a real,
	# concrete check that Normals/Construction/Rig/Poses are genuinely on disk, not just present in
	# some in-memory intermediate the importer happens to reconstruct another way.
	var a3d_v2_file := FileAccess.open(a3d_v2_export_path, FileAccess.READ)
	var a3d_v2_json := JSON.new()
	a3d_v2_json.parse(a3d_v2_file.get_as_text())
	a3d_v2_file.close()
	var a3d_v2_manifest: Dictionary = a3d_v2_json.data
	check_true(int(a3d_v2_manifest.get("SchemaVersion", 0)) == A3DFormat.SCHEMA_VERSION, "exported manifest should declare the current SCHEMA_VERSION (checked dynamically, not a second hardcoded literal that could desync on a future bump)")
	check_true((a3d_v2_manifest.get("OptionalCapabilities", []) as Array).has("rig.core"), "manifest should declare rig.core since this asset really has a rig")
	check_true((a3d_v2_manifest.get("OptionalCapabilities", []) as Array).has("construction.arco3d"), "manifest should declare construction.arco3d since this asset really has base_mesh_data")
	var a3d_v2_component: Dictionary = ((a3d_v2_manifest["Asset"] as Dictionary)["Root"] as Dictionary)["Children"][0]
	check_true((a3d_v2_component["Children"] as Array).is_empty(), "the picking-collider Area3D must NOT leak into the exported Children list")
	check_true((a3d_v2_component["Mesh"] as Dictionary).has("Normals"), "exported mesh should carry real per-vertex normals")
	check_true(a3d_v2_component.has("Construction"), "exported component should carry a Construction block (base_mesh_data + modifiers)")
	check_true(a3d_v2_component.has("Rig"), "exported component should carry a Rig block")
	var a3d_v2_rig_json: Dictionary = a3d_v2_component["Rig"]
	check_true((a3d_v2_rig_json["Bones"] as Array).size() == main.HUMANOID_RIG_PRESET.size(), "exported Rig.Bones should have the full humanoid bone count")
	check_true((a3d_v2_rig_json["Poses"] as Array).size() == 1, "exactly one live pose should be exported, since exactly one bone was posed away from bind")
	var a3d_v2_bone_rotations: Array = (a3d_v2_rig_json["Poses"][0] as Dictionary)["BoneRotations"]
	check_true(a3d_v2_bone_rotations.size() == 1, "only the ONE actually-posed bone should appear in BoneRotations (RFC-0050's own sparse-pose requirement), not all 17")
	check_true(int((a3d_v2_bone_rotations[0] as Dictionary)["BoneId"]) == 5, "the posed bone's exported BoneId should be LeftShoulder's own index (5)")

	# Now the real end-to-end proof: reimport through the SAME path a human's File > Import would
	# use (Main._on_import_path_chosen), not just the standalone parser -- confirms Main.gd's own
	# post-import _recompute_modifiers call (this increment's own fix for a real, previously-
	# existing gap: imported objects used to carry no base_mesh_data/arco_rig at all) actually
	# reconstructs a posed, editable object, not just a static baked mesh wearing metadata no one
	# ever applies.
	var a3d_v2_objects_before_import: int = main.scene_objects.size()
	await main._on_import_path_chosen(a3d_v2_export_path)
	check_true(main.scene_objects.size() == 1, "importing should have replaced the whole scene with the one exported object")
	var a3d_v2_imported: MeshInstance3D = main.scene_objects[0]
	check_true(a3d_v2_imported.has_meta("arco_rig"), "the reimported object should have a real arco_rig again, not just a baked mesh")
	check_true(a3d_v2_imported.has_meta("base_mesh_data"), "the reimported object should have real base_mesh_data again (Edit Mode should work on it)")
	var a3d_v2_reimported_bones: Array = (a3d_v2_imported.get_meta("arco_rig") as Dictionary)["Bones"]
	check_true(int(a3d_v2_reimported_bones[0]["Parent"]) == -1, "reimported Hips should still be the root after ParentId -> Parent-index resolution")
	check_close(float(a3d_v2_reimported_bones[5]["PoseRotationDegrees"][1]), 90.0, "reimported LeftShoulder should still be posed at 90 degrees around Y")
	# _on_import_path_chosen's own new _recompute_modifiers call should have already applied that
	# pose to the DISPLAYED mesh -- verify the actual computed geometry, not just the metadata that
	# describes it, matches the exact same hand-derived rotate-around-a-pivot result already proven
	# correct earlier in this same test run for the original (pre-export) object.
	check_true(a3d_v2_imported.has_meta("computed_mesh_data"), "reimporting a rigged object should trigger a real recompute, producing computed_mesh_data")
	var a3d_v2_reimported_weights: Array = a3d_v2_imported.get_meta("arco_vertex_bone_index", [])
	check_true(a3d_v2_reimported_weights.size() == ((a3d_v2_imported.get_meta("base_mesh_data") as Dictionary)["Vertices"] as Array).size(), "reimported skin weights should cover every base vertex")
	main._select(0)
	main._toggle_rig_mode()
	check_true(main.current_tool == main.ToolMode.RIG, "Rig Mode should activate on the reimported object -- proves it isn't a dead, metadata-only rig")
	main._toggle_rig_mode() # back to Select, don't leave test state in a mode later sections don't expect
	check_true(main.current_tool == main.ToolMode.SELECT, "should be back in Select after toggling Rig Mode off")

	a3d_v2_root.queue_free()

	# --- ArcoBASIC console: real session continuity across SEPARATE submitted commands ---
	# This is the core claim the console makes ("run everything via arcobasic commands" as a real,
	# persistent REPL, not one-shot scripts) -- verified here by actually submitting several
	# SEPARATE _submit_console_command calls (each one its own real arcosh subprocess run under the
	# hood) and confirming a variable set in an EARLIER command is still valid in a LATER one.
	if not arcosh_path.is_empty():
		var objects_before_console := main.scene_objects.size()
		await main._submit_console_command("myBlock = SpawnBlock()")
		check_true(main.scene_objects.size() == objects_before_console + 1, "console SpawnBlock should spawn exactly 1 object")
		await main._submit_console_command("ignoredCall = SetPosition(myBlock, 5, 6, 7)")
		var console_block: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
		check_close(console_block.position.x, 5.0, "a LATER console command should still see 'myBlock' from an EARLIER, separately-submitted command")
		check_close(console_block.position.y, 6.0, "console SetPosition Y")
		check_close(console_block.position.z, 7.0, "console SetPosition Z")

		# A bare PRINT (not a recognized SPAWN/POSITION/etc. command) should be echoed to the
		# console log as plain text, not silently dropped -- the whole point of _process_script_
		# command now returning whether it recognized a line.
		var output_length_before_print := main.console_output.text.length()
		await main._submit_console_command("PRINT \"hello from console\"")
		check_true(main.console_output.text.length() > output_length_before_print, "a bare PRINT should append real output text to the console log")
		check_true(main.console_output.text.contains("hello from console"), "the console log should contain the PRINT's actual text")

		# A command that errors must NOT be adopted into the persistent session -- otherwise one
		# typo would break every future command by permanently corrupting the replayed transcript.
		var console_lines_before_error := main.console_lines.size()
		await main._submit_console_command("this is not valid arcobasic +++")
		check_true(main.console_lines.size() == console_lines_before_error, "a failing console command must NOT be added to the persistent session")
		# The session should still work normally afterward -- confirms the rollback didn't corrupt
		# console_processed_line_count or leave the session in a broken state.
		await main._submit_console_command("ignoredCall = SetPosition(myBlock, 1, 1, 1)")
		check_close(console_block.position.x, 1.0, "the console session should keep working normally after a rolled-back error")

	# --- Workspace tabs: BUILD/SURFACE/RIG/POSE are real now, not inert placeholders -- the user's
	# own blunt, correct question ("why didn't you put all of this into the SURFACE section?") once
	# Material got bolted onto the same flat sidebar Mount Points was, instead of into the SURFACE
	# tab that had been sitting there, disabled, since the seventh increment specifically for this.
	check_true(main.current_workspace_tab == "BUILD", "should start on the BUILD workspace tab")
	check_true(main.left_build_section.visible and main.right_build_section.visible, "BUILD's own panels should be visible on startup")
	check_true(not main.right_surface_section.visible and not main.left_rig_section.visible, "SURFACE/RIG panels should NOT be visible while on BUILD")

	main._set_workspace_tab("SURFACE")
	check_true(main.right_surface_section.visible, "switching to SURFACE should show the Material panel")
	check_true(not main.right_build_section.visible and not main.left_build_section.visible, "switching to SURFACE should hide BUILD's own panels")
	check_true(main.current_tool == main.ToolMode.SELECT, "switching to SURFACE should fall back to Select (materials have no tool mode of their own)")

	# RIG/POSE tabs must refuse to actually enter that tool mode on an object with no rig -- a real
	# guard, not just a panel switch, matching the exact same refusal the G/P keyboard shortcuts
	# already had before this tab wiring existed.
	main.spawn_block()
	var tab_test_object: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	main._select(main.scene_objects.find(tab_test_object))
	main._set_workspace_tab("RIG")
	check_true(main.left_rig_section.visible, "switching to RIG should show the Rigging panel regardless of whether the object has a rig yet")
	check_true(main.current_tool != main.ToolMode.RIG, "switching to RIG should NOT actually enter Rig Mode on an object with no rig")

	await main._add_humanoid_rig()
	main._set_workspace_tab("RIG")
	check_true(main.current_tool == main.ToolMode.RIG, "switching to RIG on an object that now has a real rig should actually enter Rig Mode")
	main._set_workspace_tab("POSE")
	check_true(main.left_rig_section.visible, "POSE should show the same Rigging panel as RIG")
	check_true(main.current_tool == main.ToolMode.POSE, "switching to POSE should actually enter Pose Mode")

	# Keyboard shortcuts (G/P/Tab/M) must keep the top bar's own tab buttons in sync, not just the
	# internal tool-mode state -- a real, direct check of the button widget itself, not just
	# current_workspace_tab (which _set_workspace_tab always updates as a matter of course).
	main._toggle_pose_mode() # currently in POSE -- this should toggle OFF, back to BUILD
	check_true(main.current_workspace_tab == "BUILD", "toggling Pose Mode off via its own function should switch the workspace tab back to BUILD")
	check_true(main.workspace_tab_buttons["BUILD"].button_pressed, "the BUILD tab BUTTON itself should show as pressed after toggling Pose Mode off, not just the internal state variable")
	main._toggle_rig_mode() # currently in BUILD -- this should toggle ON, into RIG
	check_true(main.current_workspace_tab == "RIG", "toggling Rig Mode on via its own function should switch the workspace tab to RIG")
	check_true(main.workspace_tab_buttons["RIG"].button_pressed, "the RIG tab BUTTON itself should show as pressed after toggling Rig Mode on")
	main._set_workspace_tab("BUILD")

	# --- delete ---
	var count_before_deletes := main.scene_objects.size()
	check_true(count_before_deletes > 0, "sanity check: there should be objects left to delete at this point")
	while not main.scene_objects.is_empty():
		var count_before_this_delete := main.scene_objects.size()
		main._delete_selected()
		check_true(main.scene_objects.size() == count_before_this_delete - 1, "each delete should remove exactly one object")
	check_true(main.scene_objects.is_empty(), "expected 0 objects left after deleting all of them")
	check_true(main.selection_handles == null, "deleting the last object should clear its handles")

	# --- Floating panels: "Can we make the side panels pop out into independent windows?" -- the
	# user's own explicit ask, presumably for a real multi-monitor workflow. Real Godot Window
	# nodes, not a simulated floating Control -- verified structurally (the actual reparenting, the
	# docked container's own visibility, and the OS close-button path), the same "headless can prove
	# the logic, not the pixels" honesty this whole project has always had about anything visual.
	check_true(main.left_sidebar_window == null, "the sidebar should start docked, not floating")
	check_true(main.left_sidebar_dock.visible, "the docked sidebar container should start visible")
	check_true(main.left_sidebar_content.get_parent() == main.left_sidebar_dock, "the sidebar's real content should start parented under its own docked container")

	main._toggle_left_sidebar_floating()
	check_true(main.left_sidebar_window != null and is_instance_valid(main.left_sidebar_window), "popping out the sidebar should create a real Window")
	check_true(main.left_sidebar_content.get_parent() == main.left_sidebar_window, "the sidebar's real content should be reparented into the new Window, not copied")
	check_true(not main.left_sidebar_dock.visible, "the docked container should hide itself while floating, reclaiming the space for the 3D viewport")
	# The SAME live Control tree, not a rebuilt copy -- every existing reference into it (built by
	# _build_left_sidebar long before any of this ran) must still resolve to a real, valid node.
	check_true(is_instance_valid(main.left_build_section), "existing references into the floated content (e.g. left_build_section) must still be valid after reparenting")
	check_true(main.left_build_section.is_inside_tree(), "the floated content must still be part of the live scene tree (inside the new Window), not orphaned")

	main._toggle_left_sidebar_floating() # toggling again while floating should dock it back
	check_true(main.left_sidebar_window == null, "toggling a second time should dock the sidebar back and clear the window reference")
	check_true(main.left_sidebar_dock.visible, "the docked container should become visible again after docking back")
	check_true(main.left_sidebar_content.get_parent() == main.left_sidebar_dock, "the sidebar's real content should be back under its own docked container")

	# The OS window's own close button (X) is a DIFFERENT code path from pressing the toggle button
	# again -- it fires the Window's own close_requested signal, not a direct call into this app's
	# own toggle function -- so it needs its own, separate, real test.
	main._toggle_left_sidebar_floating()
	var floated_window: Window = main.left_sidebar_window
	floated_window.close_requested.emit()
	check_true(main.left_sidebar_window == null, "closing the floated window via its own close_requested signal (the real OS close-button path) should dock the sidebar back too")
	check_true(main.left_sidebar_dock.visible, "the docked container should become visible again after a close-button dock-back")
	check_true(main.left_sidebar_content.get_parent() == main.left_sidebar_dock, "the sidebar's real content should be back under its own docked container after a close-button dock-back")

	# The right panel is a SEPARATE instance of the same mechanism -- floating one must not affect
	# the other's own state at all.
	check_true(main.right_panel_window == null, "the object panel should start docked too")
	main._toggle_left_sidebar_floating()
	check_true(main.right_panel_window == null, "floating the sidebar must not affect the object panel's own docked state")
	main._toggle_left_sidebar_floating() # dock the sidebar back before moving on

	main._toggle_right_panel_floating()
	check_true(main.right_panel_window != null and is_instance_valid(main.right_panel_window), "popping out the object panel should create its own real Window")
	check_true(main.right_panel_content.get_parent() == main.right_panel_window, "the object panel's real content should be reparented into its own new Window")
	check_true(not main.right_panel_dock.visible, "the object panel's docked container should hide itself while floating")
	check_true(is_instance_valid(main.name_field), "existing references into the floated object panel (e.g. name_field) must still be valid after reparenting")
	main._toggle_right_panel_floating()
	check_true(main.right_panel_window == null, "toggling the object panel a second time should dock it back")
	check_true(main.right_panel_dock.visible, "the object panel's docked container should become visible again after docking back")

	# --- Undo/Redo: a real, previously entirely-missing safety net. "Delete, Bevel, Cut Edge,
	# Bridge, and especially Join (which deletes an object outright and merges its geometry
	# irreversibly) were permanent with no way back except manually rebuilding" -- the user's own
	# direct pick, asked what to build next once Join/Bridge landed. Starts from an empty scene (the
	# "delete" section above cleared everything) for a clean, easy-to-reason-about sequence.
	check_true(main.scene_objects.is_empty(), "sanity check: the scene should be empty before this section")
	# Every spawn/delete/etc. throughout this WHOLE cumulative test file has already been pushing
	# real snapshots onto undo_stack (that's the actual feature working correctly) -- clear both
	# stacks here so THIS section's own assertions about "nothing to undo yet"/"undo restores
	# exactly this section's own prior state" aren't operating against leftover history from
	# everything that ran earlier in the file, the same kind of isolation every other section here
	# already gets from starting with fresh, uniquely-positioned objects.
	main.undo_stack.clear()
	main.redo_stack.clear()

	await main._perform_undo()
	check_true(main.status_label.text == "Nothing to undo.", "undoing with an empty stack should say so, not silently do nothing or error")
	await main._perform_redo()
	check_true(main.status_label.text == "Nothing to redo.", "redoing with an empty stack should say so too")

	# --- Undo a spawn, redo brings it back ---
	main.spawn_block()
	check_true(main.scene_objects.size() == 1, "sanity check: spawning should add exactly one object")
	await main._perform_undo()
	check_true(main.scene_objects.is_empty(), "undoing a spawn should remove the object again")
	await main._perform_redo()
	check_true(main.scene_objects.size() == 1, "redoing should bring the spawned object back")

	# --- Undo a delete: the object comes back with its OWN real data intact, not a blank placeholder ---
	var undo_spawn_target: MeshInstance3D = main.scene_objects[0]
	undo_spawn_target.position = Vector3(5, 1, 2)
	var vertex_count_before_delete: int = (undo_spawn_target.get_meta("base_mesh_data")["Vertices"] as Array).size()
	main._select(0)
	main._delete_selected()
	check_true(main.scene_objects.is_empty(), "sanity check: delete should remove the object")
	await main._perform_undo()
	check_true(main.scene_objects.size() == 1, "undoing a delete should bring the object back")
	var restored_after_delete_undo: MeshInstance3D = main.scene_objects[0]
	check_close(restored_after_delete_undo.position.x, 5.0, "the restored object should have its own real position back, not a reset default")
	check_close(restored_after_delete_undo.position.y, 1.0, "the restored object should have its own real position back, not a reset default")
	check_close(restored_after_delete_undo.position.z, 2.0, "the restored object should have its own real position back, not a reset default")
	check_true((restored_after_delete_undo.get_meta("base_mesh_data")["Vertices"] as Array).size() == vertex_count_before_delete, "the restored object's real geometry should come back intact, not a blank placeholder")

	# --- Undo a destructive Edit Mode operation (Bevel): the exact topology reverts, not just "an
	# object exists again" ---
	main._select(0)
	main._toggle_edit_mode()
	var faces_before_bevel_undo: int = (restored_after_delete_undo.get_meta("base_mesh_data")["Faces"] as Array).size()
	var vertices_before_bevel_undo: int = (restored_after_delete_undo.get_meta("base_mesh_data")["Vertices"] as Array).size()
	main._cycle_selected_vertex()
	await main._apply_bevel_vertex(main.DEFAULT_BEVEL_RATIO)
	check_true((main.scene_objects[0].get_meta("base_mesh_data")["Faces"] as Array).size() != faces_before_bevel_undo, "sanity check: bevel should actually change the face count")
	await main._perform_undo()
	var bevel_undo_result: MeshInstance3D = main.scene_objects[0] # a NEW node -- undo rebuilds the whole scene, the old reference is freed
	check_true((bevel_undo_result.get_meta("base_mesh_data")["Faces"] as Array).size() == faces_before_bevel_undo, "undoing a bevel should restore the exact original face count")
	check_true((bevel_undo_result.get_meta("base_mesh_data")["Vertices"] as Array).size() == vertices_before_bevel_undo, "undoing a bevel should restore the exact original vertex count")
	main._toggle_edit_mode()

	# --- Undo a Join: the headline case this feature was built for -- Join deletes an object
	# outright and merges its geometry irreversibly; undoing it must bring BOTH objects back as
	# separate, independently-selectable objects again, not just "an object with the right vertex
	# count". Mirrors exactly what the real ArcoBASIC JOIN_INTO dispatch case does (push, then join).
	main.spawn_block()
	var join_undo_source: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	join_undo_source.position = Vector3(50, 0, 0)
	join_undo_source.name = "JoinUndoSource"
	main.spawn_cylinder()
	var join_undo_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	join_undo_target.position = Vector3(52, 0, 0)
	join_undo_target.name = "JoinUndoTarget"
	var object_count_before_join_undo := main.scene_objects.size()
	var target_faces_before_join: int = (join_undo_target.get_meta("base_mesh_data")["Faces"] as Array).size()

	main._push_undo_snapshot()
	var join_offset := main._join_object_into(join_undo_source, join_undo_target)
	check_true(join_offset >= 0, "sanity check: the join itself should succeed")
	await main._recompute_modifiers(join_undo_target)
	main._rebuild_scene_list()
	check_true(main.scene_objects.size() == object_count_before_join_undo - 1, "sanity check: join should reduce the object count by 1")
	check_true((join_undo_target.get_meta("base_mesh_data")["Faces"] as Array).size() > target_faces_before_join, "sanity check: the target should have gained the source's geometry")

	await main._perform_undo()
	check_true(main.scene_objects.size() == object_count_before_join_undo, "undoing a Join should bring BOTH objects back as separate objects again")
	var found_source_after_join_undo := false
	var found_target_after_join_undo := false
	for obj in main.scene_objects:
		if obj.name == "JoinUndoSource":
			found_source_after_join_undo = true
		if obj.name == "JoinUndoTarget":
			found_target_after_join_undo = true
			check_true((obj.get_meta("base_mesh_data")["Faces"] as Array).size() == target_faces_before_join, "the un-joined target should have its ORIGINAL face count back, not the merged one")
	check_true(found_source_after_join_undo, "the joined-away source object should be back after undo")
	check_true(found_target_after_join_undo, "the target object should still be present after undo, with its pre-join data")

	# --- A NEW action after an undo must invalidate the redo stack (standard undo/redo semantics,
	# not something to leave implicit) ---
	main.spawn_sphere()
	var count_before_redo_invalidation_test := main.scene_objects.size()
	main._select(main.scene_objects.size() - 1)
	main._delete_selected()
	await main._perform_undo() # brings the deleted sphere back
	check_true(main.scene_objects.size() == count_before_redo_invalidation_test, "sanity check: undo should restore the deleted object")
	check_true(not main.redo_stack.is_empty(), "sanity check: the delete we just undid should be sitting on the redo stack")
	main.spawn_cone() # a genuinely NEW action
	check_true(main.redo_stack.is_empty(), "a new action after an undo must clear the redo stack, not leave a stale future available")
	await main._perform_redo()
	check_true(main.status_label.text == "Nothing to redo.", "redo should correctly report nothing available after the stack was invalidated by a new action")

	# --- Undo a drag-based edit (vertex move): one snapshot per GESTURE (drag start), not per
	# drag-update frame -- confirmed by checking the exact restored position, not just "something
	# changed back". ---
	main.spawn_block()
	var drag_undo_target: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	# A small offset near the origin, not this file's usual large "spatial isolation" offset -- this
	# specific test needs main.camera.unproject_position to produce a real, usable screen position,
	# which only works reliably within the camera's own fixed 8-unit orbit distance around
	# camera_target (Vector3.ZERO) -- the same real constraint an earlier increment's own raycast
	# test hit and fixed the same way.
	drag_undo_target.position = Vector3(4, 0, 0)
	main.camera_target = drag_undo_target.position
	main._update_camera()
	main._select(main.scene_objects.find(drag_undo_target))
	main._toggle_edit_mode()
	var vertex_before_drag: Array = (drag_undo_target.get_meta("base_mesh_data")["Vertices"] as Array)[0].duplicate()
	main._begin_handle_drag({"mode": main.DragMode.VERTEX, "target": drag_undo_target, "vertex_index": 0}, main.camera.unproject_position(drag_undo_target.position))
	await main._update_vertex_drag(main.camera.unproject_position(drag_undo_target.position + Vector3(2, 0, 0)))
	main._end_drag()
	var vertex_after_drag: Array = (drag_undo_target.get_meta("base_mesh_data")["Vertices"] as Array)[0]
	check_true(float(vertex_after_drag[0]) != float(vertex_before_drag[0]), "sanity check: the drag should have actually moved the vertex")
	await main._perform_undo()
	var drag_undo_result: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1] # a NEW node -- undo rebuilds the whole scene
	var vertex_after_drag_undo: Array = (drag_undo_result.get_meta("base_mesh_data")["Vertices"] as Array)[0]
	check_close(float(vertex_after_drag_undo[0]), float(vertex_before_drag[0]), "undoing a vertex drag should restore its exact original position (X)")
	check_close(float(vertex_after_drag_undo[1]), float(vertex_before_drag[1]), "undoing a vertex drag should restore its exact original position (Y)")
	check_close(float(vertex_after_drag_undo[2]), float(vertex_before_drag[2]), "undoing a vertex drag should restore its exact original position (Z)")
	main._toggle_edit_mode()

	# --- The ArcoBASIC UNDO/REDO dispatch path, not just the direct _perform_undo/_perform_redo
	# calls every check above already used -- confirms the real script-facing command works too.
	var undo_script_object_count_before := main.scene_objects.size()
	main.spawn_wedge()
	check_true(main.scene_objects.size() == undo_script_object_count_before + 1, "sanity check: the spawn for this section should add exactly one object")
	await main._process_script_command("UNDO", {}, {}, {})
	check_true(main.scene_objects.size() == undo_script_object_count_before, "the ArcoBASIC UNDO command should undo the spawn, same as the direct call")
	await main._process_script_command("REDO", {}, {}, {})
	check_true(main.scene_objects.size() == undo_script_object_count_before + 1, "the ArcoBASIC REDO command should bring it back")
	main._select(main.scene_objects.size() - 1)
	main._delete_selected() # clean up after this section's own probe object

	# --- Golden asset: the Arco Archer signature model (content/ArcoArcher.a3d) ---
	# Two rounds of real, honest revision behind this file, both driven by REAL feedback once a
	# human could actually see it (this sandbox never could): v1 was a from-scratch Boolean-Union
	# primitive kitbash with a guessed pose -- "really bad" (geometry glitches, broken pose, bad
	# proportions, misplaced bow) once a real screenshot (arco3d/archer.png) was possible, then
	# "kinda... not a human" even after a T-pose rebuild fixed the guessed-pose problem. Current
	# version: a real CC0-licensed reference human model (Kenney "Blocky Characters", see
	# content/CREDITS.md) converted through this repo's own Blender Bridge exporter
	# (arco3d_blender_bridge/tools/convert_kenney_reference_human.py) and rigged with Arco3D's own
	# Humanoid preset inside Godot (tools/rig_and_export_arco_archer.gd) -- real dogfooding of the
	# RIGGING pipeline specifically, without also gambling on this sandbox's own from-scratch
	# modeling being any good sight-unseen. tools/build_arco_archer.gd (the original primitive-
	# kitbash builder) is kept in the repo as a real, working example of building a character using
	# ONLY Arco3D's own tools, even though it's no longer what's shipped as the actual asset. This
	# test is a real sanity check that the checked-in file still imports correctly, not a
	# re-verification of the rig math itself (covered exhaustively above against synthetic
	# fixtures) -- if this ever fails after a schema change, the asset needs regenerating via that
	# same two-step pipeline, not hand-patching.
	# Goes through the same _find_arco_archer_path() resolution the real "Load Arco Archer (Demo)"
	# button uses (see that function's own comment for the real bug this fixed: a headless export
	# does NOT pull content/*.a3d into the .pck, so this MUST check beside-the-executable first,
	# not just assume res:// always works) -- not a hardcoded res:// path, so this test would have
	# caught the actual reported bug if it had exercised the exported-binary lookup path too.
	var archer_path := main._find_arco_archer_path()
	if not archer_path.is_empty():
		var archer_result := A3DFormat.import_asset(archer_path)
		check_true(archer_result.get("Ok", false), "the Arco Archer golden asset should import cleanly: %s" % str(archer_result.get("Error", "")))
		if archer_result.get("Ok", false):
			var archer_root: Node3D = archer_result["Root"]
			var archer_children := archer_root.get_children()
			check_true(archer_children.size() == 2, "the Arco Archer asset should have exactly 2 components (body + bow), got %d" % archer_children.size())
			var archer_body: MeshInstance3D = null
			var archer_bow: MeshInstance3D = null
			for child in archer_children:
				if child.name == "ArcoArcher":
					archer_body = child
				elif child.name == "Bow":
					archer_bow = child
			check_true(archer_body != null, "expected a component named 'ArcoArcher'")
			check_true(archer_bow != null, "expected a component named 'Bow'")
			if archer_body != null:
				check_true(archer_body.has_meta("arco_rig"), "the archer body should have a real rig")
				if archer_body.has_meta("arco_rig"):
					var archer_bones: Array = (archer_body.get_meta("arco_rig") as Dictionary)["Bones"]
					check_true(archer_bones.size() == main.HUMANOID_RIG_PRESET.size(), "the archer's rig should be the full humanoid preset, got %d bones" % archer_bones.size())
					# The reference model's own rest pose (arms at its sides, not T-pose) is used
					# as-is -- no bone rotation is applied anywhere in this pipeline, so bind pose
					# should still carry zero rotation on every bone, same invariant as before, just
					# no longer specifically framed as "because it's a T-pose."
					for bone in archer_bones:
						var rotation: Array = bone["PoseRotationDegrees"]
						check_close(float(rotation[0]), 0.0, "%s should have zero pose rotation (no pose is applied anywhere in this pipeline)" % bone["Name"])
						check_close(float(rotation[1]), 0.0, "%s should have zero pose rotation (no pose is applied anywhere in this pipeline)" % bone["Name"])
						check_close(float(rotation[2]), 0.0, "%s should have zero pose rotation (no pose is applied anywhere in this pipeline)" % bone["Name"])
					# Every vertex should have gotten a real weight assignment -- catches an import
					# that silently produced a rig with no usable skinning (e.g. a weight-count
					# mismatch falling through to nothing) rather than trusting bone count alone.
					var archer_weights: Array = archer_body.get_meta("arco_vertex_bone_index", [])
					check_true(archer_weights.size() == (archer_body.get_meta("base_mesh_data") as Dictionary)["Vertices"].size(), "every vertex of the reference body should have a real weight assignment")
				var archer_aabb: AABB = archer_body.mesh.get_aabb()
				check_true(archer_aabb.size.y > 1.0, "the archer body should be a real, human-proportioned figure (AABB height > 1 unit), got %s" % archer_aabb.size.y)
				check_true(archer_aabb.size.y > archer_aabb.size.x, "a standing human figure (arms at its sides) should be taller than it is wide, got height %s / width %s" % [archer_aabb.size.y, archer_aabb.size.x])
			archer_result["Root"].queue_free()

		# Also exercise the actual discoverable, real UI entry point ("Load Arco Archer (Demo)")
		# via a res:// path, not just the lower-level A3DFormat parser above -- proves the bundled
		# resource resolves correctly through Godot's own res:// filesystem (works identically
		# whether running from source or a packaged .pck export) and that the real import call site
		# (Main._on_import_path_chosen, with its own post-import _recompute_modifiers fix) handles
		# a real multi-component file correctly, not just the single-object fixtures used earlier.
		await main._load_arco_archer_demo()
		check_true(main.scene_objects.size() == 2, "loading the Arco Archer demo should populate the scene with exactly 2 objects (body + bow)")
	else:
		# Not a failure -- a fresh checkout that hasn't run the builder tool yet, or a build
		# environment that deliberately excludes content/, shouldn't break the whole suite over a
		# demo asset. Regenerate via: godot --headless --script tools/build_arco_archer.gd
		print("(skipping Arco Archer golden-asset check -- %s not present)" % archer_path)

	if failures.is_empty():
		print("ALL CHECKS PASSED")
		quit(0)
	else:
		for failure in failures:
			print("FAIL: " + failure)
		quit(1)
