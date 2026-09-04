# RFC-0050: A3D and A3M Asset and Material Interchange

RFC Number: RFC-0050
Title: A3D and A3M Asset and Material Interchange
Status: Draft
Category: File Format / Interchange Architecture
Authors: Arcology Project
Created: 2026-08-28
Last Updated: 2026-08-28
Supersedes: None
Superseded By: None
Related RFCs: RFC-0000, RFC-0049, RFC-0051

## 1. Executive Summary

This RFC defines the architectural requirements for two Arcology creative interchange formats:

- **A3D (.a3d)** — Arcology 3D Asset
- **A3M (.a3m)** — Arcology 3D Material

A3D is the portable asset boundary between Arco3D and downstream applications such as Blender. It SHALL be capable of preserving geometry, components, rigs, skin weights, poses, pose catalogs, lightweight pose sequences, material references, paint-layer metadata, texture resources, and stylization metadata.

A3M is a reusable portable material package containing material channels, texture sources, generated maps, paint/material layers, masks, mapping metadata, and style-oriented material attributes.

The formats SHALL be application-neutral. Blender support is provided by a bridge (RFC-0051) rather than by embedding Blender project state into A3D.

## 2. Motivation

This interchange layer addresses three traditional problems.

### 2.1 Problem One: Authoring Files Become Application Lock-In

Native project formats frequently serialize internal application state rather than portable creative intent.

A3D SHALL instead represent the asset as an interchange object independent of any one editor.

### 2.2 Problem Two: Export Often Discards High-Level Authoring Semantics

Conventional interchange frequently preserves final meshes and basic materials while losing pose libraries, paint-layer meaning, stylization metadata, or reusable component structure.

A3D/A3M SHALL retain high-level semantics when a receiving application can use them, while also allowing derived/baked fallbacks.

### 2.3 Problem Three: Binary Creative Formats Are Commonly Opaque

A3D/A3M SHALL be versioned, inspectable, and documented sufficiently for independent implementations.

## 3. Goals

The formats SHALL:

- be versioned;
- support forward-compatible extension;
- preserve stable object identifiers;
- permit unknown optional sections to be skipped safely;
- support embedded and externally referenced resources;
- preserve asset hierarchy;
- preserve rig and pose semantics;
- preserve material/paint layer semantics;
- support efficient loading of only required sections;
- support integrity validation;
- avoid application-specific canonical structures;
- provide deterministic interchange semantics;
- support Blender import/export through a separate adapter.

## 4. Non-Goals

This RFC does not define:

- Blender's internal node graphs;
- Blender .blend serialization;
- a production scene format for complete film projects;
- arbitrary embedded scripts;
- DRM;
- cloud asset management;
- network collaboration;
- final renderer state;
- every possible DCC feature.

## 5. Terminology

- **Container** — The complete .a3d or .a3m package.
- **Manifest** — Required metadata describing format version, resource table, root objects, capabilities, and integrity information.
- **Chunk** — Independently addressable container section.
- **Resource** — Embedded or referenced binary/text data such as textures or mesh buffers.
- **Stable ID** — Identifier intended to remain stable across save/load and ordinary asset revision.
- **Required capability** — Feature that a reader must understand to load the asset correctly.
- **Optional capability** — Feature a reader may ignore without invalidating core asset semantics.

## 6. General Container Architecture

A3D and A3M SHOULD use a chunked container model.

The physical encoding MAY evolve, but it MUST permit:

- a small header;
- version identification;
- manifest discovery;
- chunk discovery without full deserialization;
- explicit byte lengths;
- integrity checks;
- unknown optional chunk skipping.

Conceptually:

```
Header
Manifest
Resource Table
Chunk Directory
Chunks...
```

An implementation MAY package these as a purpose-built binary container or another seekable archive form provided normative semantics are preserved.

The initial reference implementation SHOULD favor simplicity, inspectability, and robust tooling over maximum compression.

## 7. A3D Logical Model

An A3D asset SHOULD be representable conceptually as:

