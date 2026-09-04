# RFC-0051: Arco3D Blender Bridge

RFC Number: RFC-0051
Title: Arco3D Blender Bridge
Status: Draft
Category: Interoperability / Tool Integration
Authors: Arcology Project
Created: 2026-08-28
Last Updated: 2026-08-28
Supersedes: None
Superseded By: None
Related RFCs: RFC-0000, RFC-0049, RFC-0050

## 1. Executive Summary

The Arco3D Blender Bridge is the official Blender integration for A3D and A3M assets.

Its purpose is to preserve Arco3D's fast preparation workflow while making Blender the primary downstream environment for advanced animation, fine tuning, simulation, and rendering.

The Bridge SHALL import an A3D asset into Blender with sufficient native Blender representation to allow normal Blender workflows without requiring Arco3D to remain open.

Most importantly, saved Arco3D poses SHALL become practical Blender animation building blocks. Where Blender's API permits, compatible Arco3D poses SHOULD be represented through Blender's pose/asset-library mechanisms so an animator can quickly block scenes from a reusable pose catalog and then interpolate or refine them with standard Blender tools.

## 2. Motivation

The Bridge addresses three problems.

### 2.1 Problem One: Interchange Commonly Preserves Geometry but Loses Workflow

A mesh imported successfully is not enough if the useful pose catalog, rig semantics, material organization, or reusable metadata disappear.

The Bridge SHALL preserve practical workflow semantics, especially poses.

### 2.2 Problem Two: Blender Is Powerful but Preparation Can Be Cumbersome

Arco3D is intended to remove friction from construction, surface preparation, and pose creation while leaving advanced animation and rendering to Blender.

The Bridge is therefore not merely a file importer; it is the contract that makes the division of responsibilities useful.

### 2.3 Problem Three: Tight Coupling Would Make Arco3D Fragile

Blender APIs and internal representations evolve.

A3D MUST remain stable regardless. The Bridge SHALL absorb Blender-version-specific translation.

## 3. Goals

The Bridge SHALL:

- import A3D geometry;
- import component hierarchy;
- import rigs/armatures;
- preserve skin weights;
- import materials and texture resources;
- translate supported Arco3D material layers into practical Blender equivalents;
- import pose catalogs;
- make poses easy to use while animating in Blender;
- import pose sequences as blocking aids where practical;
- preserve stable A3D IDs as metadata;
- provide clear diagnostics for unsupported features;
- support export from Blender back to A3D for the agreed portable subset;
- preserve application neutrality of the A3D specification;
- remain versioned independently from A3D and Arco3D.

## 4. Non-Goals

The Bridge SHALL NOT:

- make .blend the canonical Arco3D storage format;
- reproduce Blender inside Arco3D;
- guarantee exact translation of arbitrary Blender node graphs into A3M;
- guarantee exact translation of arbitrary Blender constraints into A3D;
- silently discard unsupported semantics;
- require Blender for validating an A3D file;
- require Arco3D to be installed for imported assets to remain usable in Blender.

## 5. Terminology

- **Bridge** — The official Blender add-on implementing A3D/A3M translation.
- **Portable subset** — A set of semantics that can round-trip reliably between Arco3D and Blender.
- **Native translation** — Representation using ordinary Blender objects/data structures.
- **Bridge metadata** — Namespaced custom properties used to preserve A3D identity or semantics not represented directly by Blender.
- **Bake/approximation** — A deliberate conversion of unsupported high-level behavior into visually or functionally similar Blender data.

## 6. Core Workflow

The intended user workflow is:

```
Arco3D
  │
  ├─ Build / kitbash
  ├─ Rig
  ├─ Create pose catalog
  ├─ Surface / paint
  └─ Style preview
  │
  ▼
Export .a3d
  │
  ▼
Blender Bridge
  │
  ├─ Mesh objects
  ├─ Armature
  ├─ Skin weights
  ├─ Materials
  ├─ Pose assets/catalog
  └─ Metadata
  │
  ▼
Blender animation / fine tuning / render
```

