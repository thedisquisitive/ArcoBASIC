# RFC-0049: Arco3D Application and Authoring Model

RFC Number: RFC-0049
Title: Arco3D Application and Authoring Model
Status: Draft
Category: Application Architecture / Creative Tooling
Authors: Arcology Project
Created: 2026-08-28
Last Updated: 2026-08-28
Supersedes: None
Superseded By: None
Related RFCs: RFC-0000, RFC-0050, RFC-0051

## 1. Executive Summary

Arco3D is a lightweight, laptop-first 3D asset construction, rigging, posing, surface-authoring, and stylization application intended to remove high-friction preparation work from heavyweight 3D suites.

Arco3D is not intended to replace Blender. It is intended to make the work that occurs before Blender faster, more discoverable, and more natural on ordinary laptop hardware.

The primary workflow is:

```
Block / Kitbash
      ↓
Non-destructive refinement
      ↓
Rig and pose
      ↓
Surface / paint / decals
      ↓
Stylize and preview
      ↓
Export A3D
      ↓
Blender
      ↓
Animation / fine tuning / render
```

Arco3D SHALL treat block modeling as a first-class construction method rather than merely an early drafting stage. Users SHALL be able to begin with simple volumes and progressively refine them non-destructively into production-ready assets.

Arco3D SHALL also provide a Photoshop/GIMP-like layered surface workflow, reusable pose catalogs, and an illustration-oriented style system suitable for deliberately graphic 3D imagery.

The complete application SHALL be usable with a standard laptop keyboard and touchpad. A mouse, numeric keypad, stylus, or specialist input device MAY improve convenience but MUST NOT add required capability.

## 2. Motivation

Modern 3D suites are extremely capable, but their breadth often creates friction for routine asset preparation.

Arco3D specifically seeks to solve the following recurring problems.

### 2.1 Problem One: Simple Asset Construction Requires Heavyweight Workflow Knowledge

Traditional general-purpose 3D suites frequently expose topology, modifier, viewport, UV, material, rigging, and scene-management concepts simultaneously.

For an artist or developer whose natural workflow begins with boxes, wedges, and other large forms, routine construction can become dominated by interface management rather than shape creation.

Arco3D SHALL instead make the high-frequency path explicit:

Scaffold → Refine → Surface → Pose → Export

### 2.2 Problem Two: Surface Authoring Is More Technical Than the Artistic Task Requires

Applying a logo, adding claw coloration, layering dirt, generating roughness from a source texture, or painting an outline accent should not require routine construction of shader graphs or repeated UV surgery.

Arco3D SHALL provide layer-based materials, direct decals, automatic mapping, texture painting, and generated surface maps as primary workflows.

### 2.3 Problem Three: Laptop Input Is Common but Treated as Secondary

Many 3D workflows still assume a three-button mouse, scroll wheel, numeric keypad, or desktop workstation.

Arco3D SHALL consider a laptop keyboard and touchpad a complete workstation configuration.

## 3. Goals

Arco3D SHALL:

- provide a fast block-first modeling workflow;
- support non-destructive refinement of block geometry;
- support reusable kitbash components and assemblies;
- provide lightweight rigging sufficient for posing and downstream animation preparation;
- provide reusable pose catalogs and pose sequences;
- interpolate between compatible saved poses for preview and animation blocking;
- provide direct texture painting with editable paint layers;
- provide material and paint-layer grouping, masking, blend modes, and channel control;
- automatically derive useful material maps from source imagery;
- simplify decals and texture layering;
- provide automatic/projected mapping so UV editing is not mandatory for routine work;
- import and export reusable materials;
- provide first-class stylized rendering controls for 3D assets intended to read graphically or illustratively;
- support stepped/stop-motion-inspired motion preview;
- export assets, rigs, poses, materials, and associated metadata through the A3D interchange model;
- integrate naturally with Blender through a dedicated bridge;
- remain responsive on ordinary laptop-class hardware;
- remain fully operable without a numeric keypad or external mouse.

## 4. Non-Goals

Initial Arco3D releases SHALL NOT attempt to replace:

