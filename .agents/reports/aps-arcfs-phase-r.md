# ArcologyFS (ArcFS) Phase R: UTF-8 String and Binary Blob Attribute Values

## Scope delivered

RFC-0042 Phase R (Section 12) -- the last of the five named phases (N-R). New attribute type
codes and public API for variable-length attribute values, with **zero on-disk Attribute Tree
format change**.

- `ArcFSAttrTypeString()` = 6, `ArcFSAttrTypeBlob()` = 7.
- `ArcFS.SetStringAttribute(oid, attributeId, bufferPtr, byteLength) AS BOOL`
- `ArcFS.GetStringAttribute(oid, attributeId, destPtr, destCapacity) AS U64`
- `ArcFS.SetBlobAttribute(oid, attributeId, bufferPtr, byteLength) AS BOOL`
- `ArcFS.GetBlobAttribute(oid, attributeId, destPtr, destCapacity) AS U64`

The Blob pair is an addition beyond RFC-0042 Section 12.2's own literal text (which named only
the String pair) -- Section 12.1 already treats String and Blob as two instances of the exact
same mechanism ("raw UTF-8 bytes... or raw bytes"), so giving Blob its own symmetric public
entry points, rather than making callers overload the String API for binary content, is a direct
reading of the RFC's own stated design, not a scope expansion.

## The representation: reusing the Data Pool, not widening the row

