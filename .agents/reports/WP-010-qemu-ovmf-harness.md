```text
STATUS
Complete

OBJECTIVE
Create a deterministic automated run harness (Packet WP-010).

SUMMARY
Formalized the ad hoc QEMU/OVMF boot check performed manually while implementing WP-009 into a
reusable, standalone script (scripts/run-uefi-hello.sh): boots a given .efi application under real
QEMU + OVMF firmware and checks its captured console output for an expected string, printing
exactly `PASS: EXPECTED` on success (matching Packet WP-010's preferred result format verbatim) or
failing with captured output on stderr otherwise.

Implements every required behavior: launches QEMU with OVMF (firmware located by searching
/usr/share/ovmf and /usr/share/OVMF for OVMF*.fd, excluding Secure-Boot variants); exposes the
built application via QEMU's virtual-FAT directory driver at EFI/BOOT/BOOTX64.EFI (Packet WP-010
explicitly accepts this as "equivalent" to a real FAT-formatted partition, and it needs no extra
tooling beyond QEMU itself); starts it automatically via the standard UEFI removable-media boot
path (no interactive input or startup.nsh needed); captures serial console output (-vga none forces
ConOut onto the serial port, which a headless harness can actually capture); enforces a timeout via
the `timeout` command wrapping the entire QEMU invocation; and returns nonzero (1 for a genuine
output mismatch, 2 for an environment problem) on any failure.

Implements the environment policy precisely: missing qemu-system-x86_64 or OVMF firmware produces a
clear ERROR message with install commands for Debian/Ubuntu, Fedora, and Arch Linux, and exits 2 --
never a silent download or a confusing downstream QEMU error. tests/systems_qemu_ovmf_harness_smoke.sh
performs the same detection before invoking the harness so a missing tool produces a clean ctest
SKIP rather than a FAIL, since not every environment running the suite will have QEMU/OVMF
installed and their absence is not a defect in ArcoBASIC.

Moved the real boot check out of tests/systems_pe_image_smoke.sh (WP-009's test, which now stays
purely structural and dependency-free, always running regardless of what else is installed) into
the new dedicated tests/systems_qemu_ovmf_harness_smoke.sh, which also proves the harness is not
vacuously passing by running it a second time against a string that is never printed and asserting
that call fails.

FILES CHANGED
scripts/run-uefi-hello.sh (created)
tests/systems_qemu_ovmf_harness_smoke.sh (created)
tests/systems_pe_image_smoke.sh (QEMU section removed, now purely structural)
CMakeLists.txt (registered the new test)
docs/systems/qemu-ovmf-harness.md (created)
docs/systems/pe32-image.md (updated to reflect the test split)
docs/systems/uefi-target.md (traceability section updated)
.agents/reports/WP-010-qemu-ovmf-harness.md (this report)

PUBLIC BEHAVIOR
New: scripts/run-uefi-hello.sh, a standalone developer tool usable outside the test suite. No
existing test or build behavior changed except that systems_pe_image_smoke.sh runs faster now
(0.05s vs ~15s previously) since it no longer boots QEMU itself.

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings (no C++ changes in this work package)
ctest --test-dir build --output-on-failure -> 12/12 passed
  arco_runtime_tests, arcosh_alpha_smoke, arcofission_alpha_smoke, systems_fixed_width_types_smoke,
  systems_freestanding_profile_smoke, systems_amir_primitives_smoke,
  systems_calling_convention_smoke, systems_uefi_bindings_smoke, systems_utf16_encoding_smoke,
  systems_x86_64_codegen_smoke: unchanged, still passing
  systems_pe_image_smoke: unchanged assertions, now faster (structural-only)
  systems_qemu_ovmf_harness_smoke: new, passing (positive case prints exactly
  "PASS: Hello from ArcoBASIC"; negative case with a never-printed string correctly fails)
Manual verification beyond the automated suite:
  - Ran scripts/run-uefi-hello.sh directly and confirmed the exact preferred output string.
  - Exercised the missing-file error path (exit 2, clear message).
  - Exercised the wrong-expected-string failure path (exit 1, captured output on stderr).

ACCEPTANCE CRITERIA (Packet "Required behavior" + "Preferred result")
launch QEMU with OVMF: PASS
expose a FAT-formatted EFI system partition or equivalent test image: PASS (virtual-FAT directory,
  explicitly accepted by the Packet's own wording)
start the generated EFI application: PASS (standard BOOTX64.EFI removable-media boot path)
capture serial or display output where practical: PASS (serial, via -vga none)
enforce a timeout: PASS (`timeout` wraps the QEMU invocation)
return a nonzero status on failure: PASS (1 for output mismatch, 2 for environment problems)
Preferred result "PASS: Hello from ArcoBASIC": PASS (exact string match verified)
Environment policy (document install requirements, provide a detection error, no silent download):
  PASS

ASSUMPTIONS
- OVMF firmware is located by filename pattern search rather than a hardcoded path, since package
  managers install it at different locations (/usr/share/ovmf/OVMF.fd vs /usr/share/OVMF/
  OVMF_CODE_4M.fd on this host, confirmed different across the two directories present here).
  Secure-Boot variants are explicitly excluded since they would reject an unsigned application for
  reasons unrelated to whether the image itself is well-formed.
- Default timeout is 20 seconds, chosen from observed real boot times (the hello-world image reaches
  its output and the firmware proceeds to its next boot option well within a few seconds; the
  remaining time is safety margin, not an expected wait).

DEVIATIONS
None from the Packet.

REGRESSIONS
None (12/12 tests passing, including all 11 pre-existing tests unchanged in behavior).

RISKS
- The harness always boots to whatever OVMF does after the application returns (its next boot
  option, typically the firmware's own UI/setup menu) rather than cleanly powering off; QEMU is
  killed by the timeout rather than exiting on its own. This is intentional and matches "enforce a
  timeout" being a required behavior, but means every harness run takes close to the full timeout
  duration even when the application succeeds quickly. Not a correctness problem, just a fixed
  per-run cost worth knowing about if more programs are tested this way later.

NEXT SAFE WORK PACKAGE
WP-011: End-to-End Milestone. No architectural decision blocks starting it -- this work package's
harness is exactly the tool WP-011 needs to demonstrate the full Definition of Done, and every
prior work package (WP-001 through WP-010) has already been individually verified; WP-011's job is
confirming they compose correctly end to end from a clean checkout, per Packet section 11's
acceptance list.
```
