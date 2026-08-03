```text
STATUS
Complete

OBJECTIVE
Build and run the complete hello-world boot application (Packet WP-011).

SUMMARY
Verified every item in the Definition of Done on a genuinely fresh checkout (`git clone` of the
local repository at commit d23cecc into an isolated directory), not the incrementally-built working
directory used throughout WP-000 through WP-010 -- the first time this mission's work has been
validated from a truly clean starting point rather than an accumulated build tree.

Along the way, an optimized (RelWithDebInfo/Release) build of `arco_tests` segfaulted inside
`shell/arcosh.cpp`'s `Process.Exists` implementation. Root-caused via `coredumpctl`/`gdb` to a
corrupted `arco::Value::get_property("Name")` call while enumerating `/proc`. Confirmed via a
control experiment (checking out and building the pre-mission baseline commit `a4c82e2` the same
way) that this is a **pre-existing bug, not introduced by this mission** -- it reproduces
identically on code that predates WP-000. It also does not reproduce with the project's standard,
unspecified build type (the configuration used for every test run in every prior work package's
report), only with explicit optimization enabled, and appears to depend on the host's current
process list. Per the Packet's scope boundaries (this is `shell/arcosh.cpp`, an unrelated
subsystem), it was documented (`.agents/reports/WP-000-repository-audit.md` section 7b) rather than
fixed, and the end-to-end verification below uses the project's standard build configuration, which
does not exhibit it.

FILES CHANGED
.agents/reports/WP-000-repository-audit.md (documented the pre-existing Release-mode crash finding)
.agents/reports/WP-011-end-to-end-milestone.md (this report)

No source code, test, or documentation changes beyond the audit addendum -- WP-011 is a
verification work package; every prior work package already made the changes being verified here.

PUBLIC BEHAVIOR
None. This work package verifies existing behavior; it does not change any.

VERIFICATION PERFORMED (on a fresh `git clone`, not the working directory)
1. Clean configure + build (project's standard invocation, `cmake .. && cmake --build .`): succeeded
   with zero warnings and zero errors.
2. Full ctest suite: 12/12 passed --
   arco_runtime_tests, arcosh_alpha_smoke, arcofission_alpha_smoke (pre-existing, unchanged),
   systems_fixed_width_types_smoke, systems_freestanding_profile_smoke,
   systems_amir_primitives_smoke, systems_calling_convention_smoke, systems_uefi_bindings_smoke,
   systems_utf16_encoding_smoke, systems_x86_64_codegen_smoke, systems_pe_image_smoke,
   systems_qemu_ovmf_harness_smoke (new, from this mission).
3. `ArcoFission build tests/systems/uefi-hello/hello.abas -o hello.efi --target uefi-x86_64`:
   produced a 12288-byte PE32+ EFI application.
4. `scripts/run-uefi-hello.sh hello.efi "$(cat tests/systems/uefi-hello/expected-output.txt)"`:
   printed exactly `PASS: Hello from ArcoBASIC` -- the real UEFI console output, byte-for-byte
   matching the required output, verified via real QEMU/OVMF boot, not a simulation.
5. `ArcoFission reveal tests/systems/uefi-hello/hello.abas at X86_64`: produced the deterministic
   disassembly-style dump (hex bytes for `.text`/`.rdata` plus the relocation list) confirming
   generated assembly/disassembly is available for inspection without any extra tooling.
6. Confirmed `tests/systems/uefi-hello/hello.abas` (and its identical copy `examples/uefi_hello.abas`)
   contains only ArcoBASIC source -- no `.asm`, `.s`, `.c`, or `.cpp` sidecar files exist alongside
   it, and the fixture directory contains only `hello.abas` and `expected-output.txt`.

ACCEPTANCE CRITERIA (Packet WP-011)
clean checkout builds successfully: PASS
all pre-existing passing tests remain passing: PASS (with the project's standard build
  configuration; see SUMMARY for the unrelated, pre-existing, optimization-only finding that does
  not affect this configuration)
new systems tests pass: PASS (9/9 systems_* tests)
EFI image is generated: PASS
QEMU/OVMF runs it: PASS
expected output appears: PASS (exact match)
no handwritten assembly exists in the example: PASS
no C exists in the example: PASS
generated assembly or disassembly is available for inspection: PASS

ASSUMPTIONS
- "Clean checkout" is interpreted as a fresh `git clone` at the current HEAD, built with the
  project's own standard/default CMake invocation (no explicit `-DCMAKE_BUILD_TYPE`), since that is
  the configuration this repository has always been built and tested with throughout every prior
  work package, and the one its own build scripts use. Testing with an explicit
  `-DCMAKE_BUILD_TYPE=Release` was additional, optional rigor beyond what "clean checkout" strictly
  requires, and is what surfaced the pre-existing, unrelated finding described above.

DEVIATIONS
None.

REGRESSIONS
None. The Release-mode crash is not a regression (proven pre-existing via the baseline-commit
control test) and does not occur under the project's standard build configuration.

RISKS
- The pre-existing `Process.Exists` crash under optimized builds remains unfixed, by design (out of
  this mission's scope). Anyone building this project with an explicit `-DCMAKE_BUILD_TYPE=Release`
  or `RelWithDebInfo` in the future should be aware `arco_runtime_tests` may fail non-deterministically
  for reasons unrelated to whatever they are working on -- documented in WP-000's report specifically
  so this is discoverable rather than rediscovered from scratch.

NEXT SAFE WORK PACKAGE
WP-012: Documentation and Handoff. No architectural decision blocks starting it -- every prior work
package (WP-000 through WP-011) already has a report documenting its own decisions, tests, and
risks; WP-012's job is to consolidate this into the prerequisites/build/test/troubleshooting
documentation Packet section 12 requires, mapping conceptual paths to the actual repository
locations recorded throughout this directory.
```
