# QEMU Harness: Explicit RAM, Empirically-Verified Ceiling

## Finding

While scoping RFC-0042 Phase Q (production-scale ArcFS capacities), a real, load-bearing constraint
surfaced: this project's own QEMU/OVMF test harness scripts (`arcology-os/scripts/run/run-uefi-
hello*.sh`, `run-arcology-hardware-image.sh`) never passed an explicit `-m` flag, meaning every
fixture in this project's history has run under whatever RAM QEMU defaults to for the `pc`
(i440fx) machine type with no flag given.

That default was previously ASSUMED to be around 128 MiB -- `kInterruptPendingTableAddress`'s own
header comment in `src/compiler/fission.cpp` already says the address was chosen "kept well under
128 MiB -- the smallest RAM size a test harness might run this table under with no explicit QEMU
-m flag." This was a reasonable, working assumption, but never actually measured.

**Confirmed empirically, not assumed**: a probe fixture wrote a known 64-bit pattern to a range of
physical addresses (16 MiB through 1000 MiB, identity-mapped by the fixture's own page tables up to
1 GiB) and read it back. Every address at or above exactly 128 MiB read back as 0 (unbacked memory,
the same "silent zero" behavior this project has already documented once for a single fixed
address, now confirmed as the real ceiling on the WHOLE default RAM allocation) -- every address
below 128 MiB round-tripped correctly. The prior assumption was exactly right, now proven rather
than inherited.

## Why this blocks Phase Q

RFC-0042 Section 11's own production-scale ArcFS capacities do not fit inside 128 MiB once actually
computed -- the Data Pool alone (`ArcFSMaxObjects() * ArcFSMaxFileChunks() * ArcFSFileCapacityBytes()`)
is multiplicative in two of the RFC's own target constants, and reaches several times 128 MiB even
under conservative revised targets (see `.agents/reports/aps-arcfs-phase-q.md` for the full
capacity-target reasoning).

## Fix

Every harness script now passes `-m 512` explicitly -- confirmed empirically the same way (the same
probe fixture, re-run against the modified harness, correctly round-trips every address up through
512 MiB and correctly fails at 768 MiB and above). This is a deliberate, documented infrastructure
change, not an incidental bump:

- Applies uniformly to all 5 scripts (`run-uefi-hello.sh` and its `-with-preload`/`-with-gpu`/
  `-with-blockio-disk` variants, plus `run-arcology-hardware-image.sh`) so every fixture in this
  project shares one RAM budget going forward, not just the new Phase Q ones.
- 512 MiB leaves comfortable headroom above Phase Q's own real live-table footprint (tens of MB,
  dominated by the Data Pool) plus the existing RAMDisk region, without being excessive for a
  CI/dev environment.
- Full regression suite re-run clean after the change (81/81) -- confirms giving every fixture more
  RAM than it strictly needs is harmless, not just theoretically safe.

## Validation

- Probe fixture (not a permanent project fixture, a one-off diagnostic): empirically confirmed the
  128 MiB default ceiling before the fix, and the 512 MiB real ceiling after it, via real
  write/read-back round trips at 23 distinct physical addresses spanning 16 MiB to 1000 MiB.
- Full regression suite (81/81) re-run clean after modifying all 5 harness scripts.

## Documented scope note

This changes test *infrastructure* only -- no ArcoBASIC compiler, ArcFS format, or on-disk behavior
changed. It is a prerequisite for RFC-0042 Phase Q's own capacity work, not part of Phase Q's own
RFC requirements (Section 11), and is reported separately from that phase's own report for exactly
that reason.
