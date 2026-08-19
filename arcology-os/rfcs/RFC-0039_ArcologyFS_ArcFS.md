# RFC-0039: ArcologyFS (ArcFS)

**RFC Number:** RFC-0039
**Title:** ArcologyFS (ArcFS)
**Status:** Draft
**Category:** Storage / Filesystem Architecture
**Authors:** Arcology Project
**Created:** 2026-08-19
**Last Updated:** 2026-08-19
**Supersedes:** None
**Superseded By:** None
**Related Architecture:** Arcology Object Architecture; Polymorphic Substrate (APS); Physical Region Database (PRD); Virtual Region Database (VRD); Runtime Handle Model; Resource Model; ArcoBASIC Hardware Semantics
**Related RFCs:** RFC-0000, RFC-0005 (ArcoBASIC Hardware Semantics), RFC-0015 (Runtime Object Handles), RFC-0017 (Substrate Resource Model), RFC-0018 (Firmware Transition and Memory Ownership), RFC-0019 (Physical Region Database), RFC-0020 (Address Spaces and Virtual Memory), RFC-0036 (APS Timer and Interrupt Routing Service), RFC-0037 (APS Substrate Dispatch Loop), RFC-0038 (APS Block Storage and Filesystem Provider Substrate)

------------------------------------------------------------------------

# 1. Executive Summary

ArcologyFS, abbreviated ArcFS, is the native general-purpose filesystem for Arcology OS.

ArcFS is not intended to be a Unix filesystem with different punctuation. It is designed around
Arcology's own architectural assumptions:

- objects have stable identity independent of their names;
- capabilities and authority are explicit;
- system components are replaceable implementations of published interfaces;
- storage is exposed through typed interfaces rather than device files;
- filesystem attachment participates in the Arcology object lifecycle;
- paths use Arcology's colon-delimited namespace;
- integrity and crash consistency are baseline requirements rather than optional enterprise
  features;
- inspection, recovery, and observability are first-class operating-system behaviors.

ArcFS SHALL use a copy-on-write transactional metadata model. Committed filesystem state is
represented by a validated checkpoint referencing immutable metadata-tree roots. A transaction
creates new metadata/data extents, validates them, flushes required storage barriers, and
atomically advances the committed checkpoint. The previous committed checkpoint remains
structurally intact until reclamation is safe.

Every persistent filesystem object SHALL have a stable Object ID. A pathname is a mapping from a
namespace component to an Object ID; it is not the object's fundamental identity. Renaming or
moving an object therefore changes namespace records without changing the identity used by open
handles, inspectors, contracts, snapshots, or internal references.

ArcFS SHALL be extent-based, sparse-file capable, checksummed, Unicode-aware, snapshot-capable,
and designed for large storage devices. Metadata checksums and persistent structural validation
are mandatory. File-data checksums are mandatory for normal ArcFS data extents. Implementations
MAY accelerate or cache verification, but MUST NOT silently weaken the on-disk integrity contract.

ArcFS SHALL expose filesystem behavior through Arcology interfaces such as `FileSystem`,
`Namespace`, `ByteStream`, `Directory`, `Materializable`, `Inspectable`, and `HealthReporter`.
ArcFS itself is one implementation of the filesystem contract. The operating system MAY support
other implementations simultaneously.

ArcFS SHALL NOT use filesystem entries to masquerade hardware, processes, sockets, or
kernel/substrate internals as files. A disk is a storage object. A network interface is a network
object. A process is a runtime object. Arcology's object model remains the system namespace; ArcFS
is the persistent content namespace.

The expected result is a filesystem that feels native to Arcology rather than inherited from
another operating system: durable, inspectable, transactional, capability-aware, friendly to
graphical administration, scriptable through ArcoBASIC, and sufficiently explicit that independent
implementations can be tested for conformance.

------------------------------------------------------------------------

# 2. Motivation

Traditional filesystem APIs combine several historically convenient concepts:

- path lookup;
- security boundary;
- object identity;
- device namespace;
- stream I/O;
- process-global mount topology;
- metadata storage;
- compatibility conventions inherited from Unix or DOS.

Arcology does not need to preserve those conceptual mergers internally.

Arcology already treats applications, devices, services, memory resources, and system facilities
as inspectable objects with explicit interfaces and lifecycle. A native filesystem should
reinforce that model rather than creating a second operating-system ontology beside it.

ArcFS exists to solve five architectural problems.

## 2.1 Stable identity

A file should not become a conceptually different object merely because it was renamed from
`:work:draft.txt` to `:work:finished.txt`. Nor should an open handle need to re-resolve a text
pathname after every operation. Stable persistent Object IDs allow namespace mutation without
invalidating identity.

## 2.2 Transactional durability

Power failure, reset, driver failure, or media removal must not routinely leave the filesystem in
an indeterminate half-mutated state. Metadata operations should have an explicit transaction
boundary and a known last committed filesystem state.

## 2.3 Native authority semantics

Arcology applications should receive authority to objects, not necessarily global visibility of a
traditional mounted filesystem tree. A file chooser can grant an application a handle to a file
without granting arbitrary traversal of the containing volume. Filesystem security must cooperate
with Arcology's capability model rather than relying exclusively on pathname-based permission
checks.

## 2.4 Replaceability

Arcology's filesystem interface must be separable from ArcFS's on-disk implementation. ArcFS is
expected to be the reference native filesystem, not the only filesystem the substrate is capable
of using.

## 2.5 Human-centered inspection and recovery

Users should not need filesystem archaeology to determine:

- which physical device backs a namespace;
- whether the volume is healthy;
- which checkpoint is active;
- whether corruption was detected;
- what a repair operation proposes to change;
- whether a snapshot exists;
- what object currently owns or references storage.

The filesystem should expose enough structured state that Arcology's recovery environment, storage
inspector, and ArcoBASIC tooling can explain it directly.

------------------------------------------------------------------------

# 3. Goals

ArcFS SHALL provide:

- A native persistent filesystem for Arcology OS.
- Stable persistent object identity independent of paths.
- Atomic metadata transactions and crash-consistent recovery.
- Copy-on-write metadata updates.
- Extent-based file storage.
- Sparse files.
- Metadata and file-data checksums.
- Fast snapshots based on copy-on-write sharing.
- Reflink/clone semantics for efficient file duplication.
- Typed metadata suitable for Arcology object inspection.
- Capability-aware access and handle-based operation.
- Colon-delimited namespace compatibility.
- Online health reporting and structural verification.
- Deterministic offline inspection and repair tooling.
- Clear block-device ordering and flush requirements.
- Versioned on-disk structures with forward evolution rules.
- Explicit limits and validation rules suitable for hostile/corrupt media.
- Interoperability with the Arcology object lifecycle and replaceable subsystem model.
- Conformance tests sufficient for independent filesystem implementations.
- A design that scales from small boot/system volumes to multi-terabyte general-purpose storage.

------------------------------------------------------------------------

# 4. Non-Goals

The initial ArcFS specification does not require:

- distributed storage;
- cluster-wide multi-writer semantics;
- network filesystem transport;
- block-level RAID implementation inside ArcFS;
- content-addressed global deduplication;
- mandatory transparent compression;
- mandatory at-rest encryption;
- POSIX compatibility as an internal design constraint;
- Unix device files;
- Unix ownership/mode bits as the authoritative security model;
- Windows drive-letter semantics;
- DOS 8.3 names;
- in-place metadata journaling;
- transactional atomicity across multiple independent ArcFS volumes;
- infinite snapshot retention;
- booting legacy firmware directly from ArcFS without an appropriate Arcology/UEFI boot path.

Compatibility layers MAY emulate external filesystem or API behavior without changing ArcFS's
native contract.

------------------------------------------------------------------------

# 5. Design Principles

## 5.1 Paths locate; Object IDs identify

A path is a human and programmatic lookup expression. A persistent Object ID is identity.

## 5.2 Committed state is immutable

A committed ArcFS metadata block SHALL NOT be modified in place. Mutations produce new
blocks/extents and become visible only when a new checkpoint commits.

## 5.3 Corruption is an error, not an interpretation opportunity

Malformed lengths, invalid tree relationships, overlapping allocated extents, impossible reference
counts, checksum failures, and invalid Object IDs MUST be rejected. Implementations MUST NOT guess
silently.

## 5.4 Recovery should explain itself

Repair tools SHOULD produce a machine-readable and human-readable change plan before destructive
repair whenever sufficient filesystem state remains to do so.

## 5.5 Files are not devices

ArcFS stores persistent content. Arcology system objects expose hardware and services.

## 5.6 Authority follows handles

Path traversal is one way to obtain an object handle. It is not the only way, and possession of a
valid authorized handle must not require repeated pathname traversal.

## 5.7 Namespace attachment replaces hidden mount magic

