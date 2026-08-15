# RFC-0022: User-Defined Runtime Errors

**RFC Number:** RFC-0022  
**Title:** User-Defined Runtime Errors  
**Status:** Implemented  
**Category:** Language / Hosted Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0007, RFC-0012, RFC-0021

------------------------------------------------------------------------

# 1. Executive Summary

ArcoBASIC can catch runtime failures with `TRY` / `CATCH`, but an ArcoBASIC function cannot
deliberately originate a runtime error. Library authors therefore cannot enforce preconditions
with accurate diagnostics. They must return sentinel values, change their public API to result
objects, or provoke an unrelated built-in failure.

This RFC adds one statement:

```basic
THROW "Mutation rate must be between 0 and 1"
```

`THROW` evaluates one string expression, creates a user-defined runtime error, and transfers
control to the nearest dynamically active `CATCH`. If no handler exists, the error terminates the
program through the existing source-located runtime diagnostic path.

The RFC also normalizes caught error objects across the interpreter, bytecode VM, and hosted native
runtime capsule. Every caught error has:

```basic
err.Message
err.Type
```

User-thrown errors have type `"UserError"`; ordinary runtime failures have type
`"RuntimeError"`.

The expected outcome is that hosted ArcoBASIC libraries can validate inputs and expose precise,
catchable failures without depending on host-language exceptions or changing return-value
contracts. Freestanding exception handling and unwinding remain outside this RFC.

------------------------------------------------------------------------

# 2. Motivation

Existing ArcoBASIC code can recover from failures produced by the runtime:

```basic
TRY
    PRINT missingValue
CATCH err
    PRINT err.Message
END TRY
```

There is no corresponding source construct for a library to reject an invalid value:

```basic
FUNCTION DecodeGenome(bits)
    IF LEN(bits) MOD 2 <> 0 THEN
        ' No valid way to originate a catchable error here.
    END IF
END FUNCTION
```

This blocks faithful ports of libraries whose API contracts distinguish valid results from
invalid calls. Examples include genome decoders, parsers, collection validators, mutation and
crossover operators, protocol encoders, and storage integrity checks.

Returning `NULL`, `FALSE`, or an error object is not an equivalent implementation when the
published API promises a value on success and a runtime error on invalid input. Deliberately
calling another function incorrectly is also unacceptable: it produces misleading diagnostics,
couples library behavior to unrelated helpers, and may stop failing after an implementation
change.

The current execution paths also disagree about caught runtime error shape. The interpreter binds
an object with `Message`; the bytecode VM binds `Message` and `Type`. A source-level `THROW`
feature requires one stable error-object contract.

------------------------------------------------------------------------

# 3. Goals

- Add a clear statement for originating a catchable runtime error from ArcoBASIC source.
- Preserve the existing `TRY` / `CATCH err` syntax and dynamic nearest-handler behavior.
- Require one message expression evaluated exactly once.
- Produce deterministic diagnostics for missing and invalid messages.
- Normalize caught error objects across every hosted execution path.
- Preserve source location and statement context for uncaught errors.
- Support throwing from functions, methods, loops, imported modules, and catch bodies.
- Lower the statement through the canonical AST, A-MIR, and hosted bytecode pipeline.
- Reject the statement clearly under `#RUNTIME NONE`.
- Add no third-party dependency and no parallel exception subsystem.

------------------------------------------------------------------------

# 4. Non-Goals

This RFC does not define:

- exception classes or user-defined error inheritance;
- object, array, number, boolean, or arbitrary payload throwing;
- error codes, causes, stack traces, or structured metadata;
- `FINALLY`, `RETRY`, `RESUME`, filters, typed catches, or multiple catch clauses;
- a bare rethrow form such as `THROW` with no expression;
- checked exceptions or function-level `THROWS` declarations;
- panic, assertion, warning, logging, or process-exit syntax;
- freestanding, UEFI, firmware, kernel, or hardware exception unwinding;
- mapping CPU faults or operating-system signals into `CATCH`;
- cross-thread or cross-process error propagation;
- changes to `STOP`, `RETURN`, `GOTO`, loop control, or program-exit signals.

------------------------------------------------------------------------

# 5. Terminology

**Throw**  
Originating a user-defined runtime error and transferring control by unwinding normal execution.

**Handler**  
The catch body belonging to a dynamically active `TRY` statement.

**Nearest Handler**  
The most recently entered active handler enclosing the current dynamic execution point.