```
A3D Asset
├─ Manifest
├─ Asset Metadata
├─ Components
│  ├─ Meshes
│  ├─ Transforms
│  └─ Attachment Points
├─ Geometry
│  ├─ Vertex Data
│  ├─ Index Data
│  ├─ Normals
│  ├─ Tangents
│  ├─ UV Sets
│  └─ Optional Color/Attribute Sets
├─ Rig
│  ├─ Bones
│  ├─ Hierarchy
│  ├─ Rest/Bind Pose
│  ├─ Constraints (portable subset)
│  └─ Skin Weights
├─ Pose Catalogs
│  ├─ Pose Records
│  └─ Compatibility Metadata
├─ Pose Sequences
├─ Materials
│  ├─ Embedded A3M
│  └─ External A3M References
├─ Textures / Resources
├─ Morphs (optional)
├─ Style Metadata
└─ Extension Chunks
```

## 8. A3M Logical Model

An A3M material SHOULD be representable conceptually as:

```
A3M Material
├─ Manifest
├─ Material Metadata
├─ Channel Definitions
├─ Source Images
├─ Generated Maps
├─ Mapping Definitions
├─ Material Layers
├─ Paint Layers
├─ Layer Groups
├─ Masks
├─ Style Attributes
├─ Preview Metadata
└─ Extension Chunks
```

## 9. Stable Identifiers

All referencable logical objects MUST have stable IDs within the container.

Examples include:

- component;
- mesh;
- material;
- layer;
- mask;
- bone;
- pose;
- pose catalog;
- texture resource;
- attachment point.

Stable IDs MUST NOT be based solely on array position.

Readers MUST validate references before use.

## 10. Coordinate and Unit Semantics

The format MUST define canonical:

- handedness;
- up axis;
- forward axis convention;
- linear unit;
- angular unit;
- quaternion ordering;
- matrix layout where serialized.

The first specification version SHOULD use a convention selected to minimize ambiguity rather than to mirror Blender internals.

Import/export adapters MUST explicitly convert when application conventions differ.

The exact numeric convention SHALL be frozen before implementation status advances beyond Draft.

## 11. Geometry Requirements

A3D MUST support indexed polygonal mesh geometry.

The base interoperable subset MUST include:

- vertex positions;
- polygon/triangle topology;
- normals;
- at least one UV set;
- material assignment.

A3D SHOULD support:

- tangents;
- multiple UV sets;
- vertex colors;
- named generic vertex attributes;
- sharp/smoothing metadata;
- mesh-part/component boundaries.

The file MAY store authoring-level non-destructive construction data as optional extension chunks, but receiving applications MUST NOT be required to implement Arco3D's complete construction evaluator merely to obtain the evaluated mesh.

When construction data is stored, A3D SHOULD also contain or permit generation of an evaluated mesh representation.

## 12. Rig and Skinning Requirements

A3D MUST support:

- named bones;
- stable bone IDs;
- parent-child hierarchy;
- rest/bind transforms;
- skin weights;
- mesh-to-rig association.

Weights MUST be normalized or accompanied by explicit normalization semantics.

Portable constraints MAY be represented, but unsupported constraints MUST NOT prevent access to the base skeleton and poses unless explicitly marked required.

## 13. Pose Representation

A pose MUST contain:

- stable pose ID;
- name;
- compatible rig identifier or compatibility signature;
- transform values for affected bones;
- transform space definition;
- optional category/tags;
- optional preview metadata.

A pose MAY contain only a subset of bones.

Readers MUST distinguish between:

- unspecified bone;
- explicitly reset/default bone;
- authored transform.

Pose interpolation semantics SHOULD favor translation/scale interpolation and quaternion-based rotational interpolation.

## 14. Pose Catalogs and Compatibility

A pose catalog MUST identify the rig family or compatibility requirements it targets.

Compatibility metadata SHOULD be based on stable semantic bone identities or an explicit mapping rather than simple bone-name coincidence.

This enables reusable catalogs such as:

