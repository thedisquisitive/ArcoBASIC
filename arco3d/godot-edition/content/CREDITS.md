# Content Credits

## Arco Archer reference body (`ArcoArcher_reference_body.a3d`, baked into `ArcoArcher.a3d`)

Arco3D's signature model uses a real, free, openly-licensed reference human model as a placeholder
-- the user's own explicit call, after two rounds of a from-scratch primitive kitbash getting real,
honest "this isn't working" feedback ("really bad", then "kinda... not a human") that this sandbox
had no way to visually catch before shipping. **This is a temporary stand-in, to be replaced once
the user is skilled enough with Arco3D itself to model an original character from scratch** -- not
a permanent design decision.

- **Source**: Kenney "Blocky Characters" -- <https://kenney.nl/assets/blocky-characters>
- **Author**: Kenney (<https://kenney.nl>)
- **License**: CC0 1.0 Universal (Creative Commons Zero) --
  <https://creativecommons.org/publicdomain/zero/1.0/>
  > You can use this content for personal, educational, and commercial purposes. Support by
  > crediting "Kenney" or "www.kenney.nl" (this is not a requirement).
- **What was actually used**: one character (`character-a.glb`) from the pack's GLB-format
  models -- six separate rigid mesh parts (head/torso/2 arms/2 legs, no built-in armature),
  joined into one mesh in Blender, rescaled, and exported through this repo's own
  `arco3d_blender_bridge` add-on. The pack's own textures/materials were NOT carried over (Arco3D's
  A3D format has no material-authoring support yet, a real, separately-documented gap -- see
  `scripts/A3DFormat.gd`'s own "Known gaps" note) -- what ships here is bare, untextured geometry.
  No animation or rig data from the source pack was used; Arco3D's own Humanoid rig preset was
  applied fresh, inside Arco3D itself, exactly the way a real user's "Add Humanoid Rig" click would.

**Regenerating this asset** (e.g. after an A3D schema change, or to swap in a different reference
character from the same pack):

```sh
# 1. Download the pack and convert one character's geometry through the Blender Bridge:
blender --background --python ../arco3d_blender_bridge/tools/convert_kenney_reference_human.py -- \
    /path/to/kenney_blocky-characters/Models/GLB\ format/character-a.glb

# 2. Rig it with Arco3D's own Humanoid preset and add the Bow prop:
godot --headless --script tools/rig_and_export_arco_archer.gd
```

Step 1 produces `ArcoArcher_reference_body.a3d` (bare geometry, no rig); step 2 reads that file and
produces the final `ArcoArcher.a3d` (rigged, plus the Bow prop).

## Everything else

Every other asset in this app (the primitives, the modifier-built demo geometry, the original
from-scratch archer attempt still kept at `tools/build_arco_archer.gd` for reference) is generated
entirely by Arco3D's own code -- no external content involved.
