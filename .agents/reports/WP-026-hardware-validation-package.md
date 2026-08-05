# WP-026: Physical Hardware Validation Package

**Status:** READY FOR HUMAN EXECUTION — NOT YET VALIDATED
**Date prepared:** 2026-08-04

Do not mark this report complete from QEMU results. Fill every bracketed field during a physical
test and attach photographs using repository-relative paths.

## Build Identity

- Git commit: `de3c6ac` (`arcology: implement deterministic hardware bring-up`)
- Dirty-tree statement at physical test: `[required; use a clean checkout of the commit above]`
- Compiler/version: `ArcoFission 0.1.0`
- Image filename: `arcology-seed-0.1-x86_64.img`
- EFI SHA-256: `ea9f2bd5bffd26bcfe57b825c8c82b9d5aca9bb01eef0c7a0322463db643542b`
- Image SHA-256: `a64394c421f25ea545c0cefc06ee55b99acb0ccc7f92faecf2e0ad48502e447d`
- Media write command/tool: `[required]`

## Test Platform

- Laptop manufacturer/model: `[required]`
- CPU: `[required]`
- Firmware vendor: `[required]`
- Firmware version/date: `[required]`
- UEFI mode enabled: `[yes/no]`
- Legacy/CSM state: `[enabled/disabled/unavailable]`
- Secure Boot state: `[enabled/disabled]`
- Removable-media make/model/capacity: `[required]`

## Checklist

- [ ] `sha256sum -c SHA256SUMS` passes before writing media.
- [ ] The selected block device was independently confirmed as removable.
- [ ] Firmware detects the media.
- [ ] Firmware loads `EFI/BOOT/BOOTX64.EFI`.
- [ ] `ARCOLOGY HARDWARE TEST` appears.
- [ ] Blank line and `Hello from ArcoBASIC` appear.
- [ ] Blank line and `System halted intentionally.` appear.
- [ ] The display remains stable for at least 60 seconds.
- [ ] The application does not return to firmware or reset.
- [ ] Power cycle exits the halt normally.

## Observation

- Start time/timezone: `[required]`
- Failure classification (RFC-0006 section 9): `[required]`
- Exact observed behavior: `[required]`
- Stable-halt duration: `[required]`
- Unexpected behavior: `[none or details]`
- Photograph paths: `[required]`
- Tester name/identifier: `[required]`

## Firmware Quirk Decision

- Quirk observed: `[yes/no]`
- If yes, RFC-0006 Appendix A entry: `[link/section]`
- Root cause evidence: `[required if claimed]`
- Resolution/workaround: `[required if applied]`

## Sign-Off

- [ ] All fields above are complete.
- [ ] Evidence corresponds to the checksummed artifact named above.
- [ ] No result was inferred from QEMU.
- Human validator/signature: `[required]`
- Date: `[required]`