```
Humanoid Standard
├─ Idle
├─ Walk Contact L
├─ Walk Contact R
├─ Crouch
├─ Sit
├─ Reach
└─ Aim
```

The format SHOULD permit a receiving application to reject, partially apply, or retarget a pose when compatibility is incomplete.

## 15. Pose Sequence Representation

A pose sequence SHOULD contain:

- ordered pose references;
- hold duration or frame-duration hints;
- transition duration hints;
- interpolation mode hints;
- optional loop flag;
- optional annotation.

Pose sequences are intended for blocking and preview.

They are not intended to replace full animation curves.

Full animation data MAY be standardized by a future RFC or extension.

## 16. Material Channels

A3M MUST support at minimum:

- base color;
- roughness;
- metallic;
- specular;
- normal;
- height/displacement;
- emission;
- opacity.

A3M MAY support additional named channels.

Every channel definition MUST state value domain, color space where relevant, and default value.

## 17. Material and Paint Layers

A3M MUST preserve ordered layer semantics.

A layer MUST include:

- stable ID;
- name;
- enabled/visible state;
- opacity;
- blend mode;
- channel targets;
- optional mask reference;
- layer type;
- layer-specific parameters.

Layer groups/folders MUST preserve ordering and hierarchy.

A paint layer MAY contribute to multiple material channels.

Example:

```
Layer: Claws
Targets:
  Base Color     1.0
  Roughness     -0.20
  Specular       0.35
  Height         0.05
```

The physical encoding need not use this textual representation.

## 18. Masks

A3M SHOULD represent:

- raster masks;
- painted masks;
- procedural mask descriptors;
- geometry/selection masks;
- curvature/cavity/slope-derived masks where portable.

When a procedural mask cannot be represented downstream, exporters SHOULD be able to produce a baked raster fallback.

## 19. Generated Maps

A3M MUST distinguish between:

- source imagery;
- generated/derived maps;
- user-painted maps;
- baked composite outputs.

Generated maps SHOULD store enough provenance/parameters to allow Arco3D to regenerate them when possible.

Readers that do not implement the generator MAY consume the cached generated image instead.

## 20. Mapping Definitions

A3M SHOULD support mapping modes including:

- UV;
- planar;
- box;
- cylindrical;
- triplanar;
- decal/projected mapping.

Mapping definitions MUST explicitly state transform and coordinate-space semantics.

A receiving application MAY bake unsupported mapping modes while preserving visual output.

## 21. Stylization Metadata

A3D/A3M SHOULD support optional style metadata including:

- shade-band count;
- shadow threshold;
- highlight threshold;
- outline contribution;
- outline color/weight hints;
- palette constraint hints;
- posterization level;
- normal simplification hints;
- specular stylization hints.

Stylization metadata MUST be marked optional unless the asset's intended appearance cannot be represented without it.

Exporters SHOULD provide baked or approximate fallback where reasonable.

## 22. Embedded and External Resources

A3D/A3M MUST support embedded resources.

They MAY support external resource references.

External references MUST be explicitly classified as one of:

- package-relative;
- project-relative;
- absolute local path;
- network/URI reference.

Readers MUST NOT automatically fetch network resources without user or policy permission.

Package-relative paths MUST NOT escape the package root.

Portable export SHOULD prefer embedded or package-relative resources.

## 23. Versioning and Capability Negotiation

The header/manifest MUST contain:

- container version;
- schema version;
- required capabilities;
- optional capabilities.

Readers MUST reject a file when an unknown required capability prevents correct interpretation.

Readers SHOULD ignore unknown optional capabilities while preserving them during round-trip when practical.

Minor schema additions SHOULD be additive.

Breaking semantic changes REQUIRE a major version change.

## 24. Integrity

Containers SHOULD provide integrity metadata for chunks/resources.

At minimum, implementations MUST validate:

- declared lengths;
- offsets;
- table counts;
- reference existence;
- decompressed size limits.

Cryptographic signatures are outside initial scope but MAY be added later.

## 25. Security Considerations

A3D/A3M files are untrusted input.

Readers MUST defend against:

