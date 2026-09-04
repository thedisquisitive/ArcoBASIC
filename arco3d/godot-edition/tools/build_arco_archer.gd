extends SceneTree

## Builds Arco3D's own signature model -- "a low-poly archer" (the user's own pick, after "Arco"
## as the Italian/Spanish word for "bow" was offered alongside a couple of other mascot concepts).
## Real dogfooding, not a hand-authored asset: built entirely through this app's own real,
## already-tested API (spawn_*, Boolean-Union kitbashing, Apply Modifiers, the Humanoid rig preset)
## -- the same operations a human would perform through the UI, just scripted here for a real,
## reproducible, regeneratable recipe (RFC-0050 Section 30's own "golden reference assets SHOULD be
## maintained in the repository").
##
## Run with:
##   godot --headless --script tools/build_arco_archer.gd
## Produces content/ArcoArcher.a3d.
##
## **Real revision after real human feedback, not a first-pass guess anymore**: v1 tried to guess
## an "archer draw" pose via hand-picked bone rotations with zero visual verification, and the
## user's blunt real assessment ("really bad" -- geometry glitches, broken pose, bad proportions,
## AND a misplaced bow, essentially everything) confirmed exactly the risk the original honest
## caveat named. The user's own direct fix: "make a t-posed human and a bow, then combine the two."
## This version does that -- the body is modeled ALREADY in a T-pose (arms built horizontal from
## the start), so the rig's BIND pose IS the T-pose with zero bone rotation needed, eliminating the
## whole class of "guess a rotation angle and hope it looks right" risk entirely. The Bow is a
## separate prop resting on the ground beside the figure, not glued to a hand -- T-pose hands don't
## grip anything, and computing a placement from a pose that no longer exists made no sense either.
##
## A real, concrete geometry bug was also found and fixed while rebuilding this, not just a vibe
## fix: v1's vertical arm cylinders were positioned at x=+/-0.275 against a torso whose own X
## half-width was ALSO 0.275 -- meaning the arms met the torso at best FLUSH, with zero or
## near-zero actual volume overlap, which is exactly the kind of marginal/degenerate Boolean-Union
## input that produces thin slivers, cracks, or z-fighting at the seam ("geometry glitches" was one
## of the reported problems). Every overlap below is now deliberately generous (way more than a
## bare epsilon) specifically to avoid that class of bug, not just to look chunkier.
##
## Still an honest first pass in one respect: proportions/scale below are real, deliberate numbers
## (documented per-part), not a repeat of "made up and never checked" -- but this sandbox still
## cannot render or screenshot anything, so the actual on-screen result is still unseen by me.

const MainScript = preload("res://scripts/Main.gd")
const A3DFormat = preload("res://scripts/A3DFormat.gd")