## 7. Blender UI Integration

The Bridge SHOULD integrate with Blender's standard File import/export UI:

```
File → Import → Arcology 3D (.a3d)
File → Export → Arcology 3D (.a3d)
```

A3M SHOULD also be importable/exportable through an appropriate material-library or file workflow.

The Bridge MAY provide an Arco3D panel for:

- import diagnostics;
- asset identity;
- pose catalog management;
- reimport/update;
- material translation status;
- export validation.

The panel MUST NOT be required for ordinary use of imported Blender objects.

## 8. Geometry Translation

A3D meshes MUST become ordinary Blender mesh data.

The Bridge MUST preserve where supported:

- positions;
- topology;
- normals;
- UV sets;
- material assignments;
- vertex colors/portable attributes;
- component hierarchy.

Stable A3D IDs SHOULD be stored in namespaced Blender custom properties.

The Bridge SHOULD avoid unnecessary mesh modification during import.

## 9. Component and Kitbash Translation

A3D components SHOULD become distinct Blender objects or collections where this preserves editing intent.

Attachment relationships SHOULD be translated to ordinary Blender parenting or equivalent constraints when possible.

The importer SHOULD offer an option to:

- preserve component separation; or
- combine evaluated mesh components where appropriate.

Preserve-separation SHOULD be the default for editable assets.

## 10. Rig Translation

A3D rigs MUST import as Blender armatures when representable.

The Bridge MUST preserve:

- bone names;
- stable bone IDs as metadata;
- hierarchy;
- rest pose;
- skin weights;
- mesh-armature relationships.

Portable constraints SHOULD be translated when an unambiguous Blender equivalent exists.

Unsupported constraints MUST produce a diagnostic and MUST NOT prevent import of the underlying armature.

## 11. Pose Catalog Translation

Pose catalogs are a primary Bridge feature.

The Bridge MUST import A3D saved poses in a form that can be applied quickly during Blender animation.

Where supported by the target Blender version, the preferred representation SHOULD use Blender-native pose asset / Asset Browser facilities or their current functional equivalent.

Each imported pose SHOULD preserve:

- name;
- category/catalog;
- compatible armature association;
- bone transforms;
- tags where representable;
- A3D pose ID.

The animator SHOULD be able to:

- select the armature;
- choose an imported pose;
- apply that pose;
- insert/use it in animation blocking;
- allow Blender to interpolate between key poses;
- refine animation with normal Blender tools.

This workflow is a Definition-of-Done requirement.

## 12. Pose Interpolation Semantics

Arco3D's preview interpolation is not authoritative animation data.

When poses are used as animation key states in Blender, Blender's animation system SHALL determine final interpolation unless explicit portable sequence metadata requests an initial interpolation suggestion.

The Bridge MAY provide helper operations such as:

- apply pose and key affected bones;
- apply pose at current frame;
- apply sequence as blocking keys;
- create stepped keys from a pose sequence;
- convert stepped blocking to Blender interpolation modes.

These helpers MUST generate ordinary editable Blender animation data.

## 13. Pose Sequence Translation

A3D pose sequences SHOULD be importable as animation-blocking aids.

A Bridge implementation MAY translate a sequence to:

- an Action;
- a set of keyframes;
- markers plus pose references;
- another standard Blender representation.

The output MUST remain editable through normal Blender animation tooling.

Hold/step metadata SHOULD be preserved when possible.

## 14. Material Translation

A3M materials SHOULD become ordinary Blender materials.

For routine PBR channels, the Bridge SHOULD construct a conventional Blender material using the current standard shader node appropriate to the supported Blender version.

At minimum, the Bridge SHOULD translate:

- base color;
- roughness;
- metallic;
- specular;
- normal;
- displacement/height when practical;
- emission;
- opacity.

The user MUST NOT be required to manually reconstruct common channel connections after import.

## 15. Paint Layers and Layer Groups

Blender does not necessarily provide a native one-to-one equivalent for Arco3D's Photoshop-like surface layer model.