Filesystem instances attach to Arcology namespace objects through explicit lifecycle and authority
operations.

------------------------------------------------------------------------

# 6. Terminology

**ArcFS Volume**
A formatted ArcFS filesystem instance backed by one logical block-storage object.

**Storage Object**
An Arcology object implementing a block-storage interface. Examples include an NVMe namespace,
SATA disk partition, RAM-backed block device, or virtual disk.

**Object ID (OID)**
A stable 128-bit identifier for an object within an ArcFS volume.

**Volume UUID**
A 128-bit identifier for an ArcFS volume.

**Namespace Record**
A mapping from (parent OID, name) to a target OID and relationship metadata.

**Directory Object**
An ArcFS object capable of owning namespace records.

**File Object**
An ArcFS object exposing a primary byte stream and file metadata.

**Extent**
A contiguous logical range of volume blocks assigned to file data or filesystem metadata.

**Logical Block**
ArcFS's minimum addressable allocation/checksum unit as defined by the volume format. The
reference format uses 4096-byte logical blocks.

**Transaction**
A set of same-volume filesystem mutations that become visible atomically at commit.

**Checkpoint**
A committed root record identifying a complete, internally consistent filesystem state.

**Generation**
A monotonically increasing 64-bit commit sequence number.

**Snapshot**
A persistent reference to a prior committed tree-root set.

**Reflink**
A new file/object stream mapping that shares immutable data extents until one side is modified.

**Namespace Attachment**
The act of attaching a filesystem namespace to an Arcology namespace location. This is the native
Arcology equivalent of a traditional mount.

**Authority Descriptor**
Persistent or runtime security metadata defining who/what may acquire specific capabilities over
an object.

**Quiesce**
A lifecycle state in which new mutations are blocked and outstanding transactions are completed or
aborted so a filesystem implementation can detach, flush, snapshot, or transition safely.

------------------------------------------------------------------------

# 7. Architectural Position

ArcFS occupies a layer above block storage and below persistent-content consumers.

```text
+----------------------------------------------------------+
| Applications / Services / Recovery Environment           |
+----------------------------------------------------------+
| Arcology Object + Capability + Namespace Facilities       |
+----------------------------------------------------------+
| FileSystem Interface                                      |
|   ArcFS       FAT Provider      ISO Provider     ...       |
+----------------------------------------------------------+
| Volume / Partition Objects                                 |
+----------------------------------------------------------+
| BlockStorage Interface                                     |
|   NVMe   SATA/AHCI   USB Mass Storage   VirtIO   ...        |
+----------------------------------------------------------+
| Hardware / Hypervisor                                      |
+----------------------------------------------------------+
```

ArcFS MUST NOT directly encode assumptions about a particular NVMe, SATA, USB, or virtual-disk
driver. It consumes the published Arcology block-storage contract.

A physical device MAY expose multiple storage objects. A storage object MAY be partitioned or
transformed by another object before ArcFS sees it.

Example:

```text
NVMe0
  implements PCIEndpoint
  attachment: NVMeDriver
    exposes BlockStorageDevice
      namespace NVMe0n1
        exposes BlockStorage
          partition GPT-3
            exposes BlockStorage
              ArcFS Volume "System"
                exposes FileSystem + Namespace + HealthReporter
```

------------------------------------------------------------------------

# 8. Filesystem Object Model

ArcFS SHALL project persistent records into Arcology objects.

A typical file may appear conceptually as:

```text
Object ArcFS:7f1c...92a0
  Type: File
  Implements:
    ByteStream
    Materializable
    Inspectable
    MetadataProvider
    StateProvider
  PersistentIdentity:
    Volume = 8a32...1d09
    OID    = 7f1c...92a0
```

A directory may expose:

```text
Object ArcFS:3c9d...0b11
  Type: Directory
  Implements:
    Namespace
    Inspectable
    MetadataProvider
```

The native system SHALL distinguish:

- filesystem object identity;
- namespace relationships;
- runtime handle identity;
- storage extents.

They MUST NOT be conflated.

------------------------------------------------------------------------

# 9. Namespace and Path Model

## 9.1 Root notation

Arcology absolute paths use a leading colon:

```text
:
:system
:system:software
:software:utility:arconote.aex
:home:documents:notes.txt
```

The colon is the namespace component separator.

## 9.2 Component rules

Native ArcFS namespace components:

- SHALL be UTF-8;
- SHALL be normalized to Unicode NFC before persistent comparison;
- SHALL be case-preserving;
- SHALL be case-sensitive in the base ArcFS format;
- MUST NOT contain `:`;
- MUST NOT contain U+0000;
- MUST NOT be empty;
- SHALL have a maximum encoded length of 255 bytes after normalization.

The base on-disk format MUST NOT allow two names in the same directory that are byte-identical
after required normalization.

A compatibility namespace provider MAY expose a case-insensitive view, but it MUST define
deterministic collision behavior and MUST NOT corrupt or ambiguously mutate the native namespace.

## 9.3 Path length

ArcFS SHALL NOT define a legacy fixed `MAX_PATH` comparable to DOS/Windows limits.
Implementations MUST support fully resolved path expressions of at least 32 KiB encoded UTF-8.
APIs SHOULD operate component-by-component and SHOULD NOT require a caller to allocate a
maximum-size path buffer.

## 9.4 Special directory tokens

`.` and `..` SHALL NOT receive magical on-disk directory entries.

A language binding or compatibility layer MAY accept parent/current-directory syntax, but it MUST
resolve those semantics before or during namespace traversal without creating persistent
pseudo-records.

## 9.5 Textual symbolic links

Traditional unrestricted pathname-string symbolic links are NOT part of the ArcFS base contract.

ArcFS instead defines an Object Link relationship. An Object Link SHOULD target a stable (Volume
UUID, OID) identity where possible. A cross-provider or intentionally path-dynamic link MAY use a
typed locator record, but such locators are distinct from native stable object links and MUST be
visibly inspectable as such.

This distinction reduces path-retargeting ambiguity, dangling references, and security-sensitive
re-resolution.

------------------------------------------------------------------------

# 10. Namespace Attachment

Arcology SHALL treat a filesystem namespace as an attachable object.

A typical lifecycle is:

```text
DISCOVER storage object
CREATE filesystem provider instance
ATTACH provider -> storage object
VERIFY media and format
ACTIVATE filesystem
ATTACH filesystem namespace -> target namespace location
```

Detachment is:

```text
QUIESCE
FLUSH
DETACH namespace relationship
DETACH filesystem provider
DESTROY provider instance
```

User-facing tools MAY use the familiar words mount and unmount, but those terms are aliases for the
explicit Arcology attachment lifecycle.

Namespace attachment MUST be represented as inspectable state. Arcology tools MUST be able to
answer:

- which filesystem provides this namespace path;
- which storage object backs it;
- whether it is writable;
- what volume UUID it has;
- what implementation is serving it;
- its health state;
- its active generation/checkpoint.

------------------------------------------------------------------------

# 11. On-Disk Format Overview

The ArcFS reference on-disk format SHALL be self-describing and versioned.

A volume consists conceptually of:

```text
+---------------------------+
| Reserved / Bootstrap Area |
+---------------------------+
| Superblock Ring            |
+---------------------------+
| Checkpoint Records         |
+---------------------------+
| Copy-on-write Metadata     |
| Trees                      |
+---------------------------+
| Data Extents                |
+---------------------------+
| Free / Unallocated Space    |
+---------------------------+
```

Metadata and data extents are physically intermingled according to allocator policy; the diagram
is conceptual rather than a fixed partitioning requirement.

------------------------------------------------------------------------

# 12. Canonical Encoding

The base ArcFS format SHALL use:

- little-endian integer encoding;
- fixed-width integer types for persistent structures;
- explicit structure version fields;
- explicit byte lengths for variable records;
- CRC or cryptographic-strength checksum fields as defined per structure;
- no compiler-native packed struct serialization;
- no persistent raw pointers;
- no host ABI-dependent enum sizes;
- no dependence on C/C++ object layout.

All persistent structures MUST be decoded using explicit bounds checking.

Unknown required feature bits MUST prevent writable activation. Unknown incompatible feature bits
MUST prevent activation entirely. Unknown compatible feature bits MAY be ignored as defined by the
feature-bit class.

------------------------------------------------------------------------

# 13. Logical Block Size

The initial ArcFS reference format SHALL use a 4096-byte logical filesystem block.

The on-disk superblock SHALL declare the logical block size. Readers MUST reject unsupported
values rather than misinterpreting the volume.

The filesystem block size MUST be compatible with the backing block-storage object's minimum I/O
and alignment constraints.

ArcFS implementations SHOULD aggregate I/O into larger extents where beneficial.

------------------------------------------------------------------------

