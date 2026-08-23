# RFC: Arcology Executable Assembly (AEX)

**RFC Number:** RFC-0046
**Title:** Arcology Executable Assembly (AEX)  
**Status:** Draft  
**Category:** Executable Format / Runtime ABI / Application Model  
**Authors:** Arcology Project  
**Created:** 2026-08-22  
**Related RFCs:** ArcFS v1.0, Address Spaces and Virtual Memory, Runtime Handle Model, Resource Model, Surface Binding / Graphics, Firmware Transition

---

# 1. Executive Summary

This RFC defines the **Arcology Executable Assembly (AEX)** format, the native executable and executable-package format for Arcology OS.

AEX is not intended to be a direct analogue of PE32+ or ELF.

Traditional executable formats primarily describe how a loader should map code, data, relocations, imports, and metadata into a process. Arcology requires a richer contract because applications are expected to participate directly in the Arcology object ecosystem, capability model, runtime inspection environment, component lifecycle system, and ArcoBASIC shell.

An AEX therefore describes an executable as a **self-describing assembly of typed objects, executable implementations, resources, interface contracts, capability requests, diagnostics, integrity information, and ArcoBASIC bindings**.

The format SHALL support native machine-code execution while remaining extensible enough to support future architectures, alternative execution representations, hot-replaceable components, shared immutable objects, portable implementations, and capabilities not known when AEX v1 is defined.

The initial filename extension SHALL be:

```text
.aex
```

The canonical term SHALL be:

```text
Arcology Executable Assembly
```

---

# 2. Motivation

Arcology currently supports executable generation for foreign or firmware environments including PE32+, Windows, and Linux targets. Arcology also has an existing portable application concept in which an ArcoBASIC VM and program assets may be packaged together as a capsule.

Neither model is sufficient for native Arcology applications.

A native Arcology executable must solve several architectural problems that conventional executable formats either do not address or defer to unrelated systems.

AEX is intended to address at least the following classes of problem.

## 2.1 Dependency Identity Versus Dependency Intent

Conventional executables frequently bind to specific library filenames, sonames, DLLs, symbol names, or implementations.

Arcology applications SHOULD instead declare the interface or service they require.

For example:

```text
REQUIRE INTERFACE Image.Decode.PNG >= 2.0 < 3.0
```

is preferable to:

```text
LOAD libpng.so
```

or:

```text
IMPORT png.dll
```

The implementation satisfying the interface MAY change without changing the consuming program.

## 2.2 Executable Opacity

Traditional release executables frequently expose little semantic information without debug symbols, reverse engineering, external symbol databases, or specialized tooling.

Ordinary Arcology applications are expected to remain inspectable through the Arcology runtime and ArcoBASIC shell.

The executable format must therefore preserve enough typed runtime metadata to permit supported introspection without requiring a debug build.

## 2.3 Security Intent Is Usually External to the Executable

Traditional executable formats do not normally provide a complete declaration of what authority an application expects to use.

AEX SHALL permit applications to declare required and optional capabilities before execution.

Capability declaration does not grant capability.

## 2.4 Process-Level Failure Boundaries Are Too Coarse

Arcology applications may consist of independently materialized components.

A failure in an optional spell-checker, renderer, codec, or provider should not necessarily require destruction of the entire application assembly.

AEX SHALL permit components to be represented and managed as individually addressable runtime objects.

## 2.5 Executable Formats Accumulate Historical Constraints

PE and ELF contain structures whose original assumptions continue to affect modern implementations.

AEX SHALL minimize permanently fixed bootstrap structures and SHALL place most semantics in typed, versioned, extensible records.

## 2.6 Runtime Automation Is Commonly Bolted On Later

Arcology treats ArcoBASIC as an ecosystem language, inspection environment, administrative shell, prototyping language, and automation interface.

Native applications SHALL be capable of exposing approved runtime objects to ArcoBASIC without requiring an ArcoBASIC VM to be embedded in every native executable.

---

# 3. Goals

AEX SHALL:

- Provide the native executable format for Arcology OS.
- Support native x86-64 machine code in AEX v1.
- Permit future architecture implementations without redesigning the container format.
- Represent applications as assemblies of components rather than as a single opaque code image.
- Declare provided and required interfaces.
- Declare required and optional capability requests.
- Support Arcology-native runtime object discovery.
- Provide first-class ArcoBASIC reflection and invocation bindings.
- Keep introspection authority separate from mutation or control authority.
- Support immutable, shared, private, copy-on-write, and other explicit memory semantics.
- Support lazy materialization of components.
- Support component-level lifecycle management.
- Support component-level diagnostics and fault attribution.
- Support resources and application assets.
- Support content integrity verification.
- Support publisher signatures without requiring every AEX to be signed.
- Permit unknown optional extensions to be safely ignored.
- Reject unknown required extensions deterministically.
- Be suitable for direct loading from ArcFS.
- Be deterministic enough for reproducible builds.
- Support future hot replacement and polymorphic implementation substitution.
- Preserve enough semantic metadata for useful inspection in production builds.
- Avoid requiring an embedded ArcoBASIC VM for ordinary native applications.
- Allow future AEX components to be backed by native code, ArcoBASIC capsules, intermediate representation, or other implementation types.

---

# 4. Non-Goals

This RFC does not define:

- The complete Arcology graphical application framework.
- The complete ArcoBASIC language specification.
- The internal format of existing ArcoBASIC capsules.
- Windows PE32+ compatibility.
- Linux ELF compatibility.
- A POSIX process model.
- A Windows process model.
- A package repository protocol.
- A software-store policy.
- Secure Boot.
- The ArcFS on-disk format.
- The exact cryptographic signing policy used by future production releases.
- Mandatory remote debugging.
- Mandatory source-code disclosure.
- Mandatory exposure of private application state.
- A requirement that all application components run in one address space.
- A requirement that all application components run in separate address spaces.
- A universal ABI for foreign operating systems.

---

# 5. Terminology

## 5.1 AEX

An **Arcology Executable Assembly**.

An AEX is a structured executable package containing one or more components and their implementations, resources, interface contracts, metadata, integrity information, and runtime bindings.

## 5.2 Assembly

The complete logical application or executable package represented by an AEX.

## 5.3 Component

An independently identifiable unit within an assembly.

Examples include:

- Application
- Editor
- DocumentModel
- Codec
- Renderer
- SpellChecker
- PrintProvider
- Importer
- ServiceAdapter

A component MAY contain executable code, state definitions, resources, interface declarations, lifecycle metadata, or a combination thereof.

## 5.4 Implementation

A concrete executable realization of a component.

Examples:

- x86-64 native machine code
- future ARM64 native machine code
- future RISC-V native machine code
- future ArcoFISSION intermediate representation
- ArcoBASIC capsule
- future hardware-backed implementation

## 5.5 Interface

A versioned contract that a component provides or requires.

Consumers SHOULD bind to interfaces rather than implementation filenames or physical module identities.

## 5.6 Capability

An Arcology authority token or policy-defined permission required to perform an operation.

An AEX may request capabilities. The loader or runtime decides whether they are granted.

## 5.7 ArcoBASIC Binding

Typed metadata that exposes approved runtime objects, properties, methods, events, and types to the ArcoBASIC ecosystem.

## 5.8 Materialization

The act of creating a usable runtime instance of a component or implementation from its AEX representation.

## 5.9 Chunk

A typed record or payload stored in the AEX container.

## 5.10 Required Chunk

A chunk whose semantics must be understood for the assembly to be safely loaded.

## 5.11 Optional Chunk

A chunk that may be ignored by an implementation that does not understand its type.

---

# 6. Design Principles

AEX SHALL be designed according to the following principles.

## 6.1 Executables Describe Intent

An executable should describe:

- what it is,
- what it provides,
- what it requires,
- what authority it requests,
- how its components relate,
- how it may be inspected,
- how it may be materialized.

It should not merely describe byte placement.

## 6.2 Executability Does Not Imply Opacity

Ordinary Arcology executables SHOULD remain meaningfully inspectable after release.

Production introspection metadata SHALL NOT depend entirely on external debug files.

## 6.3 Implementations Are Subordinate to Interfaces

A consuming component SHOULD depend on a versioned interface contract.

The runtime MAY satisfy that interface with any compatible implementation permitted by policy.

## 6.4 Authority Is Explicit

Requested capabilities SHALL be machine-readable before component activation.

A capability request SHALL NOT be interpreted as a capability grant.

## 6.5 Extension Is Normal

The format SHALL assume that new chunk types, implementation types, memory semantics, lifecycle rules, and metadata classes will appear later.

## 6.6 Minimal Permanent Bootstrap State

The permanently fixed AEX bootstrap header SHALL remain small.

New semantics SHOULD normally be introduced through typed chunks rather than bootstrap-header expansion.

---

# 7. File Identification

AEX files SHALL use the extension:

```text
.aex
```

The file SHALL begin with a fixed magic value sufficient to distinguish it from PE, ELF, capsule, archive, and arbitrary data files.

The initial canonical magic SHALL be:

```text
ARCOAEX
```

The exact byte encoding and fixed-width field definition SHALL be standardized by the implementation work package associated with this RFC.

The loader SHALL validate the magic before interpreting any variable-length structure.

---

# 8. High-Level Container Layout

An AEX SHALL consist of:

```text
+----------------------------------+
| Bootstrap Header                 |
+----------------------------------+
| Chunk Directory                  |
+----------------------------------+
| Typed Chunk                      |
+----------------------------------+
| Typed Chunk                      |
+----------------------------------+
| Typed Chunk                      |
+----------------------------------+
| ...                              |
+----------------------------------+
| Integrity / Signature Material   |
+----------------------------------+
```

Physical chunk order SHALL NOT imply semantic dependency unless a future required extension explicitly states otherwise.

References between chunks SHOULD use stable identifiers rather than positional adjacency.

The loader SHALL NOT require application code to appear directly after the header.

---

# 9. Bootstrap Header

The bootstrap header SHALL contain only information required to locate and begin interpreting the remainder of the file.

The initial model SHOULD resemble:

```text
AEX_HEADER
    Magic
    FormatMajor
    FormatMinor
    HeaderSize
    AssemblyID
    DirectoryOffset
    DirectoryLength
    RootManifestReference
    Flags
    Reserved
```

The final binary field widths SHALL be fixed by implementation.

## 9.1 Requirements

The header SHALL:

- have a fixed minimum size,
- be naturally parseable without loading arbitrary application data,
- contain explicit format version fields,
- identify the assembly,
- locate the chunk directory,
- locate the root manifest or provide a directory reference to it,
- reserve expansion space,
- reject malformed offsets before following them.

## 9.2 Prohibited Header Growth Pattern

Future AEX revisions SHOULD NOT add application-level concepts such as:

- icon offsets,
- import counts,
- architecture names,
- permission lists,
- subsystem fields,
- UI properties,
- resource-table offsets

directly to the bootstrap header unless required to bootstrap parsing itself.

Those concepts belong in typed chunks.

---

# 10. Chunk Directory

The chunk directory is the authoritative index of stored AEX chunks.

Each entry SHALL identify at least:

```text
ChunkID
ChunkTypeID
Flags
Offset
StoredLength
LogicalLength
Alignment
EncodingOrCompression
HashReferenceOrHash
```

The exact representation SHALL be standardized by implementation.

## 10.1 Chunk Type Identifiers

Chunk types SHALL use a large, collision-resistant namespace.

AEX v1 SHOULD use 128-bit UUID-compatible identifiers for chunk types.

Small global enum spaces SHALL NOT be the sole extension mechanism.

## 10.2 Required and Optional Semantics

Every chunk directory entry SHALL indicate whether the chunk is:

```text
REQUIRED
```

or:

```text
OPTIONAL
```

If the loader encounters an unknown OPTIONAL chunk type, it SHALL ignore that chunk unless another understood required structure depends upon it.

If the loader encounters an unknown REQUIRED chunk type, it SHALL reject the assembly with a specific unsupported-required-extension error.

## 10.3 Bounds Validation

Before reading any chunk, the loader SHALL verify:

- offset does not overflow,
- stored length does not overflow,
- offset + stored length is within the file,
- alignment is valid,
- overlapping regions do not violate AEX storage rules,
- directory entries do not point into invalid bootstrap structures,
- decompression sizes are bounded by policy.

Malformed chunk metadata SHALL fail closed.

---

# 11. Standard AEX v1 Chunk Classes

AEX v1 SHALL define standard chunk types for at least:

```text
MANIFEST
COMPONENT
IMPLEMENTATION
CODE
DATA
RESOURCE
INTERFACE
REQUIREMENT
CAPABILITY_REQUEST
ARCO_BINDING
LIFECYCLE
MEMORY_POLICY
DIAGNOSTIC
SYMBOL
RECOVERY
INTEGRITY
SIGNATURE
```

Additional chunk types MAY be standardized before AEX v1 is declared stable.

---

# 12. Manifest

Every AEX SHALL contain exactly one root MANIFEST.

The manifest SHALL describe the assembly-level identity and topology.

It SHALL include at least:

- Assembly UUID
- Display name
- Canonical name
- Version
- Build identifier
- AEX format compatibility
- Arcology ABI compatibility
- Root component
- Declared components
- Implementation references
- Required runtime interfaces
- Requested capabilities
- minimum loader requirements
- deterministic build metadata sufficient for reproducibility policy

The manifest MAY include:

- Publisher identity
- Human-readable description
- Localization references
- Update-channel metadata
- Source package identity
- Project URL metadata
- license metadata
- development-channel metadata

The loader SHALL NOT trust human-readable strings for security decisions.

---

# 13. Assembly Identity

Every AEX assembly SHALL have a stable 128-bit Assembly ID.

An assembly version SHALL be distinct from an assembly identity.

For example:

```text
Assembly:
    Arcology.ArcoNote

AssemblyID:
    <stable UUID>

Version:
    2.4.1

BuildID:
    <unique build identity>
```

The stable Assembly ID SHOULD survive ordinary version upgrades.

The Build ID SHALL distinguish different produced artifacts when required by reproducibility, crash analysis, or signature verification.

---

# 14. Component Model

An AEX SHALL contain one or more COMPONENT declarations.

Each component SHALL have:

- Component ID
- Component name
- Component type or role
- owning assembly
- zero or more provided interfaces
- zero or more required interfaces
- zero or more implementations
- lifecycle policy
- memory policy references
- diagnostic identity

A component MAY be:

```text
EAGER
LAZY
ON_DEMAND
SERVICE_BOUND
OPTIONAL
```

The exact standard lifecycle flags SHALL be finalized by implementation.

## 14.1 Root Component

Every executable AEX SHALL identify one root component.

For a conventional user application this will normally be the application component.

Libraries or provider assemblies MAY use a provider root component rather than a GUI application root.

## 14.2 Independent Addressability

Components SHALL be independently addressable by the Arcology runtime.

A component name alone SHALL NOT be assumed globally unique.

The canonical component identity SHALL combine assembly identity and component identity.

## 14.3 Failure Isolation

The component model SHALL permit the runtime to attribute faults to a component.

Future runtimes MAY restart, rematerialize, replace, unload, or isolate a failed component if the component's lifecycle and state contracts permit it.

AEX v1 does not require all such recovery behavior to be implemented immediately.

---

# 15. Implementation Records

A component MAY have one or more IMPLEMENTATION records.

Each implementation SHALL identify:

- owning component,
- implementation type,
- target architecture or execution environment,
- ABI version,
- code/data references,
- required CPU features if applicable,
- preference or priority metadata,
- compatibility constraints.

Example:

```text
IMPLEMENTATION
    Component = Editor
    Type = NATIVE
    Architecture = X86_64
    ABI = Arcology.Native.1
```

Future examples may include:

```text
Type = NATIVE
Architecture = ARM64
```

or:

```text
Type = ARCO_IR
```

or:

```text
Type = ARCOBASIC_CAPSULE
```

## 15.1 Loader Selection

The loader SHALL choose only an implementation compatible with:

- current CPU architecture,
- required CPU features,
- Arcology ABI,
- runtime policy,
- capability policy,
- trust policy,
- implementation-type support.

If no compatible required implementation exists, materialization SHALL fail with a specific implementation-resolution error.

---

# 16. Native Code Requirements

AEX v1 SHALL support native x86-64 code.

Native Arcology code SHOULD be position independent by default.

Native implementations SHOULD avoid preferred virtual image bases.

Absolute addressing SHALL be represented through standardized relocation or binding metadata when unavoidable.

The native ABI SHALL define:

- call convention,
- integer argument passing,
- floating-point argument passing,
- stack alignment,
- return-value rules,
- object-handle representation,
- interface dispatch conventions,
- fault boundaries,
- lifecycle entry conventions.

The native ABI MAY be specified in a related RFC if not already covered elsewhere.

---

# 17. Memory Semantics

AEX SHALL describe memory intent explicitly.

Memory regions SHALL NOT rely solely on historical section names such as `.text`, `.data`, or `.rdata`.

A memory policy SHALL be able to express at least:

```text
READ
WRITE
EXECUTE
IMMUTABLE
PRIVATE
SHARED
COPY_ON_WRITE
ZERO_INITIALIZED
```

Future policy SHOULD be able to express:

```text
EPHEMERAL
PERSISTENT
TRUST_DOMAIN_SHARED
ASSEMBLY_SHARED
SYSTEM_SHARED
DISCARDABLE
RECONSTRUCTABLE
```

## 17.1 W^X

Writable and executable memory simultaneously SHOULD be prohibited by default.

A component requiring writable executable memory SHALL require explicit runtime policy and SHOULD require a specific capability.

## 17.2 Shared Immutable Content

Code and immutable resources MAY be shared across compatible component instances.

The runtime MAY deduplicate identical immutable content when integrity identity permits it.

---

# 18. Interface Contracts

AEX SHALL support explicit provided and required interfaces.

A provided interface declaration SHALL include:

- interface identity,
- interface version,
- providing component,
- callable operation definitions or ABI reference,
- type references.

A required interface declaration SHALL include:

- interface identity,
- acceptable version range,
- requesting component,
- required versus optional status.

Example:

```text
REQUIRE INTERFACE
    Name = Image.Decode.PNG
    Version >= 2.0
    Version < 3.0
    Required = TRUE
```

## 18.1 Interface Resolution

The runtime SHOULD resolve interface requirements by contract.

AEX applications SHOULD NOT require physical library filenames for ordinary Arcology-native dependencies.

## 18.2 Provider Substitution

The runtime MAY satisfy an interface with any compatible provider permitted by policy.

This permits:

- implementation upgrades,
- alternate providers,
- hardware-accelerated providers,
- development providers,
- compatibility providers,
- future polymorphic substitution.

---

# 19. Capability Requests

AEX SHALL contain machine-readable capability declarations.

Capability requests SHALL distinguish at least:

```text
REQUIRED
OPTIONAL
```

Example:

```text
REQUIRED
    User.Documents.Read
    User.Documents.Write
    UI.Display

OPTIONAL
    Network.Client
    Document.Print
```

## 19.1 Request Is Not Grant

A capability declaration is a statement of possible or required application intent.

It SHALL NOT grant authority.

The runtime, user policy, trust policy, administrator policy, or other Arcology authority mechanism SHALL decide what is granted.

## 19.2 Required Capability Failure

If a required capability is denied before activation and the component cannot operate without it, activation SHALL fail deterministically.

## 19.3 Optional Capability Failure

If an optional capability is denied, the component SHALL be permitted to activate if its declared lifecycle allows degraded operation.

The application SHOULD be able to discover the denied capability through the runtime API.

## 19.4 Undeclared Capability Use

