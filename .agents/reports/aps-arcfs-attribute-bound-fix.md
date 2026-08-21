# ArcologyFS (ArcFS): attributeTreeCount Sanity Bound Fix

## Scope delivered

A real, previously-unfound bug: `ArcFSReadCheckpoint`'s own sanity bound on the checkpoint's
claimed `attributeTreeCount` checked it against `ArcFSMaxObjects()` (64) instead of
`ArcFSMaxAttributes()` (128) -- a leftover from the multi-attribute-per-object increment (RFC-0040
Section 12.1), which raised the real Attribute Table capacity to `ArcFSMaxAttributes()`, independent
of `ArcFSMaxObjects()`, for the first time. Found while scoping RFC-0042 (touching this exact
function's own read path for its checkpoint self-description work), not by a failing test -- no
fixture had ever populated more than a handful of attributes at once, so the stale bound was never
exercised.

**The consequence, concretely**: a legitimate checkpoint honestly claiming between 65 and 128 active
attributes -- entirely within the real Attribute Table's own capacity -- would have been wrongly
treated as structurally invalid and refused to mount, exactly as if it were corrupt.

## The fix

```
IF attributeTreeCount > ArcFSMaxAttributes() THEN
    RETURN 0
END IF
```

replacing the stale `ArcFSMaxObjects()` comparison. One line; no other function in
`arcology-os/stdlib/arcfs_policy.abas` had the same staleness (confirmed by grep -- this was the
only sanity-bound check anywhere in the file still referencing the old constant for an
attribute-count comparison).

## Validation

- Structural check: `ArcFSReadCheckpoint`, `ArcFS.MountImage`, `ArcFS.SetAttribute`,
  `ArcFS.CommitImage` all compile cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-attribute-bound-fix.abas`): 10 objects
  each get 12 attributes (120 total active attributes -- comfortably past the old buggy bound of 64,
  comfortably within the real 128-row capacity). A real commit, then a real remount: the volume
  mounts successfully, and a handful of individually-checked attributes on two different objects
  round-trip correctly through the real Attribute Tree.
- **Confirmed the test genuinely exercises the bug, not just a name**: reverting the fix in a
  separate copy of the fixture (bound check restored to `ArcFSMaxObjects()`) and rebuilding
  reproduces the exact failure this fix closes -- the second `ArcFS.MountImage()` call (the one
  reading back the 120-attribute checkpoint) genuinely refuses, `FAIL 1`. This is the negative
  control for this specific fixture: not an assertion flip, but the literal pre-fix code path,
  proving the fixture would have caught this bug had it existed when the multi-attribute increment
  first shipped.
- Deterministic across 3 repeated runs of the fixed build.
- New smoke test `systems_arco_basic_arcfs_attribute_bound_fix_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest`.
- Full regression suite re-run clean.

## Why this matters for RFC-0042

RFC-0042 Section 8/9 replaces exactly this function's own hardcoded-constant checksum coverage with
a self-describing `recordLength`. Finding a real, previously-invisible bug in this function's own
existing bound-checking logic -- caused by a constant that quietly stopped matching reality one
increment ago -- is a direct, concrete illustration of Section 2.1's own motivating argument: fixed
constants baked into a reader with no self-description or real cross-checking are exactly what
silently drift out of sync with the format they're meant to validate.