- Blender's production renderer;
- Cycles or comparable path tracers;
- Blender's compositor;
- advanced fluid simulation;
- advanced cloth simulation;
- large-scale particle systems;
- Geometry Nodes;
- full digital sculpting suites;
- full CAD systems;
- Blender's Graph Editor;
- Blender's Nonlinear Animation system;
- advanced procedural shader authoring;
- final production animation tooling.

Arco3D MAY gain advanced features later, but such expansion MUST NOT compromise the primary preparation workflow.

## 5. Guiding Principles

### 5.1 Construct First

The fastest route from an empty scene to recognizable form SHOULD use primitives, direct manipulation, snapping, duplication, and reusable parts.

### 5.2 Non-Destructive by Default

Refinement operations SHOULD remain editable until the user explicitly commits them.

### 5.3 Artist Intent Over Implementation Detail

The user SHOULD specify outcomes such as "decal," "roughness," "outline," "dirt," or "pose," while Arco3D handles the implementation mechanics where practical.

### 5.4 Blender Is a Finishing Environment

Arco3D SHALL not duplicate advanced Blender functionality merely to avoid handing work to Blender.

### 5.5 Laptop Is the Baseline

The baseline input target is a standard laptop keyboard and touchpad.

### 5.6 Stylization Is First-Class

Illustrative, cel-shaded, high-contrast, posterized, and deliberately non-photoreal rendering SHALL be treated as primary artistic outcomes rather than special effects.

## 6. Terminology

- **Asset** — A reusable Arco3D object or assembly containing one or more components and optional rig, surface, pose, or style data.
- **Component** — A reusable mesh or logical sub-asset that may participate in a larger asset.
- **Construction Stack** — Ordered non-destructive operations used to transform source geometry.
- **Paint Stack** — Ordered editable paint layers applied to surface channels.
- **Material Stack** — Ordered sources and operations defining material response.
- **Style Stack** — Rendering-oriented stylization operations that modify graphic presentation without requiring destructive texture changes.
- **Pose** — A named skeletal state for a compatible rig.
- **Pose Catalog** — A reusable collection of named poses.
- **Pose Sequence** — An ordered set of poses with lightweight timing metadata, intended primarily for blocking or preview.
- **Look Profile** — A reusable set of style/render presentation settings.
- **A3D** — Arcology 3D asset interchange container, defined separately (RFC-0050).
- **A3M** — Arcology 3D material package, defined separately (RFC-0050).

## 7. Requirements

### 7.1 Modeling Requirements

Arco3D MUST provide at minimum:

- cube/block creation;
- wedge creation;
- cylinder creation;
- plane creation;
- sphere creation;
- reusable custom primitive/component insertion;
- vertex, edge, and face selection;
- move, rotate, and scale;
- extrude;
- inset;
- bevel;
- cutting/subdivision operations;
- duplication;
- mirror/symmetry;
- array or repeated duplication;
- snapping;
- alignment;
- local/global transform spaces.

Arco3D SHOULD provide proportional editing and silhouette-oriented refinement tools.

### 7.2 Construction Stack Requirements

Construction operations SHOULD be represented as an ordered stack where the operation permits non-destructive evaluation.

A stack operation MUST support, where applicable:

- enable/disable;
- parameter editing;
- reorder;
- duplication;
- explicit commit/apply.

Example:

```
Block
 ↓
Mirror
 ↓
Extrusion Group
 ↓
Bevel
 ↓
Subdivision
 ↓
Detail Pass
```

The user MUST NOT be forced to destructively commit geometry merely to preview later refinement.

### 7.3 Kitbash Requirements

Arco3D MUST support reusable components.

Components SHOULD support:

- attachment points;
- snapping points;
- transforms relative to parent assets;
- replace/swap operations;
- grouping;
- reusable assemblies;
- material inheritance or override;
- compatible rig attachment metadata where practical.

### 7.4 Rigging Requirements

Arco3D MUST support:

- bone creation;
- bone hierarchy;
- parenting;
- bind/rest pose;
- skin weights;
- automatic weighting;
- manual weight painting;
- simple IK chains;
- posing.

Arco3D SHOULD support reusable rig templates and joint constraints.

Arco3D MAY support advanced constraints when they can be translated reliably downstream.

