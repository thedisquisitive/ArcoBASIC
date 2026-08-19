# ArcologyFS (ArcFS) Phase A: In-Memory Semantic Model (RFC-0039)

## Scope delivered

RFC-0039's own Phase A ("in-memory semantic model, no persistent disk yet... validate the object
and API contract before disk-format complexity") is implemented and proven end to end under
QEMU/OVMF: stable Object IDs independent of path, a colon-delimited namespace supporting real
multi-component resolution, a handle layer distinct from persistent OID identity, byte-stream
Read/Write through that handle, and rename/move/remove -- all backed by fixed-capacity in-memory
tables, no on-disk format of any kind.

This is the largest single implementation in the RFC-0036/37/38/39 chain so far (roughly 850 lines
of policy code plus a comparably-sized proof fixture), and the first to hit and document a real
compiler-imposed ceiling (the 4095-byte stack-frame limit) as an ordinary, expected constraint to
design around rather than a bug.

### Six documented Phase A scope reductions, stated up front in `stdlib/arcfs_policy.abas`'s own
header comment

1. **64-bit OIDs, not RFC-0039's 128-bit.** A monotonic counter; RFC-0039 Section 19 already
   treats even its own 128-bit space's overflow as a non-concern at any sane rate.
2. **Fixed-capacity in-memory tables** (64 objects, 128 namespace rows, 8 open handles) instead of
   a growable structure or an on-disk B+tree -- the same "one instance / small fixed table"
   pattern every prior RFC in this chain has used, extended here to a whole object graph.
3. **Fixed 4096-byte file capacity**, one data-pool page per file, 1:1 with its object-table row --
   not extents or sparse files. Phase A's job is the handle/Read/Write *contract*, not real
   storage layout (Phase C+ work).
4. **Paths and names are raw byte buffers with an explicit length, not `STRING`.** Not a leftover
   workaround -- freestanding `STRING` equality is now fixed (see `.agents/reports/
   freestanding-string-equality.md`) -- but `STRING` has no substring/indexing/length operations
   at all on this backend (`LEN`/`MID` are rejected outright, cleanly, as undeclared functions,
   confirmed directly while scoping this work), and colon-delimited path parsing fundamentally
   needs those. Byte buffers with `MEMORY.Read8` loops are the same, already-proven pattern
   RFC-0038 established for exactly this reason.
5. **No transactions**, despite RFC-0039 Phase A's own list including them. Every mutation here
   (Create, Rename, Remove) is already a single, indivisible operation on fixed in-memory tables --
   there is no multi-step staging area to make atomic yet. A no-op `BeginTransaction`/
   `CommitTransaction` pair was considered and deliberately rejected as actively misleading (it
   would imply composability across calls that does not exist). Left for Phase C, where a real
   staging concept has something to stage.
6. **No capability/authority checks** (RFC-0039 Section 26). RFC-0017 (Substrate Resource Model)
   has no concrete freestanding callable surface anywhere in this repository (confirmed by search
   during RFC-0038's implementation, re-confirmed here) -- building one specifically for this file
   would be separate, real scope creep, the same judgment RFC-0038 already made about the same gap.

## Design

Two logically separate fixed-capacity tables, matching RFC-0039 Sections 16.1/16.2's own
Object-Tree/Namespace-Tree split (scaled down from on-disk B+trees to in-memory arrays):

- **Object Table** (64 rows, 40 bytes each: OID, type, size, data-pool slot). Created once per
  object and never touched again by rename/move -- this is what makes OID identity survive a move.
- **Namespace Table** (128 rows, 64 bytes each: parent OID, name, name length, child OID, active
  flag). `ArcFS.Rename` mutates only this table's `parentOID`/`name` fields on the *existing* row
  for that OID; the Object Table row, and therefore the OID, is never touched -- directly proving
  RFC-0039 Section 5.1's central design principle ("a path is a human and programmatic lookup
  expression. A persistent Object ID is identity") rather than merely asserting it in prose.
- **Handle Table** (8 rows: OID, active) implements RFC-0039 Section 47's runtime-handle-vs-OID
  distinction concretely: closing a handle does not touch the object; a handle opened before a
  rename/move keeps working because it only ever stores the OID, never a path.

`ArcFS.Resolve` walks a colon-delimited path (`:home:documents:notes.txt`) component by component
via `ArcFS.Lookup`, entirely through pointer/length arithmetic -- no `STRING` operation involved
anywhere in path parsing, per scope reduction #4.

## Two real, general findings, both from finishing this proof, neither a fix to ArcFS or the
compiler's actual logic

### 1. A single flat `Main()` hit a real, working compiler safeguard (not a bug)

The first version of the QEMU fixture put the entire ~25-check test sequence in one `Main()`
function and failed to build: `"function \"Main\" needs a stack frame larger than the systems
backend's supported stack layout"`. This is `fission.cpp`'s own deliberate `frame_size > 4095`
guard, not a defect -- this backend's uniform "every named value gets its own stack slot, always
reloaded, never kept live across instructions" strategy (a stated Packet WP-008 non-goal:
"register allocator sophistication beyond correctness") means every subexpression evaluated
anywhere in a function, across every block an `IF`/`WHILE` creates, claims its own permanent slot,
and ~25 comparison checks plus ~80 individual byte-buffer-construction statements in one function
easily exceeds the budget. Fixed by decomposing the test into a dozen small functions, each
recomputing its own buffer pointers from two small fixed scratch addresses (the AllocatePages'd
scratch-page base, and a shared OID/handle/failure-count state table) rather than threading
values through parameters -- the same "policy-owned fixed scratch address" pattern this whole RFC
chain already uses for everything else, applied here to keep individual function frames small
rather than to persist state across reboots. Worth remembering for any future large ArcoBASIC test
fixture in this project: split early, don't wait to hit the ceiling.

