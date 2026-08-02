```text
STATUS
Complete

OBJECTIVE
Represent the message in the encoding required by UEFI (Packet WP-007).

SUMMARY
Implemented a pure, standalone UTF-8-to-UTF-16 encoder (include/arco/utf16.hpp:
encode_utf16_null_terminated) that produces the null-terminated UTF-16 code-unit sequence
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString's CHAR16* parameter requires (confirmed in WP-006).
Handles the full UTF-8 decode space (1-4 byte sequences), correctly emits surrogate pairs for code
points above the Basic Multilingual Plane, and always appends a u'\0' terminator. Rejects, with a
specific reason for each: an embedded NUL byte (which would silently truncate the string), invalid
UTF-8 lead/continuation bytes, truncated multi-byte sequences, a UTF-8 encoding of a surrogate code
point, and code points beyond U+10FFFF.

Wired compile-time validation into core/parser.cpp at the same site WP-006 added UEFI field-chain
validation (Parser::validate_utf16_arguments, called alongside validate_uefi_field_chain wherever a
call's receiver has a known UEFI parameter type): any plain string-literal argument to such a call
is now UTF-16-encoded at parse time, and a failure produces a clear diagnostic. This keeps the
check precisely scoped to the systems/UEFI call surface -- verified by a regression case showing an
ordinary hosted-mode PRINT with the exact same embedded-NUL string is completely unaffected.

FILES CHANGED
include/arco/utf16.hpp (created)
core/parser.hpp (validate_utf16_arguments declaration)
core/parser.cpp (implementation; wired into the existing UEFI-typed-call validation site)
tests/runtime_tests.cpp (unit tests: exact code units for "Hello from ArcoBASIC", empty string,
  a BMP character outside ASCII, a surrogate-pair character, and five rejection cases)
tests/systems_utf16_encoding_smoke.sh (created)
CMakeLists.txt (registered the new test)
docs/systems/utf16-encoding.md (created)
docs/systems/uefi-target.md (traceability section updated)
.agents/reports/WP-007-utf16-encoding.md (this report)

PUBLIC BEHAVIOR
New: a string-literal argument to a call through a UEFI-typed function parameter must be
UTF-16-encodable (no embedded NUL, well-formed UTF-8) or compilation fails with a clear diagnostic.
No other program's behavior changes -- verified by an explicit regression case and the full existing
suite.

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings
./build/arco_tests (direct run)  -> exit 0, all require() assertions passed including the new
  UTF-16 unit tests (exact code units for the hello string, an accented character, a surrogate
  pair, and five distinct rejection cases)
ctest --test-dir build --output-on-failure -> 9/9 passed
  arco_runtime_tests (includes the new unit tests), arcosh_alpha_smoke, arcofission_alpha_smoke,
  systems_fixed_width_types_smoke, systems_freestanding_profile_smoke,
  systems_amir_primitives_smoke, systems_calling_convention_smoke, systems_uefi_bindings_smoke:
  unchanged, still passing
  systems_utf16_encoding_smoke: new, passing
Manual verification beyond the automated suite:
  - Confirmed examples/uefi_hello.abas still parses end to end.
  - Discovered and fixed a shell-quoting bug in my own draft golden test while writing it: `"\xC3\xA9"`
    inside plain single/double-quoted bash text produces the literal six characters backslash-x-C-3-
    backslash-x-A-9, not the raw bytes, because ArcoBASIC's own lexer has no \x escape either
    (confirmed via core/lexer.cpp's string() escape handling: only \n, \r, \t, \", \\, \0). Fixed by
    using bash's $'...' ANSI-C quoting for the two test cases that need real non-ASCII/invalid bytes
    in the generated .abas fixture, verified byte-for-byte with `od -c` before trusting the fix.

ACCEPTANCE CRITERIA
A test verifies the exact emitted UTF-16 code units: PASS (tests/runtime_tests.cpp asserts the
exact 22-code-unit sequence for "Hello from ArcoBASIC", including the trailing null terminator,
plus exact sequences for an empty string, an accented character, and a surrogate-pair character)

ASSUMPTIONS
- Only plain string literals are validated (matching validate_fixed_width_initializer's established
  literal-only scope from WP-002); a variable or expression passed as a UEFI call's string argument
  is not statically checked at this stage.
- CESU-8/WTF-8 (UTF-8 encodings of lone surrogates) are rejected rather than tolerated, since they
  are not valid Unicode text and would produce ambiguous UTF-16 output.

DEVIATIONS
None from docs/systems/uefi-target.md or docs/systems/uefi-bindings.md.

REGRESSIONS
None (9/9 tests passing, including all 8 pre-existing tests unchanged, plus an explicit regression
case proving the validation does not leak into hosted-mode string handling).

RISKS
- The encoded UTF-16 code units are not yet stored anywhere in A-MIR, bytecode, or any data-section
  representation -- this work package only proves the encoding is correct and validates literals
  early. WP-008/009 will need to actually call encode_utf16_null_terminated when lowering a real
  CALL_EXTERNAL to OutputString and place the result in the image's data section (also where
  "preserve constant lifetime for the duration of the call" becomes a real, actionable requirement
  rather than a property of a C++ std::vector's own lifetime).
- Malformed UTF-8/embedded-NUL detection only fires for calls through a UEFI-typed parameter. If a
  future work package adds more systems entry points that accept strings (e.g. additional protocol
  bindings), each will need the same validate_utf16_arguments call wired in explicitly -- it is not
  automatically inherited by new binding surfaces.

NEXT SAFE WORK PACKAGE
WP-008: x86-64 Code Generation. No architectural decision blocks starting it; docs/systems/
uefi-target.md section 9 records the decision to build a small internal instruction encoder rather
than shell out to nasm/clang, and calling_convention.hpp/uefi_bindings.hpp/utf16.hpp are the three
pieces of verified, tested groundwork it should consume rather than re-derive.
```
