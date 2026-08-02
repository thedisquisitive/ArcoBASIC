```text
STATUS
Complete

OBJECTIVE
Add a compiler profile that assumes no operating system or standard runtime (Packet WP-003).

SUMMARY
Implemented the four new systems directives designed in docs/systems/uefi-target.md section 2:
#PROFILE (only "UEFI" accepted in this milestone), #RUNTIME (only "NONE" accepted), #CALLCONV
(only "UEFI" accepted), and #EXPORT (a quoted symbol name, stored verbatim). Each rejects any other
value with a diagnostic naming what was given and what is accepted. Extended the existing #TARGET
directive per the RFC's reuse decision: under an active #PROFILE UEFI, #TARGET's first word is also
read as the codegen architecture (only "X86_64" accepted this milestone); outside a systems profile,
#TARGET's original platform-list metadata behavior is completely unchanged (verified with a
regression test). All new/extended directive handling lives in the single existing preprocessor
hook (Runtime::preprocess_source), consistent with WP-000's finding that this is the one place both
the interpreter and ArcoFission read directives from.

Threaded a `freestanding_runtime_none` flag from Runtime::compile_metadata() (already public) into
Parser, gated at every construction site across compiler/fission.cpp (all 5 entry points) and
runtime.cpp's run_string, so the restriction applies identically whether a #RUNTIME NONE program is
processed by the interpreter or by any ArcoFission command. Under that flag, two things become
compile errors instead of silently parsing: PRINT (using the packet's own illustrative diagnostic
verbatim: "UEFI target does not provide the standard console runtime. Use UEFI.SystemTable.ConsoleOut
or select a hosted runtime profile."), and calls whose dotted name's first segment exactly matches
one of a short, explicit denylist (File, Network, Net, System, Web, Printer, Process, Host,
Document) -- matched as a whole leading dot-segment, not a substring, so a parameter named
"systemTable" is never mistaken for the "System" namespace. Class instantiation and array/object
literals are NOT rejected in this milestone (see RISKS) -- deliberately narrower than the full list
sketched in the RFC, to avoid guessing at rules more complex constructs would need.

FILES CHANGED
include/arco/runtime.hpp (CompileMetadata gains profile/runtime_mode/arch/callconv/export_symbol)
runtime/runtime.cpp (preprocess_source: #PROFILE/#RUNTIME/#CALLCONV/#EXPORT handling, #TARGET
  extended for architecture selection; run_string passes the freestanding flag to Parser)
core/parser.hpp (Parser constructor takes freestanding_runtime_none, defaulted false; new private
  member)
core/parser.cpp (constructor updated; is_hosted_runtime_namespace/freestanding_diagnostic helpers;
  print_statement and the dotted-call construction site enforce the restriction)
compiler/fission.cpp (all 5 Parser construction sites pass runtime.compile_metadata().runtime_mode)
tests/systems_freestanding_profile_smoke.sh (created)
CMakeLists.txt (registered the new test)
.agents/reports/WP-003-freestanding-profile.md (this report)

PUBLIC BEHAVIOR
New: #PROFILE, #RUNTIME, #CALLCONV, #EXPORT directives; #TARGET gains architecture-selection
meaning specifically when #PROFILE UEFI is active. Under #RUNTIME NONE, PRINT and File./Network./
Net./System./Web./Printer./Process./Host./Document.* calls are compile errors with corrective
diagnostics. All of this is inert for any program that does not use #RUNTIME NONE -- verified by
regression test and by re-running the full existing suite unchanged.

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings
ctest --test-dir build --output-on-failure -> 5/5 passed
  arco_runtime_tests, arcosh_alpha_smoke, arcofission_alpha_smoke,
  systems_fixed_width_types_smoke: unchanged, still passing
  systems_freestanding_profile_smoke: new, passing (12 cases: trivial freestanding function accepts,
  the full hello-world fixture accepts, the systemTable false-positive check accepts, PRINT/File/
  Network/System rejections, bad #PROFILE/#RUNTIME/#TARGET values rejected, hosted-program
  regression accepts)
Manual verification beyond the automated suite:
  - examples/uefi_hello.abas and tests/systems/uefi-hello/hello.abas (from WP-001) now parse
    successfully end to end for the first time.
  - Confirmed #TARGET Linux Windows (no #PROFILE) still compiles and runs exactly as before.

ACCEPTANCE CRITERIA
A trivial function with no host dependencies compiles under the freestanding profile: PASS
A program using an unavailable host-dependent feature fails with a clear message: PASS

ASSUMPTIONS
- The forbidden-namespace list (File, Network, Net, System, Web, Printer, Process, Host, Document)
  is deliberately short and exact-match rather than an exhaustive classification of every stdlib
  helper; picked as the smallest set that is unambiguously hosted-runtime-only and needed no
  judgment calls about edge cases. Recorded as an explicit, documented choice rather than presented
  as a complete enumeration.
- Directive value validation (#PROFILE/#RUNTIME/#CALLCONV/#TARGET architecture) rejects anything
  other than the single accepted value per Packet section 3's locked decisions (UEFI/NONE/UEFI/
  X86_64) rather than silently accepting and ignoring unknown values, since Packet section 12 asks
  for deterministic diagnostics and the milestone has exactly one supported combination.

DEVIATIONS
The RFC's section 7 error-behavior list additionally named "class instantiation that relies on the
interpreter's object runtime" and "dynamic array/object literals" as things #RUNTIME NONE should
reject. Neither is enforced in this work package: classes require knowing whether an identifier
resolves to a user-defined CLASS in the same file (real complexity, uncertain acceptance-test value
this milestone), and array/object literals may yet get a freestanding lowering in WP-004. Enforcing
either now risked getting the rule wrong in a way that would need walking back later. This is a
documented narrowing of scope, not a silent gap -- the hello-world program in section 8 needs
neither construct.

REGRESSIONS
None (5/5 tests passing, including all 4 pre-existing tests unchanged, plus an explicit regression
check that #TARGET's old platform-list behavior is untouched outside a systems profile).

RISKS
- Class instantiation and array/object literals remain usable (unchecked) under #RUNTIME NONE. If
  WP-004 or later work packages need those to be hard compile errors before the milestone's
  Definition of Done, that enforcement still needs to be added; it was not silently assumed done.
- The freestanding flag is derived once per compilation from Runtime::compile_metadata() after
  preprocessing completes, then handed to a single Parser instance. Any future code path that
  constructs a second Parser over related source (e.g. #INCLUDE's recursive preprocess_source calls
  already flatten into one token stream before a single parse, so this should be fine) should double
  check it also threads the flag through, rather than assuming Parser's default of false is always
  correct.

NEXT SAFE WORK PACKAGE
WP-004: A-MIR Systems Primitives. No architectural decision blocks starting it; docs/systems/
uefi-target.md sections 3-6 give the required A-MIR capabilities (fixed-width values, pointers,
function parameters/returns, external/ABI-bound calls, constants, structure field access).
```
