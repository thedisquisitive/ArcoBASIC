
# Agent Packet 002
## Arcology Seed: Real Hardware Bring-Up
### Revision 1.0

---

# Mission Commander's Intent

The objective of this mission is confidence, not feature count.

A smaller implementation that boots reliably on physical hardware is preferred over a larger implementation that introduces new capabilities without validation.

When forced to choose, prioritize:

1. Determinism
2. Reproducibility
3. Architectural cleanliness
4. Clear diagnostics
5. New functionality

---

# Mission

Transform the existing UEFI "Hello from ArcoBASIC" milestone into the first reproducible Arcology Seed hardware validation artifact.

This mission is **not** about expanding the language. It is about proving the compiler, runtime, and build pipeline function correctly on real hardware.

---

# Governing RFCs

- RFC-0000 – RFC Process
- RFC-0004 – Arcology Seed 0.1
- RFC-0005 – ArcoBASIC Hardware Semantics
- RFC-0006 – Real Hardware UEFI Bring-Up
- RFC-0007 – ArcoBASIC Interactive Program Model
- RFC-0012 – ArcoFission Frontend → A-MIR Contract

---

# Mission Objectives

The implementation SHALL:

1. Preserve existing QEMU functionality.
2. Produce deterministic USB-bootable media.
3. Implement `CPU.Halt`.
4. Implement `CPU.HaltForever`.
5. Lower hardware semantics through A-MIR.
6. Boot on the designated Arcology test laptop.
7. Display the Arcology validation banner.
8. Enter an intentional halted state.
9. Produce complete validation documentation.

---

# Explicit Non-Goals

Do **NOT** implement:

- IF / ELSE
- FOR / NEXT
- WHILE
- Arrays
- Classes
- Objects
- ArcFS
- Users
- Sessions
- Packages
- Prism
- Native executable loader
- ExitBootServices()
- New runtime systems

If a task appears to require one of these, stop, document why, and report it.

---

# Work Packages

## WP-020 – Repository Verification

### Tasks

- Clean checkout
- Build current milestone
- Verify all existing tests
- Verify QEMU/OVMF boot

### Deliverable

Baseline verification report.

### Stop Condition

If the existing milestone cannot be reproduced.

---

## WP-021 – Hardware Semantic Infrastructure

Implement:

```basic
CPU.Halt
CPU.HaltForever
```

Requirements:

- Semantic operations only
- A-MIR representation
- Backend lowering
- Documentation
- No exposed inline assembly

---

## WP-022 – x86-64 Backend Lowering

Implement backend lowering for:

- CPU.Halt
- CPU.HaltForever

The backend owns machine instruction selection.

No parser changes.

---

## WP-023 – Hardware Test Program

Produce a minimal UEFI ArcoBASIC application displaying:

```text
ARCOLOGY HARDWARE TEST

Hello from ArcoBASIC

System halted intentionally.
```

Terminate using:

```basic
CPU.HaltForever
```

---

## WP-024 – Boot Artifact

Produce deterministic USB-ready boot media.

Requirements:

- Reproducible
- Documented
- Suitable for hardware testing

---

## WP-025 – Regression Validation

Verify the identical artifact under:

- QEMU
- OVMF

Confirm:

- Banner appears
- Intentional halt
- No regressions

---

## WP-026 – Hardware Validation Package

Prepare (do not fabricate) a hardware validation package containing:

- Laptop model
- Firmware version
- CPU
- Secure Boot status
- Test checklist
- Requested photographs
- Observation template

Human validation is required.

---

## WP-027 – Firmware Quirk Documentation

Update RFC-0006 Appendix A whenever hardware quirks are discovered.

Document:

- Vendor
- Model
- Firmware version
- Symptoms
- Root cause (if known)
- Resolution
- Related commits

---

## WP-028 – Final Validation Report

Produce:

- Summary
- Completed work
- Remaining issues
- Known limitations
- Recommendations
- Commit list

---

# Coding Rules

The implementation SHALL:

- Consume the authoritative AST
- Generate A-MIR from semantic analysis
- Lower through architecture backends
- Avoid duplicated parsing
- Keep hardware semantics architecture-independent

---

# AI Rules

Agents SHALL:

- Never fabricate hardware results
- Never silently skip work packages
- Never broaden mission scope
- Stop immediately when blocked
- Explain blockers with RFC references
- Document architectural decisions

---

# Deliverables

- Source code
- Tests
- Documentation
- USB boot artifact
- Hardware validation package
- Final validation report
- Commit history

---

# Definition of Done

Mission completion requires:

- Existing milestone preserved
- CPU.Halt implemented
- CPU.HaltForever implemented
- A-MIR updated
- x86-64 backend updated
- USB artifact generated
- QEMU validation passing
- Hardware validation package produced
- Documentation updated
- No unrelated regressions
