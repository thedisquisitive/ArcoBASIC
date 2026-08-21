# ArcologyFS (ArcFS): Multi-Attribute-Per-Object Model (RFC-0040 Section 12.1)

## Scope delivered

Closes the standing gap: "the Attribute Table is one-slot-per-object, so a second
`ArcFS.SetAttribute` call on the same object silently clobbers the first." The Attribute Table is
now genuinely `(OID, attributeId)`-keyed, matching RFC-0040 Section 12.1's own "(OID, attribute
namespace, attribute name/ID)" model -- reduced to a single caller-defined integer `attributeId`
rather than a namespace-plus-name string pair, this backend's own already-established "use integer
IDs instead of strings" pattern (LEN/MID remain unimplemented; every other keyed table in this file
-- FAT32 raw buffers, OIDs, chunk indices -- already made the same call).

- **Row layout widened from 24 to 56 bytes**: `[oidLow][oidHigh][attributeId][active][type]
  [valueLow][valueHigh]`, reusing the existing `ArcFSAttributeTableAddress()` (28,672 bytes of
  headroom before the next-used address comfortably fit the new table's 128*56=7168 bytes).
- **`ArcFSMaxAttributes()` (128)**, independent of `ArcFSMaxObjects()` (64) for the first time --
  the table is no longer parallel-indexed with the object table, since one object can now own more
  than one row.
- **New `ArcFSFindAttributeRow`/`ArcFSFindFreeAttributeRow`** helpers, replacing every public
  function's old `ArcFSFindObjectRow`-then-reuse-that-row-index pattern.
- **Public API gained an `attributeId` parameter**: `ArcFS.SetAttribute`, `ArcFS.HasAttribute`,
  `ArcFS.GetAttributeType`, `ArcFS.GetAttribute`, `ArcFS.GetAttributeHigh` all now take
  `(oid, attributeId, ...)`. `oid` itself stays a single low-half `U64`, matching this file's own
  already-accepted low-64-bits-only lookup limitation (RFC-0040 Section 10's own header note) --
  real `oidHigh` is still stored per row and round-tripped faithfully to/from disk even though
  lookup never compares it.
- **New `ArcFS.RemoveAttribute(oid, attributeId)`**: meaningful for the first time now that an
  individual attribute can be cleared without clobbering an object's other attributes -- the old
  one-slot-per-object model made this equivalent to simply never calling `SetAttribute` again, since
  there was nothing else on the row to preserve.
- **On-disk Attribute Tree entry width widened from 40 to 48 bytes** (adds `attributeId`), touching
  `ArcFSGatherSortedActiveAttributes` (now walks the Attribute Table's own independently
  `active`-flagged rows directly, rather than cross-referencing a parallel object-row index; sorts
  by `(oidLow, attributeId)` so every key stays unique even across one object's several attributes),
  `ArcFSWriteOneAttributeLeafNode`, `ArcFSBuildAttributeTree`'s `firstKey` offset math,
  `ArcFSAttributeTreeCollectAllEntriesInto`/`...Tolerant` (entry width and bound, the bound now
  `ArcFSMaxAttributes()` instead of `ArcFSMaxObjects()`), and `ArcFSLoadAttributes`'s own
  entry-offset math.
- **`ArcFSPopulateAttributeRow` redesigned**: previously resolved the *owning object's* row and
  wrote into that same index (the old 1:1 assumption); now takes an explicit `index` -- the tree
  walk's own sequential position -- and writes to `ArcFSAttributeRowAddress(index)` directly,
  exactly matching how `ArcFSPopulateObjectRow` and the Namespace Tree's own populate function
  already use their own walk-sequential index as the destination row. Still checks the owning
  object actually exists and fails closed if not, preserving this file's established "never
  silently guess" discipline for a mount-time inconsistency.
- **Attribute Table scratch addresses relocated**: the sorted/level-A/level-B/collect-count
  addresses (`ArcFSAttributeTreeSortedAddress` and friends) needed two pages instead of one
  (48*128=6144 bytes vs. the old 40*64=2560), so they moved forward into the large free gap before
  `ArcFSDataPoolAddress` (0x2200000) rather than trying to cram into the old single-page gap.
- **`ArcFS.Initialize()`'s Attribute Table clear loop widened** from 1536 to 7168 bytes; **`ArcFS.
  PrepareCommit`'s `activeAttributeCount` pre-count** now walks `ArcFSMaxAttributes()` rows checking
  the new `active` flag at offset 24, instead of `ArcFSMaxObjects()` rows checking the old
  presence-via-type-field-at-offset-0 convention.

## Validation

- All 48 public `ArcFS.`-prefixed entry points, plus every new/touched internal function, compile
  cleanly at X86_64 codegen level, individually reveal-checked against the live stdlib combined
  with `block_device_policy.abas`.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-multi-attribute.abas`): two objects (A,
  B) are created; A is given TWO distinct attributes at once (`attributeId` 1 and 2) -- the core
  proof, impossible under the old model, where a second `SetAttribute` call on the same object
  silently overwrote the first. B is given its own `attributeId=1` to prove rows key on the FULL
  `(oid, attributeId)` pair, not `attributeId` alone -- A's id=1 and B's id=1 never collide.
  `ArcFS.RemoveAttribute(A, 1)` clears exactly that one row, leaving A's id=2 and B's id=1 both
  intact. A commit+remount cycle confirms the removed attribute stays gone and the two survivors
  round-trip byte-for-byte through a real on-disk Attribute Tree walk, with `ArcFS.GetHealthState()`
  reporting Healthy. A SECOND commit+remount cycle re-adds a fresh attribute at the (oid,
  attributeId) `RemoveAttribute` had vacated, proving the freed row is genuinely reusable across
  generations (not just within one in-memory session) and that the bulk tree rebuild correctly
  reflects a row whose identity changed between two committed generations.
- A negative control (flipping an expected post-remount attribute value) confirmed the fixture
  correctly reports `FAIL 2` (both the pre-commit and post-remount assertions against that value
  caught the injected fault) rather than a false PASS.
- Deterministic across 3 repeated runs.
- New smoke test `systems_arco_basic_arcfs_multi_attribute_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest`.
- Full regression suite re-run clean (75 tests, including the 3 pre-existing frozen fixtures that
  reveal-check the OLD 4-argument `ArcFS.SetAttribute`/2-argument `ArcFS.GetAttribute` signatures --
  unaffected, since `reveal --entry` compiles a function's own definition, not a call site with a
  fixed arity, and every frozen fixture carries its own independent, unchanged copy of the stdlib
  as of its own authoring time anyway).

## Documented scope reductions

1. **`attributeId` is a caller-defined integer, not RFC-0040's literal "namespace + name/ID" string
   pair.** Matches this file's own established, repeatedly-reasoned pattern of using integer IDs
   wherever the RFC's own text implies string-keyed lookup, since freestanding STRING has no
   substring/length operations on this backend (LEN/MID remain flatly unimplemented).
2. **`oid` lookup still compares only the low 64 bits**, unchanged from every other table in this
   file (RFC-0040 Section 10's own already-accepted limitation) -- real `oidHigh` is stored and
   round-tripped per row, but never compared.
3. **UTF-8 string/binary blob attribute values remain out of scope**, unaffected by this increment
   -- only the integer-representable value types (`ArcFSAttrType*`) this file already supported are
   reachable, for the same STRING-operations reason as (1).
4. **128 total attribute rows across the whole volume**, not per-object -- the same "small enough to
   exercise fully under QEMU" bound every other table in this file already uses (64 objects, 128
   namespace rows, 8 handles).

## Remaining activation gate

RFC-0040's other remaining named gaps are unaffected and unchanged: UTF-8 string/binary blob
attribute values (Phase L, see scope reduction 3 above); extent-overlap repair's authoritative-
selection policy still deviates from the RFC's own literal per-entry-generation-number framing
(Phase M, a deliberate, reasoned design choice, not treated as a gap to close). With this increment,
the Attribute Table itself finally matches RFC-0040 Section 12.1's own literal per-object model, not
just its type system -- the last major named structural gap in Phase L.
