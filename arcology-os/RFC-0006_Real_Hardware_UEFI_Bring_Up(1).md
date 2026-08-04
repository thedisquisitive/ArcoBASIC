
# RFC-0006: Real Hardware UEFI Bring-Up

**RFC Number:** RFC-0006  
**Title:** Real Hardware UEFI Bring-Up  
**Status:** Draft  
**Category:** Platform / Hardware Validation  
**Authors:** Arcology Project  
**Created:** 2026-08-03  
**Related RFCs:** RFC-0000, RFC-0004, RFC-0005

---

# 1. Executive Summary

This RFC defines the process for validating that Arcology boots on real x86-64 UEFI hardware.

QEMU is a development and regression environment. Physical hardware validation is the authoritative milestone.

---

# 2. Motivation

Real firmware implementations differ in subtle and sometimes undocumented ways. Arcology should discover those differences early instead of assuming QEMU behavior reflects the real world.

---

# 3. Goals

- Produce a reproducible bootable UEFI image.
- Boot on the designated Arcology test laptop.
- Display a unique validation banner.
- Enter a stable halted state after successful execution.
- Build institutional knowledge about firmware behavior.

---

# 4. Non-Goals

This RFC does not define:

- ExitBootServices()
- Native Arcology boot stages
- Graphics drivers
- Native keyboard drivers
- Secure Boot signing
- Legacy BIOS support

---

# 5. Reference Test Platform

The project maintains a dedicated hardware validation laptop.

Unlike a contributor's everyday computer, this machine exists specifically to validate operating system development and **may intentionally have its internal storage modified, repartitioned, reformatted, or replaced as required by future milestones.**

Its purpose is to be a sacrificial development platform.

Contributors testing on personal machines SHOULD continue using removable media and avoid modifying internal storage.

---

# 6. Boot Media

The build system SHALL produce deterministic boot media suitable for x86-64 UEFI firmware using the standard removable-media layout.

Preparing boot media should require as few manual steps as possible.

---

# 7. Boot Procedure

1. Build the UEFI target.
2. Prepare the boot media.
3. Boot the reference laptop.
4. Observe the Arcology validation banner.
5. Verify the expected behavior.
6. Record the results.

---

# 8. Expected Output

A successful run SHOULD display an unmistakable validation marker, such as:

ARCOLOGY HARDWARE TEST

Hello from ArcoBASIC.

System halted intentionally.

The wording may evolve while remaining clearly identifiable.

---

# 9. Failure Classification

Every failure SHALL be categorized before investigation:

- Media not detected
- EFI image rejected
- Secure Boot rejection
- Black screen
- Reset or crash
- Unexpected return to firmware
- Partial output
- Successful execution with intentional halt

---

# 10. Evidence Collection

Each hardware run SHOULD record:

- Git commit
- Compiler version
- Laptop model
- CPU
- Firmware vendor/version
- Secure Boot state
- Media preparation method
- Photographs
- Observed behavior
- Failure category (if applicable)

---

# 11. Safety

For the dedicated Arcology hardware:

- Internal storage MAY be modified as required.
- Experimental boot chains MAY replace previous Arcology builds.
- Reinstallation is expected.

For non-dedicated contributor systems:

- Prefer removable media.
- Avoid modifying firmware configuration permanently.
- Avoid touching internal operating systems unless explicitly intended.

---

# 12. Developer Experience

The hardware validation workflow should be:

Build
→ Prepare Media
→ Boot Hardware
→ Observe
→ Record Result

The process should be simple enough that contributors without firmware expertise can reproduce it.

---

# 13. AI Implementation Guidance

Agents SHALL treat QEMU as regression testing only.

Hardware compatibility SHALL NOT be claimed without documented physical validation.

---

# 14. Appendix A: Firmware Quirk Catalog

Every discovered firmware quirk SHOULD be documented instead of silently worked around.

Each entry should include:

- Vendor
- Model
- Firmware version
- Symptoms
- Root cause (if known)
- Resolution
- First fixed version
- References to commits or work packages

Example:

Vendor: Lenovo

Model: ThinkPad T480

Issue:
Unsigned EFI image rejected despite Secure Boot being disabled.

Resolution:
Corrected PE header alignment.

Status:
Resolved in ArcoFission 0.x.

Over time this appendix becomes the project's institutional knowledge base for firmware compatibility.

---

# 15. Definition of Done

This RFC is complete when:

1. A reproducible boot artifact is generated.
2. The reference laptop boots the artifact.
3. The validation banner appears.
4. Execution reaches the intentional halt state.
5. A hardware validation report is committed.

---

# 16. Revision History

| Version | Date | Summary |
|---------|------|---------|
|0.2|2026-08-03|Added dedicated test hardware policy and Firmware Quirk Catalog appendix.|