Runtime policy SHOULD reject privileged operations not covered by the component's granted capability set.

---

# 20. ArcoBASIC Binding Table

AEX SHALL define an **ArcoBASIC Binding Table (ABT)** representation.

The ABT exposes approved runtime functionality to the Arcology ArcoBASIC ecosystem.

A normal native Arcology application SHOULD contain ArcoBASIC bindings.

The ABT SHALL be capable of describing:

- objects,
- properties,
- methods,
- parameters,
- return types,
- events,
- enums,
- structures,
- references,
- read/write semantics,
- inspection authority requirements,
- invocation authority requirements,
- mutation authority requirements,
- documentation strings or references.

Example conceptual declaration:

```text
OBJECT Editor

PROPERTY CurrentDocument
    Type = DocumentRef
    Access = READ

PROPERTY InsertMode
    Type = BOOL
    Access = READ_WRITE

PROPERTY CursorLine
    Type = U64
    Access = READ

METHOD Save
    Return = Result

METHOD InsertText
    Parameter Text : STRING
    Return = Result

EVENT DocumentChanged
    Parameter Document : DocumentRef
```

---

# 21. ArcoBASIC Runtime Model

The presence of an ABT SHALL NOT require an ArcoBASIC VM to be embedded inside every native AEX.

Arcology SHOULD provide shared ArcoBASIC runtime and shell services.

Native applications SHALL expose typed bridges into their native implementations.

Conceptually:

```basic
APP = SYSTEM.APPLICATIONS("ArcoNote")

PRINT APP.Editor.CursorLine

APP.Editor.InsertText("Hello")
```

The bridge SHALL dispatch into the native component through the standardized Arcology object/interface ABI.

## 21.1 Same Metadata, Multiple Inspectors

The ABT SHOULD be usable by:

- ArcoBASIC shell,
- graphical object inspector,
- diagnostics tools,
- automation tools,
- testing tools,
- development IDEs.

Application-specific introspection metadata SHOULD NOT have to be separately redefined for each tool.

---

# 22. Introspection Versus Authority

Introspection SHALL NOT imply modification authority.

The ABT SHALL permit independent capability requirements for:

- discovering an object,
- reading a property,
- writing a property,
- invoking a method,
- subscribing to an event.

Example:

```text
OBJECT NetworkSession

PROPERTY RemoteAddress
    READ_CAPABILITY = Application.Inspect

METHOD Disconnect
    INVOKE_CAPABILITY = Application.Control

PROPERTY EncryptionKeys
    EXPOSED = FALSE
```

The ArcoBASIC shell MAY therefore be able to inspect safe runtime state while being denied control operations.

Sensitive data SHALL NOT become visible merely because an object is otherwise inspectable.

---

# 23. ArcoBASIC Binding Requirement

For AEX v1:

- Normal user applications SHOULD provide an ABT.
- System services SHOULD provide an ABT unless doing so would violate a security or bootstrap constraint.
- Low-level bootstrap modules MAY explicitly declare `NO_ARCO_BINDING`.
- The absence of an ABT SHALL be visible to diagnostic tools.
- An application SHALL NOT falsely claim to be introspectable if bindings are omitted.

A later Arcology policy MAY make ABT presence mandatory for specific application classes.

---

# 24. Lifecycle Model

AEX SHALL declare component lifecycle entrypoints or lifecycle interfaces.

The standard lifecycle model SHOULD include at least:

```text
Materialize
Initialize
Activate
Suspend
Resume
Quiesce
Shutdown
Destroy
```

Not every component must implement every phase.

The lifecycle record SHALL specify which operations exist and which are required.

## 24.1 No Universal `main()` Requirement

AEX SHALL NOT require all components to revolve around a single C-style `main()` entrypoint.

A root application MAY expose a conventional activation entry through the lifecycle ABI, but lifecycle semantics are authoritative.

## 24.2 Lazy Materialization

A component declared lazy MAY remain unmaterialized until:

- an interface is requested,
- an event requires it,
- another component explicitly requests it,
- the runtime policy activates it.

---

# 25. Resources

AEX SHALL support packaged resources.

Resources MAY include:

- icons,
- UI definitions,
- strings,
- localization data,
- images,
- audio,
- shaders,
- templates,
- dictionaries,
- static data,
- arbitrary application assets.

Resources SHALL have stable resource identities.

Resources SHOULD support:

- immutable mapping,
- compression,
- integrity verification,
- lazy loading,
- content addressing.

A resource SHALL NOT become executable merely by being stored inside an AEX.

---

# 26. Content Addressing

AEX SHOULD support cryptographic content hashes for chunks and components.

The initial implementation SHOULD use a modern cryptographic hash with at least 256 bits of output.

The specific algorithm SHALL be explicitly versioned.

Hashes MAY be used for:

- integrity checking,
- immutable-object identity,
- cache validation,
- ArcFS deduplication,
- shared-code reuse,
- update optimization,
- crash-report identification.

Content hash identity SHALL NOT replace semantic component identity.

---

# 27. Integrity Tree

Every executable AEX SHALL contain integrity information sufficient to verify required executable content before activation.

The integrity model SHOULD permit a root digest covering:

- root manifest,
- required components,
- executable implementations,
- required resources,
- interface metadata,
- capability declarations,
- ArcoBASIC bindings.

The runtime MAY defer verification of optional lazy resources until they are first accessed if the integrity model safely supports such behavior.

Executable code SHALL NOT be activated before its required integrity verification succeeds.

---

# 28. Signatures

AEX SHALL support signatures.

Signatures MAY identify:

- publisher,
- build authority,
- system authority,
- local administrator,
- development authority.

A signature SHALL cover a defined integrity root rather than an ambiguous subset of mutable file bytes.

AEX v1 SHALL permit unsigned assemblies unless platform policy says otherwise.

## 28.1 Component Trust Visibility

The design SHOULD allow diagnostics to distinguish trust states such as:

```text
VERIFIED
UNSIGNED
LOCALLY MODIFIED
UNKNOWN SIGNER
INVALID SIGNATURE
REVOKED
```

Future versions MAY support separately signed optional components or overlays.

---

# 29. Diagnostics Metadata

Every ordinary executable AEX SHALL contain minimal diagnostic metadata.

Release builds SHALL retain at least:

- assembly identity,
- version,
- build identity,
- component identities,
- interface identities,
- lifecycle identities,
- public diagnostic operation names,
- error domains,
- implementation identity sufficient for fault attribution.

Full source-level symbols MAY remain optional.

## 29.1 Fault Reporting Goal

AEX-aware fault reporting SHOULD be capable of producing information conceptually similar to:

```text
FAULT

Application:
    ArcoNote 2.4.1

Component:
    DocumentModel

Operation:
    Document.Parse

Fault:
    Invalid memory access

Implementation:
    X86_64 Native

Offset:
    DocumentModel + 0x19A2
```

instead of exposing only a raw virtual address.

---

# 30. Symbol Metadata

A SYMBOL chunk MAY provide:

- public symbol names,
- private development symbols,
- function ranges,
- source mapping,
- line mapping,
- compiler-generated object mapping.

Production builds MAY strip optional detailed symbols.

Removing detailed symbols SHALL NOT remove mandatory component and interface diagnostic identity.

---

# 31. Recovery Metadata

AEX MAY contain RECOVERY metadata.

Recovery metadata MAY describe:

- whether a component can be restarted,
- whether state can be reconstructed,
- whether state can be serialized,
- dependencies required before rematerialization,
- whether the component is safe to replace while the assembly remains active.

AEX v1 does not require automated recovery to be implemented.

The format SHALL leave room for it.

---

# 32. Dynamic Loading

Arcology-native dynamic dependency resolution SHOULD operate through interface resolution rather than arbitrary filesystem library names.

Applications SHOULD request:

```text
Image.Decode.PNG/2
```

rather than:

```text
:system:libraries:pngdecoder.aex
```

unless a specific implementation identity is explicitly required for a valid reason.

## 32.1 Explicit Implementation Binding

AEX MAY permit explicit implementation binding for:

- testing,
- hardware requirements,
- security-sensitive providers,
- compatibility shims,
- development environments.

Such binding SHOULD be visible in diagnostics.

---

# 33. Object Resolution

The runtime SHALL provide a mechanism for resolving:

- assemblies,
- components,
- interfaces,
- exported runtime objects.

Resolution SHALL use stable identities rather than relying solely on display names.

Human-readable names MAY be used by shells for convenience when unambiguous.

Example:

```basic
APP = SYSTEM.APPLICATIONS("ArcoNote")
```

is shell syntax.

Internally, resolution SHOULD use stable assembly and object identity.

---

# 34. AEX and ArcFS

AEX is the native executable format expected to reside on ArcFS.

The AEX loader SHOULD take advantage of ArcFS capabilities where available.

Potential integration includes:

- direct immutable mapping,
- content hash verification,
- shared object caching,
- sparse or lazy resource access,
- deduplication,
- atomic replacement,
- versioned application storage.

AEX SHALL NOT require ArcFS implementation details to leak into the portable structure of every chunk.

---

# 35. Deterministic Builds

ArcoFISSION SHOULD be capable of producing byte-for-byte deterministic AEX output given identical:

- compiler version,
- source,
- build inputs,
- dependency resolution,
- target configuration,
- explicitly included metadata.

Uncontrolled timestamps, random ordering, nondeterministic UUID creation, or unstable chunk ordering SHOULD NOT prevent reproducible builds.

Build identity generation SHALL have a deterministic mode.

---

# 36. Compression

Individual chunks MAY be compressed.

Compression SHALL be declared per chunk.

Executable code MAY be stored compressed but SHALL be expanded into verified executable memory before activation unless a future implementation supports verified executable mapping directly.

The loader SHALL enforce decompression limits.

A malicious AEX SHALL NOT be able to request unbounded decompressed memory.

---

# 37. Loader Stages

The AEX loader SHOULD operate in explicit stages.

Recommended stages:

```text
1. Open ArcFS object
2. Validate bootstrap header
3. Validate directory bounds
4. Parse root manifest
5. Validate format compatibility
6. Validate required chunk support
7. Validate integrity root
8. Evaluate signature state
9. Resolve architecture implementation
10. Resolve required interfaces
11. Evaluate capability policy
12. Construct component graph
13. Create required address-space mappings
14. Materialize eager components
15. Register ArcoBASIC bindings
16. Initialize root component
17. Activate assembly
```

Failure at any stage SHALL produce a deterministic error category.

---

# 38. Loader Error Classes

The loader SHALL distinguish at least:

```text
INVALID_MAGIC
UNSUPPORTED_FORMAT_MAJOR
UNSUPPORTED_REQUIRED_EXTENSION
MALFORMED_HEADER
MALFORMED_DIRECTORY
OUT_OF_BOUNDS_CHUNK
OVERLAPPING_CHUNK
INVALID_MANIFEST
MISSING_REQUIRED_CHUNK
NO_COMPATIBLE_IMPLEMENTATION
ABI_INCOMPATIBLE
CPU_FEATURE_UNAVAILABLE
INTERFACE_UNRESOLVED
REQUIRED_CAPABILITY_DENIED
INTEGRITY_FAILURE
SIGNATURE_INVALID
MEMORY_POLICY_REJECTED
ARCO_BINDING_INVALID
LIFECYCLE_INVALID
ACTIVATION_FAULT
```

Error reporting SHOULD identify the responsible assembly and component when known.

---

# 39. Validation Before Execution

No untrusted executable machine code SHALL run during AEX structural parsing.

The loader SHALL validate:

- all relevant integer arithmetic,
- offsets,
- lengths,
- alignment,
- directory structure,
- required chunks,
- implementation compatibility,
- integrity metadata

before transferring control into application code.

Capability policy SHALL be established before privileged application operations become possible.

---

# 40. Resource Limits

The loader SHALL enforce policy limits for untrusted AEX files.

Limits SHOULD exist for:

- total chunk count,
- directory size,
- manifest size,
- decompressed chunk size,
- total eager materialization size,
- recursion depth,
- object graph depth,
- string size,
- metadata count,
- interface count,
- ArcoBASIC binding count.

These limits MAY vary by trust domain.

---

# 41. ArcoFISSION Support

ArcoFISSION SHALL gain an Arcology native target.

The canonical source configuration SHOULD eventually resemble:

```basic
#TARGET ARCOLOGY
#PROFILE APPLICATION
```

or equivalent target syntax consistent with the compiler's existing directive model.

ArcoFISSION SHALL emit:

- AEX bootstrap header,
- directory,
- manifest,
- component declarations,
- native x86-64 implementation records,
- code and data chunks,
- interface metadata,
- capability declarations,
- lifecycle metadata,
- diagnostic metadata,
- ArcoBASIC bindings,
- integrity metadata.

---

# 42. Compiler-Generated ArcoBASIC Bindings

ArcoFISSION SHOULD automatically generate ABT entries from explicitly exported ArcoBASIC-visible declarations.

Example source:

```basic
OBJECT Counter

    PUBLIC Value AS U64

    PUBLIC SUB Increment()
        Value = Value + 1
    END SUB

END OBJECT
```

may generate metadata equivalent to:

```text
OBJECT Counter

PROPERTY Value
    Type = U64
    Access = READ_WRITE

METHOD Increment
    Parameters = 0
    Return = VOID
```

The compiler SHALL NOT expose every internal variable automatically.

Visibility SHALL follow explicit language/runtime export rules.

---

# 43. Native-Language Interoperability

AEX SHALL not be limited to binaries originating from ArcoBASIC.

Other compilers MAY generate AEX files if they implement:

- AEX container rules,
- Arcology native ABI,
- interface ABI,
- lifecycle ABI,
- capability metadata,
- required diagnostics,
- ABT metadata where applicable.

This permits future C, C++, Rust, or other language tooling without weakening the Arcology runtime model.

---

# 44. AEX Versus ArcoBASIC Capsule

AEX and the existing ArcoBASIC capsule concept SHALL remain distinct.

## 44.1 Capsule

A capsule is intended to package a portable ArcoBASIC execution environment, program code, and assets.

Typical targets may include:

- Windows,
- Linux,
- browser,
- Arcology,
- future foreign systems.

## 44.2 AEX

An AEX is the native Arcology executable assembly.

It integrates directly with:

- Arcology interfaces,
- APS/runtime object management,
- capabilities,
- ArcFS,
- ArcoBASIC inspection,
- Arcology lifecycle semantics.

## 44.3 Future Capsule Implementation Inside AEX

A future AEX MAY contain an implementation whose execution type is an ArcoBASIC capsule.

This permits a component interface to remain unchanged while its implementation varies.

Example:

```text
Component: Importer

Implementation A:
    Native X86_64

Implementation B:
    ArcoBASIC Capsule
```

The runtime may select an allowed compatible implementation.

---

# 45. Extensibility Rules

AEX extensions SHALL follow these rules.

1. New semantics SHOULD use new typed chunks.
2. Unknown optional chunks SHALL be ignorable.
3. Unknown required chunks SHALL cause deterministic rejection.
4. Existing chunk semantics SHALL NOT be silently redefined.
5. New incompatible semantics SHALL require a new major format version or a required extension with explicit versioning.
6. New optional fields in versioned records SHALL have defined default behavior.
7. Reserved bits SHALL be zero when written and ignored when allowed by the relevant version rule.
8. Extension identifiers SHALL come from a collision-resistant namespace.

---

# 46. Format Versioning

AEX SHALL have:

```text
FormatMajor
FormatMinor
```

A major version change indicates that an older loader cannot safely assume it understands the structural contract.

A minor version change SHOULD remain readable when only optional backward-compatible features are added.

Required extensions MAY allow capability growth without a major version increase.

---

# 47. ABI Versioning

AEX format version and Arcology runtime ABI version SHALL be independent.

An AEX container may be structurally valid while containing an implementation targeting an unsupported runtime ABI.

The loader SHALL report the correct category.

Example:

```text
AEX format:
    Supported

Native ABI:
    Arcology.Native.4

System supports:
    Arcology.Native.1 through 3

Result:
    ABI_INCOMPATIBLE
```

---

# 48. Interface Versioning

Interface versions SHALL be independently versioned from:

- AEX format,
- application version,
- Arcology ABI.

A required interface SHOULD permit version ranges.

Exact version binding SHOULD be used only when semantically necessary.

---

# 49. Hot Replacement Readiness

AEX v1 SHALL be designed so future runtimes can replace compatible components without redesigning the format.

The format SHOULD preserve:

- stable component identity,
- interface identity,
- implementation identity,
- lifecycle hooks,
- state/recovery metadata,
- content hashes.

Actual live component replacement MAY be deferred to later runtime milestones.

---

# 50. Security Considerations

AEX is a parser for attacker-controlled binary data and SHALL be treated as a security boundary.

The loader SHALL:

- validate all arithmetic,
- reject malformed graphs,
- reject invalid references,
- bound decompression,
- reject executable content failing integrity policy,
- establish memory permissions explicitly,
- avoid writable-executable mappings by default,
- enforce capability policy,
- enforce object exposure policy,
- avoid running code during metadata parsing.

ArcoBASIC introspection SHALL not bypass application capability controls.

The shell SHALL not gain ambient authority merely because it can discover an object.

---

# 51. Privacy Considerations

Runtime introspection can expose sensitive user or application state.

AEX ABT definitions SHALL permit data to be:

- unexposed,
- inspectable,
- inspectable only with authority,
- mutable only with authority.

Applications SHOULD NOT expose:

- credentials,
- encryption keys,
- authentication tokens,
- private memory buffers,
- user secrets

through general-purpose introspection bindings.

---

# 52. Developer Experience

The developer workflow SHOULD eventually be:

```text
ArcoBASIC / Source
        ↓
ArcoFISSION
        ↓
.aex
        ↓
ArcFS
        ↓
AEX Loader
        ↓
Arcology Runtime
```

A developer SHOULD be able to inspect the result with tools such as:

```text
aexinfo Program.aex
```

or an Arcology-native equivalent.

Expected information SHOULD include:

- assembly identity,
- components,
- implementations,
- required interfaces,
- provided interfaces,
- capability requests,
- memory policies,
- ABT exports,
- integrity status,
- signature status.

---

# 53. Runtime Inspection Example

A normal running application SHOULD support an experience conceptually similar to:

```basic
APP = SYSTEM.APPLICATIONS("ArcoNote")

INSPECT APP
```

Possible output:

```text
ArcoNote
    State: Running
    Version: 2.4.1
    Build: 82C...

Components:
    Application        ACTIVE
    Editor             ACTIVE
    DocumentModel      ACTIVE
    SpellChecker       DORMANT
    Printing           DORMANT

Capabilities:
    User.Documents.Read       GRANTED
    User.Documents.Write      GRANTED
    Clipboard.Read            GRANTED
    Network.Client            DENIED/OPTIONAL

Interfaces:
    Document.Editor/2
    Document.Model/3
    UI.Application/4
```

This output is illustrative, not a mandatory shell formatting contract.

---

# 54. Minimum AEX v1 Implementation

The first usable AEX implementation SHALL NOT attempt to implement every future capability in this RFC.

The minimum implementation SHALL support:

- fixed AEX bootstrap header,
- chunk directory,
- root manifest,
- components,
- one x86-64 native implementation per required component,
- code chunks,
- data chunks,
- resource chunks,
- required interface declarations,
- capability declarations,
- basic lifecycle metadata,
- basic diagnostic metadata,
- ArcoBASIC bindings,
- integrity verification,
- deterministic load errors.

The first implementation MAY defer:

- compression,
- signatures,
- multi-architecture packages,
- component hot replacement,
- automatic recovery,
- cross-assembly deduplication,
- portable IR,
- capsule-backed implementations.

