# RFC-0033: Language Assertions and Test Runner

**RFC Number:** RFC-0033  
**Title:** Language Assertions and Test Runner  
**Status:** Draft  
**Category:** Language / Tooling  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0022, RFC-0031, RFC-0034

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds `ASSERT`, declarative `TEST` blocks, and an `arco test` runner. PetriBrain's genome,
mutation, crossover, and simulation contracts can be translated into isolated ArcoBASIC tests with
source-located failures and deterministic summaries.

------------------------------------------------------------------------

# 2. Motivation

Shell smoke scripts can test programs but provide no language-level assertion context, discovery,
per-test isolation, or concise expected-error checks.

------------------------------------------------------------------------

# 3. Goals

- Add assertions usable in ordinary code and tests.
- Add named test blocks excluded from normal program execution.
- Discover and run tests reproducibly with useful exit status and diagnostics.

------------------------------------------------------------------------

# 4. Non-Goals

- Mocking, coverage, property generation, snapshots, parallel execution, IDE protocol design, or
  compatibility with pytest syntax and plugins.

------------------------------------------------------------------------

# 5. Terminology

**Assertion Failure:** A source-defined test failure distinct from an ordinary runtime error.  
**Test Block:** A named top-level body registered for the test runner.  
**Test File:** A source matching the runner's discovery convention.

------------------------------------------------------------------------

# 6. Requirements

The parser MUST accept:

```basic
ASSERT condition
ASSERT condition, "message"

TEST "genome round trip"
    ASSERT Decode(Encode("ACGT")) = "ACGT"
END TEST
```

The condition is evaluated once. False produces an `AssertionError` with optional message and
source location. Assertions execute normally outside test blocks.

The initial expected-error form MUST be:

```basic
ASSERT THROWS Decode(invalidBits), "multiple of 2"
```

It evaluates one call expression, passes only if a runtime error occurs, and optionally requires
the error message to contain the supplied string. It MUST NOT catch `RETURN`, loop control,
`STOP`, process exit, or instruction-limit termination.

`TEST` is valid only at top level, has a non-empty unique literal name per file, and does not run
during ordinary script execution. Each test runs in a fresh runtime after module loading so global
mutation, random defaults, handles, and instruction counts do not leak between tests.

`arco test [PATH...]` MUST discover files named `test_*.abas` or `*_test.abas`, sort canonical paths
lexicographically, run tests in source order, print pass/fail/error counts, and return nonzero when
any test fails or errors. `--filter TEXT` and `--list` MUST be supported.

------------------------------------------------------------------------

# 7. Architecture

AST/A-MIR/bytecode gain assertion statements and test metadata. The runner compiles each file,
enumerates blocks, and creates isolated runtimes using the same hosted execution path as normal
programs.

------------------------------------------------------------------------

# 8. User Experience

```text
$ arco test tests
PASS genome round trip
FAIL invalid nucleotide: expected error message containing "Invalid nucleotide"
1 passed, 1 failed, 0 errors
```

------------------------------------------------------------------------

# 9. Developer Experience

Failures MUST show file, test name, line, asserted source text, and message. `HELP assert`,
`HELP test`, and command help MUST distinguish assertion failures from unexpected runtime errors.

------------------------------------------------------------------------

# 10. Security Considerations

Tests have only the capabilities of their runner profile and remain subject to limits. Discovery
MUST avoid symlink cycles and hidden build trees by default. Expected-error assertions cannot catch
termination signals.

------------------------------------------------------------------------

# 11. Privacy Considerations

Failure output may contain evaluated messages. The runner MUST not upload results or source.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Pass/fail/error states require words and exit status, not color alone. Summaries must remain readable
with color disabled.

------------------------------------------------------------------------

# 13. Performance Considerations

Fresh runtimes favor isolation over speed. Parsed immutable module artifacts MAY be cached when no
state is shared. Parallel tests are deferred.

------------------------------------------------------------------------

# 14. Compatibility

`ASSERT` and `TEST` become reserved. Ordinary programs without test blocks are unchanged. RFC-0022
errors remain runtime errors; assertion failures add the public `AssertionError` type.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
TEST "addition"
    ASSERT 2 + 2 = 4
END TEST
```

The runner reports one passing test; ordinary execution prints nothing.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST test assertions inside/outside blocks, messages, expected errors, forbidden signals,
duplicate names, discovery order, filters, list mode, runtime isolation, exit codes, malformed
files, module loading, AST/A-MIR/bytecode, capsule exclusions, and color-free output.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement assertion semantics and diagnostics before runner discovery. Reuse RFC-0022 error
carriers without catching control signals. Do not share mutable runtimes or add pytest compatibility.
Stop if test blocks execute during ordinary entry-point execution.

------------------------------------------------------------------------

# 18. Future Extensions

Setup/teardown blocks, parameterized tests, property testing, coverage, tags, and machine-readable
reports.

------------------------------------------------------------------------

# 19. Open Questions

None for sequential isolated local testing.

------------------------------------------------------------------------

# 20. References

- RFC-0022, User-Defined Runtime Errors.
- RFC-0031, Isolated Modules and Explicit Exports.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial assertions and test-runner proposal. |