- integer overflow;
- invalid offsets;
- oversized allocations;
- decompression bombs;
- path traversal;
- cyclic reference structures that cause uncontrolled recursion;
- malformed image resources;
- arbitrary code execution through metadata.

The base specification SHALL NOT define executable script chunks.

Future executable extensions require a separate security RFC.

## 26. Privacy Considerations

The formats SHOULD NOT include user-identifying metadata by default.

Author names, workstation paths, geolocation, account identifiers, or project history MUST NOT be embedded unless explicitly requested or required by a separately documented workflow.

Exporters SHOULD provide metadata inspection/removal before distribution.

## 27. Performance Considerations

The container architecture SHOULD allow selective loading.

Examples:

- inspect manifest without loading texture images;
- preview geometry without loading pose catalogs;
- load pose catalog without decoding all material resources;
- load a low-resolution preview before full texture data.

Large binary data SHOULD be independently addressable.

Compression MAY be per-resource/chunk rather than whole-file to preserve random access.

## 28. Compatibility

A3D/A3M are application-neutral.

The format MUST NOT require Blender to interpret a valid core file.

The format MUST NOT require Arco3D to consume a valid core file authored by another conforming implementation.

Adapters MAY retain application-specific metadata in namespaced optional extension chunks.

Such metadata MUST NOT redefine core semantics.

## 29. Reference Manifest Example

The following is illustrative only:

```yaml
format: A3D
container_version: 1
schema_version: 1
asset_id: <stable-id>
asset_name: "Creature_01"
required_capabilities:
  - mesh.core
  - rig.core
optional_capabilities:
  - pose.catalog
  - material.layers
  - style.graphic
roots:
  - <component-id>
resources:
  - <resource-table-reference>
```

The final manifest encoding SHALL be decided during implementation design.

## 30. Testing Strategy

Conforming implementations SHALL test:

- malformed headers;
- malformed chunk directories;
- unknown optional chunks;
- unknown required capabilities;
- stable-ID resolution;
- geometry round-trip;
- rig/weight round-trip;
- pose/catalog round-trip;
- material-layer round-trip;
- mask round-trip;
- embedded texture resources;
- external package-relative resources;
- path traversal rejection;
- large-resource bounds;
- forward-compatible optional metadata preservation.

Golden reference assets SHOULD be maintained in the repository.

## 31. Definition of Done

The first A3D/A3M specification is implementation-ready when:

- physical container encoding is selected;
- header and chunk-directory layouts are frozen for v1;
- coordinate/unit conventions are frozen;
- stable-ID encoding is defined;
- core mesh schema is defined;
- rig/skin schema is defined;
- pose/catalog schema is defined;
- A3M channel/layer/mask schema is defined;
- resource embedding/reference rules are defined;
- version/capability negotiation is defined;
- at least one golden character asset and one golden material package round-trip between Arco3D and a standalone validator;
- Blender Bridge imports the golden asset successfully.

## 32. AI Implementation Guidance

Autonomous coding agents SHALL:

- implement a standalone parser/validator before coupling the format to UI code;
- treat file contents as untrusted;
- use explicit fixed-width integer types for on-disk binary structures;
- centralize bounds checking;
- preserve unknown optional chunks where practical;
- avoid serializing in-memory pointer values or compiler-layout-dependent structs directly;
- avoid Blender-specific canonical fields;
- keep evaluated geometry accessible even when optional Arco3D construction metadata is present;
- produce deterministic serialization where practical to improve testing and source-control diagnostics.

Agents MUST stop for architectural review before:

- changing frozen v1 binary layouts;
- adding executable content;
- adding mandatory network references;
- changing coordinate/unit conventions;
- adding required capabilities that cannot be ignored by existing v1 readers.

## 33. Future Extensions

Possible extensions include:

- full animation curves;
- morph/shape keys;
- LOD groups;
- physics hints;
- collision geometry;
- procedural geometry descriptors;
- digital signatures;
- content-addressed resources;
- streaming profiles;
- scene-level A3D collections;
- Arcology engine runtime profiles.