Deferred features SHALL remain representable by the format design.

---

# 55. Initial Loader Milestone

The first native AEX loader milestone SHOULD load a deliberately minimal executable assembly from ArcFS.

The validation AEX SHOULD contain:

```text
Assembly:
    AEXHello

Components:
    Application

Implementation:
    X86_64 Native

Capabilities:
    UI.Display or Console.Output as appropriate

ArcoBASIC Binding:
    Application.Message
    Application.Counter
```

The executable SHOULD:

1. load from ArcFS,
2. pass structural verification,
3. resolve its implementation,
4. receive its capability set,
5. materialize the root component,
6. execute native x86-64 code,
7. expose at least one property through the ABT,
8. permit the ArcoBASIC shell or a temporary test harness to inspect that property,
9. shut down cleanly.

---

# 56. Initial Test Fixtures

The project SHALL add deterministic fixtures covering at least:

```text
valid-minimal.aex
invalid-magic.aex
invalid-header-size.aex
directory-out-of-bounds.aex
chunk-out-of-bounds.aex
unknown-optional-chunk.aex
unknown-required-chunk.aex
missing-manifest.aex
missing-required-component.aex
unsupported-architecture.aex
abi-incompatible.aex
required-capability-denied.aex
invalid-integrity.aex
invalid-abt.aex
```

Each fixture SHALL have an expected loader result.

---

# 57. Loader Fuzzing

The AEX parser SHOULD be fuzz-tested.

Fuzz targets SHOULD include:

- bootstrap header parser,
- directory parser,
- manifest parser,
- chunk reference resolver,
- ABT parser,
- integrity parser.

No malformed AEX should cause:

- memory corruption,
- arbitrary execution,
- unchecked allocation,
- infinite recursion,
- unbounded decompression,
- silent acceptance of invalid required metadata.

---

# 58. QEMU Validation

QEMU SHALL be used for regression testing of AEX loading.

At minimum, automated QEMU validation SHOULD prove:

- ArcFS can locate the `.aex`,
- AEX structural parsing succeeds,
- native code executes after UEFI boot services are unavailable,
- lifecycle activation occurs,
- ABT metadata is registered,
- the application can be terminated or shut down without fault.

QEMU is not sufficient for final hardware validation of the complete native application path.

---

# 59. Real Hardware Validation

After the Arcology native input and runtime path permits practical application execution, AEX SHALL be validated on physical x86-64 hardware.

The real-hardware test SHALL load the exact AEX artifact also exercised in QEMU.

Evidence SHOULD record:

- Git commit,
- ArcoFISSION version,
- AEX loader version,
- ArcFS version,
- hardware model,
- CPU,
- firmware version,
- AEX file hash,
- observed output,
- ABT inspection result,
- shutdown result.

Hardware compatibility SHALL not be claimed solely from QEMU results.

---

# 60. AI Implementation Guidance

Agents implementing this RFC SHALL NOT reduce AEX to a renamed PE/ELF container.

Agents SHALL preserve these architectural requirements:

- typed chunk extensibility,
- component identity,
- interface-oriented dependencies,
- explicit capability requests,
- ArcoBASIC bindings,
- lifecycle metadata,
- diagnostics,
- future multi-implementation support.

Agents SHALL NOT:

- embed an ArcoBASIC VM in every native AEX merely to satisfy introspection,
- treat capability requests as grants,
- expose all native memory to ArcoBASIC,
- use arbitrary library filenames as the primary Arcology-native dependency model,
- require a fixed virtual image base,
- silently ignore unknown required chunks,
- execute application code during metadata parsing,
- claim signing support before verification is implemented,
- claim hot replacement merely because component records exist.

Agents SHOULD implement the smallest complete vertical slice first.

---

# 61. Suggested Implementation Order

Implementation SHOULD proceed approximately as follows:

```text
Phase 1
    AEX binary constants and parser structures

Phase 2
    Bootstrap header validation

Phase 3
    Chunk directory validation

Phase 4
    Manifest and component graph

Phase 5
    Native x86-64 implementation record

Phase 6
    ArcFS-backed code/data mapping

Phase 7
    Native ABI lifecycle invocation

Phase 8
    Interface metadata

Phase 9
    Capability request evaluation

Phase 10
    ArcoBASIC binding registration

Phase 11
    Integrity verification

Phase 12
    Diagnostic tooling

Phase 13
    Negative fixtures and fuzzing

Phase 14
    QEMU validation

Phase 15
    Physical hardware validation
```

Each phase SHALL preserve the ability to reject malformed unsupported input safely.

---

# 62. Suggested Source Tree

The exact source tree is implementation-dependent, but a clean split SHOULD resemble:

```text
systems/
    aex/
        format/
        parser/
        loader/
        manifest/
        component/
        interface/
        capability/
        bindings/
        integrity/
        diagnostics/

compiler/
    targets/
        arcology/
            aex/

tools/
    aexinfo/

tests/
    aex/
        fixtures/
        parser/
        loader/
        runtime/
```

Existing Arcology repository conventions take precedence where they differ.

---

# 63. Tooling Requirements

The project SHOULD provide an AEX inspection utility before AEX v1 is declared stable.

The utility SHOULD be capable of reporting:

```text
Header
Manifest
Components
Implementations
Interfaces
Capabilities
Memory Policies
Resources
ArcoBASIC Bindings
Integrity
Signatures
Diagnostics
```

The utility SHOULD support a non-executing mode that never materializes native code.

---

# 64. Example Conceptual AEX

```text
ArcoNote.aex
│
├── MANIFEST
│
├── COMPONENT Application
│   └── IMPLEMENTATION X86_64
│       └── CODE
│
├── COMPONENT Editor
│   └── IMPLEMENTATION X86_64
│       ├── CODE
│       └── DATA
│
├── COMPONENT DocumentModel
│   └── IMPLEMENTATION X86_64
│       ├── CODE
│       └── DATA
│
├── COMPONENT SpellChecker
│   └── IMPLEMENTATION X86_64
│       ├── CODE
│       └── RESOURCE Dictionary
│
├── INTERFACE Document.Editor
├── INTERFACE Document.Model
├── REQUIREMENT UI.Application
├── CAPABILITY_REQUEST
├── ARCO_BINDING
├── LIFECYCLE
├── DIAGNOSTIC
├── INTEGRITY
└── SIGNATURE
```

This diagram is illustrative.

Physical file ordering is not normative.

---

# 65. Future Extensions

The following features are explicitly anticipated by the AEX design but are not required for the first implementation:

- ARM64 native implementations
- RISC-V native implementations
- ArcoFISSION portable intermediate representation
- capsule-backed components
- component-level live replacement
- state migration
- automatic component restart
- hardware-backed implementations
- accelerator-specific implementations
- encrypted resources
- trust-domain sharing
- cross-assembly content deduplication
- delta updates
- signed optional overlays
- application-defined optional chunk classes
- distributed interface providers
- remote object providers
- persistent component state
- JIT-generated implementations under explicit capability
- sealed application profiles

Future support for these features SHALL NOT require replacing the AEX container merely because the feature was not implemented in v1.

---

# 66. Compatibility Policy

Once AEX v1 is declared stable:

- valid AEX v1 files SHALL remain loadable by future Arcology systems where the required native ABI remains supported,
- optional extension growth SHALL not invalidate existing files,
- required incompatible behavior SHALL be explicitly versioned,
- tools SHALL clearly distinguish format incompatibility from ABI incompatibility.

The project SHALL avoid undocumented loader heuristics that become accidental ABI.

---

# 67. Definition of Done

This RFC is complete when all of the following are true:

1. The AEX v1 binary bootstrap header is formally defined.
2. The AEX chunk-directory format is formally defined.
3. Required versus optional chunk behavior is implemented.
4. A root manifest is implemented.
5. Component declarations are implemented.
6. Native x86-64 implementation records are implemented.
7. ArcoFISSION can emit a native Arcology `.aex`.
8. ArcFS can locate and provide the AEX to the loader.
9. The loader safely validates malformed offsets and lengths.
10. The loader can map native code and data according to AEX memory policy.
11. The Arcology native ABI can activate the root component.
12. Required interfaces can be resolved or rejected deterministically.
13. Required and optional capability requests are evaluated before privileged use.
14. An ArcoBASIC Binding Table is emitted by ArcoFISSION.
15. At least one running native AEX object can be inspected through ArcoBASIC or the temporary equivalent runtime test harness.
16. Introspection access and control access are demonstrably separable.
17. Integrity verification rejects a modified executable chunk.
18. An AEX inspection tool can parse an AEX without executing it.
19. Negative fixtures cover malformed and unsupported input.
20. Automated QEMU validation passes.
21. The same AEX artifact is demonstrated on physical x86-64 Arcology hardware.
22. A hardware validation report is committed.
23. No claim of multi-architecture, hot replacement, signatures, recovery, or capsule-backed implementations is made unless separately implemented and validated.

---

# 68. Appendix A: Initial Standard Chunk Registry

The implementation SHALL assign stable identifiers to the standard chunk classes.

The logical registry begins with:

| Logical Name | Requirement | Purpose |
|---|---|---|
| MANIFEST | Required | Assembly identity and topology |
| COMPONENT | Required | Component declaration |
| IMPLEMENTATION | Required for executable component | Concrete implementation descriptor |
| CODE | Required for native executable implementation | Executable machine code |
| DATA | Optional | Initialized or zero-initialized component data |
| RESOURCE | Optional | Non-executable packaged assets |
| INTERFACE | Optional | Provided interface contract |
| REQUIREMENT | Optional | Required interface contract |
| CAPABILITY_REQUEST | Optional | Required and optional capability intent |
| ARCO_BINDING | Recommended / policy-dependent | ArcoBASIC object bindings |
| LIFECYCLE | Required for executable component | Component lifecycle contract |
| MEMORY_POLICY | Required where not implied by implementation type | Memory mapping semantics |
| DIAGNOSTIC | Required for ordinary executable assemblies | Production diagnostic identity |
| SYMBOL | Optional | Extended symbols and source mapping |
| RECOVERY | Optional | Recovery and rematerialization metadata |
| INTEGRITY | Required | Content integrity information |
| SIGNATURE | Optional | Publisher or authority signatures |

The numeric or UUID values SHALL be frozen only when implementation begins.

---

# 69. Appendix B: Initial Capability Example

A text representation used by development tooling MAY resemble:

```text
CAPABILITY_REQUEST

Required:
    UI.Display
    User.Documents.Read
    User.Documents.Write

Optional:
    Clipboard.Read
    Network.Client
    Document.Print
```

This representation is illustrative.

The binary representation SHALL use stable capability identities, not user-facing strings alone.

---

# 70. Appendix C: Initial ArcoBASIC Binding Example

```text
ARCO_BINDING

Object:
    Editor

Properties:
    CurrentDocument : DocumentRef
        Read = Application.Inspect

    InsertMode : BOOL
        Read = Application.Inspect
        Write = Application.Control

    CursorLine : U64
        Read = Application.Inspect

Methods:
    Save() -> Result
        Invoke = Application.Control

    InsertText(Text : STRING) -> Result
        Invoke = Application.Control

Events:
    DocumentChanged(Document : DocumentRef)
        Subscribe = Application.Inspect
```

The exact binary schema SHALL be separately frozen.

---

# 71. Appendix D: Initial Loader Output Example

A successful development-mode load MAY report:

```text
AEX LOAD

Assembly:
    AEXHello

Format:
    AEX 1.0

Architecture:
    X86_64

Components:
    Application

Integrity:
    VERIFIED

Capabilities:
    Console.Output        GRANTED

ArcoBASIC:
    REGISTERED

Lifecycle:
    MATERIALIZED
    INITIALIZED
    ACTIVE
```

A failed load SHOULD identify the stage and reason.

Example:

```text
AEX LOAD FAILED

Assembly:
    Example

Stage:
    Capability Evaluation

Error:
    REQUIRED_CAPABILITY_DENIED

Capability:
    Hardware.RawPCI
```

---

# 72. Appendix E: Constitutional AEX Rules

The following rules are intentionally restated because future implementation shortcuts are likely to pressure them.

1. **Executables describe intent, not merely memory layout.**
2. **Executability does not imply opacity.**
3. **Implementations are subordinate to interfaces.**
4. **Capability request does not imply capability grant.**
5. **ArcoBASIC introspection does not imply control authority.**
6. **Unknown optional extensions are survivable.**
7. **Unknown required extensions fail closed.**
8. **Application code never executes merely to parse executable metadata.**
9. **Native AEX applications do not require a private embedded ArcoBASIC VM.**
10. **AEX is an object assembly, not a renamed PE or ELF image.**

---

# 73. Revision History

| Version | Date | Summary |
|---------|------|---------|
|0.1|2026-08-22|Initial Arcology Executable Assembly specification. Defines the AEX container, component and implementation model, interface-oriented dependency resolution, capability requests, explicit memory semantics, ArcoBASIC Binding Table, lifecycle metadata, diagnostics, integrity, signatures, extensibility rules, ArcoFISSION target requirements, validation fixtures, QEMU testing, and real-hardware Definition of Done.|