The Bridge SHALL therefore use a tiered strategy:

- preserve original A3M layer metadata in Bridge metadata;
- translate layer structures to Blender node groups or image resources where practical;
- provide baked composite channel textures when exact live translation is not practical;
- retain individual source layer images/masks when available so the asset is not irreversibly flattened at import time.

The Bridge MUST report whether a material was:

- translated live;
- approximated;
- partially baked;
- fully baked.

## 16. Decal Translation

Arco3D decals SHOULD translate to one of:

- projected material/node structures;
- decal mesh/geometry;
- baked texture contribution.

The importer SHOULD preserve decals as independently editable constructs when practical.

When baking is necessary, the Bridge MUST retain diagnostic metadata describing the conversion.

## 17. Stylization Translation

Arco3D style metadata SHOULD be translated to Blender material, line, compositor, or viewport/render constructs only when the translation is stable and useful.

Because final rendering occurs in Blender, exact correspondence is not guaranteed.

The Bridge SHOULD distinguish:

- directly translatable style settings;
- approximated settings;
- unsupported settings.

A user MAY choose to import only neutral PBR/material data and ignore Arco3D style metadata.

## 18. Motion Profile Translation

Stepped/stop-motion-oriented preview metadata MAY be imported as initial animation timing/interpolation settings.

Possible translations include:

- constant/stepped interpolation;
- generated keyframe holds;
- motion-blur hints;
- sequence timing metadata.

The Bridge MUST NOT permanently constrain the animator to the Arco3D preview style.

Imported animation remains ordinary Blender data.

## 19. Reimport and Update

The Bridge SHOULD support reimport/update based on stable A3D IDs.

A future implementation MAY allow:

```
Arco3D asset revised
       ↓
Re-export same asset ID
       ↓
Blender: Reimport/Update
       ↓
Update geometry/material resources
while preserving downstream animation where safe
```

Safe reimport is difficult and SHALL be incremental.

The first implementation MUST NOT claim non-destructive update behavior it cannot guarantee.

## 20. Export from Blender

The Bridge SHOULD export the portable subset back to A3D.

Initial export SHOULD include:

- mesh geometry;
- transforms;
- component/object hierarchy where mappable;
- armature;
- skin weights;
- compatible poses/pose assets;
- common PBR material channels;
- texture resources.

Unsupported Blender-only features MUST produce diagnostics.

The exporter MUST NOT silently imply that arbitrary Blender scenes can round-trip losslessly.

## 21. Diagnostics

Every import/export operation SHOULD produce a concise result summary including:

- successful translations;
- approximations;
- baked features;
- ignored optional features;
- unsupported required features;
- missing external resources;
- compatibility warnings.

Warnings SHOULD identify the affected A3D object/layer/pose by stable ID and human-readable name where available.

## 22. Blender Version Compatibility

The Bridge SHALL define a supported Blender-version policy.

Because Blender APIs evolve, compatibility code MUST be isolated from A3D parsing.

Recommended architecture:

```
A3D Parser / Model
       ↓
Bridge-neutral translation layer
       ↓
Blender version adapter
       ↓
Blender API
```

The A3D parser MUST NOT directly depend on UI-specific Blender APIs where avoidable.

## 23. Security Considerations

The Bridge imports untrusted files into a scripting-capable application.

It MUST:

- use a validated A3D/A3M parser;
- refuse malformed required structures;
- avoid executing embedded code;
- avoid automatically following network references;
- validate external paths;
- avoid evaluating untrusted textual expressions as Python;
- bound image/resource loading.

Bridge metadata MUST be treated as data, not executable instructions.

## 24. Privacy Considerations

The Bridge MUST NOT automatically upload assets or telemetry as part of import/export.

It SHOULD avoid writing local absolute paths into exported portable assets unless the user explicitly chooses that behavior.

## 25. Accessibility Considerations

Bridge functionality SHOULD be available through standard Blender menus and keyboard-accessible operators.

