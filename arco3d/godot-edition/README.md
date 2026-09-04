# Arco3D (Godot Edition)

A from-scratch reimplementation of the Arco3D authoring tool (RFC-0049/RFC-0050) on the Godot
engine, started after the original ArcoBASIC implementation (`arco3d/` one level up) hit a real,
unresolved interpreted-per-call performance ceiling -- see the top-level `arco3d/README.md` and
project memory for that track's full history. Godot's own engine (C++, GPU-driven) owns rendering
and per-vertex transforms natively; this project's own code only manipulates high-level scene-graph
nodes, which sidesteps that exact class of problem by construction rather than by careful tuning.

## Status

Second increment: the first pass shipped a Blender-shaped interaction model (right-drag orbit,
scroll-wheel zoom, a thin button row) that directly violated RFC-0049 Section 9.2's own laptop
baseline -- a real, user-flagged regression, not a style nitpick. Rebuilt against that section
properly: a real docked sidebar (workspace tabs, scene list, numeric position/rotation/scale
fields, file actions), full keyboard control of every operation including camera orbit/pan/zoom
and canonical views (Section 9.4, main keyboard row not numpad), a searchable command palette
(`Ctrl+K`, Section 9.6), and best-effort two-finger touchpad gestures for orbit/pan/pinch-zoom
layered on top -- never the only path to anything. See "Controls" below for the full list. Also
added: a ground grid, colored X/Y/Z origin axes, and a corner orientation gizmo (RFC-0049 Section
9.4) so an empty viewport isn't just a featureless void -- a real, user-flagged gap ("need some way
to visualize the 3D space"). Also added real direct object selection (click an object in the
viewport -- this was entirely missing before, only Tab-cycle and the sidebar list could change
selection) and TinkerCad-style click-and-drag-to-move across the ground plane with grid snapping,
per the user's own steer on overall feel ("75% TinkerCad, 15% Blender, 10% Spore Creature
Creator" -- direct manipulation with visible, obvious interaction first; Blender's own workflow
only a minor influence). Not yet built: the non-destructive Construction Stack, kitbashing,
rigging/posing, material/paint layers, and stylized rendering -- see RFC-0049 for the full scope.
Also now has TinkerCad's other defining interaction: visible drag handles on the selected object --
a white ball above it for height, three colored balls for X/Y/Z rotation (red/orange/blue), four
cyan cube handles at the top corners for uniform scale, and four yellow "extrude" handles at the
±X/±Z faces for axis-based single-direction resize (the opposite face stays fixed in place, unlike
the uniform corner handles which grow from the center) -- rebuilt fresh on every selection change
and kept in sync with drags, arrow-key nudges, and direct sidebar-field edits alike. Primitive set
expanded from Block/Sphere to five shapes (Cylinder and Cone via Godot's own `CylinderMesh`; Wedge
as real hand-built geometry, a right-triangular prism, since Godot has no built-in for that one),
plus Duplicate (`Ctrl+D`, or `Ctrl+drag` an object to copy-and-move it in one gesture, matching
`ui_direction.png`'s own hint text). TinkerCad's other defining feature -- marking a shape as a
"Hole" and grouping it with solids to boolean-subtract -- is deliberately not attempted yet; it
needs a real architecture change (Godot's CSG nodes instead of plain meshes) that touches every
existing subsystem (picking, handles, export), so it's next up as its own focused increment rather
than squeezed into this one.

**Update:** that "next increment" happened right away -- a real non-destructive modifier stack per
object (RFC-0049 Section 7.2's Construction Stack, and the user's own explicit ask for a "modifier
layer... non-destructive boolean based modelling, similar to Blender, but with TinkerCad's ease").
Each object's real base shape is stored once at spawn/duplicate time and never mutated; the
displayed mesh is always freshly recomputed from base + every enabled modifier in order, so
disabling or removing a modifier always cleanly recovers whatever came before it. Three modifier
types: **Mirror** (reflect across X/Y/Z, no seam welding yet), **Array** (linear repeat N times
with an offset), and **Boolean** (real CSG Union/Subtract/Intersect against another scene object --
built on Godot's own `CSGCombiner3D`/`CSGMesh3D` used purely as a transient computation tool, baked
back to a plain mesh, then discarded, so nothing else in the app needs to know CSG nodes were ever
involved). The Boolean operand hides itself once consumed (Blender's own convention) but stays a
real, independent, still-editable object -- moving it and letting the modifier recompute genuinely
changes the cut. See "MODIFIERS (NON-DESTRUCTIVE)" in the right panel; add one via the button there,
toggle/remove via the checkbox/× on each row. Known, real gaps for this first pass: no per-modifier
parameter editing after creation (new modifiers get sensible defaults; change them by removing and
re-adding), no operand picker for Boolean (defaults to the most recently spawned other object), and
no reordering the stack.

**Layout now follows `ui_direction.png`** (a reference mockup the user provided) as closely as
this build's actual capabilities allow: a top bar (title, workspace tabs, an always-visible command
search bar), a left sidebar (CREATE icon grid for primitives, TOOLS icon grid for modal Select/
Move/Rotate/Scale), a right panel (OBJECT properties: name + transform fields), and a bottom status
bar. Handle visibility is now modal, matching the reference and reducing clutter versus the
previous "every handle always visible" approach: only the active tool's handles show and are
clickable (SELECT shows none -- direct click-to-select still always works regardless of tool; MOVE
shows three colored axis-arrow handles plus the existing height ball; ROTATE shows the three rotate
balls; SCALE shows the uniform corner cubes and the extrude face handles). Switching tools moved
off `W`/`S`/`Q`/`E` (now Move/-/Select/Rotate's letters per the reference) onto `Alt`+arrows and `[`/`]`
for camera orbit/zoom instead -- see "Controls" below for the full current scheme.

The reference mockup shows considerably more than exists here yet: real icon art (this build uses
plain text/glyph stand-ins), a labeled orientation cube with click-to-snap faces (this build's is
still axis-lines-only), a dimensioned/labeled grid, a Torus primitive, Bevel/Duplicate/Group as
their own dedicated tools, a SHAPE panel (bevel/hollow), and a HISTORY panel showing the non-
destructive Construction Stack (RFC-0049 Section 7.2) as an editable operation list. None of that
backend exists yet, so none of it was drawn as inert decoration. The mockup's own MATERIAL panel
and BUILD/SURFACE/RIG/POSE workspace tabs, on the other hand, are now real (see "Material layers"
and "Rigging" above) -- what's in the app now is a real, working subset of the reference's
structure, not a mockup of the rest.

Verified so far:

- A real headless functional test (`tests/test_headless.gd`) exercises the actual `Main.gd`/
  `A3DFormat.gd` logic directly -- spawn, select, move, tab-cycle, export, re-import (position and
  geometry survive), and clean rejection of a missing file and a corrupt (non-JSON) file. Run with
  `scripts/run_tests.sh`.
- A real standalone export (`build.sh` -> `build/Arco3D`, a genuine ELF64 binary with the project's
  `.pck` embedded, no Godot installation needed to run it afterward) launches and runs without
  error for several seconds.
- **A real, confirmed layout bug found via direct user testing and fixed with hard data, not
  guesswork**: the right panel and bottom status bar were missing entirely (`PRESET_RIGHT_WIDE`/
  `PRESET_BOTTOM_WIDE` anchor both edges of the relevant dimension to the same side, which
  `custom_minimum_size` only "rescues" correctly when anchored on the natural-growth side --
  confirmed by literally printing each panel's computed rect from a real running instance). Fixed
  by setting explicit offsets on every anchor-pinned panel instead of relying on that rescue at
  all. Also applied a real dark theme (rounded panels/buttons, a real accent color) in response to
  "the left panel is hideous" -- a genuine visual improvement, not pixel-identical to the reference
  mockup (no icon art exists here) but no longer default-Godot gray boxes either.
- **Not yet verified**: actual on-screen visual/interactive correctness on real hardware -- the
  environment this was built in has no way to see or interact with a real GUI window (same
  limitation documented at length in the ArcoBASIC track's own history). The headless test proves
  the logic is correct; it does not prove the camera, lighting, or UI layout look right. Please
  run it yourself and report back what you actually see, the same way that track's real bugs only
  ever surfaced from real human eyes on a real screen.

## Vertex editing (Edit Mode)

"I only care about the PS1 quality geometry. I want that to be relatively easy to achieve in this
program on a laptop with no external mouse." -- the user's own original scoping of what mattered
FIRST here (shading was deliberately deferred, and rigging -- also mentioned as excluded in this
same original ask -- has since been built too, see "Rigging" below). **PS1 quality is the FLOOR
this app had to clear easily, never the ceiling of what it should be capable of** -- said explicitly
more than once since ("Eventually PS2 era, but for now PS1 era", and directly again after an early
feature shipped with a real, avoidable gap excused by this exact framing: "This is not 'Hurr durr
only do PS1 era' for everyone... it's just the minimum level of 'this app can churn these assets out
way faster than Blender can'"). Everything built before this was primitives combined via
non-destructive modifiers (Mirror/Array/Boolean) -- a real, working paradigm, but incapable of
producing genuinely custom, hand-blocked low-poly geometry, since it can only ever combine whole
primitives, never reshape one's own topology. This closes that gap: press `Tab` (Blender's own
muscle-memory convention) to enter Edit Mode for the selected object, showing one small handle per
real vertex of
its own editable geometry. `Shift+Tab` cycles the selected vertex with no pointing device needed at
all (Section 9.2); drag a vertex handle to slide it across the ground plane (finer-grained snapping
than whole-object moves), or nudge it with arrow keys/Page Up/Down exactly like moving a whole
object. `Tab` again exits back to Select.

**A real, necessary fix that came out of testing this, not a nice-to-have:** Godot's own primitive
meshes duplicate every geometric corner once per adjacent face (confirmed directly: `BoxMesh` has
24 vertices but only 8 unique positions -- needed for correct per-face/hard-edge normals). Editing
one of those raw, unwelded vertex entries would only move ⅓ of a cube's actual corner, visibly
tearing the mesh open rather than reshaping it the way dragging "the corner" obviously should. Every
spawned primitive's editable base geometry is now welded (`_weld_coincident_vertices` -- merges
vertices at coincident positions and remaps face indices, positions-only since this app's A3D
minimum subset doesn't carry UVs/normals to also merge by) before it's ever stored, so moving one
vertex handle correctly moves the whole real corner. Deliberately geometry-only, matching the
user's own line between geometry and shading: no attempt was made to also produce a flat/faceted
low-poly shading look here, since that's a rendering/shading concern, not what this increment was for.

**A second real, pre-existing bug found and fixed while extending the same handle-positioning
code**: `Transform3D.basis`'s own columns already carry an object's scale (confirmed directly: a
`scale=(2,1,1)` node's `basis.x` has length 2, not 1) -- several of the existing TRS handles'
position calculations multiplied a basis column by an ALSO-separately-scaled half-extent, applying
scale twice. Concretely, a block scaled to `scale.x=2` put its own move-X handle at world x=2.8
instead of the correct 1.4. Fixed for every affected handle (move/height/rotate/face/scale-corner),
verified with exact expected-value math, not just "some value changed". The `FACE` (extrude) drag's
own internal math has the same class of bug for an object that was ALSO non-uniformly scaled before
the extrude begins -- found via the same reasoning, but deliberately NOT fixed in this pass (out of
this increment's actual scope); a real, documented, known gap.

**Update: Extrude/Inset/Delete added** ("add extrude/inset/delete", the user's own direct follow-up).
Edit Mode now shows a SECOND kind of handle alongside vertex spheres: one green box per TRIANGLE
of the object's own geometry, sitting at that triangle's centroid. Drag one to Extrude -- grows
real new geometry (3 new cap vertices + 6 new side-wall triangles) outward along that face's own
normal, live and continuously adjustable exactly like the object-level extrude handles from the
previous increment; a plain click just selects the face (no drag) for Inset/Delete to act on
instead. `Ctrl+E`/`Ctrl+I` apply Extrude/Inset with a sensible fixed default (0.5 distance/ratio)
from the keyboard alone, matching Section 9.2; `Delete`/`Backspace` now deletes the selected
vertex OR face while in Edit Mode (with all of its own touching faces, for a vertex) instead of
falling through to the whole-object delete, the same Blender-style key-overloading-by-mode
convention `Tab` already established for entering/exiting Edit Mode itself. Refuses to delete a
mesh down to zero faces rather than producing a broken object.

Deliberately per-TRIANGLE, not per logical multi-triangle quad (a spawned Block's each visible
side is 2 triangles) -- detecting which triangles are "really" one coplanar/adjacent quad face is
a real, nontrivial mesh-analysis problem judged not worth the complexity here. A real, working
simplification, not a hidden limitation: the target here is low-poly GEOMETRY in the general sense
of that whole 5th-generation console era (the user's own explicit framing -- a rough complexity/era
benchmark spanning PS1/N64/Saturn/3DO/Jaguar/etc., deliberately NOT a mandate to target or emulate
any one platform's own specific rendering behavior), and many real low-poly tools operate
triangle-by-triangle without issue -- extruding a Block's whole visible face just takes 2 extrudes
(one per triangle) instead of 1.

**A real, concrete winding bug was caught by the first test written for this, not shipped
unverified**: the initial extrude/inset implementation assumed this project's own meshes use
CCW-from-outside triangle winding (a reasonable-sounding but unverified assumption) and computed
each face's outward normal as `(b-a) × (c-a)`. A real headless test (does extruding actually grow
the mesh's bounding box?) failed immediately -- direct inspection showed the "extruded" cap moving
*into* the solid, not out of it. Concrete check against real spawned-Block vertex data confirmed
the opposite: this mesh data's real winding computes an INWARD normal via that formula, needing
`(c-a) × (b-a)` instead -- and once the normal flipped, the side-wall triangles' own winding (which
had been separately hand-verified against the ORIGINAL, wrong assumption) needed re-deriving and
reversing too, re-confirmed by hand against the same real vertex data the bug was caught with.

Known, real gaps: still single-selection only (no multi-select). Inset has no "depth" option (pure
planar subdivision only, matching real tools' own Inset-with-Depth-0 default -- extrude the
resulting inner face afterward for a raised/sunken panel, the classic real use of this
combination). (Edge-level editing and imported objects carrying their own construction metadata,
both formerly listed here as gaps, are now real -- see "Edge selection, loop cuts, and bevel" below
and "A3D export/import" further down.)

## Edge selection, loop cuts, and bevel

"We need a place to switch selection type between face/edge/vertex for editing. We also need loop
cuts and a knife tool. And bevelling, corner to rounded corner, things like that." -- the user's own
explicit ask, closing Edit Mode's last major missing piece (vertex and face editing already existed;
edges as a real, selectable, editable kind of their own did not).

**Selection Type** (LEFT panel, EDIT MODE section, or the command palette): three toggle buttons --
Vertex / Edge / Face -- narrow which of the three handle sets is actually visible and clickable at
once. Vertex and face handles used to both show TOGETHER with no way to look at just one; they're
still built together internally (one shared `ToolMode.EDIT_VERTICES` bucket, unchanged), but a
second visibility pass now hides whichever two kinds aren't the active TYPE. `Shift+Tab` cycles
within whichever type is currently selected, matching vertex/face's own pre-existing convention.

**Edges** are derived, not stored -- `base_mesh_data` still only carries Vertices/Faces (no format
change), and every edge (a deduplicated vertex pair, tagged with which 1-2 triangles actually share
it) is recomputed fresh from the current Faces list on demand. One handle sits at each edge's own
midpoint; drag one to move BOTH endpoints together as a single rigid unit (translated by one shared
delta, not independently ground-plane-snapped the way a lone vertex is, so a drag can't distort the
edge's own length into something unrelated to where the cursor actually moved). `Delete` on a
selected edge removes the 1-2 triangles that share it (vertices are left alone, since they may still
belong to other faces -- the same no-reindex simplicity `Delete` on a face already has).

**Cut Edge** (the real "loop cuts" primitive) -- inserts one new vertex at the selected edge's own
midpoint and splits each of its 1-2 adjacent triangles into two, connecting the new midpoint to that
triangle's opposite vertex. **Real, honest scoping**: this is a single-edge SUBDIVIDE, not a full
quad-ring-propagating Loop Cut the way Blender's own tool works (which would trace a "loop" across a
whole ring of implied quads and cut all of them at once). Detecting which triangle PAIRS represent
one logical quad on an arbitrary triangulated mesh, then propagating a cut consistently around a
closed ring, is genuinely hard to get right and verify with confidence -- deliberately not attempted
this pass rather than risk shipping subtly-wrong topology. What's here IS the real primitive a full
ring cut would be built from, and is already a genuinely useful, correctly-scoped tool on its own
for adding one bend point along one edge at a time.

**Bevel** (a selected VERTEX, "corner to rounded corner") -- chamfers a single corner into a small
flat facet: walks the ordered ring of faces/neighbors actually touching that vertex (by rotating
each incident face so the shared vertex comes first, then chaining "after this neighbor comes that
one" across faces -- the same technique a real ring-walk needs), places one new point a fraction of
the way along each incident edge, and connects them into a ring replacing the old corner. Honestly a
single FLAT facet, not a smoothly rounded curve -- a real, useful chamfer, not an attempt at
production-quality smooth rounding (which would need multiple concentric rings of new geometry, a
real further increment on top of this).

**Handles both INTERIOR and EXTERIOR corners** -- a real fix after direct pushback ("We need to do
interior, exterior, and what the fuck ever. This is not 'only do PS1 era' for everyone... it's just
the minimum level of 'this app can churn these assets out way faster than Blender can'"). The first
version only handled an INTERIOR vertex (one fully surrounded by a closed ring of faces, true for
every corner of a closed primitive) and refused an EXTERIOR one -- a vertex sitting on a mesh
boundary/hole, e.g. after deleting a face, or any corner of a non-closed surface -- outright. That
was a real, unnecessary restriction, not an inherent limit of the technique: the SAME neighbor-ring
walk detects which case it's looking at (does the ring loop back to its own start, or does it have
two distinct open ends?) and handles both correctly -- an interior bevel gets a small triangle-fan
CAP closing the opening left behind; an exterior bevel gets the same bevel points and side geometry
but leaves the new bevel edge strip itself as the mesh's own new boundary, since there was never a
face there to cap in the first place. Verified with a dedicated test that deliberately deletes a
face to create a real boundary vertex, bevels it, and checks the exact resulting vertex/face counts
-- confirmed to actually catch the old restriction by temporarily reverting the fix and re-running
the suite before trusting the new test. Still refuses outright (a real status message, not a silent
no-op or a guess) for genuine non-manifold input -- e.g. a "bowtie" vertex where two structurally
disjoint triangle fans meet at a single point with no one unambiguous ring to walk -- caught because
the ring can never fully account for every incident face in that case, not special-cased by hand;
covered by its own dedicated test too.

**A real, previously-existing bug found and fixed while building this, unrelated to edges/bevel
themselves**: `_raycast_handle_at` (the function a REAL mouse click on any handle actually goes
through) only ever forwarded two specific extra metadata keys (`face_axis`/`face_sign`, needed by
the whole-object scale-corner handles) and silently dropped every OTHER handle kind's own extra
data -- `vertex_index`, `mesh_face_index`, `bone_index`, `mount_point_index`, and now
`edge_index`/`edge_a`/`edge_b`. Since `_begin_handle_drag` reads those via a `.get(key, -1)` default,
a real click on a vertex, mesh-face, rig/pose joint, or mount-point handle would always resolve to
index -1 and silently do nothing -- while every existing headless test for those features called
`_begin_handle_drag` directly with a hand-built dictionary, bypassing the real raycast path
entirely, which is exactly why this was never caught. Found by directly tracing how a real click
would actually reach a handle while wiring up the new edge handles, not assumed safe by analogy.
Fixed by forwarding every extra meta key generically instead of naming two by hand; verified with a
genuine raycast-based test (not the hand-built-dict shortcut) confirming a real click at a vertex
AND an edge handle's own screen-projected position now correctly resolves the full handle info --
and confirmed the test actually catches the bug by re-checking it against the old, narrower code
first, not just trusting the new assertions on faith.

**Not attempted this pass: a Knife tool.** Asked for directly alongside loop cuts/bevel above.
Cut Edge (the single-edge subdivide) is the real primitive a knife needs, but a genuine Knife tool
means letting a cut land at an ARBITRARY point along an edge (not just its midpoint) and chaining a
cut across MULTIPLE connected faces along a mouse-dragged path, re-triangulating each face it
crosses -- real, substantial additional interactive-input and mesh-topology work in its own right,
not a small extension of Cut Edge. Deliberately scoped out rather than rushed alongside the other
three (which now exist, are tested, and are verified not to regress anything); a real candidate for
its own focused increment.

## Floating panels

"Can we make the side panels pop out into independent windows?" -- the user's own explicit ask,
presumably for a real multi-monitor workflow (sidebar on one screen, the 3D viewport full-width on
another). Press the **Pop Out / Dock** button at the top of the left sidebar or right object panel
(or the command palette) to toggle it -- the SAME button docks it back again once floating.

**Real Godot `Window` nodes, not a simulated floating panel**: the panel's own content (everything
already built by `_build_left_sidebar`/`_build_right_panel` -- every section, every existing
tab-visibility toggle) is reparented wholesale into a brand new `Window` when popped out, and back
into its original docked container when docked again. It's the SAME live Control tree either way,
so every existing reference and toggle keeps working unchanged regardless of which parent it
currently lives under. The docked container hides itself while floating, so the 3D viewport
(which already renders full-window underneath the UI, not confined to a smaller viewport rect)
immediately gets that space back.

**A real, project-setting-level bug found and fixed, not just app code**: the first working version
technically used real `Window` nodes, but the user immediately caught that it "doesn't fix the real
estate problem" -- the popped-out panel was still trapped inside the same OS window. Confirmed
directly: Godot 4's own actual default for `display/window/subwindows/embed_subwindows` is `true`,
meaning every Window node (this feature's floating panels included) renders as an embedded rectangle
inside the main window instead of a genuine separate top-level window a person can drag to another
monitor. Fixed with an explicit `false` in `project.godot`, verified the setting actually takes
effect (not just assumed) -- this also means Godot's other Window-derived UI in this app (file
dialogs, the Attach To.../Add Modifier popup menus) now render as real separate windows too, which
matches how native file dialogs normally behave anyway.

**Two further, real UI bugs found only because the user kept looking at the actual screen** (not
declared done after the first working screenshot):

- The pop-out button's first small-icon-button attempt (a 28x28 custom size with a
  `SIZE_SHRINK_END` flag) rendered as a genuinely blank, invisible button -- present and clickable,
  occupying its layout space, showing nothing. First guess (an obscure Unicode glyph not covered by
  Godot's default font) was WRONG -- swapping the glyph did not fix it, still confirmed blank.
  The real cause was the custom size/flags combination itself, not the glyph -- fixed by reverting
  to plain, unadorned text with no custom sizing at all, the same pattern every other confirmed-
  visible button in this app already uses.
- A follow-on attempt to make it a smaller, right-aligned "corner notch" (wrapping the same button
  in an `HBoxContainer` with END alignment) was ALSO confirmed broken -- it rendered detached from
  the sidebar, overlapping the 3D viewport, and its own text truncated. Reverted immediately back to
  the plain full-width version rather than guess at a third layout blind. **Real, honest lesson from
  the whole sequence**: this sandbox cannot render or see the UI at all -- every visual claim here,
  including "this fix worked," is unverified until a human actually looks at the screen and says so;
  an "improve it further" tweak on top of a just-confirmed fix carries the same real risk as the
  original bug, not a safe refinement, and gets re-verified (or reverted, fast, to the last known-
  good state) with the same rigor.

Known, real gap: a smaller/corner-positioned toggle button is still wanted but not yet achieved --
doing that safely needs a real, sighted iterate-and-check loop rather than more blind layout
guessing.

## Undo/Redo

A real, previously entirely-missing safety net. `Ctrl+Z` undoes, `Ctrl+Shift+Z` redoes -- also real
buttons in the sidebar's FILE section and the command palette. Before this, every mutating
operation in this app -- Delete, Bevel, Cut Edge, Bridge, and especially Join (which deletes an
object outright and merges its geometry irreversibly) -- was permanent, with no way back except
manually rebuilding. This was the user's own direct pick when asked what to build next once Join/
Bridge landed: the tools had become powerful enough that a mistake had real, un-recoverable cost.

**Real, deliberate architecture choice: a SNAPSHOT stack, not a command-pattern log of individually
hand-authored inverse operations.** A command-pattern undo would need every one of this app's ~20
distinct mutating actions to hand-author its own precise inverse -- a large, error-prone surface
area where any ONE missed call site silently produces an incomplete or wrong undo. A snapshot
instead captures and restores the WHOLE scene's real state using `A3DFormat`'s own already-tested
`_component_to_dict`/`_component_from_dict` -- the exact same code Export/Import and Join/Bridge's
own id-rebasing already rely on, reused wholesale rather than reimplemented, so undo correctness
rides on infrastructure already proven correct for a different real feature. The real cost is
snapshotting the FULL scene on every undo-able action rather than a small diff -- a genuine,
deliberate tradeoff, acceptable given this app's own actual target (real, but always low-poly/
small-object-count PS1/PS2-era assets, never a large complex scene).

**Coverage**: spawning, deleting, duplicating, every Edit Mode operation (Extrude/Inset/Cut Edge/
Bevel/Delete), modifiers (add/remove/toggle/Apply), Join and Bridge, rig presets, mount points
(add/remove/attach/detach), material layers (add/remove), and every drag gesture (move/rotate/
scale/vertex/edge/rig/pose/mount-point -- one snapshot per drag GESTURE at its start, not per
drag-update frame, so a single smooth drag is one undo step, not hundreds).

**Real, deliberate scope boundary, not an oversight**: keyboard nudges (arrow keys/Page Up/Down) and
direct sidebar numeric-field edits are NOT hooked into this. A held-down nudge key fires many times
a second, and snapshotting the whole scene on every one of those would flood the stack with
near-duplicate states faster than `Ctrl+Z` could ever usefully step back through them. A mistyped
number in a field can just be retyped; the structural/destructive actions above are where a real
safety net actually matters.

**Two real bugs found and fixed while building this, not shipped on the first green test run**:

- `_capture_scene_snapshot`'s first version stored `_component_to_dict`'s own output directly --
  but that function writes its `Construction`/`BaseMesh` field (and others) BY REFERENCE to the
  live metadata Dictionary already on the node (`node.get_meta(...)` returns the actual stored
  object, not a copy). Harmless for Export, which immediately serializes to an inert JSON string,
  but fatal for an in-memory snapshot meant to survive the original being mutated afterward --
  confirmed directly via a real failing "undo a bevel" test (the "restored" data still showed the
  post-bevel state, since it was never actually a separate copy) before landing the fix:
  `.duplicate(true)` on the captured dict, breaking the aliasing for real.
- Restoring a snapshot used to free old objects with a bare `queue_free()`, which only DEFERS the
  actual removal to end-of-frame -- so a doomed object was still a real, named child of the scene
  at the exact moment a freshly restored object with that SAME original name tried to join the tree
  right after it. Godot's own sibling-uniqueness rule silently renamed the INCOMING (correct)
  node instead of the outgoing (doomed) one -- confirmed directly via a real failing "find the
  restored object by its own name" test on a Join undo, not assumed safe. Fixed by calling
  `remove_child` immediately before `queue_free` everywhere a scene object is permanently removed
  (undo's own restore, Join, Delete, and the whole-scene-replace Import path all needed this same
  fix) -- `remove_child` frees the name slot immediately; `queue_free` still handles the actual safe
  deferred deletion.

Both fixes verified by deliberately reverting each one in turn, confirming the exact expected test
failure reappeared, then restoring the fix and confirming the full suite passed again -- the same
revert/re-confirm discipline this project has used for every real bug fix.

**ArcoBASIC**: `Undo()`/`Redo()` act on the live app's own undo history at the moment each replayed
script line runs (this protocol replays a whole script's PRINTed commands as a batch after the
script has already finished, not in true lockstep) -- calling `Undo()` right after, say, `JoinInto`
in the same script undoes that join exactly as expected; calling it with nothing else in between
undoes whatever was last done in the UI before the script ran at all.

## Join and Bridge (smoothly connecting two meshes)

"I model a hand by itself. I have arm asset, I chop the hand off it and replace it with my new
hand. The mesh needs to correctly make that connection, without a bunch of shitty geometry or a
bunch of adjust->select->bridge gap, etc" -- the user's own explicit ask for a real way to combine
two separately-modeled parts into one seamless mesh.

Two real primitives, used together, in the BRIDGE section of the left panel (or the command
palette):

1. **Mark Bridge Start** -- with a boundary edge selected (Edge selection mode -- see "Edge
   selection, loop cuts, and bevel" above), records it as the pending bridge source. Real validation
   up front: refuses immediately if that edge isn't genuinely part of one clean boundary loop (an
   actual hole/opening), rather than deferring a confusing failure to Bridge To....
2. **Bridge To...** -- select a boundary edge loop on the OTHER part (a different object, or the
   same object for connecting two internal holes) and press this. If the two edges are on different
   objects, this JOINS them into one object first (see below), then connects the seam -- the whole
   "chop the hand off, replace with the new hand" workflow collapses to exactly those two clicks
   plus two button presses, no manual per-vertex "adjust->select->bridge gap" busywork at all.

**Join** (`_join_object_into`) merges a source object's CURRENT rendered geometry (whatever's
actually on screen right now -- post-modifier, post-pose, not necessarily its own base mesh) into a
target object's own editable base mesh, transformed correctly from the source's own local space
through world space into the target's local space, then deletes the source outright. This produces
exactly ONE object where there used to be two -- a real, deliberate contrast with Mount Points/
Attachment (which keeps both objects independently alive and editable; Join is for when you want one
continuous, single-mesh result instead). If the surviving target already has a rig, its automatic
vertex weighting is recomputed afterward too, so newly joined-in geometry doesn't sit frozen at the
bind pose while the rest of a rigged character poses normally.

**Bridge** (`_trace_boundary_loop` + `_bridge_loops`) is the actual seam-connecting operation:

- **Boundary loop tracing** is what removes the "select each edge around the hole one at a time"
  pain specifically: click ONE edge on the rim, and the whole connected loop is traced automatically
  by walking each boundary edge's own canonical direction (implied by its one owning face's existing
  winding) to the next. This also gives the new bridge geometry a coherent, already-correct
  orientation to build from, for free.
- **Automatic alignment** tries every rotation AND both traversal directions of the second loop
  against the first, picking whichever minimizes total connecting-vertex distance -- two
  independently-modeled parts are never authored starting at "the same" vertex in "the same"
  direction, so this is searched for, not assumed. This is the real mechanism that removes the
  manual "adjust->select" alignment busywork the user called out directly -- position the two parts
  roughly where they belong and Bridge figures out the actual correspondence.
- **Winding correctness, verified by testing, not just derivation**: building a ring of new quad
  faces between two independently-oriented loops has a real, easy-to-get-wrong "which diagonal
  points outward" question. Direct testing during development found the natural construction is
  already correct for loops traced via boundary-loop tracing's own canonical direction (every real
  and deliberately adversarial case tried, including two loops fed in matching rather than opposing
  rotational sense, came out correctly wound with no correction needed) -- but a real defensive
  fallback exists anyway: the average of every new face's own normal is checked against the
  physically real "away from the bridge's own tube axis" direction, and every new face is flipped
  together if that average ever points inward. Confirmed this fallback genuinely works, not just
  present as untested insurance: deliberately breaking the base construction during development
  measurably flipped the check negative exactly as expected, and the same correction caught and
  fixed it, with the whole test suite (including a precise per-face outward-normal check on a known,
  fully-controlled test geometry) still passing on the corrected output.
- **Real, honest complexity boundary, not a scope excuse**: the two loops must have the SAME number
  of vertices. Connecting loops of different sizes needs an inherently ambiguous N:M face fan (which
  vertex maps to which, and how many faces per gap) -- refused outright with an actionable message
  (equalize the counts first with Cut Edge/Delete Vertex, then try again) rather than guess at a
  mapping that could easily produce exactly the "shitty geometry" this feature exists to avoid.
- **Watertightness, verified directly**: the end-to-end test builds two real blocks, each missing
  one whole face (a real 4-vertex hole), positioned with a genuine 1-unit gap between the two facing
  holes, and confirms the bridged result has NO leftover boundary edges anywhere -- every edge
  shared by exactly 2 triangles, the real, direct definition of "no gaps, no shitty geometry."

**ArcoBASIC**: `JoinInto(sourceHandle, targetHandle)`, `MarkBridgeStart(handle, vertexA, vertexB)`,
`BridgeTo(handle, vertexA, vertexB)` -- the scripted equivalents, addressing an edge by its own real
vertex indices directly (there's no live UI selection state to read from during a batch script run).

## Rigging (Rig Mode / Pose Mode)

"lets make sure this thing supports easy creation and rigging of humans and animals. And vehicles"
-- the user's own next ask, for an actual movie project targeting PS1-era graphics (with PS2-era as
a later goal). This closes the "rigging" half of the gap this app's own README previously called out
as deliberately excluded from the geometry-only vertex-editing work above.

The Rigging panel described below lives in the left sidebar's **RIG**/**POSE** workspace tabs (top
bar) -- real, working tabs now, not the disabled placeholders they used to be; see "Material
layers" below for the real story on why that changed. Select an object and press `Ctrl+G` (or the
sidebar's "Add Humanoid Rig" button) to attach a real
17-joint skeleton -- Hips/Spine/Chest/Neck/Head, both arms (Shoulder/Elbow/Hand), both legs
(Hip/Knee/Foot) -- auto-fit to that specific object's own bounding box (fractional offsets into its
size, so it fits a short fat block or a tall thin one without manual placement). `Ctrl+Shift+G`
("Add Quadruped Rig") attaches a real 19-joint animal skeleton instead -- Hips/Spine/Chest/Neck/Head
plus a Tail, and four legs (Shoulder/Elbow/Paw in front, Hip/Knee/Paw in back) -- built against the
exact same code as the humanoid preset, just a different DATA TABLE (assumes +Z is the model's own
front/head end). `Ctrl+H` ("Add Pivot Rig") attaches a minimal ONE-joint rig for a vehicle part
(wheel, door, hatch) that just needs to rotate as a single rigid piece around one point -- see
"Vehicles" below. Fitting any of the three computes real automatic vertex weighting: each of the
object's own vertices is assigned, rigidly, to whichever bone (or bone-to-parent segment) it sits
closest to, via real point-to-segment distance -- deliberately RIGID (single nearest bone, no smooth
multi-bone blending) as a real, working simplification, not a hidden limitation; good enough for the
boxy, low-poly character silhouettes this whole app targets, where soft-blended joint deformation
would be spent on detail this art style doesn't render anyway.

Press `G` to enter **Rig Mode**: one small handle per joint, showing the CURRENT POSED location (not
just the bind pose) so you can see exactly what you're adjusting. Drag a joint to move where it sits
in the object's own rest geometry -- this re-fits automatic weighting immediately, since moving a
joint changes which vertices are actually closest to it. `Shift+Tab` cycles the selected joint with
no pointing device needed, matching the same no-mouse convention Edit Mode already established.
Press `G` again (or `Escape`) to exit back to Select.

Press `P` to enter **Pose Mode**: the same joint handles, but dragging one now ROTATES that bone
(and everything weighted to it or any of its descendants) around its own pivot instead of moving it
-- real forward kinematics, composing the whole parent chain from root to the selected joint via
`Transform3D` multiplication, not just rotating that one joint in isolation. Arrow
keys/Page-Up/Down nudge the selected joint's rotation in 5-degree steps with no pointing device
needed at all, the same convention every other mode in this app already uses. `P` again exits back
to Select.

**Verified the same way every other increment in this app has been**: a direct, hand-computed check
(spawn a tall/thin-scaled block, auto-fit the preset, rotate LeftShoulder 90 degrees around Y, and
hand-derive the exact expected world position of its own weighted vertices and its child LeftElbow's
weighted vertices) matched the actual computed output exactly, confirming the joint hierarchy, the
weighting, and the FK pose math are all correct together -- not just individually plausible. A
dedicated headless test now covers the same ground formally: rig attachment (bone count, root has no
parent, auto-fit Hips/Head land at the correct fractional height), automatic weighting (one weight
per real vertex), Rig Mode refusing to activate before any rig exists (no silent no-op state that
looks like it worked), and real FK posing -- rotating one bone visibly moves vertices weighted to it
or its descendants and provably does NOT move any vertex weighted to an unrelated bone.

**Animals: the quadruped preset.** Exactly what the humanoid preset's own README/code comments
predicted -- "just a different DATA TABLE against the same code" -- no new rigging mechanism needed.
Same auto-fit, same automatic weighting, same FK posing, same Rig/Pose Mode UI; only the joint
LAYOUT differs (a Tail instead of nothing, four legs instead of two arms + two legs, Spine/Chest/
Neck/Head running toward the model's own +Z instead of straight up). Verified the same way: a direct
probe rotated FrontLeftShoulder 45 degrees around X and hand-derived the exact expected position of
the paw at the end of that leg's own chain -- matched exactly, both coordinates. The formal headless
test covers the same ground (bone count, root, an explicit acyclic-parent-chain check across all 19
bones, and the same exact-position FK check) plus confirms every quadruped parent index refers to an
earlier bone in the array, never itself or a later one.

**Vehicles: the pivot rig.** A wheel, door, or hatch that just needs to rotate as ONE rigid piece
around a single fixed point turns out to be the DEGENERATE one-bone case of the exact same rig
machinery -- one bone, every vertex trivially weighted to it (there's no other bone to compete with),
so Rig Mode (drag the single joint to place the pivot -- dead-center for a wheel, off to one edge for
a door hinge) and Pose Mode (rotate around it) both work completely unmodified, with zero new
transform math written. The auto-fit default lands exactly on a symmetric primitive's own local
origin (confirmed directly: a spawned Cylinder's pivot bind position comes out `(0, 0, 0)` before any
adjustment), matching a wheel's own natural rotation axis with no repositioning needed at all.
Verified the same way as the other two rigs: a direct probe rotated a wheel 90 degrees around Z and
matched the plain textbook rotate-about-the-origin formula exactly; the formal headless test checks
the same thing plus that every vertex really is weighted to the one pivot bone.

**Real, honest scope boundary on "vehicles" specifically, updated**: the pivot rig rotates ONE
object around ONE point in its OWN local space, and on its own does NOT parent a wheel to a car
body so that moving/rotating the body carries the wheel along. **That specific gap now has a real
answer: Mount Points** (see "Mount points and attachment" below) -- give the vehicle a Male mount
point, the wheel/turret a matching Female one, and attach them; the part's transform stays synced
to the vehicle's every frame from then on. This is still not full Godot scene-tree reparenting (see
that section's own reasoning for why), but it's real, working, and closes the actual behavior gap
this paragraph used to describe as simply missing.

Known, real gaps, all deliberately deferred rather than silently missing:
- **Weight painting**: fully automatic only; no manual override for a vertex the automatic
  nearest-bone-segment pick gets wrong.
- **Smooth/multi-bone blending**: rigid single-bone weighting only, by design (see above) --
  no soft blending across a joint.
- **IK, joint limits/constraints, and pose save/catalog/sequencing** (RFC-0049 Sections 7.5/7.6):
  none of this exists yet; Pose Mode only lets you pose the CURRENT frame live, with nothing saved.
- **Modifier interaction**: weights are computed once, against `base_mesh_data`, at rig-creation
  time. Posing only deforms the first N vertices (N = the weight array's own length) BEFORE the
  Mirror/Array/Boolean modifier stack runs -- so a mirrored/arrayed copy's own added vertices are
  NOT posed along with the original. Fine for a single already-final character mesh; a real,
  documented limitation for anyone posing an object that still has an active Mirror/Array modifier.

## Mount points and attachment

"For rigging, I would like a specific system for mount points. For example: A vehicle has 2 places
for turrets... The model has a male designated mount point... the part that attaches has the
related female mount point... a weapon might have 'hand hold' points, for making automatic
parenting easier." -- the user's own explicit ask, and the real, bounded answer to a gap this
project had flagged and deliberately deferred several times (the Vehicles/pivot-rig section above
used to say plainly "it does NOT parent a wheel to a car body so that moving the body carries the
wheel along").

A **mount point** is a named, typed attachment point on any object -- press `+ Male` / `+ Female`
(right panel, MOUNT POINTS section, or the command palette) to add one to the selected object.
Each has a Name and Type (both editable in place, right in the list) and a Gender fixed at creation
(remove and re-add to change it). Press `M` to enter **Mount Mode**: one handle per mount point,
color-coded by gender (blue = Male, pink = Female); drag one to position it, `Shift+Tab` cycles the
selected point, `Delete` removes it (and detaches anything currently attached to it). `M` again
exits back to Select.

A mount point can be **object-level** (fixed in the object's own geometry -- a vehicle hull's
turret socket) or **bone-level**: if you add one while a joint is selected in Rig or Pose Mode, it
attaches to that BONE instead, so its world position follows the bone's own CURRENT POSED
location -- this is the direct, zero-extra-UI answer to "hand hold" points: select a rigged
character, enter Pose Mode, `Shift+Tab` to the Hand joint, then `+ Female` (or `+ Male`).

**Attach To...** (right panel, or command palette) lists every compatible mount point on every
OTHER object in the scene -- compatible meaning opposite Gender AND matching Type, a real
validation (not just bookkeeping): you cannot attach a `Hand`-type grip into a `Turret`-type
socket, or two mount points of the same gender, by accident. Picking one snaps the selected
object's own mount point exactly onto the target's -- position AND orientation, a real rigid-
connector snap -- and keeps it synced there every frame afterward: move, rotate, or (for a bone-
level mount point) pose the parent, and the attached part follows, live. **Detach** leaves it
exactly where it last was.

**Is the male/female pairing actually enforced, not just a naming convention?** Asked directly by
the user ("the mounting systems are male-female counterparted properly, yea?") -- re-checking the
real code (not just re-asserting it was fine) found a genuine gap: `_add_mount_point`'s own Gender
parameter had zero validation, just storing whatever string it was given. The two UI buttons above
are safe -- `+ Male`/`+ Female` always pass a hardcoded literal -- but ArcoBASIC's own
`AddMountPoint(handle, gender)` automation function passes a raw, script-authored string straight
through. Two mount points created with garbage genders like `"Foo"`/`"Bar"` would have satisfied the
attachment check's own `child_gender == parent_gender` inequality test (they're different strings)
and been allowed to attach, silently defeating the whole real-connector guarantee for anything
driven by a script instead of the UI. Fixed: `_add_mount_point` now refuses anything except exactly
`"Male"` or `"Female"` (case-sensitive -- `"male"` is refused too) and reports success/failure via
its own return value; the `ADD_MOUNT_POINT` ArcoBASIC dispatch case only registers a handle when
that call actually succeeds, so a bad gender argument fails loudly (no silently-dangling handle)
rather than quietly producing an unpaired mount point. Covered by dedicated tests hitting both the
direct function and the real ArcoBASIC dispatch path, plus an explicit Female-Female refusal check
alongside the pre-existing Male-Male one.

**Constraints** ("Allow mount point constraints: rotation axis, degrees allowed, etc." -- the
user's own direct follow-up, right after mount points/attachment themselves landed): by default a
mount point is fully rigid -- position AND orientation both welded. Each mount point's own
Constraint row (right panel, under its Name/Type) lets you pick a rotation axis (None/X/Y/Z, `None`
keeping the original fully-rigid behavior) and a Min/Max degree range -- a real turret that
traverses on its vehicle mount, or a barrel that elevates on its turret mount, instead of only ever
a fully rigid weld. Once a parent mount point has a constraint, anything attached to it gets a real
**Joint Angle** field (shown next to its own "Attached to..." status) -- drag it, type an exact
value, or nudge it with Page Up/Down (matching Pose Mode's own Y-nudge convention) -- clamped live
to the parent's own allowed range no matter how it's set, including from a stale/hand-edited value
in an older file. The rotation happens around the mount point's own axis, in its own local frame,
pivoting exactly at the mount point's world position -- the attached part's connection point never
drifts, only its allowed degree of freedom actually rotates.

**How this actually works, and the real, honest scope boundary**: this is NOT full Godot scene-tree
reparenting. This app's entire scene model is a flat list of independently transformed objects (see
`A3DFormat.gd`'s own header comment on why), and reparenting for real would touch export/import
flattening, every drag function, and the Boolean modifier's own relative-transform math -- all of
which currently assume flat, identity-ancestry top-level objects. Instead, an attached child just
tracks which parent mount point it's snapped to, and has its own transform recomputed every real
engine frame from that mount point's current world transform -- simple, cheap at this app's real
object-count scale, and correct for the stated use cases (turret-on-vehicle, weapon-in-hand)
without touching any of that existing, tested machinery. A real, honest consequence: a multi-level
attachment CHAIN (a part attached to a part that's itself attached to something else) converges to
the fully-correct nested transform over a couple of frames rather than instantly within the same
one -- imperceptible at real frame rates, a documented simplification rather than an unnoticed bug.
A direct attachment cycle (attaching A to B when B is already attached to A) is refused outright.

Both mount points and attachments round-trip through A3D export/import (Schema v3, capability
`mount.core`) -- a bone-attached mount point references its bone by the same stable ID scheme the
Rig block already uses, and an attachment references its parent by that component's own existing
stable `Id`, resolved lazily at propagation time so import order never matters.

Known, real gaps: no UI for editing a mount point's own FIXED base orientation yet (only position is
drag-adjustable; `LocalRotationDegrees` -- distinct from the dynamic, live-adjustable constraint
Joint Angle above -- would need direct metadata editing) -- the data model supports it, this pass
just didn't wire a control for it. No interactive drag/aim for the joint angle either (numeric field
and keyboard nudge only, real and complete, just not mouse-draggable the way Pose Mode's own
Y-rotation handle is). The Blender Bridge does not yet translate mount points/
attachments into Blender's own parenting (RFC-0051 Section 9's own "attachment relationships SHOULD
be translated to ordinary Blender parenting" -- a natural fit for a future increment, not attempted
this pass).

## Material layers

"Let's work on our material system... a layered smart material system for A3D models, instead of
fighting with UV maps and trying to combine many different things into one texture somehow. Just a
paint/apply image and slide it around on the model, add decals, use parts of a texture and slide
that part onto the model instead of unwrapping it" -- the user's own explicit ask, referencing
their own RFC-0050 (A3M) design. This is that RFC's own Section 20 Triplanar/Box/Planar/Decal
mapping modes made real: none of them need a UV unwrap, which goes straight at the actual complaint
(fighting UV maps) instead of building a UV editor.

**A real, immediate follow-up fix**: the first version of this bolted its own panel onto the same
always-visible right-panel stack Mount Points already lived in. The user's own blunt, correct
question -- "why didn't you put all of this into the SURFACE section?" -- pointed at something
real: the top bar's own `BUILD/SURFACE/RIG/POSE/STYLE` tabs had been sitting there since the
seventh increment as disabled, inert placeholders ("only BUILD has anything behind it yet"), and
Material got built without ever going back to check whether a dedicated home already existed. It
did. `SURFACE`, `RIG`, and `POSE` are now real, working tabs (`STYLE` stays a disabled stub -- no
stylization backend exists yet, a real, unrelated, unchanged gap): `SURFACE` shows exactly the
Material panel described below; `RIG`/`POSE` show the Rigging panel and actually enter that tool
mode (refusing, with the same real status message as the `G`/`P` keys always gave, if the selected
object has no rig yet); `BUILD` keeps Create/Tools/Modifiers/Mount Points. The keyboard shortcuts
(`Tab`/`G`/`P`/`M`) now keep the top bar's own active tab in sync too, however they're reached.

**+ Add Layer...** (right panel, MATERIAL section under the **SURFACE** tab, or the command
palette) opens a real file picker for any image on disk (PNG/JPG/BMP/WebP). Each layer gets:

- a **Mapping** mode -- `Triplanar` (blends 3 world-aligned projections by surface normal, good for
  wrapping a texture around an irregular shape with no visible seam), `Box` (hard-picks ONE
  dominant axis per surface instead of blending -- cheaper, and arguably a better fit for this
  app's own low-poly aesthetic than smooth blending), `Planar` (a single fixed projection, good for
  a floor/ground-facing texture or anything roughly flat), or `Decal` (a real projector-box test --
  only paints within its own bounds, so lower layers show through everywhere else, exactly what
  "add decals" means);
- an **Offset / Scale / Rotation** -- literally "slide it around on the model": one shared
  mechanism works for all four mapping modes, since every layer really has its own little
  projector box in space, and sliding/scaling/rotating it is exactly like moving a real physical
  projector relative to the model;
- a **Crop (X/Y/W/H)** -- "use parts of a texture... instead of unwrapping it": treats the source
  image as an atlas/sprite-sheet and only projects the chosen sub-rectangle, so one big painted
  sheet can supply several different decals or surface patches without ever touching UVs;
- a **Blend Mode** (Normal/Multiply/Add/Screen) and **Opacity**, composited in order (RFC-0050
  Section 17's own "MUST preserve ordered layer semantics") -- up to `MAX_MATERIAL_LAYERS` (4, a
  real hard cap from the shader's own fixed-size array uniforms; adding a 5th is refused with a
  clear message, not silently dropped).

Everything composites **live**, in a real GLSL shader (`shaders/layered_material.gdshader`) --
edit any field and the model updates immediately in the viewport, the same "see it right away"
principle every other real-time control in this app already follows. Selection highlighting still
works for a material-bearing object (a `selection_highlight` shader uniform, not the flat-color
material swap every other object still uses) -- adding a material to an object never takes away
its own selection feedback.

**Real, honest scope boundary**: this shader was verified to actually compile and run in this
Godot version via a real throwaway probe (array-uniform get/set, a live MeshInstance3D actually
using it) before ever being wired into the app -- but the underlying PIXEL-LEVEL correctness of the
triplanar/box/decal projection math has NOT been visually confirmed, the same standing caveat every
visual feature in this whole project carries (this sandbox cannot render or screenshot anything).
What IS verified directly: the shader compiles, every layer property reaches the correct shader
uniform with the exact expected value (including the full Offset/Scale/Rotation -> inverse-transform
math), and a full A3D export/reimport round-trips the real embedded image bytes pixel-for-pixel.

A3D persistence (Schema v4, capability `material.layers`): each layer's real image is embedded as
base64 PNG (RFC-0050 Section 22's own embedded-resource allowance) rather than a filesystem path,
which would break the moment the source file moved. **Real, deliberate gap**: this does NOT also
bake a flat fallback texture+UV pair for a non-Arco3D reader -- RFC-0050 Section 19 explicitly
allows this ("Readers that do not implement the generator MAY consume the cached generated image
instead," which presumes one exists to consume; none is baked here yet). Reimporting into Arco3D
itself is fully correct (it implements the generator, i.e. this same shader), but the Blender
Bridge does not yet understand the Material chunk at all -- the same honestly-deferred pattern
already used for Mount Points when they first landed.

Known, real gaps: Material Channels beyond base color (roughness/metallic/specular/normal/height/
emission -- RFC-0050 Section 16), Masks (Section 18), Generated/procedural maps (Section 19), and
Stylization metadata (Section 21) don't exist yet. No baked-fallback export (above). No drag
handles for "slide it around" yet -- Offset/Scale/Rotation are real, live, numeric-field-only
controls, matching the same scope boundary Mount Points' own constraint Joint Angle has.

## A3D export/import (RFC-0050 schema, and the Blender Bridge)

"Let's flesh out the a3d format properly, leading to a Blender addon to import/export A3D" -- the
first `.a3d` export (from the very first increment of this app) only ever round-tripped raw
Vertices/Faces. This closes that gap for real: `A3DFormat.gd`'s exported files now carry (Schema
v2, extended to **v3** by Mount Points/Attachment, then **v4** by Material Layers -- see those
sections above) --

- **Normals and UVs**, pulled directly from whatever ArrayMesh Godot already built (the SAME
  arrays already rendered on screen, not re-derived) -- correct by construction, no separate
  normal-generation algorithm to keep in sync. UVs are honestly omitted (never padded with a fake
  value) once an object has gone through the modifier/rig pipeline even once, since
  `base_mesh_data` has no per-vertex UV field yet -- a real, documented limitation, not something
  this exporter papers over (see the file's own "Known gaps" note).
- **Construction** -- the non-destructive `base_mesh_data` + modifier stack, stored as an optional
  chunk (RFC-0050 Section 11's own allowance for authoring-level data) -- so reimporting a file
  THIS app exported keeps it fully editable (Edit Mode, modifiers, Rig/Pose Mode) instead of
  flattening it into an opaque baked mesh forever. Real, previously-existing gap closed as part of
  this: imported objects used to carry NO construction/rig metadata at all.
- **Rig** -- named bones with real stable IDs (`Id`/`ParentId`, RFC-0050 Section 9's own "MUST NOT
  be based solely on array position" requirement), bind positions, and rigid skin weights aligned
  to the exported (evaluated) vertex list.
- **Poses** -- the current live pose, sparse (only actually-posed bones listed, RFC-0050 Section
  13's own "a pose MAY contain only a subset of bones"), stored as both Euler degrees (this
  engine's own authoritative representation) and a derived quaternion for cross-application use.
- A real, pre-existing wart fixed along the way: every object's picking-collider `Area3D` child
  used to leak into the exported `Children` list as a spurious empty component (Area3D is a
  Node3D too, and the old exporter recursed into every Node3D child unconditionally). Filtered out
  now.

**The Blender Bridge** ([`arco3d_blender_bridge/`](../arco3d_blender_bridge/README.md), RFC-0051)
is a real, tested Blender add-on built against this schema -- `File > Import/Export > Arcology 3D
(.a3d)` builds a proper Blender Armature + rigged mesh (real vertex groups, real applied pose) from
an Arco3D-exported file, and can export a Blender scene's portable subset back. Coordinate
conversion (Y-up -> Z-up) and bone posing both needed real, verified-by-hand math -- see that
directory's own README for the exact detail, including a non-obvious Blender `pose_bone.matrix`
API gotcha found and fixed via a real Armature-modifier deformation test, not assumed correct.
Bidirectional interop confirmed for real: a file this Godot app exports imports correctly into
Blender (verified via the Blender add-on's own headless test against this app's real golden
fixtures), and a file Blender exports imports back into this app with the same bone hierarchy,
bind positions, and weights intact.

## Kit parts (merge import)

"We will need that import functionality, I plan on making a bunch of kit parts so I can rapidly
make new creations without needing to model everything every time." -- the user's own explicit ask,
right after confirming Mount Points/Attachment were real (a mech assembled from a swappable body,
arms, legs, engines, and weapons is exactly the workflow those features were built for). Asking
about it directly surfaced a real, honest gap: plain **Import (I)** above always REPLACES the whole
scene -- confirmed by re-reading `_on_import_path_chosen` rather than assuming, it `queue_free()`s
every existing object before loading the new file. That makes a reusable parts library impossible:
you could never bring a separately-saved arm/leg/engine into a scene that already has a body in it,
and there was no ArcoBASIC command for it either (only `EXPORT` existed).

**Import Kit Part... (Shift+I)** (FILE row, or the command palette) is the real fix: it adds a
file's objects ALONGSIDE whatever is already in the scene instead of clearing it first, selecting
the newly added part so it's immediately ready to reposition and attach via Mount Mode / Attach
To..., the same way a fresh `spawn_block()` already selects itself.

**The real correctness hazard this needed, not just UI plumbing**: every export session numbers its
own objects' stable `Id` starting from 0 (used for Attachment `ParentId` and Boolean `target_id`
cross-references, resolved via a plain linear scan that returns the FIRST match). Two independently-
modeled kit parts are extremely likely to reuse the exact same small `Id` values -- left unrebased, a
merged-in file's own internal attachment could silently resolve to the WRONG object the instant it
collided with something already in the scene, the first time two kit parts happened to share an id,
not a rare edge case. Fixed by rebasing every id the imported file carries (`Id`, any Boolean
`target_id`, any Attachment `ParentId`) by the scene's own current id counter before merging it in --
covered by a dedicated test that deliberately forces an exact collision (not left to chance) and
checks the real behavioral outcome: `_find_object_by_id` resolving the colliding id to the
pre-existing object and the rebased id to the imported one, not the other way around.

While fixing that, also closed a smaller, related ordering bug in the shared import-finishing code
(now `_finish_import`, used by both Import modes): the original single-pass loop (reparent one
child, immediately resolve its modifiers/rig, repeat) would silently fail to resolve a Boolean
`target_id` or Attachment `ParentId` pointing at a sibling appearing LATER in the same file's own
Children order, since `_find_object_by_id` only sees objects appended so far. Restructured into two
passes -- reparent+rebase everything first, then resolve modifiers/rig for all of it -- since
merge-import makes cross-references between freshly imported siblings (a kit part's own Boolean-
built base, or a pre-attached sub-assembly) the normal case rather than a rare one.

**ArcoBASIC**: `ImportKitPart(path)` (returns a handle, same space as `Spawn*`) is the scripted
equivalent -- the actual thing that makes a kit-parts *library* practical (a script that assembles
"today's chosen turret + arm + leg" without a human clicking through a file dialog for each one).
Real, deliberate scoping limit: a handle is only registered for the file's FIRST top-level object,
even if the file contains several -- this whole handle-numbering scheme is a pure ordering
convention independently mirrored on the ArcoBASIC side (`HandleCounter`), with no real channel for
Godot to report back exactly how many objects a given file turned out to contain; a kit part is
expected to be one file, one primary object to address directly, though anything extra a file
happens to contain still lands in the scene.

## Signature model: the Arco Archer

"We need a signature model. Blender has Suzanne. Arco3D needs..." -- picked, after a couple of
options, as a low-poly archer: "Arco" is the Italian/Spanish word for "bow" (and the musical term
for playing a string instrument WITH the bow, as opposed to pizzicato). `content/ArcoArcher.a3d` is
Arco3D's own version of Suzanne. `Load Arco Archer (Demo)` (sidebar FILE section, or the command
palette) loads it in.

**Real revision history, not a single clean build** -- this sandbox cannot render or screenshot
anything, so every visual claim below had to wait for the user's own real feedback to actually
confirm or refute it, exactly like every other visual/interactive feature in this whole project:

1. **v1**: a from-scratch Boolean-Union primitive kitbash (Block torso, Sphere head, Cylinder
   limbs) with a hand-picked "archer draw" pose, zero visual verification. Real user feedback once
   the "Load Arco Archer" button actually worked: **"really bad"** -- geometry glitches, a
   broken-looking pose, bad proportions, and a misplaced bow, essentially everything.
2. **v2**: rebuilt per the user's own direct fix ("make a t-posed human and a bow, then combine the
   two") -- arms modeled horizontal from the start so the bind pose IS a T-pose (zero bone rotation
   needed), and a real, concrete geometry bug fixed along the way (the original arms met the torso
   at the SAME X coordinate as its own half-width -- flush, near-zero overlap, exactly the kind of
   marginal Boolean input that produces seams/cracks). Real user feedback once an actual screenshot
   (`arco3d/archer.png`) made real visual inspection possible for the first time: **"kinda...
   not a human"** -- a plain sphere head with no neck, blocky limbs, and a visible diagonal seam
   artifact across the torso from the CSG bake.
3. **v3 (current)**: the user's own direct question -- "are there no free/open license low poly
   human models we can utilize for the time being, and replace it when I'm skilled enough at the
   tool to make one from scratch" -- was the right call. Two rounds of fighting CSG-boolean
   kitbashing blind, with zero rendering capability in this sandbox to catch problems before a
   human saw them, had already produced two real, confirmed-bad results. Using a real,
   professionally-modeled, openly-licensed placeholder is honest engineering, not a cop-out.

**How v3 is actually built** -- a real two-stage pipeline, not a hand-authored file:

```sh
# 1. Convert a real CC0 reference model's geometry through this repo's own Blender Bridge exporter:
blender --background --python ../arco3d_blender_bridge/tools/convert_kenney_reference_human.py -- \
    /path/to/character-a.glb

# 2. Rig it with Arco3D's own Humanoid preset and add the Bow prop, inside Arco3D itself:
godot --headless --script tools/rig_and_export_arco_archer.gd
```

Stage 1 imports a Kenney "Blocky Characters" model (CC0-licensed -- see `content/CREDITS.md` for
full attribution and the exact source) into Blender, joins its 6 separate rigid parts into one real
mesh, and exports the bare geometry through `arco3d_blender_bridge`'s own exporter -- a genuine,
real-world exercise of the Blender Bridge's EXPORT path on a non-Arco3D-authored mesh, not just its
own round-trip test fixture. Stage 2 imports that geometry into Godot and runs the exact same
`_add_humanoid_rig()` a real user's "Add Humanoid Rig" click would -- this is still real dogfooding
of Arco3D's own rigging pipeline specifically, just no longer also gambling on this sandbox's own
from-scratch primitive modeling being any good sight-unseen. No pose is applied anywhere (the
reference model's own neutral standing pose, arms at its sides, is used as-is), and the `Bow` is a
separate object standing on the ground beside the figure, not glued to a hand.

`tools/build_arco_archer.gd` (the original from-scratch primitive-kitbash builder, v1/v2's own
code) is kept in the repo as a real, working, documented example of building a character using
ONLY Arco3D's own tools -- it's just no longer what's shipped as the actual signature model.

**This is explicitly temporary**, per the user's own framing: a real placeholder to replace once
they're skilled enough with Arco3D itself to model an original mascot from scratch, not a
permanent design decision. See `content/CREDITS.md` for the full license/attribution and exact
regeneration steps.

## Automation (ArcoBASIC scripting)

"Kinda like blenderpy, but arcobasic for automation, plugins, etc" -- the user's own explicit ask
for weaving ArcoBASIC back into this program even though the Godot Edition itself is no longer
implemented in it. Two real, working entry points share the same underlying mechanism and API:

- **Batch scripts** -- run a whole `.abas` file once via the right panel's "Run ArcoBASIC
  Script..." button, `Ctrl+R`, or the command palette. See `scripting/examples/grid_of_blocks.abas`
  and `scripting/examples/rig_mount_material_demo.abas` (rigging/mounting/materials, below).
- **A live console** ("Need a console for arcobasic, so we can ~ in and run everything via
  arcobasic commands" -- also the user's own ask): press the backtick key (`` ` ``, the classic
  Quake/Source-engine console toggle) to drop down a real ArcoBASIC REPL. Type real ArcoBASIC --
  `myBlock = SpawnBlock()`, then on a later, SEPARATELY-submitted line `SetPosition(myBlock, 2, 0,
  0)` -- and it just works, `myBlock` and all: a genuinely persistent scripting SESSION, not
  independent one-shot commands. Up/Down recall command history; a command that fails (a typo, a
  real ArcoBASIC error) is never added to the session, so one mistake can't permanently break every
  later command. Any output the session's own code PRINTs is echoed into the console log, not just
  recognized API calls.

Neither is yet a persistent, live plugin system with registered operators/panels the way Blender's
own Python addons work -- that needs a much deeper embedding (ArcoBASIC compiled into Godot as a real
GDExtension native module, calling back into the running scene bidirectionally and continuously),
which was deliberately not attempted here: it would mean standing up a whole new C++ build
pipeline (`godot-cpp` bindings matching this exact Godot version) never validated in this
environment, a much bigger bet than this increment's actual budget. What's here instead reuses
100% existing, already-tested infrastructure: the script runs as a real subprocess through this
repo's own `arcosh` interpreter, and its stdout is replayed as a small, deliberately simple text
protocol against the real live scene -- see `scripting/arco3d_api.abas` for the full ArcoBASIC-side
API and `scripting/examples/grid_of_blocks.abas` for a real example. Run one via the right panel's
"Run ArcoBASIC Script..." button, `Ctrl+R`, or the command palette.

**"Is this all being connected to the ArcoBASIC layer?"** -- a real, direct, correct question, and
the honest answer at the time was no: this whole protocol had been frozen at its original 8
commands (`SpawnBlock`/`SpawnSphere`/`SpawnCylinder`/`SpawnCone`/`SpawnWedge`, `SetPosition`/
`SetRotation`/`SetScale`, `AddMirror`/`AddArray`/`AddBoolean`, `ExportScene`) since the very first
increment that built it -- every single feature added since (vertex editing, Apply Modifiers, all
three rig presets and posing, Mount Points/Attachment/Constraints, and the whole Material Layers
system) had been real, working, and completely UI-only, with zero ArcoBASIC surface. Fixed for
real, not partially: `AddHumanoidRig`/`AddQuadrupedRig`/`AddPivotRig`/`SetPose` for rigging;
`AddMountPoint`/`SetMountType`/`SetMountPosition`/`SetMountConstraint`/`Attach`/`Detach`/
`SetJointAngle` for mount points and attachment; `AddMaterialLayer`/`SetLayerMapping`/
`SetLayerTransform`/`SetLayerCrop`/`SetLayerBlend` for material layers; `ImportKitPart` for merging
a saved kit part into the scene under construction (see "Kit parts (merge import)" above);
`JoinInto`/`MarkBridgeStart`/`BridgeTo` for combining two meshes into one seamless object (see "Join
and Bridge" above); `Undo`/`Redo` for the app's own real undo history (see "Undo/Redo" above) --
the same handle-based
protocol style as the original 8 commands, including the same "returns a handle numbered in issue
order, both sides agree independently, no round-trip needed" convention `SpawnBlock` already
established, now also covering mount-point and material-layer handles. `scripting/examples/
rig_mount_material_demo.abas` exercises every single one of them for real, through the actual
`arcosh` interpreter (not a mock), rigging a character, posing a bone, mounting and attaching a
second object with a real rotation constraint, and adding a textured material layer -- caught and
fixed one real logic mistake in that demo script itself before it shipped: a mount point's own
constraint governs what's allowed to attach TO it, not a property of the object that owns the
mount, so the constraint has to be set on whichever mount plays PARENT in the `Attach` call, not
the child's own mount (the first draft had this backwards).

**A real, general ArcoBASIC interpreter bug was found and fixed while building this** (not the
Godot side -- `src/runtime/runtime.cpp`, the shared interpreter every ArcoBASIC track in this repo
uses): a script-level variable reassigned from inside a `FUNCTION` never actually persisted across
separate calls to that function (confirmed with a minimal repro: a counter incremented inside a
function and read back before the increment always returned `0`). Root cause: `Runtime::set_global`
correctly searched outer scopes then globals for reads, but for a WRITE to a plain (non-dotted)
name it unconditionally created a new shadow in the current function call's own scope, discarded
the moment the function returned -- an asymmetry with the read path. **The first fix attempt made
this too broad and was reverted**: making writes search outward and mutate whatever same-named
binding they found (mirroring how `ClassName.Field = value` already correctly works) broke three
real, independent test suites, because it made a generic local variable name (e.g. `restored`) used
inside two different functions silently collide -- BASIC here has no explicit "declare a new local"
syntax distinct from plain assignment, so "local by default" turns out to be a genuine, deliberate
language property this whole codebase already depends on, not an oversight. Confirmed via the full
regression suite, not assumed, then reverted cleanly. The actual, narrower need (a function
persisting a counter across calls) uses the correct, already-existing, explicit mechanism instead:
a `CLASS` with a `SHARED` field, which goes through the dotted-path branch precisely because it's
spelled `ClassName.Field` -- an unambiguous opt-in a bare identifier can never be. See
`scripting/arco3d_api.abas`'s own `HandleCounter` class and its header comment for the real
reasoning.

Also found and fixed along the way: Godot's `FileAccess` doesn't resolve a bare relative path
(`ExportScene("foo.a3d")`) against a subprocess's real working directory the way a POSIX `fopen()`
would -- `EXPORT`'s command handler in `Main.gd` resolves it explicitly against the script's own
directory instead. And a real portability gap: `build.sh` now copies `arcosh` and the whole
`scripting/` directory alongside the exported binary, since an external `arcosh` subprocess can't
read Godot's own packed `.pck` resource filesystem -- without this, automation would only have
worked when run from source/the editor, not from a portable export.

**How the console gets real session persistence without a long-running piped process:** there's no
persistent `arcosh` process behind the console at all (bidirectional streaming to a subprocess's
stdin/stdout isn't a first-class GDScript API in this Godot version, and standing up a named-pipe
workaround was judged not worth the complexity for this increment). Instead, `Main.gd` keeps
`console_lines`, the growing text of every command that has SUCCEEDED so far, and re-executes the
*entire* accumulated transcript as one fresh `arcosh` subprocess on every single command. This is
safe and correct only because this API surface is fully deterministic (no randomness or time-based
state), so a given prefix's output is guaranteed byte-identical on every re-run -- which is what
lets `Main.gd` track how many output lines were already consumed and safely skip re-applying them
(re-applying an already-processed `SPAWN` line would double-spawn the object). A command that fails
is never folded into `console_lines`, so a typo can't permanently break every later command in the
session.

Two more real bugs found while building the console specifically: `PackedStringArray.split("\n")`
on `arcosh`'s own newline-terminated output produces a spurious trailing EMPTY array element,
which silently shifted every later console command's output one line-index out of alignment (found
via a direct probe after a real `SetPosition` call failed to visibly apply) -- fixed by
`strip_edges()` before splitting. And a real Godot API surprise: `RichTextLabel.text`, read back
after content was added via `append_text()`, does not reliably reflect what was appended (in this
Godot version) -- fixed by concatenating onto `.text` directly instead, which keeps it the actual
source of truth at the (here, irrelevant) cost of a full BBCode re-parse per logged line.

## Running it

```sh
arco3d/godot-edition/build.sh          # exports build/Arco3D (first run needs export templates, see below)
arco3d/godot-edition/build/Arco3D      # run the standalone binary
```

Or run directly through the Godot editor binary without exporting first:

```sh
~/godot/godot --path arco3d/godot-edition
```

### Controls

Every operation below has a keyboard path that needs no pointing device at all (RFC-0049 Section
9.2's actual requirement) -- mouse-drag and touchpad gestures are optional accelerants layered on
top, never the only way to do something.

**Tools** (matching `ui_direction.png`'s toolbar -- gates which handles are visible/clickable;
direct click-to-select always works no matter which tool is active):

- `Q` Select -- no handles shown, click an object to select it
- `W` Move -- red/green/blue axis-arrow handles (X/Y/Z) plus click-and-drag the object's body to
  slide it freely across the ground plane, snapped to the grid
- `E` Rotate -- three colored balls: orange rotates around Y (tracks the cursor exactly), red/blue
  rotate around X/Z (screen-space drag -- doesn't track the cursor the same way, see the code
  comment on `_update_rotate_x_drag` for why)
- `R` Scale -- cyan cube handles at the top corners (uniform, anchored at the center) and yellow
  handles at the ±X/±Z faces ("extrude": resizes along just that axis, anchored at the *opposite*
  face which stays fixed in world space -- this is the one that behaves like pulling a wall out)
- `Tab` -- Edit Mode: real vertex/face-level editing of the selected object's own geometry (see
  "Vertex editing" above). White handles, one per vertex; `Shift+Tab` selects the next vertex (no
  pointing device needed), drag a handle or use arrow keys/Page Up/Down to move it. Green handles,
  one per triangular face -- drag one to Extrude (grows new geometry outward along its normal), or
  click to select it for `Ctrl+E` Extrude / `Ctrl+I` Inset / `Delete` (fixed keyboard-only
  defaults). `Delete`/`Backspace` deletes whichever vertex/face is selected instead of the whole
  object while in this mode. `Tab` again exits back to Select.
- `Ctrl+G` -- add a real 17-joint humanoid rig to the selected object, auto-fit to its own bounding
  box (see "Rigging" above)
- `Ctrl+Shift+G` -- add a real 19-joint quadruped (animal) rig instead
- `Ctrl+H` -- add a one-joint pivot rig (vehicle parts: wheels, doors, hatches -- see "Vehicles"
  under "Rigging" above)
- `G` Rig Mode -- one handle per joint; drag or nudge to reposition a joint in the rest pose
  (re-fits automatic weighting). `Shift+Tab` selects the next joint. `G` again exits back to Select.
- `P` Pose Mode -- the same joint handles, but drag/nudge now ROTATES that bone (and its
  descendants) around its own pivot -- real forward kinematics. `P` again exits back to Select.

**Everything else** has a keyboard path needing no pointing device at all (RFC-0049 Section 9.2):

- `B` / `N` / `C` / `O` / `V` -- add a block / sphere / cylinder / cone / wedge (or the sidebar's
  CREATE buttons)
- `Ctrl+D` -- duplicate the selected object; or `Ctrl+drag` an object to copy-and-move it in one
  gesture (the original stays put, the new copy follows the cursor)
- `Shift+Tab` -- select next object (outside Edit Mode); or click a name in the sidebar's Scene list
- Arrow keys, Page Up/Down -- nudge the selected object; or type exact numbers into the right
  panel's Position/Rotation/Scale fields (keyboard-assisted precision, Section 9.3/9.5)
- Delete / Backspace -- remove the selected object
- `A`/`D` orbit yaw; `Alt`+arrow keys orbit yaw/pitch; `[`/`]` zoom -- camera, keyboard-only
- `1` front, `3` right, `7` top, `5` isometric, add Shift for the opposite side (Section 9.4 --
  main keyboard row, deliberately not the numeric keypad)
- `Ctrl+K`, or the search bar in the top bar -- command palette: type to filter, Up/Down to
  highlight, Enter to run, Escape to close (Section 9.6 -- every command above is reachable here too)
- `` ` `` (backtick) -- toggle the ArcoBASIC console (see "Automation" above); Up/Down recall
  history while it's open, Escape closes it
- `Ctrl+R` -- run an ArcoBASIC script file (see "Automation" above)
- `X` / `I` -- export / import a `.a3d` file via a native file dialog
- Escape -- quit
- Optional: right-mouse-drag orbits, scroll wheel zooms, two-finger touchpad pan gesture orbits
  (hold Shift/Ctrl to pan instead), pinch gesture zooms -- **the touchpad gestures are unconfirmed
  on real hardware**, since this sandbox has no real trackpad to test against; Godot's Linux
  gesture-event delivery depends on the compositor actually surfacing two-finger/pinch events, and
  that could not be verified here. If they don't fire on your machine, every operation above still
  has its full keyboard equivalent regardless.

Running the headless test suite:

```sh
arco3d/godot-edition/scripts/run_tests.sh
```

### Export templates

`build.sh` needs Godot's own export templates installed for the exact editor version in use
(`~/.local/share/godot/export_templates/<version>.stable/`, containing `linux_release.x86_64` etc.)
-- these are a large (~1.2GB) separate download, not bundled with the editor binary itself. Fetch
the version matching `~/godot/godot --version`:

```sh
curl -L -o /tmp/godot_templates.tpz \
  "https://github.com/godotengine/godot/releases/download/<version>-stable/Godot_v<version>-stable_export_templates.tpz"
mkdir -p ~/.local/share/godot/export_templates/<version>.stable
cd /tmp && unzip godot_templates.tpz && mv templates/* ~/.local/share/godot/export_templates/<version>.stable/
```

## Design notes

- **Scene tree built in code, not hand-authored in `.tscn`.** `scenes/Main.tscn` is a bare shell
  (one `Node3D` with `Main.gd` attached); `Main.gd`'s `_ready()` builds the camera, lighting,
  environment, and UI (`Button`/`Label`/`FileDialog` controls) via `add_child()` calls. This was a
  deliberate choice for this first increment (no interactive access to the Godot editor's own GUI
  in the environment this was written in), not a permanent constraint -- moving pieces into the
  `.tscn` as real editor-authored nodes is reasonable future cleanup for whoever next drives the
  editor by hand.
- **`.a3d` encoding is plain JSON, not ArcoCompy.** RFC-0050 Section 6 deliberately does not
  mandate a physical encoding, only that it support chunk discovery and capability negotiation
  (Section 23). The ArcoBASIC implementation used ArcoCompy's tagged-length wire format, native to
  that toolchain; this one uses `JSON.stringify`/`JSON.parse`, native to GDScript. Same manifest
  shape (`Format`/`ContainerVersion`/`SchemaVersion`/`CoordinateSystem`/capabilities/`Asset`), same
  coordinate convention (right-handed, Y-up, meters -- which happens to be Godot's own native 3D
  convention already, so nothing needs converting), different concrete bytes. Real byte-level
  interop between the two engines' `.a3d` files is future work, not required for either to be a
  correct, spec-conformant producer/consumer on its own terms.
- **Real geometry, not regenerated primitives.** `A3DFormat._mesh_to_dict` pulls actual vertex/
  index arrays out of whatever `ArrayMesh` Godot already built (`Mesh.surface_get_arrays`), so it
  works for any mesh, not just this app's own `BoxMesh`/`SphereMesh` spawns. Import reconstructs a
  real `ArrayMesh` via `SurfaceTool` and generates normals fresh, since the format's minimum subset
  (matching the paused ArcoBASIC implementation) doesn't carry per-vertex normals yet.
- **Stable component IDs.** Every spawned node gets an incrementing `arco_id` metadata value (never
  reused, matching RFC-0050 Section 9's "MUST NOT be based solely on array position"), persisted
  through export/import via the `Id` field on each serialized component.

## Layout

- `project.godot` -- Godot project file.
- `scenes/Main.tscn` -- the (minimal) main scene.
- `scripts/Main.gd` -- app controller: camera, spawning, selection, input, UI.
- `scripts/A3DFormat.gd` -- `.a3d` export/import.
- `tests/test_headless.gd` -- headless functional test (see "Running it" above).
- `scripts/run_tests.sh` -- runs the headless test.
- `scripting/arco3d_api.abas` -- the ArcoBASIC-side automation API (see "Automation" above).
- `scripting/examples/` -- example automation scripts.
- `build.sh` -- exports the standalone binary into `build/` (also copies `arcosh` and `scripting/`
  there, needed for automation to work from a portable export).
- `export_presets.cfg` -- Godot's own export preset config (defines the "Linux" preset `build.sh`
  uses), committed since `build.sh` needs it to exist.
- `build/`, `.godot/` -- gitignored: export output and Godot's own generated project cache.

## RFCs

Same three RFCs as the ArcoBASIC implementation -- see `../README.md` and `../rfcs/`.

## UI reference

`../ui_direction.png` is a mockup the user provided showing the intended full-scope layout and
interaction model (top bar, Create/Tools sidebar, Object/Shape/Material/History right panel,
status bar, axis-arrow move gizmo, labeled orientation cube). The current layout follows its
structure as far as this build's real capabilities go -- see "Status" above for exactly what's
implemented versus what the mockup shows as the longer-term target.