**User Error**  
An error deliberately originated by the `THROW` statement. Its caught `Type` is `"UserError"`.

**Runtime Error**  
A failure originated by existing language or runtime behavior. Its caught `Type` is
`"RuntimeError"`.

**Uncaught Error**  
An error for which no active handler exists. It becomes the program's top-level runtime failure.

**Error Object**  
The object bound to the optional identifier following `CATCH`.

------------------------------------------------------------------------

# 6. Requirements

## 6.1 Syntax

The language MUST accept:

```basic
THROW expression
```

`THROW` MUST be a reserved keyword and a statement introducer.

One expression is mandatory. `THROW` without an expression MUST be a source-located parser error.

The expression MUST end according to ordinary statement boundaries, including newline, end of
source, or an existing colon statement separator.

## 6.2 Message value

The expression MUST evaluate to a `String`. The runtime MUST NOT silently convert another value
with `STRING()` or `to_string` semantics.

An empty string is permitted. It remains an error with an empty `Message`; implementations MAY
recommend non-empty messages in documentation but MUST NOT invent substitute text.

The expression is evaluated exactly once before control transfers.

If evaluation of the message expression itself produces a runtime error, that original runtime
error propagates. No `UserError` is created.

## 6.3 Control transfer

A successfully evaluated `THROW` MUST stop executing the current statement sequence immediately.
Statements after it on the same source line or in the same block MUST NOT execute.

The error MUST propagate across function, method, loop, import, and class-method call boundaries
until it reaches the nearest active handler.

The handler's catch body executes once. Normal execution continues after `END TRY` when the catch
body completes normally.

An error thrown from a catch body MUST propagate to an outer active handler, if present. The
handler currently executing MUST NOT catch its own catch-body error.

## 6.4 Existing control signals

`THROW` MUST remain distinct from the internal signals used for:

- `RETURN`;
- `EXIT FOR`, `EXIT WHILE`, and `EXIT DO`;
- `CONTINUE FOR`, `CONTINUE WHILE`, and `CONTINUE DO`; and
- process/program exit helpers.

Adding `THROW` MUST NOT make an existing `CATCH` intercept any of those signals.

The current alpha interpreter's interaction between `TRY` and internal `GOTO` / `STOP` signals is
outside this RFC. The implementation MUST preserve that existing behavior and MUST NOT use this
feature as authority to redesign it.

## 6.5 Hosted execution parity

The interpreter, hosted bytecode VM, serialized bytecode execution, and hosted native runtime
capsule MUST produce equivalent observable behavior.

Identical source and input MUST select the same handler, bind the same error object, skip the same
statements, and produce the same uncaught headline message on every hosted execution path.

## 6.6 Freestanding boundary

`THROW` is not available under `#RUNTIME NONE` in this RFC. The shared frontend MUST emit a
deterministic source-located diagnostic explaining that hosted runtime unwinding is unavailable.

A freestanding backend MUST NOT lower `THROW` to a CPU fault, halt, return code, print operation,
infinite loop, or ignored statement.

------------------------------------------------------------------------

# 7. Architecture

## 7.1 Compilation and execution flow

```text
THROW source statement
        |
        v
shared lexer and parser
        |
        v
canonical Throw AST node
        |
        v
semantic/profile validation
        |
        v
THROW A-MIR operation
        |
        v
hosted bytecode THROW opcode
        |
        v
nearest active TRY handler or top-level diagnostic
```

No later stage may reparse source text or infer a throw from a function-call spelling.

## 7.2 Canonical AST contract

The canonical AST MUST add one statement kind representing `THROW` with exactly one child
expression in a stable role such as `message`.

The AST dump MUST expose the statement and its expression deterministically. An illustrative form
is:

```text
Throw
  Literal "invalid genome"
```

Equivalent formatting is acceptable when it follows existing AST conventions.

## 7.3 Runtime error representation

The implementation SHOULD use one internal error carrier capable of preserving the public error
type and message through interpreter and bytecode unwinding.

The internal C++ class name, inheritance, and storage are private implementation details. They MUST
NOT become ArcoBASIC ABI or allow control-flow signals to be caught accidentally.

## 7.4 A-MIR contract

A-MIR MUST contain a canonical operation equivalent to:

```text
THROW messageValue
```

The message operand is the result of ordinary expression lowering. The operation is terminal for
its current normal-control-flow path. No normal fallthrough edge exists after it.