# 14. Superblock Ring

ArcFS SHALL maintain multiple redundant superblock copies at predetermined discoverable locations.

A superblock SHALL include at minimum:

- ArcFS magic/signature;
- format major version;
- format minor version;
- volume UUID;
- volume creation time;
- logical block size;
- total logical block count;
- feature flags;
- checksum algorithm identifiers;
- checkpoint region description;
- last known committed generation hint;
- volume label reference or bounded label field;
- superblock checksum.

No single superblock copy SHALL be the sole authority for committed filesystem state.

At activation, the implementation MUST validate available superblock copies and choose a
compatible quorum/most-recent valid format view according to the recovery algorithm.

If copies disagree in a way that cannot be safely resolved, writable activation MUST fail.

------------------------------------------------------------------------

# 15. Checkpoints and Commit Protocol

## 15.1 Checkpoint contents

A checkpoint identifies a complete committed generation and SHALL include root references for
required metadata trees.

At minimum:

```text
Checkpoint
  Generation
  PreviousGeneration
  Timestamp
  ObjectTreeRoot
  NamespaceTreeRoot
  ExtentTreeRoot
  AllocationTreeRoot
  AttributeTreeRoot
  AuthorityTreeRoot
  SnapshotTreeRoot
  CheckpointChecksum
```

Optional roots MAY be introduced through compatible feature mechanisms.

## 15.2 Commit ordering

A transaction commit SHALL obey the following durability sequence:

1. Allocate new extents required by the transaction.
2. Write new or modified file-data extents.
3. Write checksums and new copy-on-write metadata blocks.
4. Ensure all transaction-dependent blocks have reached the durability boundary required by the
   backing storage contract.
5. Write the new checkpoint record.
6. Flush/order the checkpoint write according to the block-storage contract.
7. Publish the new generation as active in runtime state.
8. Schedule unreachable old extents for safe reclamation.

An implementation MUST NOT expose the new generation as durable before the storage device contract
confirms the required write ordering/flush semantics.

## 15.3 Failed commit

If failure occurs before the new checkpoint is durably committed, recovery SHALL select the prior
valid checkpoint.

Blocks written for an uncommitted transaction are unreachable garbage and SHALL be reclaimable by
recovery or later space accounting.

## 15.4 Transaction scope

ArcFS MUST support transactions containing mutations to multiple files/directories on the same
volume.

The base specification does not require atomic transactions across separate volumes.

------------------------------------------------------------------------

# 16. Metadata Trees

The reference ArcFS implementation SHALL use checksummed copy-on-write balanced trees suitable for
large volumes. B+tree-like behavior is RECOMMENDED. The precise node split heuristics are
implementation-defined as long as the persistent format and ordering rules are obeyed.

## 16.1 Object Tree

Maps OID to core persistent object records.

Object records include at minimum:

- OID;
- object type;
- creation generation;
- modification generation;
- logical size where applicable;
- timestamps;
- flags;
- authority descriptor reference;
- attribute-set reference;
- stream/extent mapping reference;
- namespace reference count where required;
- object checksum or containing-node checksum coverage.

## 16.2 Namespace Tree

Maps:

```text
(parent OID, normalized name) -> target OID + relationship type + flags
```

Renaming a file primarily mutates Namespace Tree records. The target OID remains stable.

## 16.3 Extent Tree

Maps object logical ranges to physical volume extents and associated integrity metadata.

## 16.4 Allocation Tree

Tracks free and allocated physical extents, allocation class, and generation constraints required
for safe reclamation.

## 16.5 Attribute Tree

Stores typed extended object metadata that does not belong in the fixed core object record.

## 16.6 Authority Tree

Stores persistent security/authority descriptors and references used by object authorization
policy.

## 16.7 Snapshot Tree

Stores named snapshot records, generation references, retention metadata, and snapshot flags.

------------------------------------------------------------------------

# 17. Object IDs

Each persistent ArcFS object SHALL receive a 128-bit OID at creation.

OID allocation MUST make accidental reuse practically impossible. Reclaimed OIDs SHOULD NOT be
reused during the lifetime of a volume under normal operation.

An OID SHALL remain stable across:

- rename;
- move within the same volume;
- namespace attachment changes;
- open/close cycles;
- reboot;
- snapshot creation;
- filesystem provider restart.

Copying a file creates a new OID.

Reflinking a file creates a new OID whose data extents may initially be shared.

A snapshot preserves the OIDs existing in the captured generation.

------------------------------------------------------------------------

# 18. File Data and Extents

ArcFS SHALL store regular-file content as logical byte ranges mapped to one or more extents.

An extent mapping SHALL describe at minimum:

- owning OID;
- logical byte/block range;
- physical block range;
- length;
- flags;
- data checksum reference/information;
- compression/encryption transform identifiers if enabled by future/optional features.

ArcFS MUST support:

- files larger than 4 GiB;
- 64-bit logical file sizes;
- sparse logical ranges;
- partial final blocks;
- fragmentation across extents;
- copy-on-write replacement of modified shared extents.

Implementations SHOULD prefer large contiguous extents where possible without creating
pathological latency.

------------------------------------------------------------------------

# 19. Sparse Files

A logical range with no physical extent SHALL read as zero bytes.

Writing into a sparse range SHALL allocate only the required extents, subject to implementation
allocation policy.

Hole punching MAY be exposed as an advanced `ByteStream` operation and, if implemented, MUST occur
transactionally.

------------------------------------------------------------------------

# 20. Checksums and Integrity

## 20.1 Metadata

All ArcFS persistent metadata blocks MUST be checksummed.

A metadata checksum MUST cover enough identity/context to prevent a valid block silently appearing
correct in an impossible location or role. Tree nodes SHOULD include generation and logical
identity fields in checksum coverage.

## 20.2 File data

Normal ArcFS file-data extents MUST have integrity checksums.

The initial reference implementation SHOULD use a modern high-performance checksum with strong
accidental-corruption detection. The algorithm is format-declared and versioned; callers MUST NOT
infer it from implementation build configuration.

## 20.3 Verification behavior

Checksum mismatch MUST surface as an integrity fault. The filesystem MUST NOT return corrupted
bytes as though the read succeeded.

If redundant lower storage or a future ArcFS redundancy feature can recover a known-good copy,
recovery MAY be attempted, but the event MUST remain observable through health reporting.

## 20.4 Scrubbing

ArcFS SHOULD support online read-only integrity scrubbing that walks allocated metadata and data
extents, validates checksums, and reports defects without requiring a destructive repair pass.

------------------------------------------------------------------------

# 21. Snapshots

Snapshots are a base ArcFS capability, not an external block-device trick.

Creating a snapshot SHALL create a persistent reference to a committed generation/root set without
copying all referenced file data.

A snapshot record SHALL include:

- snapshot identifier;
- human label;
- source generation;
- creation time;
- creator/authority metadata;
- retention/pinning flags;
- optional descriptive attributes.

Initial ArcFS snapshots SHALL be read-only.

Deleting a snapshot removes its root reference transactionally. Extents that are no longer
reachable from the active generation or any remaining snapshot become reclaimable.

Writable clones MAY be introduced as a later compatible extension.

------------------------------------------------------------------------

# 22. Reflinks and Clones

ArcFS SHALL support efficient file cloning within a volume.

A reflink operation:

- creates a new OID;
- creates new namespace metadata if requested;
- initially maps the new object's logical data to existing immutable extents;
- records sharing required for safe reclamation;
- copy-on-writes modified ranges independently thereafter.

A reflink MUST NOT cause later writes to one file to modify the visible data of the other.

------------------------------------------------------------------------

# 23. Deletion and Lifetime

Deleting a pathname removes a namespace relationship. It does not necessarily destroy the
underlying object immediately.

An object may remain live because it is:

- referenced by another namespace record;
- held by an open runtime handle;
- reachable from a snapshot;
- referenced by another persistent ArcFS structure.

Physical extents become reclaimable only when no committed reachable state requires them.

ArcFS SHOULD avoid exposing POSIX inode/link-count terminology in user-facing interfaces, though
internal reference accounting may be equivalent.

A desktop Trash/Recycle Bin is a higher-level namespace/service policy. It is not the primitive
deletion mechanism of the filesystem.

------------------------------------------------------------------------

# 24. Typed Attributes

ArcFS SHALL support typed extended attributes.

An attribute record SHALL identify:

- attribute namespace;
- attribute name or stable attribute ID;
- value type;
- value length;
- value or extent reference;
- flags controlling inheritance, indexing eligibility, visibility, and security semantics where
  applicable.

Base value types SHOULD include:

- UTF-8 string;
- signed integer;
- unsigned integer;
- boolean;
- timestamp;
- binary blob;
- UUID/OID;
- typed locator.