The 64-byte Attribute Tree row (`oidLow`/`oidHigh`/`attributeId`/`active`/`type`/`valueLow`/
`valueHigh`/`reserved0`) is completely unchanged. For a String/Blob-typed row, `valueLow` and
`valueHigh` simply change MEANING: `valueLow` becomes a real data-pool slot index (the same
allocator, `ArcFSAllocateDataSlot`, a file's own `dataSlot` already uses) and `valueHigh` becomes
the value's real byte length -- reusing the exact machinery Section 12.1 specifies, rather than
inventing a second allocator or growing the row. Capped at `ArcFSFileCapacityBytes()` (4096
bytes, one chunk) -- plenty for a real string or small binary value, and keeps the touched-chunk
bookkeeping to exactly one chunk per value rather than needing an ordinary file's multi-chunk
logic.

`ArcFSGatherUniqueDataSlots` (already the single shared source every Extent Tree build/count/
gather pass reads from -- confirmed by re-reading `ArcFSCountTouchedChunks`,
`ArcFSGatherSortedExtentEntries`, `ArcFSBuildExtentTree`, and `ArcFSWriteOneExtentLeafNode`, none
of which needed ANY change) is the one place that needed extending: a second pass now also scans
active Attribute rows, adding any String/Blob-typed row's own `valueLow` to the same unique-slot
list a FILE's own `dataSlot` already populates. This one extension point is what makes an
attribute-owned value's real chunk content actually get written to and read from disk -- every
other stage of the commit/mount pipeline already treats "a data-pool slot with touched chunks"
generically, regardless of who owns it.

## A real design flaw caught before it shipped: row-index-as-slot-identity does not generalize

`ArcFSPopulateObjectRow`'s own mount-time pattern for a first-time FILE owner reuses the object
row's own index directly as the live data-pool slot identity -- safe ONLY because the object-row-
index domain and the data-pool-slot domain are the exact same size (`ArcFSMaxObjects()`, both
2048 since Phase Q). Copying that same trick into `ArcFSPopulateAttributeRow` (using the
attribute row's own index, up to `ArcFSMaxAttributes()` = 4096) would have silently written
attribute-owned slot content into data-pool addresses at index 2048-4095 -- outside the Data
Pool's own committed 2048-slot capacity, and (worse, at smaller scale) capable of colliding with
a live FILE-owned slot at the same numeric index, since the two domains are not the same size.
Found by reasoning through the address arithmetic before writing the mount-time loader, not by a
failing fixture.

Fixed by allocating a genuinely fresh live slot for each attribute row at mount time
(`ArcFSAllocateDataSlot()`, the exact same call `ArcFS.SetStringAttribute` uses at write time)
instead of reusing the row's own index. This exposed a second, real ordering bug: `ArcFS.
MountImage` only bumped `ArcFSNextDataSlotAddress()` past every object-claimed slot at the very
END of the function, AFTER `ArcFSLoadAttributes()` (and therefore `ArcFSPopulateAttributeRow`'s
new `ArcFSAllocateDataSlot()` call) had already run -- so a fresh allocation during attribute
loading could have collided with a slot an object row loaded moments earlier still owned, using a
stale, not-yet-bumped counter. Fixed by moving that one bump to run immediately after
`ArcFSLoadObjects()` succeeds, before `ArcFSLoadNamespace()`/`ArcFSLoadAttributes()` run -- found
and fixed while designing the mount-time loader, before any QEMU run, not discovered as a live
bug.

One consequence, named plainly: attribute-owned String/Blob values and FILE content now share the
SAME 2048-slot Data Pool budget (`ArcFSAllocateDataSlot`'s own capacity, unchanged this phase) --
a deliberate simplification, not an oversight. A future increment that needs the two to scale
independently would give attributes their own slot domain; this phase reuses the existing one, as
Section 12.1 itself directs.

## Validation

- All touched entry points (`ArcFSAttrTypeString`, `ArcFSAttrTypeBlob`, `ArcFSGatherUniqueDataSlots`,
  `ArcFSPopulateAttributeRow`, `ArcFS.MountImage`, `ArcFSSetVariableAttribute`, `ArcFS.
  SetStringAttribute`, `ArcFS.SetBlobAttribute`, `ArcFSGetVariableAttribute`, `ArcFS.
  GetStringAttribute`, `ArcFS.GetBlobAttribute`) compile cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-string-blob-attribute.abas`): a real
  object gets three attributes -- a pre-existing integer-typed one (424242), a genuine multi-byte
  UTF-8 string (`"caf" + U+00E9 + " " + U+2615`, 9 real bytes spanning 1, 2, and 3-byte UTF-8
  sequences, not ASCII-only), and a real 37-byte binary blob spanning the full 0..255 byte-value
  range including a real `0x00` mid-buffer and a real `0xFF`. A real commit + remount, then every
  value read back and compared byte-for-byte: the integer attribute unchanged, the string exactly
  9 bytes matching, the blob exactly 37 bytes matching. Also proves: a value exceeding
  `ArcFSFileCapacityBytes()` is refused outright (returns 0) and leaves NO attribute row behind
  (`HasAttribute` confirms absence, not just a rejected write); `GetStringAttribute` fails closed
  (returns 0, does not partially copy) when the caller's own destination buffer is too small.
- Passed on the first real attempt; negative control (corrupting the expected integer-attribute
  value) confirmed a real `FAIL 1`; deterministic across 3 repeated runs.
- New smoke test `systems_arco_basic_arcfs_string_blob_attribute_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest`.
- Full regression suite re-run -- see the RFC-0042 revision history entry for the final count;
  confirms the `ArcFS.MountImage` ordering fix and the `ArcFSGatherUniqueDataSlots`/
  `ArcFSPopulateAttributeRow` extensions broke nothing in any of the other ArcFS fixtures.

## Documented scope reductions

1. **No independent capacity for attribute-owned values.** They draw from the same 2048-slot Data
   Pool a FILE's own content already uses (see "A real design flaw" above) -- named as a
   deliberate simplification, not attempted as a separate allocator this phase.
2. **One chunk (4096 bytes) per value, not `ArcFSSlotSizeBytes()`'s full 32768.** A String/Blob
   value could technically use up to the full slot (all 8 chunks, matching the max file size), but
   this phase caps at one chunk to keep the touched-chunk bookkeeping simple (`ArcFSChunkTouchedSet
   (dataSlot, 0, 1)`, always chunk 0) -- plenty for the realistic tag/label/small-document use case
   Section 12 itself describes; a future increment could raise this without any format change,
   since the cap lives purely in `ArcFSSetVariableAttribute`'s own validation, not the row layout.
3. **Attribute-owned slots are never reflinked.** `ArcFSGatherUniqueDataSlots`'s attribute-scanning
   pass runs the same dedup check as its object-scanning pass for uniformity, but nothing in this
   phase ever gives two attribute rows the same slot the way `ArcFS.Reflink` does for files -- so
   the dedup branch never actually fires in practice today. Left generic rather than special-cased,
   matching the RFC's own "reuse existing mechanism" direction.

## RFC-0042 status

With Phase R delivered, all five named phases (N-R) are implemented and QEMU-proven. The
Allocation Bitmap's own multi-sector redesign (surfaced by Phase Q's own coupling finding, and
named in Section 14's own phase-ordering note) remains real, honestly-scoped future work beyond
this RFC's five named phase letters -- not a Phase R gap, and not attempted here.