`ArcoFission reveal FILE at A-MIR` MUST display the operation and its operand. It MUST NOT report
the accepted statement as unsupported hosted lowering.

## 7.5 Bytecode contract

Hosted bytecode MUST add one stable opcode equivalent to:

```text
THROW value
```

Executing the opcode validates that the value is a string, then originates a `UserError` carrying
that exact string.

When a handler is active, VM error dispatch uses the same handler stack as existing runtime
failures. When no handler is active, execution returns the error to the top-level source-located
runtime diagnostic path.

## 7.6 Error object contract

When `CATCH name` binds an error, it MUST bind an object with these fields:

```basic
{
    "Message": String,
    "Type": String
}
```

The required `Type` values are:

| Origin | `Type` |
|---|---|
| `THROW` | `UserError` |
| Existing language/runtime failure | `RuntimeError` |

`Message` for `THROW` is exactly the thrown string. `Message` for an existing failure is its
existing runtime diagnostic headline without top-level source-context decoration.

Implementations MAY add fields in a future accepted RFC. Programs MUST NOT infer additional fields
from a particular host exception type in this RFC.

`CATCH` without a name executes normally but binds no error object.

## 7.7 Uncaught diagnostic contract

An uncaught user error MUST use the existing top-level runtime error reporting path. Its first-line
headline is the thrown message. Existing source context MUST identify the executing `THROW`
statement.

Illustrative output:

```text
invalid nucleotide: B
runtime error at line 4, column 5
    THROW "invalid nucleotide: " + base
    ^
```

Exact surrounding CLI decoration and color follow existing runtime conventions. The thrown message
MUST NOT be prefixed with an implementation-language class name.

## 7.8 Nested handler behavior

```text
outer TRY entered
    inner TRY entered
        THROW
    inner CATCH selected
    inner CATCH throws
outer CATCH selected
```

Handler-stack entry, exit, and unwinding MUST remain balanced for normal completion, caught errors,
returns, loop control, and nested errors.

------------------------------------------------------------------------

# 8. User Experience

Library validation becomes direct and readable:

```basic
FUNCTION DecodeGenome(bits)
    IF LEN(bits) MOD 2 <> 0 THEN
        THROW "Genome bit length must be a multiple of 2"
    END IF
    ' decode valid input
END FUNCTION
```

Callers may recover:

```basic
TRY
    genome = DecodeGenome([0, 1, 1])
CATCH err
    PRINT err.Type + ": " + err.Message
END TRY
```

Expected output:

```text
UserError: Genome bit length must be a multiple of 2
```

Uncaught errors keep the existing runtime diagnostic experience:

```basic
THROW "configuration is invalid"
```

The programmer does not need to understand C++ exceptions, bytecode handlers, or VM internals.

------------------------------------------------------------------------

# 9. Developer Experience

`THROW` is a statement, not a host helper. Tooling can recognize it in tokens, AST output, A-MIR,
bytecode reveal output, source diagnostics, and syntax help.

Library authors should use `THROW` when their API cannot return its promised value because caller
input violates a documented precondition. They should continue using ordinary return values for
expected alternatives that are part of normal control flow.

Recommended messages identify the failed contract and, when safe, the invalid value:

```basic
IF rate < 0 OR rate > 1 THEN
    THROW "Mutation rate must be between 0 and 1"
END IF
```

The implementation must not require authors to construct error objects manually. `CATCH` creates
the public object from the propagated error.

Parser diagnostics should distinguish missing syntax from runtime type errors:

```text
expected an expression after THROW
```

```text
THROW message must be String; received Number
```

------------------------------------------------------------------------

# 10. Security Considerations

Thrown messages may cross abstraction boundaries and appear in logs, terminal output, test
reports, or API responses. Programs SHOULD NOT include passwords, tokens, cryptographic material,
private file contents, or other secrets in error messages.

`THROW` does not grant access to host exception internals or stack memory. Catch objects expose only
the fields defined by this RFC.

An attacker may trigger validation failures repeatedly. Implementations MUST preserve existing
instruction and resource limits while unwinding and handling errors. Error creation MUST NOT bypass
sandbox accounting.

Catch behavior must not intercept program-exit or loop-control signals; doing so could bypass
cleanup or control-flow safety assumptions.

------------------------------------------------------------------------

# 11. Privacy Considerations

The language collects and stores no new information. Error messages exist only as program values
or diagnostics unless the program explicitly persists or transmits them.

Library documentation SHOULD warn when an invalid input value may contain personal information and
SHOULD recommend a generic message instead of echoing that value.