System-reserved attribute namespaces MUST be protected from unprivileged mutation.

Applications SHOULD use registered or application-scoped namespaces rather than inventing
unqualified global names.

Examples of future attributes include:

```text
content.mime
content.title
content.author
arcology.tags
arcology.origin
app.arconote.document-version
```

ArcFS attributes are metadata, not a substitute for an unrestricted hidden stream mechanism.

------------------------------------------------------------------------

# 25. Time Semantics

ArcFS object records SHALL support at least:

- creation time;
- content modification time;
- metadata modification time.

Access time SHALL NOT be required as a synchronously updated field for every read because doing so
would create unnecessary write amplification.

Implementations MAY support lazy/coarse access-time tracking as an optional feature.

Persistent timestamps SHALL use a format with explicit epoch and resolution. The reference format
SHALL store UTC-derived monotonic wall-clock representations without embedding local timezone
rules.

------------------------------------------------------------------------

# 26. Authority and Security Model

## 26.1 Filesystem authority is not only a mode bit

ArcFS SHALL integrate with Arcology's object/capability authority system.

Opening by pathname requires authority to traverse the relevant namespace and acquire the
requested capability over the target object.

Once a valid runtime handle is granted, subsequent operations SHALL be authorized against the
handle's capability set and current revocation/lifecycle rules rather than blindly re-resolving
the original path.

## 26.2 Capability examples

Filesystem object capabilities may include:

```text
ObserveMetadata
ReadData
WriteData
AppendData
EnumerateChildren
CreateChild
Rename
Relink
DeleteRelationship
ModifyMetadata
ModifyAuthority
Snapshot
AdministerVolume
RepairVolume
```

Exact names are subject to the broader Arcology capability lexicon, but the distinction between
data, namespace, metadata, and administrative authority is normative.

## 26.3 File picker authority

A graphical file picker MAY grant an application a handle to a specific selected object without
granting arbitrary namespace enumeration or parent-directory access.

This is a desired native security behavior.

## 26.4 TOCTOU resistance

Security-sensitive code SHOULD resolve a path once, receive a stable authorized object handle, and
operate on that handle.

APIs SHOULD avoid patterns equivalent to:

```text
check(path)
...time passes...
open(path)
```

when a stable object resolution can perform authorization and handle acquisition atomically.

## 26.5 Object Links

Stable OID-based Object Links MUST still be subject to authority checks when resolved. Stable
identity is not a privilege bypass.

------------------------------------------------------------------------

# 27. Encryption

Transparent at-rest encryption is intentionally separable from the core ArcFS v1 requirement.

The format SHALL reserve feature negotiation and transform metadata sufficient to add file-,
subtree-, or volume-level encryption without redefining core object identity.

A future encryption design MUST specify:

- key ownership;
- key lifecycle;
- recovery semantics;
- metadata visibility;
- snapshot behavior;
- reflink behavior;
- checksum ordering relative to encryption;
- secure deletion limitations on copy-on-write media.

ArcFS implementations MUST NOT invent incompatible private encryption formats and still claim
base-format conformance.

------------------------------------------------------------------------

# 28. Compression

Transparent compression MAY be added as a compatible optional feature.

If enabled, compression MUST occur at independently recoverable extent or chunk boundaries.
Compression metadata MUST be checksummed, length-bounded, and validated before allocation or
decompression.

Failure to decompress MUST be reported as an integrity/data fault rather than returning partial
unverified data.

------------------------------------------------------------------------

# 29. Free-Space Management

ArcFS SHALL track free and allocated extents transactionally.

Allocation metadata MUST make it possible to determine, from a committed checkpoint, which
physical blocks are reachable and which are free/reclaimable.

No committed generation may describe the same physical block as simultaneously allocated to
incompatible owners.

The allocator SHOULD distinguish allocation classes such as:

- metadata;
- file data;
- checkpoint/reserved;
- temporary transaction reservation;
- future transform-specific storage.

The allocator SHOULD attempt locality for related data while avoiding long global allocation
locks.

------------------------------------------------------------------------

# 30. Space Accounting

ArcFS MUST expose at least:

- physical capacity;
- currently allocated physical bytes;
- immediately free physical bytes;
- snapshot-retained bytes where calculable;
- reclaimable/deferred bytes where calculable;
- metadata usage;
- user-visible logical data size.

Because reflinks and snapshots share extents, ArcFS MUST NOT pretend that summing logical file
sizes equals physical usage.

Storage UI SHOULD explain shared/snapshot-retained space rather than presenting apparently
contradictory numbers without context.

------------------------------------------------------------------------

# 31. Quotas

Quotas are an optional initial implementation feature but the architecture SHALL reserve a
quota/accounting tree or compatible extension mechanism.

Future quota policy may target:

- principals;
- applications;
- namespaces;
- projects/workspaces;
- snapshot classes.

Quotas MUST be implemented in terms of explicit policy subjects rather than assuming POSIX UID/GID
semantics.

------------------------------------------------------------------------

# 32. Concurrency

ArcFS implementations SHALL permit concurrent reads.

Independent mutations SHOULD proceed concurrently when they do not require conflicting tree/object
locks.

Transaction commit serialization MAY exist at the checkpoint publication boundary, but
implementations SHOULD minimize the duration of the global commit critical section.

Lock ordering MUST be documented by the reference implementation.

A filesystem operation MUST NOT hold substrate-global locks while waiting indefinitely on physical
storage I/O.

------------------------------------------------------------------------

# 33. Caching

ArcFS MAY use:

- metadata cache;
- directory lookup cache;
- file-data page cache;
- extent map cache;
- checksum cache;
- negative name cache.

Caches are non-authoritative accelerators.

Cache invalidation MUST follow committed generation and object mutation semantics. A stale cache
entry MUST NOT cause a caller to receive authority it no longer possesses or data from an invalid
object generation.

Dirty data MUST be tied to a known transaction/writeback state and observable during
quiesce/flush.

------------------------------------------------------------------------

# 34. Flush and Durability Semantics

ArcFS MUST consume explicit block-storage ordering semantics.

The underlying `BlockStorage` contract MUST distinguish, directly or through capabilities:

- completion to volatile device cache;
- durable completion;
- ordered writes/barriers;
- explicit flush;
- force-unit-access or equivalent where available.

If the backing implementation cannot provide the durability guarantees ArcFS requires, ArcFS MUST
report degraded durability and MUST NOT falsely claim strong crash consistency.

A `File.Flush` operation SHOULD define whether the caller requests:

- data visibility to the filesystem;
- transaction commit;
- durable media persistence.

The API MUST NOT overload one ambiguous flush meaning for all three without documented semantics.

------------------------------------------------------------------------

# 35. Bad Blocks and Media Faults

ArcFS SHALL report block I/O faults through structured health events.

The base filesystem does not implement physical remapping that belongs to the storage device.
However, ArcFS MAY mark logical extents unusable when the backing storage cannot remap them and
the administrator explicitly authorizes such degradation.

A metadata read fault that prevents structural verification MUST cause the affected structure to
be treated as unavailable/corrupt, not partially trusted.

------------------------------------------------------------------------

# 36. Health Model

An active ArcFS volume SHALL expose a `HealthReporter`-compatible interface.

Recommended states include:

```text
Healthy
Degraded
ReadOnlySafety
NeedsScrub
NeedsOfflineCheck
Corrupt
Unavailable
```

Health detail SHOULD include:

- current generation;
- active checkpoint;
- last clean quiesce status;
- checksum error count;
- I/O error count;
- uncommitted-space recovery count;
- scrub history;
- repair history;
- storage-device health summary if authorized.

ArcFS MUST distinguish filesystem-structure faults from lower-device faults when the underlying
information is available.

------------------------------------------------------------------------

# 37. Activation and Recovery

When an ArcFS volume is activated, the implementation SHALL:

1. validate block geometry and required storage capabilities;
2. locate and validate superblock copies;
3. negotiate format features;
4. locate candidate checkpoints;
5. validate candidate checkpoint checksum and root references;
6. select the newest fully valid committed generation;
7. reject structurally impossible root relationships;
8. recover or classify unreachable writes from incomplete transactions;
9. establish health state;
10. activate read-write only if safety requirements are satisfied.

ArcFS SHOULD NOT require a traditional journal replay to make committed metadata coherent, because
committed metadata is copy-on-write and checkpoint-rooted.

------------------------------------------------------------------------

# 38. Read-Only Safety Mode

If ArcFS can establish a trustworthy committed checkpoint but cannot safely continue mutation, it
SHOULD activate in `ReadOnlySafety` mode where possible.

Examples include:

- allocation-tree inconsistency not affecting known readable extents;
- missing durability features after device/driver replacement;
- detected metadata fault outside currently required read path;
- version compatibility permitting read but not write.

