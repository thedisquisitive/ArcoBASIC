# ArcologyFS (ArcFS) Phase K, offset-based partial I/O (RFC-0040 Section 11.4)

## Scope delivered

RFC-0040 Section 16's Phase K explicitly deferred three items when it first shipped: a true
variable-count Extent Tree (Section 11.1), offset-based partial reads/writes (Section 11.4), and
on-disk reflink sharing (Section 11.5). This increment closes the second of those three.

`ArcFS.HandleWrite`/`ArcFS.HandleRead` both gain a fourth parameter, `offset`, and now behave like
real `pwrite(2)`/`pread(2)`:

- **A write no longer unconditionally replaces the file from byte 0.** `size` becomes
  `max(oldSize, offset + written)` -- a write never shrinks a file on its own; only
  `ArcFS.Resize` does that deliberately. This is a genuine behavioral fix, not just an addition:
  it retroactively closes the exact gap Phase K's own original fixture had to work around with
  low-level, non-public-API verification (see "What this fixes" below).
- **A write whose `offset` starts past the file's current size** zero-fills the gap
  `[oldSize, offset)` in memory (mirroring `ArcFS.Resize`'s own growth path exactly) but does not
  mark those chunks touched -- the gap becomes a real sparse hole at the next commit, the same
  construction Section 11.3 already established, now reachable through a write's own offset+growth
  instead of only through an explicit `Resize` call.
- **`ArcFS.HandleRead` honors `offset` too**, clamped to `[offset, size)`; reading at or past EOF
  returns 0, matching `pread(2)`.

## What this fixes

Phase K's own report (`.agents/reports/aps-arcfs-phase-k.md`) documents a real fixture-design bug
its first QEMU attempt caught: a short write through a reflinked clone was expected to leave the
rest of the file's prior content visible, but `HandleWrite` had no offset parameter, so every
write replaced the file from byte 0 and shrank `size` to exactly what was written. The chunks
beyond the write stayed real and correctly persisted, but became genuinely unobservable through
the public read API -- the fixture had to be corrected to verify them directly against the
post-mount data pool instead of through `HandleRead`.

This increment closes that gap for real, not just for that one fixture. A short write at a
mid-file offset now patches its own range and leaves everything else genuinely visible and
correct through the ordinary public API -- exactly the behavior a real filesystem write is
expected to have, and exactly what RFC-0039 Section 18's "MUST support... partial writes" already
asked for.

## Design

Both functions keep their first three parameters unchanged in position (`handle`, `buffer`,
`byteCount`/`maxBytes`) and append `offset` as a fourth, matching `pwrite`/`pread`'s own
convention of appending offset last. There is no internal caller of either function anywhere in
`arcfs_policy.abas` (confirmed by grep before making the change), so this signature change has
zero blast radius within the live stdlib itself -- every frozen fixture in this chain (Phases A
through K, RFC-0041's own fixtures) embeds its own snapshot of the stdlib as it existed at
authoring time and is completely unaffected, matching this project's established "frozen fixture"
discipline (first documented in the Phase H report).

`ArcFS.HandleWrite`'s new body: bound-checks `offset` against `ArcFSSlotSizeBytes()`, clamps
`toWrite` so the write never exceeds the slot, treats a zero-length write (`byteCount = 0`, or an
`offset` that clamps `toWrite` to 0) as a true no-op with zero side effects, zero-fills the gap
`[oldSize, offset)` when `offset > oldSize`, writes the real bytes, marks every chunk from
`offset \ ArcFSFileCapacityBytes()` through `(offset + toWrite - 1) \ ArcFSFileCapacityBytes()`
touched, and sets `size = max(oldSize, offset + toWrite)`.

`ArcFS.HandleRead`'s new body: returns 0 immediately if `offset >= size` (matching `pread`'s own
"reading at or past EOF" contract), otherwise clamps `toRead` to `size - offset` and reads from
`dataBase + offset`.

## Documented scope reductions (unchanged from Phase K's own report, restated for clarity)

1. **Still a fixed-count (4) extent list, not a true variable-length Extent Tree** (RFC-0040
   Section 11.1) -- file size stays capped at `ArcFSSlotSizeBytes()` (16384 bytes). This increment
   does not touch that.
2. **Still no on-disk reflink sharing** (RFC-0040 Section 11.5) -- two OIDs sharing a data-pool
   slot still each get their own full duplicate set of real chunk extents on disk while
   undiverged. This increment does not touch that either.
3. **`ArcFS.SnapshotReadFile` was NOT given an offset parameter this increment** -- it remains a
   from-byte-0-only read against a pinned snapshot generation. RFC-0040 Section 11.4 names
   `ArcFS.HandleWrite`/`HandleRead` specifically; `SnapshotReadFile` is a narrower, less central
   function (RFC-0039 Phase E) and adding offset support there is additive, straightforward future
   work once it is worth doing, not a load-bearing gap this increment needed to close.

## Validation

- `ArcFS.HandleWrite`, `ArcFS.HandleRead`, `ArcFS.Resize`, and `ArcFSEnsurePrivateSlot` (unchanged,
  re-verified for regressions) all compile cleanly at X86_64 codegen level, individually
  reveal-checked against the live stdlib.
- A full sweep of all 35 public `ArcFS.`-prefixed entry points confirms nothing else regressed --
  zero internal callers of the changed functions meant zero collateral edits were needed anywhere
  else in the file.
- Confirmed `systems_arco_basic_arcfs_phase_a_smoke.sh` and `systems_arco_basic_arcfs_phase_k_smoke.sh`
  (the two OTHER smoke tests whose own structural-check entry lists name
  `ArcFS.HandleWrite`/`HandleRead`) still pass their structural check against the new 4-arg
  signature -- a `reveal --entry` structural check only requires the named function to exist and
  compile, not that some other part of the module calls it with a specific arity, so this was
  expected but confirmed directly rather than assumed.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-offset-io.abas`): writes a 100-byte
  pattern at offset 0, commits, remounts, confirms it round-trips. Writes a 20-byte patch at
  offset 40 (entirely within the 100-byte file); confirms `size` stays 100 (the core fix) and the
  read-back is exactly `pattern1[0:40) + patch + pattern1[60:100)`, checked both in-session and
  after a real commit/remount; also confirms an offset-based `HandleRead` at offset 40 returns
  exactly the 20-byte patch directly, without ever reading from byte 0. Writes a 30-byte pattern
  at offset 9000 -- deep in chunk 2, far past the size at that point (100) -- confirming `size`
  grows to 9030, the gap `[100, 9000)` reads as zero (spanning chunk 0's own tail, ALL of chunk 1,
  and the start of chunk 2 -- a real multi-chunk sparse gap created by a WRITE's own
  offset+growth, not an explicit `Resize` call), and chunk 1 specifically stays genuinely
  untouched (verified directly against the data pool's own touched-chunk bitmap, the same
  "public API can't observe a real-vs-hole distinction, so check the underlying state directly"
  discipline Phase K's own fixture already established) both in-session and after a real
  commit/remount. Health scrub reports Healthy; a final real reclaim leaves everything intact.
  Passed on the first real QEMU attempt. A negative control (flipping the chunk-1-untouched
  assertion to expect the wrong outcome) confirmed the fixture correctly reports `FAIL 1` rather
  than silently passing. Deterministic across 3 repeated runs.
- Full suite: every pre-existing ArcFS/APS test re-run unchanged alongside the new
  `arcfs_offset_io_smoke` test; no stale structural-check entries found anywhere else.

## Remaining activation gate

- **A true variable-count Extent Tree is still not implemented** (RFC-0040 Section 11.1) -- files
  remain capped at 16384 bytes.
- **On-disk reflink sharing at the extent level is still not implemented** (RFC-0040 Section
  11.5) -- reflinked files still duplicate real extents on disk while undiverged.
- **`ArcFS.SnapshotReadFile` still has no offset parameter** -- see scope reduction #3 above.
- **Phase L (persistent attributes) and Phase M (full health/repair) remain ahead**, per RFC-0040
  Section 16's own dependency chain. RFC-0040's own `Status` remains `Draft`.
