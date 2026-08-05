# WP-020: Repository Verification

**Mission:** Agent Packet 002 — Arcology Seed: Real Hardware Bring-Up
**Packet:** `arcology-os/Agent_Packet_002_Arcology_Seed_Real_Hardware_Bring_Up.md`
**Baseline commit:** `b52e6b8`
**Date:** 2026-08-04

## Status

Complete.

## Objective

Reproduce the existing ArcoBASIC UEFI x86-64 milestone from a clean checkout before beginning
Packet 002 implementation.

## Isolation

The verification used a fresh local clone in a directory created by `mktemp -d`. This ensured that
the build did not consume the existing build tree, untracked Arcology RFCs, or unrelated modified
Lazarus files in the working checkout. The temporary clone and generated artifacts were removed
automatically after the run.

## Commands

```sh
git clone --no-local . "$CLEAN_REPOSITORY"
cmake -S "$CLEAN_REPOSITORY" -B "$CLEAN_REPOSITORY/build"
cmake --build "$CLEAN_REPOSITORY/build" -j"$(nproc)"
ctest --test-dir "$CLEAN_REPOSITORY/build" --output-on-failure

"$CLEAN_REPOSITORY/build/ArcoFission" build \
    "$CLEAN_REPOSITORY/tests/systems/uefi-hello/hello.abas" \
    -o "$TEMPORARY_ROOT/hello.efi" \
    --target uefi-x86_64

file "$TEMPORARY_ROOT/hello.efi"
"$CLEAN_REPOSITORY/scripts/run-uefi-hello.sh" \
    "$TEMPORARY_ROOT/hello.efi" \
    "Hello from ArcoBASIC" \
    20
```

`CLEAN_REPOSITORY` and `TEMPORARY_ROOT` above name paths under the isolated `mktemp -d`
directory; they are descriptive placeholders rather than persistent environment requirements.

## Results

- Clean configure and build: PASS.
- Full regression suite: PASS, 12/12 tests.
- QEMU/OVMF integration test within CTest: PASS.
- EFI image generation: PASS, 12,288 bytes.
- Independent file identification: `PE32+ executable for EFI (application), x86-64, 2 sections`.
- Direct QEMU/OVMF run: PASS, `PASS: Hello from ArcoBASIC`.

## Acceptance Criteria

- Clean checkout: PASS.
- Build current milestone: PASS.
- Verify all existing tests: PASS, 12/12.
- Verify QEMU/OVMF boot: PASS.

## Existing Conditions

- The Packet 002 RFCs currently live in the untracked `arcology-os/` directory and therefore were
  intentionally absent from the clean baseline clone. They must be normalized and tracked before
  Packet 002 can be reproduced from Git.
- Unrelated Lazarus modifications and untracked assets remain present in the main working checkout.
  WP-020 did not read, modify, stage, or remove them.

## Stop Condition

Not triggered. The existing milestone is reproducible.

## Next Safe Work

Resolve the Packet 002 specification conflicts identified during review, then implement the
RFC-0012 frontend-to-A-MIR contract as an explicit prerequisite before adding hardware semantics.