The user interface MUST clearly state why writes are disabled and what recovery action is
recommended.

------------------------------------------------------------------------

# 39. Offline Check and Repair

ArcFS SHALL have an official offline checker/repair implementation suitable for the Arcology
recovery and installation environment.

The checker SHALL support at least:

- superblock validation;
- checkpoint validation;
- tree structural validation;
- OID reference validation;
- namespace reachability analysis;
- extent overlap detection;
- allocation-tree reconciliation;
- checksum verification;
- orphan/unreachable object detection;
- snapshot reachability validation;
- authority-record structural validation.

Repair SHOULD be divided into:

- **Inspect** — no mutation.
- **Plan** — produce proposed repairs.
- **Apply** — perform specifically authorized repairs.
- **Verify** — validate the resulting committed state.

A repair operation MUST NOT silently discard reachable user data merely to make metadata
internally tidy.

When data cannot be reattached to its original namespace, recovery SHOULD preserve it in a clearly
identified recovery namespace/object collection with provenance metadata.

------------------------------------------------------------------------

# 40. Formatting

Formatting a new ArcFS volume SHALL create:

- volume UUID;
- redundant superblocks;
- initial checkpoint storage;
- root directory OID;
- empty required metadata trees;
- initial allocation state;
- initial authority descriptors;
- generation 1 committed checkpoint.

Formatting MUST verify that the target block-storage object is not currently attached to an active
filesystem unless an explicitly privileged destructive workflow has first detached/quiesced it.

The formatter SHOULD support a dry-run layout report.

------------------------------------------------------------------------

# 41. Volume Labels

Volume labels are human metadata, not identity.

Multiple volumes MAY share the same label.

Arcology SHALL use Volume UUID for stable volume identity.

Changing a volume label MUST NOT change Volume UUID or filesystem object OIDs.

------------------------------------------------------------------------

# 42. Native API Shape

The exact ABI is defined elsewhere, but ArcFS requires interfaces conceptually equivalent to the
following.

```text
INTERFACE FileSystem
  Activate(storageHandle, options) -> FileSystemHandle
  Quiesce()
  Flush(durability)
  GetRoot() -> NamespaceHandle
  Snapshot(options) -> SnapshotHandle
  GetHealth() -> HealthState
  Inspect() -> ObjectInfo

INTERFACE Namespace
  Resolve(name, requestedCapabilities) -> ObjectHandle
  Enumerate(filter) -> Iterator
  CreateFile(name, options) -> ObjectHandle
  CreateDirectory(name, options) -> ObjectHandle
  Relink(sourceHandle, newName, options)
  Remove(name, options)

INTERFACE ByteStream
  Read(offset, buffer) -> bytesRead
  Write(offset, buffer, options) -> bytesWritten
  Resize(length)
  Flush(durability)
  GetSize() -> U64
```

Native APIs SHOULD accept handles and namespace components rather than repeatedly accepting giant
flattened path strings.

------------------------------------------------------------------------

# 43. ArcoBASIC Developer Experience

ArcFS should be pleasant to use from ArcoBASIC without exposing raw on-disk machinery for ordinary
tasks.

Illustrative syntax:

```basic
LET docs = SYSTEM.GET(":home:documents")
LET file = docs.OPEN("notes.txt", READ)
PRINT file.READTEXT()
file.CLOSE()
```

Or directly:

```basic
LET file = FILE.OPEN(":home:documents:notes.txt", READ)
PRINT file.READTEXT()
```

Object inspection:

```basic
LET file = FILE.OPEN(":software:utility:arconote.aex", READ)
PRINT file.OBJECTID
PRINT file.SIZE
PRINT file.VOLUME.UUID
PRINT file.ATTRIBUTES
```

Snapshot workflow:

```basic
LET volume = STORAGE.VOLUME(":system")
LET snap = volume.SNAPSHOT("before-update")
PRINT snap.GENERATION
```

Health inspection:

```basic
LET volume = STORAGE.VOLUME(":system")
PRINT volume.HEALTH.STATE
PRINT volume.HEALTH.LAST_SCRUB
```

Capability-aware selection:

```basic
LET chosen = UI.FILEPICKER.OPEN(READ)
PRINT chosen.READTEXT()
```

The last example SHOULD grant authority to the chosen object without requiring the application to
gain unrestricted access to the entire containing directory.

------------------------------------------------------------------------

# 44. Inspector Experience

Arcology's system inspector SHOULD present an ArcFS volume approximately as:

```text
ArcFS Volume: System

Identity
  UUID              8A32-...-1D09
  Format            ArcFS 1.x
  Provider          Arcology.ReferenceArcFS

Storage
  Backing Object    NVMe0n1 / GPT-3
  Capacity          1.00 TB
  Physical Used     412 GB
  Snapshot Retained 38 GB
  Free              550 GB

State
  Health            Healthy
  Mode              Read / Write
  Generation        184392
  Active Checkpoint 184392
  Last Scrub        2026-08-18 03:15

Namespace Attachments
  :system

Features
  Checksums          Metadata + Data
  Snapshots          Yes
  Reflinks           Yes
  Compression        Off
  Encryption         Off
```

Raw tree structures MAY be available in developer/diagnostic mode, but normal users should receive
conclusions and relationships rather than hexadecimal archaeology.

------------------------------------------------------------------------

# 45. User Experience Requirements

Arcology storage UI SHALL prioritize identity and consequences.

For destructive workflows, the UI SHOULD display:

- friendly volume label;
- volume UUID suffix;
- physical device model;
- capacity;
- current namespace attachment;
- filesystem type;
- data-used estimate;
- a visual device relationship.

The system MUST avoid relying on ambiguous labels such as only Disk 0, Disk 1, or drive letters
when richer identity is available.

Formatting, repair, restore, and clone operations SHOULD make source and destination visually
unmistakable.

------------------------------------------------------------------------

# 46. Replaceable Filesystem Implementations

ArcFS is both an on-disk format and a reference implementation target.

The Arcology system SHALL consume filesystem providers through a published `FileSystem` contract.

Possible coexistence:

```text
SystemVolume
  storage -> ArcFS Reference Provider

RecoveryUSB
  storage -> FAT Provider

ArchiveDisc
  storage -> ISO9660 Provider

ExperimentalVolume
  storage -> NightshadeFS Provider
```

A third-party implementation claiming ArcFS compatibility MUST pass ArcFS on-disk conformance
tests in addition to the generic `FileSystem` interface tests.

Live replacement of the implementation serving an active ArcFS volume MAY be supported only
through a strict sequence:

```text
QUIESCE
COMMIT/ABORT outstanding transactions
FLUSH durable state
FREEZE runtime namespace mutation
DETACH implementation A
ATTACH implementation B
VALIDATE volume + checkpoint
VERIFY runtime object rebind rules
RESUME
```

The reference implementation MUST NOT assume it is globally unique.

------------------------------------------------------------------------

# 47. Runtime Handle Interaction

ArcFS persistent OIDs and Arcology runtime handles are distinct.

A runtime handle SHOULD contain or resolve to:

- provider instance identity;
- volume UUID;
- persistent OID;
- runtime generation/liveness information;
- granted capabilities;
- provider-private cached state.

Destroying/closing a runtime handle does not delete the persistent object.

Deleting a persistent namespace relationship does not immediately invalidate an already-authorized
open handle unless policy explicitly requires revocation.

Stale runtime handles MUST be rejected using the Arcology runtime handle generation/liveness
mechanism.

------------------------------------------------------------------------

# 48. Memory and VM Interaction

ArcFS must cooperate cleanly with Arcology's PRD/VRD-managed memory model.

Filesystem I/O buffers SHALL have explicit ownership and lifetime.

A storage driver MUST NOT retain arbitrary virtual addresses beyond the buffer-mapping contract.

Direct I/O or DMA paths MUST use mappings and pinning compatible with APS authority and VRD
policy.

The filesystem page/data cache MUST NOT bypass PRD physical ownership accounting.

Memory pressure handling SHOULD allow clean cached data to be discarded without filesystem
mutation and dirty data to be flushed or throttled according to defined policy.

------------------------------------------------------------------------

# 49. Storage Hot Removal

If the backing storage object reports removal or imminent removal, ArcFS SHALL transition
according to current state.

For surprise removal:

- new I/O MUST fail predictably;
- outstanding operations MUST complete with structured errors;
- the filesystem object SHALL transition to an unavailable/faulted lifecycle state;
- namespace attachments SHOULD become visibly unavailable rather than silently redirecting;
- cached dirty data MUST NOT be represented as durable.

For graceful removable-media eject:

```text
QUIESCE
COMMIT
FLUSH
DETACH NAMESPACE
DETACH FILESYSTEM
EJECT STORAGE
```

------------------------------------------------------------------------