Pose import/application controls SHOULD use human-readable labels and not depend solely on thumbnails or color.

Diagnostics SHOULD be available as selectable/copyable text.

## 26. Performance Considerations

The Bridge SHOULD:

- parse A3D once per operation;
- batch mesh creation where practical;
- avoid redundant texture decoding;
- reuse shared texture/material resources;
- lazily create optional previews;
- avoid applying poses one-by-one through expensive UI operators when direct data APIs are available;
- keep large imports responsive with progress reporting/cancellation where Blender APIs permit.

## 27. Compatibility

A3D/A3M format compatibility is defined by RFC-0050.

The Bridge has an independent version number.

The Bridge SHOULD record:

- Bridge version;
- source A3D schema version;
- import timestamp;
- translation profile/version;

in non-invasive metadata to aid diagnostics.

## 28. Reference Workflow: Pose-Driven Animation Blocking

1. Import Character.a3d.
2. Bridge creates mesh + armature.
3. Bridge registers imported pose catalog.
4. Animator selects frame 1 and applies "Idle".
5. Animator keys the pose.
6. Animator selects frame 10 and applies "Walk Contact L".
7. Animator keys the pose.
8. Animator selects frame 16 and applies "Walk Passing L".
9. Blender interpolates between poses or uses stepped blocking as selected.
10. Animator adjusts timing, arcs, hands, head, secondary movement, and overlap using normal Blender tools.
11. Blender performs final rendering.

The workflow SHALL NOT require returning to Arco3D merely to modify Blender animation data.

## 29. Testing Strategy

The Bridge SHALL maintain golden A3D/A3M fixtures covering:

- static mesh;
- multi-component kitbash asset;
- skinned humanoid;
- multiple pose catalog;
- pose sequence;
- layered material;
- decal;
- generated material maps;
- stylization metadata;
- unsupported optional extension.

Automated tests SHOULD verify:

- geometry counts;
- transform accuracy;
- bone hierarchy;
- skin-weight preservation;
- pose transform accuracy;
- pose catalog visibility/application;
- PBR texture hookup;
- custom ID metadata;
- exporter diagnostics;
- round-trip portable subset.

Manual acceptance testing MUST verify the pose-driven animation workflow in Section 28.

## 30. Definition of Done

The first Blender Bridge milestone is complete when:

- the add-on installs on the designated supported Blender version;
- .a3d appears in Blender's import UI;
- the golden asset imports with correct geometry;
- its armature and skin weights function;
- saved Arco3D poses are visible and readily applicable in Blender;
- at least three imported poses can be placed at different frames and Blender can interpolate or step between them;
- common PBR materials import without manual node wiring;
- texture resources resolve correctly;
- unsupported features generate visible diagnostics rather than disappearing silently;
- the asset can be animated and rendered without Arco3D running;
- the portable subset can be exported back to A3D with validation output.

## 31. AI Implementation Guidance

Autonomous coding agents SHALL:

- read and validate A3D/A3M through a dedicated parser/model layer;
- keep Blender API translation separate from format parsing;
- prefer native Blender data structures for imported results;
- preserve stable A3D IDs in namespaced custom metadata;
- make pose catalogs a high-priority implementation milestone;
- report unsupported mappings rather than silently dropping them;
- preserve original source resources when baking/approximating material layers where practical;
- avoid building an Arco3D runtime dependency into imported Blender files.

Agents MUST stop for architectural review before:

- changing A3D canonical semantics to simplify Blender import;
- embedding executable Python into A3D assets;
- claiming lossless round-trip for unsupported Blender constructs;
- implementing reimport behavior that could destroy user animation without explicit safeguards.

## 32. Future Extensions

Potential extensions include:

- robust stable-ID reimport/update;
- automatic pose retargeting;
- shared Arco3D pose-library browsing directly in Blender;
- live-link development mode;
- batch asset import/export;
- automatic LOD handoff;
- Blender-to-Arco3D material approximation;
- richer motion-profile translation;
- custom Asset Browser previews generated from A3D metadata.
