extends SceneTree

## Second half of Arco3D's signature-model pipeline (first half:
## arco3d_blender_bridge/tools/convert_kenney_reference_human.py, which imports a real CC0
## reference human model into Blender and exports its bare geometry through this repo's own
## Blender Bridge exporter -- see content/CREDITS.md for the source/license).
##
## **Real revision after real feedback, the second time**: the FIRST signature model (a from-
## scratch Boolean-Union primitive kitbash) got blunt, honest human feedback -- "kinda... not a
## human" -- once an actual screenshot (arco3d/archer.png) made it possible to really look at it,
## on top of the earlier "really bad" round. The user's own direct question -- "are there no free/
## open license low poly human models we can utilize for the time being, and replace it when I'm
## skilled enough at the tool to make one from scratch" -- is the real, sensible call: fighting
## CSG-boolean kitbashing blind (no rendering/screenshot capability in this sandbox) had already
## produced two rounds of real, confirmed defects. A real, professionally-modeled, real CC0-licensed
## placeholder is honest engineering, not a cop-out -- and the from-scratch attempt
## (tools/build_arco_archer.gd) is kept in the repo as a real, working, documented example of
## building a character using ONLY Arco3D's own tools, even though it's no longer what's shipped as
## the actual signature model.
##
## Run with:
##   godot --headless --script tools/rig_and_export_arco_archer.gd
## Reads content/ArcoArcher_reference_body.a3d (regenerate via the Blender script above if the
## source model ever changes), produces the final content/ArcoArcher.a3d.

const MainScript = preload("res://scripts/Main.gd")
const A3DFormat = preload("res://scripts/A3DFormat.gd")

func _initialize() -> void:
	var main := MainScript.new()
	get_root().add_child(main)
	main._ready()
	await process_frame

	var body_path := ProjectSettings.globalize_path("res://content/ArcoArcher_reference_body.a3d")
	var import_result := A3DFormat.import_asset(body_path)
	if not import_result.get("Ok", false):
		printerr("Failed to import reference body: ", import_result.get("Error", ""))
		quit(1)
		return
	var imported_root: Node3D = import_result["Root"]
	for child in imported_root.get_children():
		imported_root.remove_child(child)
		main.scene_root.add_child(child)
		main.scene_objects.append(child)
	imported_root.queue_free()

	if main.scene_objects.is_empty():
		printerr("No objects came in from the reference body file")
		quit(1)
		return
	var body: MeshInstance3D = main.scene_objects[0]
	body.name = "ArcoArcher"

	# The imported mesh has no picking collider of its own (A3DFormat builds plain MeshInstance3D
	# nodes) -- give it one sized to the real geometry, matching the app's own real import call
	# site's own established pattern (Main._on_import_path_chosen).
	var aabb: AABB = body.mesh.get_aabb()
	var box_shape := BoxShape3D.new()
	box_shape.size = aabb.size
	main._add_picking_collider(body, box_shape, aabb.position + aabb.size / 2.0)

	print("Reference body imported: %d vertices (base_mesh_data synthesized+welded since the source file has no Construction chunk)" % (body.get_meta("base_mesh_data") as Dictionary)["Vertices"].size())

	# --- Rig it with Arco3D's own Humanoid preset -- the whole point of using a real dogfooding
	# pipeline instead of a hand-authored rig: this exercises the app's own auto-fit+automatic-
	# weighting exactly the way a real user's "Add Humanoid Rig" click would. ---
	main._select(0)
	await main._add_humanoid_rig()
	print("Rigged with the Humanoid preset (%d bones)" % main.HUMANOID_RIG_PRESET.size())

	# --- Bow prop: same simple, separate, ground-standing object as before -- not glued to a hand,
	# for the same reason as last time (this model's own rest pose has arms at its sides, not
	# gripping anything, and no pose is being guessed here either). ---
	main.spawn_cylinder()
	var bow: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	bow.name = "Bow"
	var bow_half_height := 0.4
	bow.scale = Vector3(0.035 / 0.5, bow_half_height * 2.0, 0.035 / 0.5)
	bow.position = Vector3(aabb.size.x * 0.5 + 0.35, bow_half_height, 0.1)
	bow.rotation_degrees = Vector3(0, 0, 8)

	# --- Export the final golden asset ---
	var output_path := ProjectSettings.globalize_path("res://content/ArcoArcher.a3d")
	var export_error := A3DFormat.export_asset(main.scene_root, "ArcoArcher", output_path)
	if export_error == OK:
		print("Exported ", output_path)
		quit(0)
	else:
		printerr("Export failed: ", export_error)
		quit(1)