The runtime MUST NOT automatically attach environment variables, paths, usernames, hostnames,
stack traces, or source contents to the catch object.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Thrown and caught errors are textual and do not depend on color, sound, animation, timing, or
pointer interaction.

Diagnostics MUST remain understandable when ANSI color is disabled. The message and source
location MUST not be communicated solely through caret color or terminal styling.

Documentation SHOULD recommend concise, actionable messages that identify the invalid contract.

------------------------------------------------------------------------

# 13. Performance Considerations

Normal execution outside `TRY` and `THROW` should retain existing performance characteristics.

Throwing is exceptional control flow. Implementations MAY use host exceptions or explicit VM
handler dispatch internally; no performance guarantee is made for repeated throwing.

Handler-stack operations must remain proportional to active nested `TRY` depth. Creating the
required two-field error object is constant-size apart from the message string.

No stack trace capture, cause-chain allocation, or error-class registry is required.

------------------------------------------------------------------------

# 14. Compatibility

`THROW` becomes a reserved keyword. Existing programs that use `throw` as an unqualified variable,
function, class, field declaration name, or other identifier may require renaming. Property names
inside object literals and quoted storage data remain ordinary strings.

The repository is in alpha and currently documents no `THROW` identifier API. The implementation
MUST nevertheless add a lexer/parser regression proving the keyword's intended reservation.

Existing `TRY` / `CATCH` programs continue to behave the same except that interpreter catch objects
gain the `Type = "RuntimeError"` field already produced by the bytecode VM. This is an additive
object-field change.

RFC-0012 remains authoritative: lexer, parser, canonical AST, semantic/profile validation, A-MIR,
hosted bytecode, tests, and documentation must change together. Native runtime capsules consume
the hosted bytecode behavior and must match the interpreter.

`#RUNTIME NONE` source that uses `THROW` changes from an unknown or invalid statement diagnostic to
the explicit unsupported-runtime diagnostic required by this RFC.

------------------------------------------------------------------------

# 15. Reference Implementation

The following source is normative for observable control flow:

```basic
FUNCTION Positive(value)
    IF value <= 0 THEN
        THROW "Positive requires a value greater than zero"
    END IF
    RETURN value
END FUNCTION

TRY
    PRINT Positive(0)
    PRINT "unreachable"
CATCH err
    PRINT err.Type
    PRINT err.Message
END TRY

PRINT "continued"
```

Expected output:

```text
UserError
Positive requires a value greater than zero
continued
```

Illustrative interpreter internals:

```text
evaluate message
if message is not String:
    originate RuntimeError("THROW message must be String; received " + type)
originate UserError(message)
```

Illustrative handler binding:

```text
catch(error):
    err = {
        "Message": error.publicMessage,
        "Type": error.userDefined ? "UserError" : "RuntimeError"
    }
```

------------------------------------------------------------------------

# 16. Testing Strategy

## 16.1 Lexer and parser tests

Tests MUST cover:

- `THROW` tokenization as a reserved keyword;
- a literal string message;
- a computed string message;
- use inside functions, methods, loops, imports, and catch bodies;
- colon-separated statements after `THROW`;
- missing expression diagnostics; and
- deterministic AST output.

## 16.2 Interpreter tests

Tests MUST cover:

- caught `UserError` message and type;
- uncaught message and source location;
- expression evaluation exactly once;
- message-expression failure preserving `RuntimeError`;
- non-string message rejection;
- statements after `THROW` not executing;
- nearest nested handler selection;
- catch-body errors propagating outward;
- propagation across function and method calls;
- catch without an identifier;
- existing runtime errors binding both `Message` and `Type`; and
- existing return, loop-control, goto, stop, and exit behavior remaining unchanged.

## 16.3 Compiler and bytecode tests

Tests MUST validate:

- canonical AST representation;
- A-MIR `THROW` representation and terminal behavior;
- stable bytecode opcode serialization;
- bytecode handler-stack balance;
- caught and uncaught execution through `compile-run`;
- serialized bytecode execution;
- hosted native runtime capsule execution; and
- exact interpreter/capsule output parity.

## 16.4 Profile tests

A `#RUNTIME NONE` fixture MUST reject `THROW` with the required hosted-unwinding diagnostic. Existing
freestanding tests must remain green.

## 16.5 Regression tests

The complete runtime, shell, compiler, Arcology OS, and Arcology Commons test suites MUST pass.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

