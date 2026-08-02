```text
STATUS
Complete

OBJECTIVE
Write the minimal implementation specification for the first systems target (Packet WP-001).

SUMMARY
Authored docs/systems/uefi-target.md specifying: target identity (x86-64 UEFI PE32+, no
BIOS/ARM64); the freestanding profile directive set (#PROFILE, #RUNTIME, #CALLCONV, #EXPORT new;
#TARGET reused for architecture selection per explicit user decision, since it already exists with
unused, compatible-in-spirit platform-list semantics); the required fixed-width type contract
(sizes/alignment/signedness) plus the two small grammar decisions needed to express it (LET gains
an optional AS Type clause instead of introducing a competing DIM keyword; parse_type_name is
extended to accept dotted identifiers so UEFI.Handle/UEFI.SystemTable can be written); the Microsoft
x64 / UEFI calling convention and entry-point mapping (ArcoBASIC Main -> #EXPORT-named PE symbol);
runtime assumptions (no libc/CRT/heap/GC under #RUNTIME NONE); required error behavior for
constructs that depend on the hosted runtime; the exact hello-world source and exact expected
output; and the PE/COFF strategy decision (a small isolated PE32+ writer built into ArcoFission,
justified by a toolchain survey showing no PE-capable linker is present on the host, so this avoids
adding a new external dependency per Packet SS5/SS10). Created the two fixture files the RFC
references (examples/uefi_hello.abas, tests/systems/uefi-hello/{hello.abas,expected-output.txt}) as
static content only -- no compiler/parser/lexer code was modified in this work package.

FILES CHANGED
docs/systems/uefi-target.md (created)
examples/uefi_hello.abas (created)
tests/systems/uefi-hello/hello.abas (created)
tests/systems/uefi-hello/expected-output.txt (created)
.agents/reports/WP-001-systems-target-rfc.md (created)

PUBLIC BEHAVIOR
None. No parser, lexer, preprocessor, or compiler behavior was changed. The new example/test files
are not yet parseable by the current toolchain (dotted type names and LET...AS Type do not exist
yet) -- they are fixtures for WP-002 onward to make real.

TESTS RUN
None required (documentation-only work package). Existing baseline (ctest, 3/3 passing, see
WP-000 report) is unaffected since no source files were touched.

ACCEPTANCE CRITERIA
Document precise enough that another agent could implement the target without selecting
architecture: PASS (target triple, types, ABI, entry point, PE/COFF strategy with toolchain survey,
runtime assumptions, exact source/output, and error behavior are all specified; every deviation from
the Packet's illustrative syntax is justified against existing conventions).

ASSUMPTIONS
- #TARGET reuse implemented as: under a systems profile, the first #TARGET argument is also read as
  the codegen architecture; outside a systems profile, existing platform-list behavior is unchanged.
  Exact code location for this branch is left to WP-003 (Freestanding Profile) as an implementation
  detail per Packet SS14 ("private... implementation details that do not affect public behavior").
- UEFI_SYSTEM_TABLE/EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL exact byte offsets are explicitly left as a
  WP-006 verification task against the UEFI Specification, not asserted as final here, per Packet
  SS9 ("do not guess ABI details").

DEVIATIONS
Two syntax deviations from the Packet's illustrative hello-world source, both documented with
rationale in docs/systems/uefi-target.md SS8: parenthesized call syntax for
systemTable.ConsoleOut.Write(...), and dotted type names requiring a parse_type_name extension.

REGRESSIONS
None.

RISKS
- The #TARGET reuse strategy (branching interpretation by active profile) has not been implemented
  or tested yet; WP-003 should confirm it composes cleanly with the existing #IFDEF/#IF conditional
  handling in preprocess_source before relying on it.
- No PE-capable linker or mingw-w64 cross toolchain is installed on this host; the "small isolated
  PE32+ writer" and "internal instruction encoder" decisions in SS9 commit WP-008/WP-009 to
  meaningfully more implementation work than shelling out to an existing linker would have been, in
  exchange for zero new build-time dependencies. This tradeoff should be revisited only if it proves
  infeasible, not silently reversed.

NEXT SAFE WORK PACKAGE
WP-002: Fixed-Width Types. No architectural decision blocks starting it; docs/systems/uefi-target.md
SS3 gives the exact type table, declaration syntax (LET ... AS Type), and conversion-rule
requirements.
```
