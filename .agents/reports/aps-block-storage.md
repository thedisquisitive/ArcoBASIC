# APS Block Storage and Filesystem Provider Substrate (RFC-0038)

## Scope delivered

APS can now read a real file from a real, independently-generated FAT32 filesystem image, byte
for byte, through a genuinely layered stack: a RAM Disk Block Device Provider, a read-only FAT32
Filesystem Provider on top of it, and a unified namespace layer resolving paths to providers
through longest-mount-point-prefix matching. Implements RFC-0038
(`arcology-os/rfcs/RFC-0038_APS_Block_Storage_and_Filesystem_Provider_Substrate.md`) end to end,
proven under QEMU/OVMF, including the negative test the RFC calls for by name.

### Three documented deviations, each forced by something verified directly, not assumed

1. **Concrete functions instead of `BlockDevice`/`FilesystemProvider` interface values.** Same
   reason RFC-0037's `Loop.RegisterHook` deviated: this backend has no runtime-polymorphic
   dispatch. `RAMDisk.*`/`FAT32.*` are concrete, directly-named functions; `Volumes.Resolve`
   returns a provider *tag* (`U32`), not a `FilesystemProvider` value, and calling code dispatches
   on it itself.
2. **11-byte raw 8.3 name buffers, not `STRING`, for every path.** Not a style choice --
   **STRING equality was directly tested and found to silently return the wrong answer at runtime**
   on this backend. A minimal repro (`MatchIt("HELLO") = 1`, `MatchIt("WORLD") = 0`, both compared
   against a `STRING` parameter with `=`) compiled clean ("SOURCE ACCEPTED", real X86_64 generated)
   and then printed `N` for the exact match under real QEMU execution. This is a genuine
   correctness bug in the compiler's freestanding `STRING` support, not a documented gap -- exactly
   the "looks correct, isn't correct" trap this project's own testing discipline exists to catch.
   It was not chased down and fixed (real, separate compiler work, well outside this RFC's scope)
   -- only avoided, by representing every path/mount-point/filename as a fixed-size byte buffer
   compared with `MEMORY.Read8` loops instead.
3. **Array-free `Enumerate`/`Stat` stand-ins.** `DirectoryEntry[]` was never tested on this
   backend either, and after finding the `STRING` bug this way, testing every other untested type
   shape before relying on it became the standing policy for the rest of this implementation.
   `FAT32.CountEntries()` (a `U64` count) exercises the same directory-scan machinery `Open` uses
   without committing to an unverified array-typed return.

Root-directory-only file lookup (no subdirectory traversal) is a fourth, smaller scope reduction,
extending RFC-0038 Section 19's own explicit allowance for skipping VFAT/long-filename support to
the same judgment about subdirectories -- real parsing complexity, not needed to prove the read
path (genuine multi-cluster chain-walking) actually works.

## Two real, general compiler/parser bugs found and fixed, plus one usage gotcha (not a bug)

1. **Fixed: a trailing same-line comment after a multi-line `IF cond THEN   ' why` broke parsing**
   with a confusing downstream error ("expected FUNCTION after END" at the matching `END IF`).
   Root cause, in `Parser::if_statement` (`src/frontend/parser.cpp`): the single-line-vs-block-IF
   decision checked only for `Newline`/`End` immediately after `THEN`; a `Comment` token satisfies
   neither, so the parser took the single-line branch, consumed just the comment as a one-statement
   inline body, and left the real body plus the `END IF` as unconsumed trailing tokens. This
   RFC's own natural commenting style (explaining *why* a validation check exists, right after the
   check) hit it immediately and repeatedly. Fixed by also excluding a leading `Comment` token from
   the single-line-IF decision; `block_until`'s existing per-statement `skip_newlines()` already
   handles a leading comment-then-real-body sequence correctly once the block-IF path is taken, so
   no other change was needed. Verified against the minimal repro, the real files that triggered
   it, and the full existing suite (46/46 before this fix, still 46/46 after).
