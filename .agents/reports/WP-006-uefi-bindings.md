```text
STATUS
Complete

OBJECTIVE
Add the smallest safe UEFI surface needed for text output (Packet WP-006).

SUMMARY
Verified the required UEFI ABI facts against the TianoCore EDK2 reference implementation
(MdePkg/Include/Uefi/UefiSpec.h, UefiMultiPhase.h, Protocol/SimpleTextOut.h -- fetched directly,
not recalled from memory, per Packet section 9's "do not guess ABI details") rather than assuming
the values I already believed were correct. Computed exact x86-64 byte offsets field-by-field from
the verified type lists: EFI_SYSTEM_TABLE.ConOut at 0x40, EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.
OutputString at 0x08, and confirmed OutputString's real C signature takes an implicit "This"
pointer plus a CHAR16* string (two arguments), while ArcoBASIC's systemTable.ConsoleOut.Write("...")
surface only shows one explicit argument -- recorded as a known, flagged gap for WP-008 rather than
silently glossed over. Full derivation and sources are in the new docs/systems/uefi-bindings.md.

Implemented this as a small, pure registry (include/arco/uefi_bindings.hpp: UEFI.Handle,
UEFI.SystemTable with only ConOut bound as "ConsoleOut", UEFI.SimpleTextOutputProtocol with only
OutputString bound as "Write") -- deliberately not the rest of the UEFI specification, per the
Packet's explicit scope limit. Wired compile-time field-chain validation into core/parser.cpp:
Parser now tracks each function's parameter name -> declared type map
(current_function_parameter_types_, populated in function_statement, mirroring the existing
current_super_class_ save/restore pattern) and, when a dotted call's receiver is a parameter with a
known UEFI.* type, walks the remaining dotted segments against the registry
(validate_uefi_field_chain), rejecting the first segment that is not a bound field with a
diagnostic naming the type and every field that *is* bound. Non-UEFI parameter types are untouched
-- the check is a no-op outside its scope, verified by a regression test.

Found and fixed a real bug while testing: validate_uefi_field_chain reassigned current_type (an
optional<UefiType>) before reading field->result_type, where field was a raw pointer into the OLD
optional's internal vector -- a dangling-pointer read that silently produced an empty type name in
error messages for any chain longer than two segments. Fixed by capturing the needed string before
reassigning. Caught by manually testing a three-segment chain (systemTable.ConsoleOut.Clear()) --
the two-segment hello-world case alone would not have exposed it.

Also found that WP-004's own "deep dotted chain" test case used UEFI.Handle (an opaque type with no
fields) as the parameter type for a fictional multi-segment chain -- valid before this work package
(no field validation existed yet) but now correctly rejected by the new, real field-chain check.
Fixed the test to use a non-UEFI type name instead, since its actual purpose (verifying CallExternal
classification through a deep chain) is orthogonal to UEFI field validity.

FILES CHANGED
include/arco/uefi_bindings.hpp (created)
core/parser.hpp (current_function_parameter_types_ member, validate_uefi_field_chain declaration)
core/parser.cpp (function_statement populates/restores the parameter-type map;
  validate_uefi_field_chain implementation; dotted-call construction site invokes it)
tests/systems_amir_primitives_smoke.sh (fixed the now-invalid deep-chain-through-UEFI.Handle case)
tests/systems_uefi_bindings_smoke.sh (created)
CMakeLists.txt (registered the new test)
docs/systems/uefi-bindings.md (created -- verification trail and sources)
docs/systems/uefi-target.md (traceability section updated)
.agents/reports/WP-006-uefi-bindings.md (this report)

PUBLIC BEHAVIOR
New: dotted calls through a UEFI.*-typed function parameter are now validated at compile time
against the bound field registry; an unbound or nonexistent field produces a clear diagnostic
instead of silently passing through as an unvalidated external call. No other program's behavior
changes -- validated by an explicit regression case (a String-typed parameter's field access is
untouched) and by re-running the full existing suite.

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings
ctest --test-dir build --output-on-failure -> 8/8 passed
  arco_runtime_tests, arcosh_alpha_smoke, arcofission_alpha_smoke,
  systems_fixed_width_types_smoke, systems_freestanding_profile_smoke,
  systems_calling_convention_smoke: unchanged, still passing
  systems_amir_primitives_smoke: fixed (see SUMMARY), now passing
  systems_uefi_bindings_smoke: new, passing (hello-world accepted; unknown field, real-but-unbound
  field, unknown method on a correctly-resolved protocol, and field access on an opaque Handle all
  rejected with correct diagnostics; a hosted String-typed parameter's field access unaffected)
Manual verification beyond the automated suite:
  - Caught and fixed the dangling-pointer bug (see SUMMARY) via a three-segment chain test before
    it was committed.
  - Confirmed examples/uefi_hello.abas still parses end to end after both the registry and the bug
    fix were in place.

ACCEPTANCE CRITERIA
The compiler can type-check the hello-world source and resolve the required UEFI fields: PASS

ASSUMPTIONS
- Field-chain validation only fires for chains rooted at a direct function parameter (matching
  WP-004's CallExternal classification scope) -- a local variable reassigned from a parameter is not
  tracked and would not be validated. Documented explicitly in uefi-bindings.md rather than assumed
  covered.
- ArcoBASIC-facing binding names (ConsoleOut, Write) intentionally differ from the real UEFI names
  (ConOut, OutputString) -- these are the names the Packet's own illustrative hello-world source
  (section 7) already used; the correspondence is recorded in uefi-bindings.md for traceability.

DEVIATIONS
None from docs/systems/uefi-target.md. The implicit-This-argument gap and the UTF-16 string gap are
pre-existing, already-anticipated deferrals (to WP-008 and WP-007 respectively), not new deviations
introduced by this work package -- both are explicitly documented in uefi-bindings.md rather than
silently left for someone else to discover.

REGRESSIONS
None (8/8 tests passing; the one test that needed changes was fixed because it exercised behavior
that was newly, correctly rejected, not because anything broke).

RISKS
- WP-008 must remember to inject the implicit "This" argument (the resolved ConsoleOut pointer
  itself) before assign_argument_locations when actually lowering a call to OutputString, or the
  generated call will pass the string where the firmware expects the protocol pointer. Flagged
  prominently in uefi-bindings.md specifically so this isn't rediscovered the hard way at debug time.
- The uefi.org spec pages returned HTTP 403 in this environment; verification relied on the edk2
  reference implementation instead, which is the same industry-standard source virtually all real
  UEFI development already treats as authoritative, but is not literally the spec document itself.
  Noted in uefi-bindings.md's Sources section rather than presented as spec-verified without
  qualification.

NEXT SAFE WORK PACKAGE
WP-007: UTF-16 Constant Support. No architectural decision blocks starting it; the OutputString
signature verified here confirms String must be CHAR16* (UTF-16, null-terminated).
```