func _initialize() -> void:
	var main := MainScript.new()
	get_root().add_child(main)
	main._ready()
	await process_frame

	# --- Torso (Boolean base) ---
	# Half-extents (0.275, 0.30, 0.175), spanning Y 0.70 to 1.30 -- every other part's overlap
	# amount below is computed against these exact numbers, not eyeballed separately.
	main.spawn_block()
	var torso: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	const TORSO_HALF_X := 0.275
	const TORSO_HALF_Y := 0.30
	const TORSO_HALF_Z := 0.175
	const TORSO_CENTER_Y := 1.0
	torso.scale = Vector3(TORSO_HALF_X * 2.0, TORSO_HALF_Y * 2.0, TORSO_HALF_Z * 2.0)
	torso.position = Vector3(0, TORSO_CENTER_Y, 0)
	var torso_top: float = TORSO_CENTER_Y + TORSO_HALF_Y # 1.30
	var torso_bottom: float = TORSO_CENTER_Y - TORSO_HALF_Y # 0.70

	# --- Head: Sphere, generous overlap into torso_top (0.144, ~70% of the head's own radius) ---
	main.spawn_sphere()
	var head: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	var head_radius: float = 0.6 * 0.34 # spawn_sphere's own base radius (0.6) * this scale factor
	head.scale = Vector3(0.34, 0.34, 0.34)
	head.position = Vector3(0, torso_top - 0.144 + head_radius, 0)

	# --- Legs: vertical cylinders, feet at y=0, generous overlap into torso_bottom (0.15) ---
	var leg_radius := 0.10
	var leg_height := 0.85 # feet-to-hip-overlap-point, so top = torso_bottom + 0.15 exactly
	main.spawn_cylinder()
	var left_leg: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	left_leg.scale = Vector3(leg_radius / 0.5, leg_height, leg_radius / 0.5)
	left_leg.position = Vector3(-0.15, leg_height / 2.0, 0)

	main.spawn_cylinder()
	var right_leg: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	right_leg.scale = Vector3(leg_radius / 0.5, leg_height, leg_radius / 0.5)
	right_leg.position = Vector3(0.15, leg_height / 2.0, 0)

	# --- Arms: T-POSE -- horizontal cylinders (rotated 90 degrees about Z from spawn_cylinder's
	# own default Y-axis orientation), NOT vertical. This is the real fix for both the pose problem
	# (there is no pose to guess anymore -- T-pose IS the rig's own bind/rest state) and the
	# geometry-overlap bug: the arm's own INNER HALF is placed deep inside the torso (way past its
	# own center), guaranteeing large, unambiguous overlap volume for a clean Boolean fusion, with
	# the OUTER half extending out to the side as the actual visible arm.
	var arm_radius := 0.09
	var arm_half_length := 0.35 # total arm length 0.70
	var shoulder_y: float = torso_top - 0.12 # a bit below the very top, roughly shoulder height

	main.spawn_cylinder()
	var right_arm: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	right_arm.scale = Vector3(arm_radius / 0.5, arm_half_length * 2.0, arm_radius / 0.5)
	right_arm.rotation_degrees = Vector3(0, 0, -90) # points the cylinder's long axis toward +X
	right_arm.position = Vector3(TORSO_HALF_X, shoulder_y, 0) # center sits ON the torso wall, so
	# half the arm (length arm_half_length) is inside torso, half sticks out to the side

	main.spawn_cylinder()
	var left_arm: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	left_arm.scale = Vector3(arm_radius / 0.5, arm_half_length * 2.0, arm_radius / 0.5)
	left_arm.rotation_degrees = Vector3(0, 0, 90) # points the cylinder's long axis toward -X
	left_arm.position = Vector3(-TORSO_HALF_X, shoulder_y, 0)

	for part in [head, left_leg, right_leg, left_arm, right_arm]:
		await main._add_modifier(torso, {
			"type": main.ModifierOp.BOOLEAN,
			"operation": CSGShape3D.OPERATION_UNION,
			"target_id": int(part.get_meta("arco_id")),
			"enabled": true,
		})

	var pre_apply_faces: int = (torso.get_meta("computed_mesh_data") as Dictionary)["Faces"].size()
	var applied := await main._apply_modifiers_permanently(torso)
	if not applied:
		printerr("Apply Modifiers failed -- aborting build")
		quit(1)
		return
	print("Body kitbash applied: %d faces baked into a real, editable base mesh" % pre_apply_faces)

	# The five part objects are now fully baked into torso's own base_mesh_data (Apply Modifiers'
	# whole point) -- delete them for real rather than leaving them sitting around hidden. Applying
	# a modifier does NOT restore/delete its operand the way removing one does (see
	# _apply_modifiers_permanently's own comment: the operand has genuinely been consumed), so
	# without this they'd sit in the exported file as redundant, invisible, confusing duplicate
	# geometry at the same positions as what torso now already contains.
	for part in [head, left_leg, right_leg, left_arm, right_arm]:
		var part_index: int = main.scene_objects.find(part)
		if part_index >= 0:
			main.scene_objects.remove_at(part_index)
		# queue_free() only schedules removal for the end of the frame -- export_asset walks the
		# REAL scene tree (scene_root.get_children()) synchronously, right after this loop, with no
		# frame boundary in between, so a deferred free would still be present at export time (a
		# real bug caught by inspecting the actual exported file's own component list, not assumed
		# fixed). remove_child + free() is immediate.
		main.scene_root.remove_child(part)
		part.free()

	# --- Rig it as one coherent humanoid. NO pose rotations are applied -- the body was modeled
	# ALREADY in T-pose above, so the auto-fit bind pose IS the T-pose. Zero bone-rotation guessing,
	# zero risk of the class of bug that made v1's draw pose look broken.
	main._select(main.scene_objects.find(torso))
	await main._add_humanoid_rig()
	torso.name = "ArcoArcher"

	# --- Bow prop: a real, SEPARATE object resting on the ground beside the figure, not glued to a
	# hand -- a T-pose hand doesn't grip anything, and computing a placement from a pose that no
	# longer exists made no sense either. "combine the two" (the user's own words) means place them
	# together in the same scene, not force a held-prop pose this sandbox can't actually verify.
	main.spawn_cylinder()
	var bow: MeshInstance3D = main.scene_objects[main.scene_objects.size() - 1]
	bow.name = "Bow"
	var bow_half_height := 0.375
	bow.scale = Vector3(0.035 / 0.5, bow_half_height * 2.0, 0.035 / 0.5)
	bow.position = Vector3(0.55, bow_half_height, 0.15) # standing upright, beside the figure's own
	# footprint (legs sit within +/-0.25 of center; 0.55 clears that with real margin), feet-level
	bow.rotation_degrees = Vector3(0, 0, 8) # a slight lean, like a bow actually left standing up

	# --- Export the real golden asset ---
	var output_path := ProjectSettings.globalize_path("res://content/ArcoArcher.a3d")
	var export_error := A3DFormat.export_asset(main.scene_root, "ArcoArcher", output_path)
	if export_error == OK:
		print("Exported ", output_path)
		quit(0)
	else:
		printerr("Export failed: ", export_error)
		quit(1)
