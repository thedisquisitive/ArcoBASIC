# RFC-0030: Deferred Cleanup and Scoped Resources

**RFC Number:** RFC-0030  
**Title:** Deferred Cleanup and Scoped Resources  
**Status:** Draft  
**Category:** Language / Hosted Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0015, RFC-0017, RFC-0022, RFC-0023

------------------------------------------------------------------------

# 1. Executive Summary

This RFC adds `DEFER` and `USING` so explicit runtime resources are released on normal return,
early return, or error propagation. PetriBrain can own `RANDOM` handles safely without repeating
cleanup at every exit.

------------------------------------------------------------------------

# 2. Motivation

Explicit handles have clear ownership but manual destruction is fragile when a function has
validation errors and multiple returns. ArcoBASIC needs deterministic structured cleanup without a
garbage collector or class destructors.

------------------------------------------------------------------------

# 3. Goals

- Execute deferred call statements in last-in-first-out order.
- Scope hosted runtime resources with automatic registered destruction.
- Define precedence with `RETURN`, loop control, `STOP`, and `THROW`.

------------------------------------------------------------------------

# 4. Non-Goals

- Finalizers, garbage collection, arbitrary block rewinding, async disposal, transaction rollback,
  or freestanding exception cleanup.

------------------------------------------------------------------------

# 5. Terminology

**Deferred Action:** A call statement scheduled for scope exit.  
**Using Scope:** A block owning one runtime handle with a registered destructor.  
**Pending Transfer:** A return, control signal, or runtime error active while cleanup runs.

------------------------------------------------------------------------

# 6. Requirements

`DEFER callExpression` MUST register an expression-call statement in the current function frame, or
the program frame at top level. Arguments and receiver MUST be captured when `DEFER` executes; the
call occurs later. Deferred actions execute LIFO on normal fallthrough, `RETURN`, uncaught runtime
error, RFC-0022 `THROW`, `STOP`, and program exit. Loop `EXIT`/`CONTINUE` do not exit a function
frame and therefore do not run function defers.

The parser MUST also accept:

```basic
USING rng AS RANDOM = Random.Create(seed)
    RunEvolution(rng)
END USING
```

The value MUST be a live explicit hosted handle with a registered destructor. Cleanup runs once on
every exit from the block. The variable is local to the block and becomes invalid after cleanup.

All scheduled cleanups MUST be attempted LIFO. A cleanup failure becomes the pending error and
overrides a pending return or earlier error; later cleanup failures replace it in turn. This
simple, deterministic rule matches `FINALLY`-style precedence and MUST be documented prominently.
Control-transfer signals MUST never be swallowed when cleanup succeeds.

------------------------------------------------------------------------

# 7. Architecture

Interpreter and VM frames gain cleanup stacks. `USING` lowers to acquisition, cleanup registration,
body, and one guarded scope-exit edge. Runtime handle kinds expose private registered destructor
callbacks through the resource system.

------------------------------------------------------------------------

# 8. User Experience

```basic
FUNCTION Evolve(seed)
    LET rng AS RANDOM = Random.Create(seed)
    DEFER Random.Destroy(rng)
    RETURN RunEvolution(rng)
END FUNCTION
```

------------------------------------------------------------------------

# 9. Developer Experience

Only call expressions may follow `DEFER`. Diagnostics MUST reject non-handle `USING` values and
handle kinds lacking cleanup. Reveal output MUST show registration and cleanup edges.

------------------------------------------------------------------------

# 10. Security Considerations

Cleanup executes under ordinary instruction and capability limits. A program cannot register a
private host destructor directly. Double destruction remains an error unless prevented by `USING`.

------------------------------------------------------------------------

# 11. Privacy Considerations

No information is collected. Cleanup diagnostics follow ordinary error privacy guidance.

------------------------------------------------------------------------

# 12. Accessibility Considerations

Structured cleanup reduces hidden lifetime assumptions. Documentation MUST explain LIFO ordering
and overriding cleanup errors with linear examples.

------------------------------------------------------------------------

# 13. Performance Considerations

Registration is constant time; scope exit is linear in registered actions. Frames with no cleanup
SHOULD retain current performance.

------------------------------------------------------------------------

# 14. Compatibility

`DEFER` and `USING` become reserved. Existing explicit destroy calls remain valid. RFC-0022 and
current control-flow semantics remain unchanged except for registered cleanup execution.

------------------------------------------------------------------------

# 15. Reference Implementation

```basic
FUNCTION Cleanup()
    PRINT "cleanup"
END FUNCTION

FUNCTION Demo(rng)
    DEFER Cleanup()
    PRINT "body"
    RETURN 7
END FUNCTION
PRINT Demo(NULL)
```

Expected output is `body`, `cleanup`, then `7`.

------------------------------------------------------------------------

# 16. Testing Strategy

Tests MUST cover capture timing, LIFO order, every exit path, nested frames/scopes, cleanup errors,
all-cleanups-attempted behavior, handle destruction exactly once, stale handles, instruction limits,
AST/A-MIR/bytecode/capsules, and freestanding rejection.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

Implement frame cleanup stacks and exhaustive control-flow tests before `USING`. Reuse resource
registry destructors. Do not add garbage collection, silently ignore cleanup errors, or expose host
destructor pointers. Stop if internal return/loop/error signals cannot be distinguished reliably.

------------------------------------------------------------------------

# 18. Future Extensions

Lexical `DEFER` scopes, error suppression objects, async disposal, and transactional cleanup groups.

------------------------------------------------------------------------

# 19. Open Questions

None; last cleanup failure wins when cleanup itself fails.

------------------------------------------------------------------------

# 20. References

- RFC-0015, Runtime Object Handles.
- RFC-0017, Substrate Resource Model.
- RFC-0022, User-Defined Runtime Errors.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---|---|---|
| 0.1 | 2026-08-14 | Initial deferred cleanup and using-scope proposal. |
