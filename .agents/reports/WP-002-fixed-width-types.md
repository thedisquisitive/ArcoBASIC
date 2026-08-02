```text
STATUS
Complete

OBJECTIVE
Implement or verify the required fixed-width systems types (Packet WP-002).

SUMMARY
Added the ten fixed-width systems types (U8/U16/U32/U64/I8/I16/I32/I64/BOOL/PTR) as a compile-time
type registry (include/arco/fixed_width_types.hpp: size, alignment, signedness, exact min/max bounds
stored as uint64_t magnitudes rather than double, so U64's max and I64's min never round-trip
through a 53-bit mantissa). Extended LET to accept an optional "AS Type" clause
(core/parser.cpp: Parser::assignment_statement, gated on had_let so bare reassignment `x = 5`
cannot declare a type), per the docs/systems/uefi-target.md section 3 decision to reuse LET instead
of inventing a competing DIM keyword. Added Parser::validate_fixed_width_initializer, which performs
exact-integer literal range checking (no floating-point rounding) against the declared type when the
initializer is a plain literal (optionally negated), and raises a diagnostic satisfying Packet
section 13's quality bar (what failed, source location via the existing token_error helper, expected
range, received value, corrective action) otherwise. Non-literal initializers are intentionally not
statically checked at this stage (deferred to later semantic-analysis/A-MIR work).

While testing, discovered that ArcoFission's A-MIR/bytecode/compile-run/build/native pipeline does
NOT consume the core/parser.cpp AST at all -- it re-derives structure from the raw token stream via
an independent AmirBuilder pattern matcher (see the correction added to
.agents/reports/WP-000-repository-audit.md). The new LET ... AS Type syntax parsed fine at the AST
level but fell back to an UNSUPPORTED A-MIR instruction until AmirBuilder::lower_assignment was
taught to tolerate (skip over) the AS Type clause. Fixed by adding an allow_type_annotation parameter
to lower_assignment, true only for the LET-prefixed call site, matching the had_let gating on the
parser side. This was necessary to avoid leaving WP-002 as a half-finished feature that only worked
for the "reveal AST" command and regressed every other ArcoFission entry point.

Added tests/systems_fixed_width_types_smoke.sh (wired into CMakeLists.txt next to the existing
smoke tests) covering every Packet WP-002 acceptance case (valid boundary values for all ten types)
and every required rejection case (U8/U16/U32/U64/I8/I64 overflow and sign mismatches, non-integer
literal into an integer type, BOOL out of range, PTR from a literal), plus a non-fixed-width type
name (String) to confirm untyped/ordinary declarations are unaffected.

FILES CHANGED
include/arco/fixed_width_types.hpp (created)
core/parser.hpp (added Parser::validate_fixed_width_initializer declaration)
core/parser.cpp (AssignStmt gains type_name; assignment_statement parses optional AS Type gated by
  had_let; new validate_fixed_width_initializer and next_wider_type_name helpers)
compiler/fission.cpp (lower_assignment gains allow_type_annotation parameter; both call sites
  updated; skips the AS Type clause during A-MIR lowering)
tests/systems_fixed_width_types_smoke.sh (created)
CMakeLists.txt (registered the new test)
docs/systems/uefi-target.md (corrected an inaccurate WP-001 claim about parse_type_name needing a
  dotted-identifier extension -- verified empirically that dotted type names already work today
  because the lexer treats '.' as an identifier-continuation character)
.agents/reports/WP-000-repository-audit.md (added a correction: A-MIR is lowered from tokens, not
  from the AST -- discovered while implementing this work package)
.agents/reports/WP-002-fixed-width-types.md (this report)

PUBLIC BEHAVIOR
New: LET name AS Type = literal is now valid ArcoBASIC syntax wherever U8/U16/U32/U64/I8/I16/I32/
I64/BOOL/PTR (or any other identifier, e.g. a class name) is used as Type; fixed-width type names
enforce compile-time literal range checking with diagnostics, other type names behave exactly as
LET already did (no runtime check, matching existing class-field/parameter behavior). Existing
LET name = expr (no AS clause) and bare x = expr reassignment are entirely unchanged. No existing
program's parse tree, A-MIR, bytecode, or execution changes as a result of this work package.

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings
ctest --test-dir build --output-on-failure -> 4/4 passed
  arco_runtime_tests: Passed
  arcosh_alpha_smoke: Passed
  arcofission_alpha_smoke: Passed
  systems_fixed_width_types_smoke: Passed (new; 16 accept cases, 10 reject cases)
Manual verification beyond the automated suite:
  - `ArcoFission compile-run` executes a LET a AS U8 = 255 program correctly (prints 255) through
    the bytecode VM.
  - `ArcoFission native ... -o OUT` builds and runs an ELF64 binary containing a typed LET
    correctly.
  - Confirmed (not a regression) that printing the U64 max literal via the interpreter's Value
    (double-based) already mis-renders large 64-bit values with or without a type annotation --
    pre-existing limitation, orthogonal to the new exact-integer literal range check, which
    operates on the token's original decimal text rather than the rounded double.

ACCEPTANCE CRITERIA
Correct sizes and signedness: PASS (include/arco/fixed_width_types.hpp table)
Literal range checking: PASS (exact-integer, not double-based)
Clear overflow diagnostics: PASS (Packet section 13 shape: what/where/expected/received/fix)
Deterministic conversions: PARTIAL -- implicit narrowing is rejected in the sense that fixed-width
  types only get static checking for literal initializers today; no general expression-level
  narrowing-conversion enforcement or explicit CAST() helper was added, since no acceptance test
  requires one and Packet non-goals exclude arbitrary-precision work. Documented as a known gap
  below rather than silently declared done.
Tests for valid and invalid values: PASS (tests/systems_fixed_width_types_smoke.sh)

ASSUMPTIONS
- Hex/binary integer literals (0x.., 0b..) are not range-checked in this milestone; the validator
  silently skips them rather than risking a false rejection, since existing lexer behavior for large
  hex/binary literals already has its own precision limitations (see RISKS). Documented as a
  follow-up, not implemented now, per Packet's minimal-scope instructions.
- Diagnostic wording, private helper names (next_wider_type_name, validate_fixed_width_initializer)
  are implementation details per Packet section 14 ("may decide without stopping").

DEVIATIONS
None from docs/systems/uefi-target.md section 3's decisions (LET AS Type, not DIM; no grammar change
needed for dotted type names).

REGRESSIONS
None (4/4 tests passing, including all 3 pre-existing tests unchanged).

RISKS
- No explicit conversion/cast helper exists yet, so "deterministic conversions" is only partially
  satisfied (see ACCEPTANCE CRITERIA). WP-004 (A-MIR Systems Primitives) or a later package should
  decide whether this is needed before the milestone's Definition of Done, since the hello-world
  program itself does not require one.
- Fixed-width type annotations are not yet enforced at runtime or in A-MIR beyond being silently
  accepted (A-MIR still stores the value as an untyped CONST/STORE, exactly like an untyped LET).
  This is intentional -- WP-004 is where A-MIR is meant to gain real fixed-width primitives -- but
  it means `LET a AS U8 = someLargeVariable` today has no enforcement at all, only literal
  initializers are checked. This is documented, not hidden.
- The lexer's existing `std::stoll` parsing of hex/binary literals (core/lexer.cpp binary_number/
  hex_number) already throws for values that don't fit in a signed 64-bit `long long` (e.g.
  0xFFFFFFFFFFFFFFFF), independent of this work package. Not fixed here (out of scope); noted so a
  future package doesn't mistake it for something WP-002 introduced.
- The AmirBuilder architecture discovery (token-based, not AST-based) means every future work
  package that adds parser-level syntax (WP-003's #PROFILE/#RUNTIME handling doesn't need this since
  those are preprocessor-level, but any new statement/expression grammar would) must remember to
  also update AmirBuilder's independent matcher, or silently get UNSUPPORTED instead of a compile
  error. Recorded in WP-000's report as the most important correction from this work package.

NEXT SAFE WORK PACKAGE
WP-003: Freestanding Profile (#PROFILE/#RUNTIME semantics, per docs/systems/uefi-target.md sections
2 and 7). No architectural decision blocks starting it.
```