# 50. Boot and System Volume Considerations

Arcology firmware boot constraints are separate from the ArcFS runtime design.

A UEFI system MAY continue to use an EFI System Partition with FAT as required by firmware
interoperability. ArcFS need not duplicate firmware-native FAT compatibility.

A typical disk may therefore contain:

```text
GPT
  EFI System Partition (FAT)
  Arcology System Volume (ArcFS)
  Optional Recovery / Other Volumes
```

After Arcology's boot environment has sufficient storage and ArcFS support, the main system image
and persistent operating-system state MAY reside on ArcFS.

The boot path MUST define exactly which ArcFS feature set is supported before the full runtime is
available. Early boot MUST fail safely on unsupported incompatible feature bits.

------------------------------------------------------------------------

# 51. System Update Integration

ArcFS snapshots SHOULD support atomic system-update workflows.

A future update service may perform:

```text
CREATE snapshot "pre-update"
BEGIN transaction/update staging
WRITE new system objects
VERIFY new content
COMMIT
MARK boot generation/update state
```

Rollback policy belongs to the system-update architecture, but ArcFS MUST expose primitives
sufficient to preserve pre-update state cheaply and inspectably.

------------------------------------------------------------------------

# 52. No Device Files

Arcology SHALL NOT require paths such as:

```text
/dev/nvme0
/dev/null
/dev/audio0
```

as native ArcFS entries.

Equivalent system services are Arcology objects exposed through the system object namespace and
capability interfaces.

A Unix compatibility environment MAY synthesize a private `/dev` view for software expecting it.
That view is not ArcFS architecture.

This separation is normative because it prevents the filesystem from becoming the universal hidden
control protocol for unrelated system subsystems.

------------------------------------------------------------------------

# 53. Errors

ArcFS APIs SHALL return structured errors rather than relying exclusively on integer errno-like
values.

Error objects SHOULD identify:

- subsystem;
- operation;
- volume UUID where safe;
- OID where safe;
- path component where relevant;
- error class;
- lower-layer cause;
- retryability;
- integrity/security implications.

Representative error classes:

```text
NotFound
AlreadyExists
NotAuthorized
ReadOnly
NoSpace
QuotaExceeded
InvalidName
InvalidObject
StaleHandle
IntegrityFault
StorageFault
UnsupportedFeature
TransactionConflict
Busy
Removed
NeedsRepair
```

Applications SHOULD be able to display useful errors without parsing English strings.

------------------------------------------------------------------------

# 54. Failure Atomicity Requirements

The following operations MUST be atomic with respect to a committed generation:

- create object + namespace entry;
- remove namespace entry;
- rename within one volume;
- move within one volume;
- metadata update;
- file resize metadata;
- extent-map replacement for a committed write transaction;
- snapshot creation/deletion;
- authority descriptor update.

A multi-operation explicit transaction SHALL be all-or-nothing if all operations target the same
ArcFS volume and no documented external side effect is included.

------------------------------------------------------------------------

# 55. Rename Semantics

Same-volume rename/move MUST preserve OID.

A rename replacing an existing destination MUST define explicit replacement policy. The default
native operation SHOULD fail if the destination exists unless replacement was requested.

Cross-volume move is not a rename. It is conceptually:

```text
COPY/CLONE-IF-PROVIDER-SUPPORTS
VERIFY
CREATE destination relationship
REMOVE source relationship
```

and is not globally atomic in the base specification.

User interfaces MUST NOT claim cross-volume moves have same-volume atomic semantics.

------------------------------------------------------------------------

# 56. Metadata Indexing

ArcFS MAY provide optional indexes over selected typed attributes.

Global desktop search architecture is outside this RFC. However, ArcFS SHOULD expose
mutation/event streams sufficient for an indexing service to track committed object changes
without repeatedly crawling the entire volume.

An index maintained outside the authoritative ArcFS trees MUST be reconstructible and MUST NOT be
required to recover user data.

------------------------------------------------------------------------

# 57. Change Events

ArcFS SHOULD publish structured post-commit events such as:

```text
ObjectCreated
ObjectModified
ObjectRelinked
NamespaceEntryAdded
NamespaceEntryRemoved
AuthorityChanged
SnapshotCreated
SnapshotDeleted
HealthChanged
```

Events MUST correspond to committed state. A listener MUST NOT receive a durable-change
notification for a transaction that later aborts.

High-volume mutation SHOULD permit event coalescing while preserving enough information for
consumers to detect that a rescan is necessary.

------------------------------------------------------------------------

# 58. Privacy Considerations

ArcFS itself SHOULD minimize hidden behavioral telemetry.

Persistent metadata may reveal:

- names;
- timestamps;
- file sizes;
- attributes;
- snapshot history;
- authority relationships;
- content checksums depending on checksum placement.

Inspection APIs MUST obey authority. An application that can read one granted file must not
automatically gain the ability to enumerate unrelated names, snapshot history, or volume-global
metadata.

Future encryption work must separately analyze metadata confidentiality.

------------------------------------------------------------------------

# 59. Accessibility Considerations

Storage health, source/destination selection, warnings, and repair state MUST NOT be conveyed by
color alone.

Arcology storage tooling SHOULD expose:

- text labels;
- icons/shapes;
- device identity summaries;
- screen-reader-accessible relationships;
- keyboard operation;
- progress and current phase text;
- non-technical explanation with expandable technical detail.

Destructive operations SHOULD avoid visually similar adjacent choices that can be confused under
stress or limited vision.

------------------------------------------------------------------------

# 60. Performance Expectations

The reference ArcFS implementation SHOULD target:

- O(log n) namespace lookup within large directories;
- O(log n) object and extent lookup;
- sequential I/O near the capability of the backing block interface when checksumming is not
  CPU-bound;
- bounded transaction commit latency under ordinary metadata workloads;
- efficient batched metadata operations;
- low-cost snapshots independent of total live data size;
- reflink creation independent of copied file byte size except metadata complexity.

Performance optimizations MUST NOT violate durability or integrity requirements.

------------------------------------------------------------------------

# 61. Scalability Targets

The initial persistent format SHOULD support at least:

- 64-bit logical block addressing;
- 64-bit file sizes;
- billions of objects in format capacity, even if early implementations are tested at smaller
  scales;
- multi-terabyte volumes without format revision;
- directories containing millions of entries without linear lookup requirements.

Implementation resource limits MUST be discoverable and MUST fail cleanly before integer overflow
or structural corruption.

------------------------------------------------------------------------

# 62. Format Feature Negotiation

ArcFS SHALL define feature flags in three categories.

## 62.1 Compatible

A reader may safely ignore the feature and still interpret the filesystem correctly.

## 62.2 Read-only compatible

A reader may activate the filesystem for reading but MUST NOT write without understanding the
feature.

## 62.3 Incompatible

A reader MUST NOT activate the filesystem if it does not understand the feature.

Feature flags MUST be persisted in redundant format metadata and included in activation
diagnostics.

------------------------------------------------------------------------

# 63. Versioning

A major format version change indicates that older implementations cannot safely interpret the
volume without explicit migration support.

A minor format version MAY add compatible behavior within feature-negotiation rules.

ArcFS migration tooling MUST prefer transactionally constructing new compatible metadata before
discarding the last old-format recoverable state.

A format upgrade MUST NOT occur merely because a volume was opened by a newer OS unless policy
explicitly authorizes the upgrade.

------------------------------------------------------------------------

# 64. Interoperability with Foreign Filesystems

Arcology MAY support FAT, exFAT, NTFS, ext4, ISO9660, UDF, network filesystems, and other formats
through independent providers.

Foreign filesystem objects SHOULD be projected into the same generic `FileSystem`, `Namespace`,
and `ByteStream` interfaces where semantics permit.

Capabilities absent from a foreign format — such as ArcFS OIDs, snapshots, typed authority, or
reflinks — MUST be reported as unsupported or emulated only when the semantic difference is safe
and visible.

Arcology MUST NOT weaken ArcFS semantics merely to make every filesystem look identical.

------------------------------------------------------------------------

# 65. Backup and Imaging Semantics

ArcFS SHOULD expose snapshot and change-generation primitives useful to backup tools.

A backup application SHOULD be able to request a stable read-only snapshot and copy from that
generation without freezing ordinary filesystem use for the entire backup duration.

Future incremental backup interfaces MAY enumerate objects/extents changed since a specified
generation.

Raw block imaging remains valid, but filesystem-aware backup SHOULD prefer semantic objects and
verified snapshots where appropriate.

------------------------------------------------------------------------

# 66. Forensics and Provenance

ArcFS repair and administrative operations SHOULD leave structured provenance records sufficient
to answer:

- when a repair occurred;
- which tool/provider version performed it;
- which generation was inspected;
- which generation resulted;
- what classes of changes were applied.

