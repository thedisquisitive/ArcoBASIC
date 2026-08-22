# ArcologyFS (ArcFS) Phase P: Reserved Growth Capacity

## Scope delivered

RFC-0042 Phase P (Section 10): real reserved growth capacity in the three fixed-width live row
layouts that had none -- the Object Table row (previously 40 bytes, its one spare field already
spent by RFC-0040 Section 10's 128-bit OID work), the Namespace Table row (previously 64 bytes, no
spare field ever), and the Attribute Table row (previously 56 bytes, this session's own
multi-attribute increment, no spare field). Each now carries genuine, currently-unpopulated
reserved fields for whatever the next per-row field turns out to be (a generation stamp is one named
candidate, RFC-0042 Section 4 Non-Goal 4).

- **Object row**: 40 -> 56 bytes, two new `reserved0`/`reserved1` U64 fields after the existing
  `oidHigh`.
- **Namespace row**: 64 -> 80 bytes, two new reserved U64 fields after the existing `active` flag.
- **Attribute row**: 56 -> 64 bytes, one new reserved U64 field after the existing `valueHigh`.

## The key simplification that made this a small change

The ON-DISK tree leaf entry shapes (Object Tree 40 bytes, Namespace Tree 56 bytes, Attribute Tree
48 bytes) are completely UNCHANGED -- only the LIVE, in-memory row strides widened. Since nothing
populates the new reserved fields yet, there is nothing new to persist, so the B+tree build/collect/
load functions for all three trees needed zero changes. This reduced the entire increment to: three
stride constants (`ArcFSObjectRowAddress`/`ArcFSNamespaceRowAddress`/`ArcFSAttributeRowAddress`) and
`ArcFS.Initialize()`'s three clear-loop sizes. The reserved bytes' own "always zero" property falls
out of `ArcFS.Initialize()`'s existing full-table clear plus the fact that no row-constructing
function (`ArcFSCreateObject`, `ArcFS.Reflink`, `ArcFSPopulateObjectRow`,
`ArcFSAttachExistingObject`, `ArcFS.SetAttribute`, `ArcFSPopulateAttributeRow`) ever writes past the
fields it already defines -- no per-function "clear the new reserved bytes" code was needed at all.

## Address headroom confirmed directly, not assumed

Every one of the three widened tables fits inside its existing fixed MMIO address gap with no
relocation needed, confirmed by reading the actual address constants rather than estimating:

- Object Table (0x2041000) to Namespace Table (0x2042000): 4096 bytes of headroom; new size
  64*56=3584 bytes.
- Namespace Table (0x2042000) to the next used address, `ArcFSNextDataSlotAddress` (0x2087000):
  282624 bytes of headroom; new size 128*80=10240 bytes.
- Attribute Table (0x2089000) to `ArcFSHandleTableAddress` (0x2090000): 28672 bytes of headroom;
  new size 128*64=8192 bytes.

This is deliberately unlike Phase Q's own upcoming capacity increase, which is large enough to
require real address-map replanning (RFC-0042 Section 11.2) -- Phase P's own row growth is small
enough that the existing "policy-owned fixed scratch address, sized with headroom" convention this
project has used since Phase A already absorbs it.

## Validation

- All 48 public `ArcFS.`-prefixed entry points compile cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF** (`aps-arcfs-reserved-rows.abas`): two objects (a
  file, a directory) with real namespace entries and two real attributes on the file -- every new
  reserved field (both on the Object rows, both on the Namespace rows, the one on each Attribute
  row) reads exactly zero immediately after creation, still exactly zero after a real
  commit+remount, and still exactly zero after a real rename and a real reflink followed by another
  commit+remount. Every existing structural check (`ArcFS.Lookup`, `ArcFS.GetAttribute`,
  `ArcFS.GetHealthState`) continues to pass unaffected by the widened rows.
- Passed on the first real attempt; negative control (flipping an expected attribute value)
  confirmed real `FAIL 1`; deterministic across 3 repeated runs.
- New smoke test `systems_arco_basic_arcfs_reserved_rows_smoke` registered in
  `arcology-os/cmake/Testing.cmake`, passes through `ctest`.
- Full regression suite re-run clean (80/80 before this increment's own new test; the stride change
  itself touches every ArcFS operation transitively, so this full-suite pass is the real regression
  evidence that widening the rows broke nothing, on top of the dedicated new fixture's own targeted
  proof).

## A stale comment fixed along the way

`ArcFSAttributeRowAddress`'s own header comment still said "LEN/MID remain unimplemented" as part of
its reasoning for using integer `attributeId`s instead of string names -- stale since this session's
own earlier LEN/MID increment. Corrected to note LEN/MID now exist but operate on in-memory `STRING`
pointers, not on-disk attribute keys, so the original reasoning for integer IDs still holds.

## Documented scope reductions

1. **No specific future use is committed for any reserved field** -- genuinely open, matching RFC-
   0042 Section 10.4's own "a real reservation, not disguised scope creep" and Section 20's Open
   Question 2, which deliberately leaves this decision for whenever a real consumer exists.
2. **The on-disk tree leaf formats are unchanged.** A future field that actually needs to be
   PERSISTED (not just held in the live row) will need its own on-disk width change, generalizing
   Section 8's own `recordLength`-style self-description pattern to tree leaf entries -- named as
   future work, not attempted here (RFC-0042 Section 10.1's own header note).

## Remaining activation gate

RFC-0042's other phases are unaffected and unchanged: Phase Q (production-scale capacities and the
Allocation Bitmap's own multi-sector redesign); Phase R (UTF-8/blob attribute values). With this
increment, every fixed-width row this format defines now has real, proven headroom for at least one
future per-row field, closing RFC-0042 Section 10's own three named requirements in one increment.