### 7.5 Pose Requirements

Users MUST be able to:

- capture the current rig state as a named pose;
- preview saved poses;
- categorize poses;
- apply saved poses;
- update or duplicate poses;
- export poses with an asset or catalog;
- interpolate between two compatible poses.

Pose interpolation MUST be intended as a preview/blocking aid and MUST NOT require Arco3D to implement a complete animation editor.

Pose catalogs SHOULD be reusable across compatible rig definitions.

### 7.6 Pose Sequence Requirements

Arco3D SHOULD allow ordered pose sequences containing:

- pose references;
- optional hold duration;
- optional transition duration;
- optional interpolation mode;
- optional annotation.

Pose sequences MUST remain lightweight and interoperable with Blender-oriented workflows.

### 7.7 Surface Requirements

Arco3D MUST expose common material channels without requiring shader graph construction.

At minimum:

- base color;
- roughness;
- metallic;
- specular;
- normal;
- height/displacement;
- emission;
- opacity.

AO/cavity MAY be represented as authoring channels or generated helpers.

### 7.8 Automatic Material Generation

Given a source texture, Arco3D SHOULD offer automatic derivation of candidate maps including:

- roughness;
- specular;
- height/displacement;
- normal;
- cavity/AO-like detail.

Generated maps MUST be presented as editable suggestions, not guaranteed physically correct measurements.

Generated-map parameters SHOULD remain non-destructive until explicitly baked.

### 7.9 Texture Set Detection

When multiple files are imported, Arco3D SHOULD recognize common channel naming conventions and associate likely maps automatically.

For example:

```
wall_basecolor.png
wall_normal.png
wall_roughness.png
wall_height.png
```

The user MUST be able to correct incorrect automatic associations.

### 7.10 Mapping Requirements

Arco3D MUST support conventional UV mapping.

Arco3D SHOULD additionally support:

- planar projection;
- box projection;
- cylindrical projection;
- triplanar projection;
- automatic unwrap;
- smart seam generation;
- consistent texel-scale preservation.

Routine decal and block-surface workflows SHOULD NOT require manual UV editing.

### 7.11 Decal Requirements

Users MUST be able to apply an image as a decal through direct viewport interaction.

A decal SHOULD expose:

- position;
- rotation;
- scale;
- projection depth;
- surface conformance;
- opacity;
- blend mode;
- affected material channels;
- mask.

Decals SHOULD remain independently editable until baked.

### 7.12 Paint Stack Requirements

Texture painting MUST use an editable layer stack.

Each paint layer MUST support:

- name;
- visibility;
- lock state;
- opacity;
- ordering;
- blend mode;
- mask;
- channel targeting.

Paint layers SHOULD support grouping/folders.

A paint layer MAY affect multiple channels simultaneously.

Example:

```
Creature Surface
├─ Base
│  ├─ Skin
│  └─ Secondary Tone
├─ Features
│  ├─ Claws
│  ├─ Teeth
│  └─ Eyes
├─ Stylization
│  ├─ Outline Accent
│  ├─ Graphic Shadow
│  └─ Highlight Accent
└─ Weathering
   ├─ Dirt
   └─ Damage
```

### 7.13 Per-Layer Attribute Requirements

Layers MUST be capable of storing independent channel strengths and relevant artistic controls.

Examples include:

- color adjustment;
- roughness contribution;
- metallic contribution;
- specular contribution;
- height contribution;
- normal strength;
- emission contribution;
- opacity;
- outline contribution;
- edge softness;
- contrast;
- saturation.

The exact control set MAY vary by layer type.

### 7.14 Blend Modes

Paint/material layers SHOULD provide familiar image-editing blend modes including:

- Normal;
- Multiply;
- Screen;
- Overlay;
- Add;
- Subtract;
- Soft Light;
- Hard Light;
- Darken;
- Lighten;
- Color;
- Value/Luminosity.

Channel-specific modes MAY include:

- Replace;
- Minimum;
- Maximum;
- Additive Height;
- Normal Combine;
- Roughness Bias.

### 7.15 Mask Requirements

Layers SHOULD support:

- painted masks;
- image masks;
- procedural masks;
- curvature masks;
- cavity/AO masks;
- slope/orientation masks;
- position masks;
- material-ID masks;
- selection masks;
- vertex-group masks.

### 7.16 Texture Painting Requirements

Arco3D MUST support direct painting on visible model surfaces.

At minimum, painting MUST support:

- base color;
- roughness;
- metallic;
- height;
- masks.

Normal, opacity, emission, and style-channel painting SHOULD be supported.

Brush tooling SHOULD include:

- size;
- hardness;
- opacity;
- flow;
- spacing;
- symmetry;
- erase;
- fill;
- gradient;
- clone;
- smudge;
- projection/stamp.

Pressure sensitivity MAY be supported but MUST NOT be required.

### 7.17 Material Import/Export Requirements

Arco3D MUST import conventional image-based material sets.

Arco3D MUST support reusable material packages through A3M.

Materials SHOULD be discoverable through a library interface and applicable by drag/drop or equivalent direct action.

### 7.18 Stylization Requirements

Arco3D MUST provide a real-time stylized preview pipeline suitable for graphic 3D presentation.

It SHOULD provide controls for:

- orthographic projection;
- perspective projection;
- isometric-oriented views;
- two-tone shading;
- three-tone shading;
- cel/banded shading;
- outline rendering;
- variable outline width;
- graphic black/shadow regions;
- palette restriction;
- posterization;
- shadow threshold;
- highlight threshold;
- specular stylization;
- normal simplification;
- depth compression;
- shallow/relief presentation.

Style settings SHOULD be separable from destructive surface texture data.

### 7.19 Motion Preview Requirements

Arco3D SHOULD support motion-preview profiles including:

- smooth interpolation;
- stepped playback;
- configurable step/hold rate;
- motion blur preview;
- selective smoothing/interpolation;
- optional overshoot/follow-through helpers.

This subsystem exists to preview intended presentation and aid blocking. Final animation SHOULD occur in Blender or another downstream animation tool.

## 8. Architecture

Arco3D SHOULD be architected as separable authoring domains over a shared scene/asset model.

```
                 ┌───────────────────────┐
                 │       Arco3D UI       │
                 └───────────┬───────────┘
                             │
                    Intent / Commands
                             │
       ┌─────────────────────┼─────────────────────┐
       │                     │                     │
       ▼                     ▼                     ▼
 Construction Engine    Surface Engine        Rig/Pose Engine
       │                     │                     │
       └──────────────┬──────┴──────────────┬─────┘
                      │                     │
                      ▼                     ▼
                Canonical Asset       Style / Preview
                      │                     │
                      └──────────┬──────────┘
                                 ▼
                              A3D/A3M
                                 │
                       ┌─────────┴─────────┐
                       ▼                   ▼
                    Blender            Future Tools
```

The canonical scene/asset representation MUST NOT depend on Blender-specific data structures.

Blender-specific translation belongs to the Blender Bridge RFC (RFC-0051).

## 9. User Experience

### 9.1 Primary Workspace

The default workspace SHOULD dedicate the majority of screen area to the 3D viewport.

Primary authoring domains SHOULD be presented as understandable workflow states rather than dozens of specialist editors.

A suggested top-level organization is:

```
BUILD   SURFACE   RIG   POSE   STYLE   EXPORT
```

These names are illustrative rather than normative.

### 9.2 Laptop Baseline

The application MUST be fully usable with:

- standard keyboard;
- standard multi-touch touchpad;
- no numeric keypad;
- no middle mouse button.

No required operation SHALL depend on:

- external mouse;
- numeric keypad;
- scroll wheel;
- stylus;
- multi-button pointing device.

### 9.3 Viewport Navigation

The default input model SHOULD support:

- two-finger orbit;
- modifier + two-finger pan;
- pinch zoom;
- direct object selection;
- keyboard-assisted precision transforms.

Where platform gesture APIs differ, Arco3D MAY provide equivalent alternatives while preserving capability.

### 9.4 Canonical Views Without Numpad

Front, rear, left, right, top, bottom, isometric, orthographic, perspective, and active-camera views MUST be available without numeric keypad emulation.