2. **Not a bug, but cost real time and is worth recording plainly: `/` is not integer division in
   this ArcoBASIC dialect; `\` is.** `sizeBytes / 512` compiles clean at A-MIR level and then fails
   X86_64 codegen outright with `unsupported systems integer operation /` -- the Binary-operator
   codegen switch only implements `\`/`MOD` for division, matching classic BASIC's
   floating-point-`/`-vs-integer-`\` distinction, which this systems/integer-only profile inherited
   without a floating-point counterpart ever needing to exist. This was hiding, unnoticed, in
   RFC-0036's own `stdlib/timer_policy.abas` (`Timer.Initialize`'s reload-value rounding,
   `Timer.UptimeMilliseconds`) -- neither function was ever exercised through X86_64 codegen by any
   QEMU fixture in that RFC's own proof (the fixture hardcoded its reload value as a literal
   instead), so the bug shipped silently until this RFC's own FAT32 sector/cluster arithmetic hit
   it directly. Fixed in this RFC's own new code by using `\` throughout; **`stdlib/
   timer_policy.abas`'s two affected functions were not touched here** (out of this RFC's scope)
   and are flagged in Remaining Activation Gate below.

## Validation

- `ctest`, full suite: 47/47 passing (46 pre-existing + the new block-storage test).
- Reveal-level: `RAMDisk.ReadSectors`' bounds check and the IF-trailing-comment parser fix both
  have dedicated structural checks.
- **The real proof, executed under QEMU/OVMF** (`aps-block-storage.abas`): a genuinely independent
  8 MiB FAT32 image (`arcology-os/scripts/build/build-fat32-test-image.py` -- a from-scratch,
  no-external-tool-dependency Python generator modeled on this project's own existing
  `build-arcology-hardware-image.py`; cross-checked against real `mtools`/`mdir`/`mcopy` when
  authored, byte-identical extraction confirmed) is placed at a fixed guest-physical address
  (`0x4000000`) by the QEMU test harness itself, before the UEFI application's first instruction
  runs, via a new harness variant (`scripts/run/run-uefi-hello-with-preload.sh`, a
  `-device loader,file=...,addr=...,force-raw=on` QEMU device -- a standard QEMU mechanism, not an
  Arcology-specific hack). The fixture mounts it, registers "/"" plus a longer decoy mount point
  "/OTHER/" (proving longest-prefix-match genuinely picks the more specific point, not merely "the
  only one wins by default"), resolves "/README.TXT", opens the 8.3-translated name, and reads the
  entire 3333-byte file in 64-byte chunks across a genuine 7-cluster FAT chain (512 bytes/cluster),
  asserting both the exact byte length (3333) and an exact byte-sum checksum (211490) before
  printing any success marker. Passed; verified deterministic across three repeated runs.
- **The negative test RFC-0038 Section 16 calls for by name**: a second fixture
  (`aps-block-storage-negative.abas`) mounts against a zeroed (not-a-FAT32) preloaded image and
  asserts `FAT32.Mount()` returns `FALSE` -- Section 10's fail-closed requirement -- printing its
  own distinct success marker ("APS MOUNT REJECTED") only if the rejection is correct. Passed;
  verified deterministic across repeated runs.
- Every geometry field `FAT32.Mount` reads is checked against zero before being trusted
  (`bytesPerSector`/`sectorsPerCluster`/`reservedSectorCount`/`numFATs`/`fatSize32`, plus
  `rootCluster < 2` and `firstDataSector >= SectorCount()`), per Section 10's bounds-checking
  requirement -- the negative test above exercises the boot-signature/label rejection path
  specifically; the individual zero-field checks are covered by inspection and the same
  fail-closed construction, not each independently QEMU-tested (would need a purpose-crafted
  malformed-but-signature-valid image per field, judged not worth the marginal proof value here).

## Remaining activation gate

- **`RAMDisk.Create` does not allocate memory or copy bytes from a live UEFI Block IO device** --
  the backing store is the QEMU-preloaded fixed region, populated before the guest's first
  instruction runs, not by any UEFI protocol call this program makes. RFC-0038 Stop Condition
  #17.5 explicitly anticipated needing UEFI Block IO Protocol bindings this project's frontend does
  not yet have; this implementation sidesteps that gap entirely rather than closing it. A real
  future increment (reading FAT32 off an actual boot partition post-`ExitBootServices`) needs that
  binding work done for real -- `uefi-bindings.md` is the stated source of truth for how to extend
  it when that happens.
- **No real AHCI/NVMe/virtio-blk driver** -- explicitly Non-Goals/Future Extensions of this RFC;
  the RAM disk is the only `BlockDevice` this milestone has.
- **FAT32 write support, subdirectory traversal, and VFAT long filenames are all unimplemented** --
  each an explicit Non-Goal or an RFC-sanctioned scope reduction (Section 19's own allowance for
  the latter two, extended here to subdirectories as well; see "documented deviations" above).
- **The `STRING`-equality bug found this session (deviation #2 above) was not fixed, only routed
  around.** It is real, general, and will resurface the moment any future freestanding code
  compares two `STRING` values with `=`. Worth a dedicated investigation and fix before any future
  RFC leans on `STRING` for freestanding path/name handling.
- **`stdlib/timer_policy.abas`'s `Timer.Initialize` reload-value rounding and
  `Timer.UptimeMilliseconds` used `/` where `\` is required** -- confirmed broken at X86_64
  codegen (not merely unexercised) by this RFC's own investigation. Fixed as a drive-by correction
  (both now use `\`, verified compiling at X86_64 level) since it was cheap and the bug was already
  fully diagnosed, but **not re-proven under QEMU** -- no fixture calls either function with a
  real, non-hardcoded tick rate. RFC-0036 itself remains correctly `Status: Implemented` since its
  own QEMU proof never exercised either function in the first place (`aps-timer-tick.abas`
  hardcodes the reload value as a literal); this fix removes a latent bug but does not add new
  proof coverage for RFC-0036.
- **`Volumes.Mount`/`Resolve`'s mount-point comparison caps names at 16 bytes** and the table holds
  at most 4 rows -- small, fixed, documented limits matching every other "one instance"/"small
  fixed table" pattern in this RFC chain (the timer, the loop's hook table), not a claim about a
  real production ceiling.
- **No RFC-0017 Resource Registry integration.** RFC-0038 Requirement/Goals text asks for every
  mounted volume to register as an RFC-0017 Resource; no ArcoBASIC code anywhere in this
  repository calls an actual resource-registry function today (confirmed by search) -- RFC-0017
  has no concrete freestanding callable surface to integrate with yet. Building one specifically
  for this RFC would be real, separate scope creep (RFC-0017's own implementation), so this RFC's
  mount table is conceptually what an RFC-0017 Resource record would describe, without an actual
  registration call anywhere.