This history MUST be bounded and administratively controllable so it does not become unbounded
hidden telemetry.

Forensic tooling MAY operate read-only against checkpoint generations and raw structures without
activating the volume read-write.

------------------------------------------------------------------------

# 67. Transaction API

Arcology SHOULD expose explicit same-volume filesystem transactions for applications that need
compound atomic changes.

Illustrative ArcoBASIC:

```basic
LET volume = STORAGE.VOLUME(":project")
LET tx = volume.BEGINTRANSACTION()

LET manifest = tx.OPEN("manifest.json", WRITE)
manifest.WRITETEXT(newManifest)

tx.RENAME("build.tmp", "build.current")
tx.DELETE("obsolete.cache")

tx.COMMIT(DURABLE)
```

If `COMMIT` fails, the previously committed generation remains authoritative.

Long-running user transactions SHOULD have implementation-defined limits to avoid indefinitely
pinning allocator state.

------------------------------------------------------------------------

# 68. Open-Handle Semantics

An open object handle refers to an OID and granted authority, not to a mutable path string.

Therefore:

1. Open `:home:documents:a.txt`.
2. Another actor renames it to `b.txt`.
3. The existing handle continues to refer to the same object.

Namespace inspection through the handle MAY reveal current relationships if authorized.

If the last namespace relationship is removed while a handle is open, the object MAY remain
accessible through that handle until it closes, subject to authority/revocation policy.

------------------------------------------------------------------------

# 69. Directory Enumeration

Directory enumeration MUST have defined behavior under concurrent mutation.

The preferred native API SHALL enumerate against either:

- a stable committed generation/snapshot view; or
- an iterator with explicit mutation detection and continuation semantics.

Implementations MUST NOT silently duplicate or omit entries unpredictably while claiming stable
enumeration.

For large directories, enumeration MUST be incremental; callers MUST NOT be required to
materialize all entries in memory.

------------------------------------------------------------------------

# 70. Atomic File Replacement

ArcFS SHALL provide a native pattern for safe file replacement.

A caller SHOULD be able to:

1. create/write a replacement object;
2. verify/flush it;
3. atomically replace a namespace relationship in one transaction;
4. retain or release the former object according to references/snapshots.

Applications SHOULD NOT be forced to emulate safe replacement using fragile sequences of
delete/rename operations.

------------------------------------------------------------------------

# 71. Content Materialization

Arcology's `Materializable` concept MAY allow an object to be represented as persistent ArcFS
content.

Materialization contracts are defined outside this RFC, but ArcFS MUST be capable of storing
opaque application content without requiring the filesystem to understand application
serialization.

Typed metadata can identify content type while the primary stream remains application-defined.

------------------------------------------------------------------------

# 72. Formatter and Checker Determinism

Given identical format options and a deterministic UUID/time seed for testing, the reference
formatter SHOULD be capable of producing structurally deterministic layouts sufficient for
fixture-based conformance tests.

The checker MUST produce deterministic findings for the same immutable volume image.

Repair ordering SHOULD be deterministic when multiple safe repairs are equivalent.

------------------------------------------------------------------------

# 73. Fuzzing and Hostile Media

ArcFS parsers MUST treat on-disk content as untrusted input.

The implementation SHALL be fuzz-tested against malformed:

- superblocks;
- checkpoint records;
- tree nodes;
- variable-length attributes;
- extent ranges;
- names/UTF-8;
- feature flags;
- reference graphs;
- checksum records.

No on-disk field may directly control memory allocation or pointer arithmetic without validated
bounds.

Integer overflow checks are mandatory for:

- block-to-byte conversion;
- offset + length;
- extent end calculations;
- tree slot calculations;
- record length arithmetic;
- volume capacity calculations.

------------------------------------------------------------------------

# 74. Security Threat Model

ArcFS must defend against:

- maliciously crafted filesystem images;
- stale/dangling runtime handles;
- namespace race attacks;
- unauthorized traversal;
- unauthorized metadata disclosure;
- integer-overflow-driven memory corruption;
- checksum-confused block substitution;
- malicious feature flags;
- decompression bombs if compression is enabled;
- repair tools escalating ordinary corruption into data destruction.

The filesystem alone cannot defend against a malicious or compromised storage driver with
arbitrary DMA/memory authority. APS capability boundaries and IOMMU policy, where available, must
constrain lower layers.

------------------------------------------------------------------------

# 75. Testing Strategy

ArcFS requires layered tests.

## 75.1 Pure structure tests

- encode/decode every persistent record;
- checksum validation;
- endianness fixtures;
- feature-bit negotiation;
- corrupt length rejection;
- overflow rejection.

## 75.2 Tree tests

- insert/delete/split/merge;
- randomized operation sequences;
- lookup consistency;
- generation preservation;
- crash points during copy-on-write updates.

## 75.3 Namespace tests

- Unicode normalization;
- invalid colon/NUL names;
- large directories;
- rename/move;
- OID stability;
- Object Link behavior;
- concurrent enumeration.

## 75.4 Extent tests

- sparse files;
- fragmented files;
- large files;
- reflinks;
- partial writes;
- truncation;
- checksum faults;
- allocator overlap rejection.

## 75.5 Transaction tests

Inject failure after every durable write boundary and verify that recovery yields either:

- the complete old generation; or
- the complete new committed generation;

but never an impossible hybrid.

## 75.6 Snapshot tests

- snapshot create/delete;
- retained extents;
- active-volume mutation after snapshot;
- reclamation after final snapshot removal;
- OID preservation.

## 75.7 Security tests

- unauthorized traversal;
- granted single-file handle without parent enumeration;
- stale handle rejection;
- link resolution authorization;
- malicious disk images.

## 75.8 Hardware tests

- QEMU/virtual block device;
- NVMe;
- SATA/AHCI;
- USB mass storage;
- forced reset during write;
- surprise removal;
- devices with volatile write cache;
- devices with differing logical/physical sector sizes.

------------------------------------------------------------------------

# 76. Conformance Suites

Arcology SHALL eventually publish two separate suites.

## 76.1 Generic FileSystem Provider Conformance

Tests the Arcology `FileSystem` interface independent of disk format.

## 76.2 ArcFS Format Conformance

Tests that an implementation reads/writes ArcFS exactly according to persistent-format and
durability rules.

A third-party filesystem provider may pass the generic suite without implementing ArcFS.

A third-party ArcFS implementation must pass both.

------------------------------------------------------------------------

# 77. Reference Implementation Modules

The reference implementation SHOULD be decomposed approximately as:

```text
ArcFS.Provider
ArcFS.Format
ArcFS.Superblock
ArcFS.Checkpoint
ArcFS.Tree
ArcFS.ObjectStore
ArcFS.Namespace
ArcFS.Extents
ArcFS.Allocator
ArcFS.Attributes
ArcFS.Authority
ArcFS.Snapshots
ArcFS.Transactions
ArcFS.Cache
ArcFS.Health
ArcFS.Recovery
ArcFS.Formatter
ArcFS.Checker
ArcFS.ArcoBASICBinding
```

These are architectural boundaries, not mandatory source filenames.

The codebase SHOULD resist collapsing all filesystem state into one monolithic implementation
object.

------------------------------------------------------------------------

# 78. Implementation Phases

**Phase A — In-memory semantic model**

Implement:

- OIDs;
- directory namespace;
- files/streams;
- handle semantics;
- transactions;
- rename/delete behavior;
- capability checks;
- no persistent disk yet.

Purpose: validate the object and API contract before disk-format complexity.

**Phase B — Read-only ArcFS image**

Implement:

- superblock parser;
- checkpoint parser;
- object/namespace trees;
- extent reads;
- checksums;
- image inspection.

**Phase C — Formatter and transactional writer**

Implement:

- allocation tree;
- copy-on-write metadata;
- commit protocol;
- durable checkpoint publication;
- crash-injection tests.

**Phase D — Mutation completeness**

Implement:

- create/write/resize;
- rename/move;
- delete;
- sparse files;
- reflinks;
- typed attributes.

**Phase E — Snapshots and reclamation**

Implement:

- snapshot roots;
- generation pinning;
- shared extent accounting;
- safe reclamation.

**Phase F — Recovery and health**

Implement:

- scrub;
- offline checker;
- repair planning;
- ReadOnlySafety activation;
- inspector health interface.

**Phase G — System integration**

Implement:

- namespace attachment;
- system volume use;
- recovery environment support;
- update snapshots;
- graphical storage tooling;
- ArcoBASIC bindings.

------------------------------------------------------------------------

# 79. AI Implementation Guidance

This RFC defines architecture. Coding agents MUST NOT treat unspecified low-level details as
permission to invent incompatible persistent format casually.

Agents implementing ArcFS SHALL follow these rules.