Arco3D SHOULD provide:

- an orientation widget/cube;
- a view radial menu or compact view menu;
- searchable view commands;
- configurable keyboard shortcuts.

### 9.5 Transform Interaction

Transform handles MUST use touchpad-friendly hit targets.

Handles SHOULD provide:

- axis movement;
- planar movement;
- free movement;
- axis rotation;
- axis scale;
- uniform scale;
- snapping;
- fine-adjustment mode.

### 9.6 Command Palette

Arco3D SHOULD provide a global searchable command palette.

The palette SHOULD:

- find commands by natural labels and aliases;
- show associated shortcuts;
- execute commands without requiring menu navigation;
- make uncommon commands discoverable.

### 9.7 Direct Block Manipulation

Common block-modeling operations SHOULD be available through direct manipulation where ambiguity can be avoided.

Examples MAY include direct face extrusion, direct edge bevel, or drag-to-duplicate interactions.

Such gestures MUST have visible affordances or discoverable alternatives; critical behavior MUST NOT depend on undocumented gestures.

## 10. Developer Experience

Arco3D implementations SHOULD isolate the following interfaces:

- scene/asset model;
- mesh/construction operations;
- rig/pose representation;
- surface/material representation;
- paint engine;
- style-preview renderer;
- file serialization;
- import/export adapters;
- command system;
- UI/input binding system.

Arco3D's internal object model SHOULD preserve stable identifiers for components, bones, layers, materials, poses, and assets so downstream tools can maintain references across revisions where possible.

The application SHOULD expose extension points for:

- importers/exporters;
- procedural masks;
- generated-map algorithms;
- material presets;
- rig templates;
- pose libraries;
- style profiles.

## 11. Security Considerations

Imported A3D/A3M content MUST be treated as untrusted data.

Arco3D MUST:

- validate container bounds and lengths;
- reject malformed references;
- constrain decompression/resource expansion;
- avoid executing arbitrary embedded code by default;
- sanitize external resource paths;
- prevent package-relative paths from escaping the package root;
- define explicit trust rules before supporting executable extensions.

Image decoders and model importers SHOULD be sandboxed or hardened where practical because creative asset files frequently cross trust boundaries.

## 12. Privacy Considerations

Arco3D SHOULD function fully offline for normal authoring.

The application MUST NOT require cloud upload for:

- modeling;
- rigging;
- posing;
- texture generation;
- texture painting;
- material processing;
- export.

If future network libraries or asset-sharing systems are introduced, they MUST be separately consented and documented.

## 13. Accessibility Considerations

Arco3D SHALL treat accessibility as an architectural concern.

The UI SHOULD support:

- configurable scaling;
- keyboard navigation;
- remappable shortcuts;
- non-color-only state indicators;
- adjustable contrast;
- reduced-motion UI mode;
- readable labels/tooltips for icon-driven controls;
- alternate interaction paths for gestures.

Touchpad gestures MUST have keyboard/UI equivalents.

Stylus pressure MUST remain optional.

## 14. Performance Considerations

Arco3D targets ordinary laptop-class systems.

The application SHOULD prioritize:

- responsive viewport interaction;
- incremental modifier/construction evaluation;
- lazy material-map generation;
- asynchronous texture processing where safe;
- GPU-assisted preview where available;
- graceful CPU fallback for non-critical operations;
- bounded undo/history memory;
- texture-resolution controls;
- proxy/preview quality settings.

Expensive derived resources SHOULD be cached and invalidated selectively.

The application SHOULD avoid recomputing unaffected construction, paint, or style stages after local edits.

## 15. Compatibility

Arco3D's canonical project/asset representation SHALL remain independent of Blender.

The A3D/A3M formats SHALL provide the stable interchange boundary.

Blender integration SHALL be maintained by a separately versioned bridge.

This separation permits:

- Blender API changes without invalidating Arco3D files;
- future import into other DCC tools;
- future Arcology-native engines or renderers;
- format inspection without launching Arco3D.

## 16. Reference Workflow

A representative character workflow is:

1. Create body from blocks.
2. Mirror major symmetrical forms.
3. Refine silhouette non-destructively.
4. Add kitbash clothing and accessories.
5. Apply humanoid rig template.
6. Auto-weight and correct important regions.
7. Save Idle, Walk Contact, Reach, Crouch, and Sit poses.
8. Apply base material.
9. Add "Skin", "Claws", "Outline", and "Dirt" paint layers.
10. Paint directly on the asset.
11. Adjust style profile for graphic/cel presentation.
12. Export A3D.
13. Import A3D into Blender.
14. Use imported poses to block animation.
15. Fine-tune animation and render in Blender.

## 17. Testing Strategy

Arco3D implementations SHALL include tests for at least:

### 17.1 Modeling

- primitive creation;
- transform accuracy;
- modifier/construction stack determinism;
- stack reordering;
- symmetry/mirror behavior;
- save/load round-trip.

### 17.2 Rig/Pose

- bone hierarchy round-trip;
- skin-weight preservation;
- pose capture/apply;
- pose interpolation;
- catalog compatibility checking.

### 17.3 Surface

- paint-layer ordering;
- blend-mode correctness;
- mask application;
- multi-channel layer contribution;
- generated-map reproducibility;
- decal transform persistence;
- texture-set detection.

### 17.4 Input

Acceptance tests MUST verify that a complete representative workflow can be completed using only a laptop keyboard and touchpad.

### 17.5 Interchange

A representative A3D asset MUST round-trip through save/load without losing supported semantics.

Blender Bridge conformance is defined separately (RFC-0051).

## 18. Definition of Done

The first conforming Arco3D milestone is complete when a user can:

- launch Arco3D on a laptop without external peripherals;
- create an asset from block primitives;
- refine it through at least mirror, extrude, bevel, and one additional non-destructive operation;
- assemble at least one reusable component;
- create or apply a basic skeleton;
- weight and pose the asset;
- save multiple named poses;
- interpolate between two poses;
- create a material from an image or imported PBR set;
- create multiple editable paint layers;
- paint at least color and one non-color material channel;
- apply a projected decal without manually editing UVs;
- preview a stylized/cel-like appearance;
- export the asset as A3D;
- import that A3D asset into Blender with geometry, rig, material resources, and saved poses intact enough for animation blocking.

## 19. AI Implementation Guidance

Autonomous coding agents implementing Arco3D SHALL preserve the following boundaries:

### Required Boundaries

- Do not implement Blender-specific state in the canonical Arco3D asset model.
- Do not replace the layer-based material workflow with mandatory shader graphs.
- Do not make mouse/numpad controls the primary or only interaction path.
- Do not destructively bake construction operations merely for implementation convenience unless the user explicitly applies them.
- Do not silently flatten paint layers on save.
- Do not silently discard unsupported A3D semantics during export.

### Recommended Implementation Order

1. Canonical scene/asset object model.
2. Laptop-first viewport and command system.
3. Primitive/block modeling and transforms.
4. Construction stack.
5. A3D serialization minimum subset.
6. Rig and pose representation.
7. Pose catalog/interpolation.
8. Basic material representation.
9. Paint stack and direct paint.
10. Decals and automatic mapping.
11. Generated texture maps.
12. Style-preview pipeline.
13. Blender Bridge integration tests.
14. Optimization and larger asset libraries.

### Stop Conditions

Agents MUST stop and request architectural review rather than independently inventing incompatible behavior when:

- an A3D schema change would break existing serialized assets;
- a feature requires embedding executable code into asset files;
- a proposed editor feature requires Blender-specific data to become canonical;
- a new workflow makes an external mouse or numpad mandatory;
- a proposed advanced subsystem materially expands Arco3D toward replacing Blender rather than preparing assets for it.

## 20. Future Extensions

Potential future extensions include:

- morph/shape-key authoring;
- procedural component generators;
- retopology assistance;
- LOD generation;
- automated atlas generation;
- shared network asset libraries;
- Arcology-native renderer integration;
- Arcology game-engine integration;
- mobile/tablet companion authoring;
- collaborative review/annotation;
- style-profile marketplace/library;
- richer pose retargeting;
- animation smear geometry generation;
- limited sculpt/refinement brushes.

These are intentionally deferred and are not commitments.
