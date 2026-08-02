```text
STATUS
Complete

OBJECTIVE
Extend A-MIR only as required to represent the hello-world UEFI program (Packet WP-004).

SUMMARY
Prior to this work package, A-MIR already had structural support for typed function parameters and
return-type declarations in the function header (verified: "FUNCTION Main(imageHandle AS
UEFI.Handle, systemTable AS UEFI.SystemTable) RETURNS U64" was already rendered correctly), so
"pointer values" and part of "function parameters"/"function returns" needed no new work -- the
dotted UEFI.Handle/UEFI.SystemTable type names already flow through as the parameter's declared
type text. Two real gaps remained and were fixed:

1. RETURN instructions inside a function body always used a hardcoded "VALUE" type tag regardless
   of the function's actual declared return type -- so a U64-returning function's `RETURN 0` showed
   "RETURN VALUE %t2" instead of "RETURN U64 %t2", losing the type information the function header
   already had. Fixed by threading function.return_type (already computed by
   parse_function_signature) into the RETURN instruction. Confirmed safe: the bytecode VM's Return
   execution (execute_bytecode, BytecodeOp::Return) never reads this tag at all, only
   operands[1] (the value) -- the tag is purely informational today, so this change cannot affect
   runtime behavior, only the A-MIR/bytecode text representation.

2. "External or ABI-bound function calls" had no distinct representation: a call through a UEFI
   protocol pointer received as a parameter (systemTable.ConsoleOut.Write(...)) lowered to the exact
   same AmirInstruction::Kind::CallValue as an ordinary namespaced host/stdlib call (File.Exists,
   Math.SIN). Added a new Kind::CallExternal (and matching BytecodeOp::CallExternal), and reclassify
   a bare dotted-call statement's already-built CallValue instruction to CallExternal when its
   receiver (the identifier before the first dot) matches one of the enclosing function's declared
   parameter names. This required no changes to the deeply recursive ExpressionLowerer class or to
   lower_assignment -- the classification happens by inspecting the single instruction
   lower_expression already produced, in the one call site (lower_simple_statement's bare-call-as-
   statement branch) that both has the enclosing AmirFunction in scope and matches the hello-world's
   exact shape (a dotted call used directly as a statement). This is a deliberately narrower scope
   than "every possible expression position" -- documented in DEVIATIONS.

CallExternal was deliberately left unimplemented in execute_bytecode (no VM case was added), so it
falls through to the existing "bytecode VM does not implement opcode yet" diagnostic when actually
invoked -- an honest outcome, since the alpha bytecode VM has no UEFI runtime to call into regardless
(that is WP-005/006/008's job on real hardware, not this Linux-hosted VM).

While testing, discovered and documented (not fixed -- out of scope, pre-existing, unrelated to this
work package) a bug in the bytecode VM's argument-binding for any function parameter with an AS Type
annotation: it matches raw joined parameter text ("name AS String") against local names ("name"),
which never matches, causing "undefined bytecode local" for any typed-parameter function actually
invoked through compile-run/run. Recorded in .agents/reports/WP-000-repository-audit.md.

FILES CHANGED
compiler/fission.cpp (new AmirInstruction::Kind::CallExternal and BytecodeOp::CallExternal with
  full render/build wiring; RETURN instruction now uses function.return_type; new
  function_has_parameter helper; bare-call-statement branch reclassifies CallValue to CallExternal
  when appropriate)
tests/systems_amir_primitives_smoke.sh (created)
CMakeLists.txt (registered the new test)
.agents/reports/WP-000-repository-audit.md (documented the pre-existing bytecode-VM
  parameter-binding bug found while verifying this work package)
.agents/reports/WP-004-amir-systems-primitives.md (this report)

PUBLIC BEHAVIOR
A-MIR text output changes for any function with a non-default return type (RETURN now shows the
real type instead of "VALUE") and for any dotted call whose receiver is a function parameter (shown
as CALL_EXTERNAL instead of CALL). Bytecode text output gains a CALL_EXTERNAL opcode. No change to
any program's actual execution result -- both changes are informational/classificational at the
A-MIR and bytecode text level; the bytecode VM's evaluated behavior for existing programs (that
don't use a parameter-receiver call) is identical to before.

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings
ctest --test-dir build --output-on-failure -> 6/6 passed
  arco_runtime_tests, arcosh_alpha_smoke, arcofission_alpha_smoke,
  systems_fixed_width_types_smoke, systems_freestanding_profile_smoke: unchanged, still passing
  systems_amir_primitives_smoke: new, passing (hello-world lowers with RETURNS U64/RETURN U64/
  CALL_EXTERNAL; ordinary File.Exists call and untyped-function VALUE return unaffected; a call
  through a parameter is external even through a multi-level dotted chain)
Manual verification beyond the automated suite:
  - examples/uefi_hello.abas's A-MIR now shows "RETURN U64 %t2" and
    "CALL_EXTERNAL systemTable.ConsoleOut.Write %t0".
  - Confirmed CALL_EXTERNAL's bytecode path throws the honest "not implemented yet" diagnostic only
    when the containing function is actually invoked (a bare FUNCTION...END FUNCTION declaration at
    top level is never itself executed, matching ordinary ArcoBASIC semantics -- ran
    compile-run on the hello-world fixture directly, which produces no output/no error because
    nothing calls Main, then confirmed the diagnostic does fire by writing a small program that
    calls a function with an external call).

ACCEPTANCE CRITERIA
The hello-world source can be lowered to valid A-MIR and printed in a deterministic textual form:
PASS (tests/systems/uefi-hello/hello.abas verified via the new golden test)

ASSUMPTIONS
- The parameter-receiver heuristic for CALL_EXTERNAL classification is a fixed-name check
  (function.params) rather than a real type check (whether the parameter's declared type is a
  systems/pointer type). A call through ANY function parameter (even a plain untyped one, or a
  hosted String parameter) is classified as external. For the freestanding-profile use case this is
  harmless (RFC section 7 already rejects hosted namespaces like File/Network under #RUNTIME NONE,
  so a hosted-profile parameter used this way is unlikely), but it is a deliberate simplification
  recorded here rather than silently assumed precise.
- CallExternal reclassification only covers the bare-call-as-statement shape (a dotted call used
  directly as a whole statement, matching the hello-world). Calls through a parameter used inside a
  larger expression (e.g. `LET result = systemTable.Foo.Bar(...)`, or nested inside another call's
  arguments) still lower as ordinary CallValue today. Threading the classification into
  ExpressionLowerer/lower_assignment for full coverage was judged out of proportion to what the
  hello-world milestone needs and was not attempted.

DEVIATIONS
Structure field access does not get a distinct A-MIR representation in this work package (no FIELD
instruction was added); systemTable.ConsoleOut.Write remains a single flattened dotted call name,
now tagged CALL_EXTERNAL rather than decomposed into a typed field load plus an indirect call.
Justification: real field-offset information for EFI_SYSTEM_TABLE does not exist yet (that is
WP-006's job), and inventing a "symbolic field access" instruction now, ahead of knowing the actual
struct shapes WP-006 will define, risked designing the wrong abstraction. The CALL_EXTERNAL tag is
judged the smallest honest step that is useful now and does not foreclose WP-006's design.

REGRESSIONS
None (6/6 tests passing, including all 5 pre-existing tests unchanged, plus explicit checks that
File.Exists and untyped-function returns keep their prior A-MIR shape).

RISKS
- WP-006 may find that CALL_EXTERNAL as currently defined (a same-shaped CallValue variant with no
  field-offset information at all) is not sufficient once real UEFI struct layouts exist, and may
  need to replace it with a proper field-access decomposition. This is flagged as expected follow-up
  work, not a surprise to discover later.
- The pre-existing bytecode-VM parameter-binding bug (see WP-000 addendum) means compile-run/run
  cannot exercise any typed-parameter function today, including the real Main entry point once
  WP-006 lands bindings. This does not block the mission (WP-008's native codegen is the real path),
  but anyone tempted to use compile-run as a stand-in "does it work" check for typed systems
  functions should know it will fail for reasons unrelated to their code.

NEXT SAFE WORK PACKAGE
WP-005: Microsoft x64 Calling Convention. No architectural decision blocks starting it; docs/systems/
uefi-target.md section 4 gives the ABI contract, and the WP-004 finding that CallExternal already
distinguishes ABI-bound calls in A-MIR gives WP-005 a concrete instruction to design calling-
convention lowering around.
```