## 17.1 Implementation boundaries

An implementation agent SHALL:

1. inspect the current `TRY` / `CATCH` interpreter and bytecode implementations;
2. record and run the unchanged baseline test suites;
3. add exactly one `THROW` keyword and statement grammar;
4. add a canonical AST kind with one message expression;
5. add deterministic hosted-profile validation;
6. add one terminal A-MIR operation and one hosted bytecode opcode;
7. preserve user-error type and message through interpreter and VM unwinding;
8. normalize ordinary interpreter catch objects with `Type = "RuntimeError"`;
9. add the complete tests required by section 16;
10. update implementation status, language reference, shell help, and examples; and
11. produce an implementation report with exact commands and results.

## 17.2 Prohibited implementation choices

An implementation agent SHALL NOT:

- implement `THROW` as a registered host function;
- provoke an unrelated runtime failure to simulate throwing;
- reparse source in A-MIR or bytecode stages;
- broaden which existing return, loop-control, or program-exit signals are caught;
- accept arbitrary payload types;
- silently convert a non-string message;
- add `FINALLY`, typed catches, rethrow, stack traces, or exception classes;
- route `#RUNTIME NONE` through hosted unwinding; or
- add a third-party exception or diagnostics dependency.

## 17.3 Required deliverables

- lexer token and keyword registration;
- parser/interpreter statement implementation;
- canonical AST support;
- profile validation;
- A-MIR and bytecode representation;
- hosted VM and native capsule execution;
- normalized catch error objects;
- lexer, parser, runtime, compiler, integration, profile, and regression tests;
- documentation and one runnable `.abas` example; and
- an implementation report mapping RFC requirements to files and tests.

## 17.4 Acceptance criteria

Implementation is complete only when:

- the reference program in section 15 produces the exact expected output;
- caught user errors bind exact `Message` and `Type` values;
- uncaught errors retain existing source-located diagnostics;
- non-string and failed message expressions behave as specified;
- nested and cross-function unwinding select the correct handler;
- A-MIR and bytecode show explicit throw operations;
- interpreter, bytecode, and hosted native capsule outputs match;
- freestanding use fails clearly;
- prior control-flow signal behavior remains unaffected;
- no existing tests regress; and
- documentation describes both use and information-disclosure risk.

## 17.5 Stop conditions

The agent MUST stop and report instead of guessing if:

- the canonical AST cannot represent a terminal expression-bearing statement;
- the bytecode handler model cannot distinguish user errors from control-flow signals;
- hosted native capsules cannot preserve interpreter-equivalent handler behavior;
- source-location decoration cannot identify the executing `THROW` statement;
- reserving `THROW` conflicts with an existing documented public identifier;
- profile validation cannot reject the statement before freestanding lowering;
- implementing the feature requires arbitrary payload semantics or a public exception hierarchy;
  or
- a complete implementation would require a third-party dependency or unrelated runtime redesign.

## 17.6 Assumptions and dependencies

This RFC assumes the existing shared lexer/parser, canonical AST, RFC-0012 pipeline, runtime source
context, hosted bytecode handler stack, and `TRY` / `CATCH` syntax remain authoritative.

Private internal exception types and bytecode opcode numbers may be selected without stopping when
they preserve the public behavior specified here.

------------------------------------------------------------------------

# 18. Future Extensions

- Stable user-defined error codes.
- Structured immutable error metadata.
- Explicit cause chaining.
- A bare rethrow statement.
- `FINALLY` with precisely defined control-flow precedence.
- Typed error categories or class-backed errors.
- Optional stack traces under a debugger profile.
- A separately specified freestanding failure and recovery model.
- Cross-task error transport after task semantics exist.

------------------------------------------------------------------------

# 19. Open Questions

1. Should a future structured-error RFC extend `THROW` to accept an immutable error object, or add a
   separate constructor while keeping this string form stable?
2. Should a future rethrow form preserve the original source location as a distinct field?
3. Should debugger builds attach an opt-in stack trace that remains absent from ordinary catch
   objects?

None of these questions blocks the string-only hosted feature defined here.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology Request for Comments Process.
- RFC-0007, ArcoBASIC Interactive Program Model.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.
- RFC-0021, Deterministic Pseudorandom Number Generation.
- RFC 2119, *Key words for use in RFCs to Indicate Requirement Levels*.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---------|------|---------|
| 0.1 | 2026-08-14 | Initial draft defining string-only user runtime errors and normalized catch objects. |
