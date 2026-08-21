# ArcologyFS (ArcFS): Reflink Sharing Now Survives a Remount (RFC-0040 Section 11.5 follow-on)

## Scope delivered

Closes the one scope reduction the previous increment (`.agents/reports/aps-arcfs-extent-tree-reflink.md`)
named explicitly: two OIDs sharing an undiverged `dataSlot` now keep sharing the SAME live
in-memory slot after a real commit and remount, not just the same on-disk content.

- **`ArcFSMountSlotOwnerAddress`**: a new, mount-time-only scratch table (64 `U64` entries, reset
  once per `ArcFSLoadObjects` call), direct-indexed by COMMITTED `dataSlot` value, holding which
  row index has claimed the real live slot for that committed identity this mount.
- **`ArcFSPopulateObjectRow` rewritten**: the first row this mount to reference a given
  `committedDataSlot` becomes that live slot's real owner -- gets a fresh refcount of 1 at its own
  index and actually reads the chunks off disk into it, exactly as before. Every LATER row sharing
  the same `committedDataSlot` now points its own `dataSlot` field at that SAME owner index instead
  of getting a private copy, incrementing the owner's refcount instead of loading anything a second
  time. Non-FILE objects (directories) are unaffected -- their own `dataSlot` is never reflinked,
  so they always own their own index, exactly as before.

## Why this was safe to add, and why it needed so little

`ArcFSSlotRefCountAddress`, `ArcFSEnsurePrivateSlot`, and `ArcFSAllocateDataSlot` never actually
assumed `dataSlot == row index` -- they only ever assumed a row's OWN `dataSlot` field is
authoritative for where its bytes live. That assumption was already correct for a genuine,
in-session reflink (where two rows already point at the same slot); mount time was the ONE place
that threw the relationship away, unconditionally resetting every row's `dataSlot` to its own
index regardless of what was committed. Restoring the relationship at exactly that one point
needed no changes anywhere else in the file:

- `ArcFSEnsurePrivateSlot` (the copy-on-write divergence gate) doesn't care whether a slot's
  refcount reached 2 via a live reflink this session or via being restored from a remount -- it
  reads the row's own `dataSlot`, checks the refcount, and diverges exactly the same way either
  path.
- `ArcFSNextDataSlotAddress`'s own resume point (`ArcFS.MountImage`, `loadedObjectCount`) stays
  correct unchanged: every live `dataSlot` assigned during mount, owner or sharer, is always SOME
  row's own index, strictly less than `loadedObjectCount` -- resuming the allocator there can never
  collide with a live slot regardless of how much sharing was restored.
- The NEXT commit's own `ArcFSGatherUniqueDataSlots` (Extent Tree build) already dedupes by
  whatever the CURRENT live `dataSlot` values are, with no assumption about how they got that way --
  it already correctly re-derives Extent Tree sharing from the now-correctly-restored in-memory
  state, with no changes needed there either.

## Validation

- All 47 public `ArcFS.`-prefixed entry points, plus the touched internal functions, compile
  cleanly at X86_64 codegen level, individually reveal-checked against the live stdlib combined
  with `block_device_policy.abas`.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-remount-reflink-sharing.abas`): file A
  gets a real 8000-byte write, commits, remounts. Reflinking B from A (undiverged) and committing,
  then remounting, shows -- read DIRECTLY from each OID's own live `dataSlot` field via
  `ArcFSFindObjectRow`, not inferred from content -- that A and B genuinely share the identical
  live slot, with that slot's own refcount reading exactly 2. A SECOND consecutive commit (no
  changes) and remount still shows the same sharing, proving this isn't a one-time coincidence. A
  100-byte write through B alone forces `ArcFSEnsurePrivateSlot` to diverge it (refcount was 2);
  after a real commit and remount, A and B are confirmed on genuinely DIFFERENT live slots (each
  with its own refcount of 1), with A completely unaffected and B showing the patch plus the rest
  of the original content -- proving the owner-table logic correctly does NOT re-merge two OIDs
  whose committed identities have genuinely diverged. A real `ArcFS.ReclaimGeneration()` pass
  afterward, followed by one more commit and remount, leaves both files' content still exactly
  correct. Passed on the first real QEMU attempt; a negative control (flipping the core
  sharing-survives-remount assertion) confirmed the fixture correctly reports `FAIL 1`;
  deterministic across 3 repeated runs.
- New smoke test `systems_arco_basic_arcfs_remount_reflink_sharing_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest`.
- Full regression suite re-run clean.

## Remaining activation gate

This closes the one named scope reduction the previous Extent Tree/reflink-sharing increment left
open. RFC-0040's other remaining named gaps are unaffected and unchanged: a full
`(OID, namespace, name/ID)` multi-attribute-per-object model (Phase L); UTF-8 string/binary blob
attribute values (Phase L); no defect-tolerant scrub coverage for the Attribute or Extent Trees;
extent-overlap repair's authoritative-selection policy still deviates from the RFC's own literal
per-entry-generation-number framing (Phase M).
