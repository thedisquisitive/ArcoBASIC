```text
STATUS
Complete

OBJECTIVE
Implement the calling convention required by x86-64 UEFI (Packet WP-005).

SUMMARY
Since no backend (register allocator, instruction encoder) exists yet in this codebase (confirmed
in WP-000, unchanged through WP-004), this work package builds the Microsoft x64 calling-convention
model as a standalone, pure, header-only module (include/arco/calling_convention.hpp) that WP-008
will consume once it exists, rather than bolting ABI logic onto anything that would need a "public
redesign" (the WP-005 stop condition does not apply: there is nothing existing to redesign).

The model computes, for an ordered list of N integer/pointer-class arguments: which of the first
four use RCX/RDX/R8/R9, and the exact stack byte offset (accounting for the 32-byte shadow space
and the 8-byte return address) for the fifth and beyond. It also records the return register (RAX),
the shadow-space and stack-alignment constants, and the callee-saved/caller-saved register lists.
Scope is explicitly integer/pointer-only (no floating-point argument classification), matching the
Packet's non-goals.

Verification follows the Packet's three explicit categories:
1. "Unit tests for register assignment": added directly to tests/runtime_tests.cpp (0/1/2/4/5/6-
   argument cases, return register, shadow space and alignment constants, and a check that the
   callee-saved and caller-saved register sets are disjoint).
2. "Golden tests for generated assembly or machine-code disassembly": added a new ArcoFission stage,
   `reveal FILE at CALLCONV`, which renders a deterministic textual computation of where every
   function parameter and every external/ABI-bound call site's arguments live -- an honest stand-in
   for real generated assembly, since actual instruction encoding is WP-008's job. Golden-tested in
   tests/systems_calling_convention_smoke.sh.
3. "Comparison against the ABI specification": documented in the new docs/systems/
   calling-conventions.md, which states the well-established Microsoft x64 facts this model
   implements and explicitly flags that real-hardware verification against primary sources remains
   WP-008/WP-010's responsibility (Packet section 9 -- this document restates public ABI facts, it
   does not substitute for checking them again before encoding real instructions).

The CALLCONV stage also demonstrates "nested calls sufficient for UEFI text output": for the
hello-world's Main function, it shows both Main's own parameters (imageHandle : RCX, systemTable :
RDX) and, independently, the ConsoleOut.Write external call site's own argument assignment
(ARG0 : RCX) -- reusing WP-004's CallExternal instruction as the input to this computation.

FILES CHANGED
include/arco/calling_convention.hpp (created)
include/arco/fission.hpp (reveal_callconv/reveal_callconv_file declarations)
compiler/fission.cpp (bare_parameter_name/render_argument_location/render_calling_convention;
  reveal_callconv/reveal_callconv_file)
tools/arcofission_main.cpp (CALLCONV recognized as a reveal stage)
tests/runtime_tests.cpp (calling-convention unit tests)
tests/systems_calling_convention_smoke.sh (created)
CMakeLists.txt (registered the new test)
docs/systems/calling-conventions.md (created -- Packet section 6 required artifact)
docs/systems/uefi-target.md (traceability section updated with the new module/CLI stage)
.agents/reports/WP-005-calling-convention.md (this report)

PUBLIC BEHAVIOR
New: `arcofission reveal FILE at CALLCONV` (aliases: `calling-convention`). No existing behavior
changed -- this is a new, additive reveal stage alongside AST/A-MIR/BYTECODE.

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings
./build/arco_tests (direct run)  -> exit 0, all require() assertions passed including the new
  calling-convention unit tests
ctest --test-dir build --output-on-failure -> 7/7 passed
  arco_runtime_tests (includes the new unit tests), arcosh_alpha_smoke, arcofission_alpha_smoke,
  systems_fixed_width_types_smoke, systems_freestanding_profile_smoke,
  systems_amir_primitives_smoke: unchanged, still passing
  systems_calling_convention_smoke: new, passing
Manual verification beyond the automated suite:
  - examples/uefi_hello.abas's CALLCONV reveal shows imageHandle:RCX, systemTable:RDX,
    RETURNS RAX (U64), and the ConsoleOut.Write call site's ARG0:RCX.
  - A six-parameter function correctly shows a:RCX b:RDX c:R8 d:R9 e:STACK+40 f:STACK+48.

ACCEPTANCE CRITERIA
Correct argument registers: PASS (RCX/RDX/R8/R9 in order, verified by unit test and golden test)
Correct return register: PASS (RAX)
Correct stack alignment: PASS (16-byte at CALL / RSP%16==8 at entry, documented and constant-tested)
Shadow space where required: PASS (32 bytes, always; documented and constant-tested)
Preserved registers: PASS (callee-saved/caller-saved lists, disjointness unit-tested)
Nested calls sufficient for UEFI text output: PASS (CALL SITES section computes the external call's
  own argument locations independent of the caller function's own parameter locations)

ASSUMPTIONS
- Argument-location assignment considers only position, not declared type, since no
  floating-point/XMM classification is in scope. This is correct for every type in
  include/arco/fixed_width_types.hpp today; it will need real classification logic if a
  floating-point systems type is ever added (currently excluded by Packet non-goals).
- The CALLCONV stage's "golden test for generated assembly" role is a deliberate textual stand-in,
  not real assembly output. Documented explicitly in calling-conventions.md so it is not mistaken
  for WP-008 having already happened.

DEVIATIONS
None from docs/systems/uefi-target.md; this work package did not require any RFC decisions to be
revisited.

REGRESSIONS
None (7/7 tests passing, including all 6 pre-existing tests unchanged).

RISKS
- calling_convention.hpp is not yet wired into anything that actually emits bytes; it is pure
  computation, unit- and golden-tested but not yet load-bearing for a real build. WP-008 is where it
  gets consumed for real; until then this work package's value is specification plus verification
  infrastructure, not working machine code.
- The CALL SITES model does not account for the hidden `This` pointer real UEFI protocol methods
  take in the underlying C ABI (see calling-conventions.md "What This Work Package Does Not Do").
  WP-006 will need to decide how that gets represented once it defines the real protocol shapes --
  it may need to inject an implicit first argument before calling assign_argument_locations.

NEXT SAFE WORK PACKAGE
WP-006: UEFI Bindings. No architectural decision blocks starting it; the CALL SITES risk above
(implicit This pointer) is the one thing WP-006 should resolve explicitly rather than guess at.
```