## 79.1 Required boundaries

- Keep generic `FileSystem` interfaces separate from ArcFS-specific format code.
- Keep block-storage driver code outside ArcFS.
- Use explicit serialization/deserialization functions.
- Use checked integer arithmetic for persistent offsets and lengths.
- Keep persistent OID identity separate from runtime handle values.
- Keep namespace relationships separate from object records.
- Keep allocation accounting derived from committed transactions.
- Never mutate committed metadata blocks in place.

## 79.2 No silent architectural substitution

An agent MUST NOT replace the specified design with:

- FAT/ext2-like in-place metadata because it is simpler;
- a POSIX inode API as the only native API;
- `/dev` semantics;
- pathname-as-identity shortcuts;
- host filesystem passthrough presented as ArcFS;
- memory-only tests presented as durable transaction validation.

## 79.3 Persistent format changes

Any change to:

- on-disk record layout;
- checksum coverage;
- checkpoint rules;
- OID semantics;
- extent ownership rules;
- feature negotiation;

requires explicit format documentation and conformance fixtures.

## 79.4 Mandatory acceptance evidence

An implementation milestone is not complete merely because it compiles.

Agents SHALL provide:

- tests added;
- tests executed;
- exact pass/fail results;
- disk images/fixtures where relevant;
- corruption/failure injections performed;
- documented deviations;
- remaining unsafe assumptions.

## 79.5 Stop conditions

An agent MUST stop and report rather than improvise if:

- the `BlockStorage` durability contract is insufficiently defined for a correct commit
  implementation;
- persistent-format fields conflict with existing authoritative Arcology storage specifications;
- runtime authority semantics are ambiguous in a way that could grant excess access;
- a proposed optimization requires weakening crash consistency;
- physical I/O alignment cannot be satisfied safely;
- current PRD/VRD buffer ownership rules cannot safely support the intended DMA path.

------------------------------------------------------------------------

# 80. Required Pre-Implementation Dependencies

Before ArcFS becomes the writable system filesystem, Arcology requires authoritative contracts
for:

**BlockStorage interface**

- geometry;
- async/sync completion;
- alignment;
- flush;
- barriers/order;
- trim/discard;
- removable-media events;
- fault reporting.

**Runtime handle model**

- generation/liveness;
- capability storage;
- revocation;
- provider detach behavior.

**Authority/capability model**

- principals;
- capability derivation;
- delegation;
- persistence rules.

**Namespace/object model**

- attachment lifecycle;
- global/system namespace relationships;
- object inspection.

**PRD/VRD and DMA buffer ownership**

- pinning;
- mapped buffer lifetime;
- cache coherency expectations;
- IOMMU integration when present.

ArcFS work MAY proceed in host-side image tooling before every runtime dependency is complete, but
agents MUST not invent permanent substitute contracts and bury them inside the filesystem.

> **Status note (added at RFC-0038's revision, same date):** the `BlockStorage interface` listed
> above is specified by RFC-0038 (APS Block Storage and Filesystem Provider Substrate), which also
> defines the generic namespace-attachment mechanism (Section 10 of this RFC) at the ArcoBASIC/APS
> level and provides one working, non-ArcFS `FileSystem` provider (read-only FAT32 over a RAM
> disk) so the `FileSystem`/`Namespace`/`ByteStream` contracts in Section 42 are exercisable now,
> ahead of ArcFS's own Phase A/B work. The Runtime handle model, Authority/capability model, and
> PRD/VRD dependencies remain open — see RFC-0015, RFC-0017, RFC-0019, RFC-0020, and this
> project's still-undesigned capability system (Open Question, RFC-0036/RFC-0037).

------------------------------------------------------------------------

# 81. Acceptance Criteria for ArcFS v1

ArcFS v1 is architecturally complete when all of the following are demonstrated.

1. A volume can be formatted deterministically.
2. A volume can be activated and inspected.
3. Files/directories can be created, read, written, resized, renamed, moved, and deleted.
4. OIDs remain stable across rename/move/reboot.
5. Sparse files operate correctly.
6. Metadata and data checksum faults are detected.
7. Same-volume transactions are atomic.
8. Forced failure at each commit stage always recovers to a complete valid checkpoint.
9. Snapshots preserve prior data without full copies.
10. Reflinks share data safely and diverge under write.
11. Allocation accounting never permits overlapping live extents.
12. Namespace attachment participates in Arcology lifecycle.
13. Single-file capability grants work without global namespace authority.
14. Offline inspection can validate a volume without mutating it.
15. Repair can propose changes before applying them.
16. ReadOnlySafety mode works on recoverable-but-unsafe-for-write media.
17. Storage hot removal fails predictably.
18. Conformance fixtures can be read by an independent ArcFS implementation.
19. The recovery environment can identify volume/device relationships clearly.
20. No native ArcFS requirement depends on Unix device files, drive letters, or POSIX path
    identity.

------------------------------------------------------------------------

# 82. Explicitly Deferred Extensions

The following are desirable but deferred until their own designs are ready:

- transparent encryption;
- transparent compression;
- writable snapshots/clones;
- per-subtree quotas;
- attribute indexes;
- incremental send/receive replication;
- network/distributed ArcFS;
- native redundancy/erasure coding;
- content deduplication;
- remote object links;
- storage tiering;
- automatic cold-data migration;
- authenticated/Merkle-tree verification modes;
- boot-environment snapshot selection;
- per-object retention/immutability policy;
- secure-delete policy for CoW and flash media.

None of these extensions may violate the base rules that paths are not identity, committed
metadata is immutable, and format features are explicitly negotiated.

------------------------------------------------------------------------

# 83. Design Consequences

ArcFS intentionally makes several tradeoffs.

## 83.1 More metadata complexity in exchange for stronger recovery

Copy-on-write trees and checkpoints are more complex than modifying a directory block in place.
The benefit is an explicit durable generation boundary and dramatically clearer crash semantics.

## 83.2 More identity machinery in exchange for safer object semantics

Stable OIDs require persistent identity records. The benefit is that rename, handles, links,
inspection, and capability grants stop depending on mutable strings.

## 83.3 Checksums cost CPU and metadata

Arcology accepts this cost because silent corruption is worse than detectable corruption. Hardware
acceleration and efficient checksum algorithms may reduce the overhead.

## 83.4 Snapshots retain space

A snapshot can make apparently deleted data continue consuming physical blocks. Arcology storage
UI must explain this explicitly.

## 83.5 Native semantics do not perfectly match POSIX

This is deliberate. POSIX compatibility belongs in a compatibility layer, not in ArcFS's
foundational ontology.

------------------------------------------------------------------------

# 84. Decision Summary

This RFC establishes the following decisions as the ArcFS baseline:

1. ArcologyFS / ArcFS is Arcology's native filesystem.
2. ArcFS is object-native, not pathname-native.
3. Persistent objects have 128-bit stable OIDs.
4. Paths use Arcology's colon-delimited namespace.
5. `:` and NUL are invalid within native name components.
6. Names are UTF-8, NFC, case-preserving, and case-sensitive.
7. ArcFS uses copy-on-write metadata.
8. ArcFS commits through durable checkpoints/generations.
9. ArcFS is extent-based and supports sparse files.
10. Metadata and normal file data are checksummed.
11. Snapshots and reflinks are base capabilities.
12. Traditional unrestricted pathname symlinks are replaced by typed Object Links as the native
    mechanism.
13. Deleting a name removes a relationship; object reclamation occurs only when no
    committed/runtime references require it.
14. ArcFS integrates with Arcology capabilities and runtime handles.
15. File pickers may grant object authority without directory-wide visibility.
16. Filesystem instances attach through the Arcology object lifecycle.
17. ArcFS does not model devices/processes/services as pseudo-files.
18. ArcFS is one implementation behind a replaceable `FileSystem` interface.
19. Recovery, health reporting, inspection, and repair planning are first-class requirements.
20. On-disk parsing treats media as untrusted input.
21. Persistent-format evolution requires explicit feature negotiation and conformance fixtures.

------------------------------------------------------------------------

# 85. Final Principle

ArcologyFS should make persistent storage behave like the rest of Arcology: explicit, inspectable,
composable, recoverable, and understandable.

The filesystem is not a bag of path strings sitting above a disk. It is a durable object graph
projected into human-readable namespaces, backed by transactional storage, governed by explicit
authority, and exposed through contracts that can be implemented and tested independently.

That is the foundation on which Arcology's system volume, user data, applications, snapshots,
recovery environment, and long-term storage tooling should be built.

------------------------------------------------------------------------

# 86. Revision History

| Version | Date       | Summary                                                      |
|---------|------------|---------------------------------------------------------------|
| 0.1     | 2026-08-19 | Initial draft, provided in full and committed as RFC-0039     |
