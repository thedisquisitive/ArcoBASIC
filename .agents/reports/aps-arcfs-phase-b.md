# ArcologyFS (ArcFS) Phase B: Read-Only ArcFS Image (RFC-0039)

## Scope delivered

RFC-0039's Phase B ("read-only ArcFS image... superblock parser; checkpoint parser; object/
namespace trees; extent reads; checksums; image inspection") is implemented and proven end to
end under QEMU/OVMF: `ArcFS.MountImage()` reads a real, independently-generated, checksummed
on-disk image through RFC-0038's `RAMDisk`/`BlockDevice` contract, validates every structure it
reads, and loads the result into Phase A's already-proven in-memory tables -- after which every
one of Phase A's own functions (`Resolve`, `OpenHandle`, `HandleRead`, ...) answers real queries
against real on-disk data completely unchanged.

This is also the first concrete use, anywhere in this project, of RFC-0039's own Section 80
dependency note ("Before ArcFS becomes the writable system filesystem, Arcology requires
authoritative contracts for: BlockStorage interface...") -- RFC-0038 is that contract, and this is
the first code that actually calls `RAMDisk.ReadSectors` from ArcFS rather than from a FAT32
provider.

## Design: populate Phase A's tables, don't build a second reader

The natural alternative -- re-reading the block device on every `Resolve`/`Read` call -- was
rejected. Instead, `ArcFS.MountImage()` is a loader: it validates the image once and writes its
contents into the exact same fixed in-memory Object/Namespace/data-pool tables Phase A already
defined and already proved correct (rename, move, handle semantics, all of it). This is not a
shortcut; it is the concrete demonstration that RFC-0039's own reason for splitting Phase A out
first -- "validate the object and API contract before disk-format complexity" -- actually holds:
Phase A's contract turned out to be genuinely disk-format-agnostic, not something that only
happened to work because everything was in memory.

## Phase B's own minimal on-disk format

RFC-0039's real format (Sections 11-17) is far more than Phase B needs to prove the read
contract. This implementation defines its own documented, minimal subset (`arcology-os/scripts/
build/build-arcfs-test-image.py`'s own header comment carries the authoritative list):

- **One superblock**, not the redundant ring Section 14 wants (deferred to Phase F, recovery).
- **One committed checkpoint**, not generation history (Phase C, the transactional writer, is
  where multiple generations would first exist).
- **Flat fixed-size-record arrays**, one record per 512-byte sector, for both the Object Tree and
  the Namespace Tree -- not real B+trees. Phase B's job is the parse/validate contract, not
  large-volume performance.
- **A simple additive sum-of-bytes checksum** (mod 2^32, though the mask never actually matters at
  this record scale), not a cryptographic-strength algorithm. RFC-0039 Section 20 requires *a*
  checksum exists and is checked; it does not mandate which.
- **One contiguous 8-sector (4096-byte) extent per file**, matching Phase A's own fixed-file-
  capacity scope reduction -- no fragmentation or chain-walking needed yet.

Record layouts (all little-endian, all checksummed over every preceding byte in the record):

- Superblock (sector 0, 512 bytes): 8-byte magic `"ARCFSB01"`, format major/minor, a placeholder
  128-bit volume ID, logical block size (512), total blocks, feature flags (0), the checkpoint's
  sector number, then a checksum.
- Checkpoint (at the superblock's declared sector): generation, Object Tree root sector + count,
  Namespace Tree root sector + count, data region start sector, checksum.
- Object record (40 bytes, one per sector from the Object Tree root): OID, type, size, extent
  sector, checksum -- deliberately the same field shape as Phase A's in-memory Object Table row.
- Namespace record (64 bytes, one per sector from the Namespace Tree root): parent OID, 32-byte
  name, name length, child OID, checksum -- same shape as Phase A's in-memory Namespace Table row.

The test image encodes exactly the tree Phase A's own proof fixture builds by hand (`:home` ->
`:home:documents` -> `:home:documents:notes.txt`, the same 97-byte/checksum-6142 file content) --
but this time with fixed, generator-assigned OIDs (root=1, home=2, documents=3, notes.txt=4)
Phase B's fixture asserts against directly, since it never calls `CreateFile`/`CreateDirectory`
itself; every object it sees came from the image.

## Validation

- Full suite: 50/50 passing (49 pre-existing + the new Phase B test). Fixing this test also
  surfaced and fixed a **latent break in the Phase A smoke test**: `stdlib/arcfs_policy.abas` is
  no longer self-contained now that Phase B's functions (appended to the same file) depend on
  `RAMDisk.*`, and this compiler's `reveal` typechecks the whole module regardless of which
  `--entry` is requested -- so Phase A's own structural check, which revealed that file alone,
  started failing even for entry points Phase B never touched. Fixed by combining
  `block_device_policy.abas` + `arcfs_policy.abas` the same way both fixtures themselves already
  do. Worth remembering: appending to a stdlib file can silently break an *earlier* phase's own
  test if that test ever revealed the file in isolation.
- Independently re-verified in Python (a from-scratch re-parse of the generated image, not just
  round-tripping the generator's own in-memory state) that every superblock/checkpoint/object/
  namespace-record checksum in the test image is correct before writing a single line of the
  ArcoBASIC parser, and that the file content's checksum (6142) matches.
- Every public Phase B entry point (`ArcFS.MountImage` and its internal helpers) compiles cleanly
  at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-phase-b.abas`): a real `RAMDisk`
  preloaded (via the same `-device loader,...` QEMU mechanism RFC-0038's own FAT32 proof
  established) with the generator's image; `ArcFS.MountImage()` succeeds; `:` (root), `:home`,
  `:home:documents`, and `:home:documents:notes.txt` all resolve to the exact OIDs (1, 2, 3, 4)
  the generator assigned; a deliberately-missing sibling path correctly resolves to 0; a handle
  opened on OID 4 reads back exactly 97 bytes with the exact expected checksum (6142) -- content
  this fixture never wrote itself, only the Python generator did. Passed on the first real attempt
  (unlike Phase A, which needed real debugging); verified deterministic across three repeated
  runs.
- **The negative test** (`aps-arcfs-phase-b-negative.abas`, matching RFC-0038's own FAT32.Mount
  precedent and RFC-0039 Sections 39/74's untrusted-media threat model): mounting against a
  zeroed `BlockDevice` correctly returns `FALSE` -- the missing magic bytes are caught first,
  before any checksum is even computed. Passed, deterministic across repeated runs.

## Remaining activation gate

- **Phases C through G are all still ahead** (formatter/transactional writer, mutation
  completeness, snapshots, recovery, system integration). This report covers Phase B only; RFC-
  0039's own `Status` remains `Draft`.
- **No write path.** `ArcFS.MountImage()` only ever reads; Phase A's own in-memory `Rename`/
  `Remove`/`HandleWrite` still work after a mount (they operate on the now-populated in-memory
  tables exactly as before), but nothing writes those changes back to the `BlockDevice` -- that is
  explicitly Phase C's job, once a real copy-on-write staging concept exists to make it
  transactional (the same reasoning Phase A's own report gave for deferring transactions).
- **No superblock ring, no checkpoint history, no real B+trees** -- all named scope reductions
  above, each with its own Phase (F, C, C respectively) where the real version belongs.
- **`ArcFS.MountImage()` re-checks `objectCount`/`namespaceCount` against Phase A's own fixed
  table capacities (64/128) and fails closed if exceeded**, but does not yet handle an image
  larger than the 64 KiB `RAMDisk` this milestone's fixtures use -- a real future block device
  (Phase B's own scope reduction, not attempted here) would need its own capacity story.
- **The volume UUID field is a placeholder constant**, not a generated identifier -- Phase B has
  no need to distinguish one volume from another yet (only one is ever mounted at a time in any
  fixture in this chain so far).