### 2. `systemTable.ConsoleOut.Write` after `ExitBootServices` hangs in this QEMU/OVMF environment

Debugging a genuine test-logic bug (see below) surfaced this: the fixture's original
failure-reporting line, `systemTable.ConsoleOut.Write("APS ARCFS FAIL")`, placed after
`ExitBootServices` (exactly the same pattern RFC-0038's own fixtures already used for their own
post-`ExitBootServices` failure messages), produced no output and no crash -- just silence until
the harness's own timeout. Root-caused by replacing it with pure `SerialByte` output, which worked
immediately. This is a real, previously-undocumented gotcha: **every prior fixture in this project
with a post-`ExitBootServices` `ConsoleOut.Write` failure message has, in every actual passing
run, never executed that line** (their tests only ever succeed) -- this is the first fixture whose
own bug actually exercised that code path, which is how this was found. Fixed by reporting failure
over serial exclusively (including a single-digit failure count, clamped to 9, for at-a-glance
diagnosis) -- the fixture's own working pattern going forward, and worth flagging for any future
work that assumed the RFC-0038 precedent proved this was safe. It did not; it was untested.

## The actual test-logic bug found and fixed (not a compiler or ArcFS defect)

The proof's own 97-byte write/read content, intended to be `("ARCFS PHASE A DATA. " * 5)[:97]`,
was generated in ArcoBASIC via `m = i MOD 20` indexing into a hand-transcribed 20-character
lookup table (`IF m = 0 THEN value = 65`, ... `IF m = 19 THEN value = 32`) -- except the first
version used `MOD 21` with a spurious 21st case, an off-by-one in counting `"ARCFS PHASE A DATA.
"` (verified independently in Python: 20 characters, not 21). This produced a genuinely different
byte sequence than the Python-computed expected checksum (6142), causing exactly two checksum
mismatches (the initial write/read round trip and the post-move round trip -- the same underlying
content-generation bug, hit twice because the proof reads the file back twice). Found via
per-stage failure-count tracing (temporary `SerialByte`/state-table instrumentation, removed once
diagnosed) once real output was available at all (see finding #2 above). Fixed by correcting the
modulus to 20 and removing the spurious 21st case; the corrected logic was independently verified
against the Python reference to produce the exact expected 97-byte sequence and checksum before
rebuilding.

## Validation

- Full suite: 49/49 passing (48 pre-existing + the new ArcFS Phase A test).
- Every public `ArcFS.*` entry point (`Initialize`, `CreateFile`, `CreateDirectory`, `Lookup`,
  `Resolve`, `Rename`, `Remove`, `OpenHandle`, `CloseHandle`, `HandleWrite`, `HandleRead`)
  compiles cleanly at X86_64 codegen level.
- **The real proof, executed under QEMU/OVMF**: creates `:home`, then `:home:documents` under it,
  then `:home:documents:notes.txt`; resolves all three full paths plus root (`:`) alone, plus a
  deliberately-missing sibling path, confirming each resolves to the exact OID `CreateDirectory`/
  `CreateFile` returned (or 0 for the missing one); opens a handle, writes 97 bytes, reads them
  back, and checksums them exactly; renames the file within the same directory and confirms the
  old path is gone while the new path resolves to the *same* OID; moves it to a *different*
  directory and confirms the same OID-preservation property across a cross-directory move, not
  just a same-directory rename; opens a *new* handle on that same OID *after* the move and
  confirms it reads back the *exact original content* (proving both identity and data survive
  the move, not just the namespace entry); removes it; confirms the path no longer resolves.
  Passed; verified deterministic across three repeated runs.

## Remaining activation gate

- **Phases B through G are all still ahead** (RFC-0039 Section 78): a real read-only on-disk image
  parser (Phase B), a formatter and transactional copy-on-write writer (Phase C, which is also
  where real transactions belong -- see scope reduction #5), full mutation completeness including
  reflinks and sparse files (Phase D), snapshots (Phase E), recovery/health (Phase F), and system
  integration (Phase G). This report covers Phase A only; RFC-0039's own Status remains `Draft`,
  not `Implemented` -- unlike RFC-0036/37/38, this RFC is far too large for one phase's completion
  to warrant that status change.
- **Subdirectory depth is unbounded in principle** (Resolve walks arbitrarily many components) but
  the 64-object/128-namespace-row capacity bounds how much can actually exist at once -- a real
  ceiling worth knowing before building anything larger on top of Phase A directly.
- **No RFC-0038 `BlockDevice`/`FilesystemProvider` integration yet.** Phase A is deliberately
  disk-free; connecting this object model to RFC-0038's now-proven RAM-disk/FAT32 attachment
  substrate is explicitly Phase B+ work, not attempted here.
- **The `frame_size > 4095` ceiling and the post-`ExitBootServices` `ConsoleOut.Write` hang are
  both genuinely useful facts for whoever builds Phase B's own QEMU fixture** (which will likely
  need at least as much test-orchestration code as this one) -- both are documented here and in
  this fixture's own comments specifically so they don't need rediscovering.
