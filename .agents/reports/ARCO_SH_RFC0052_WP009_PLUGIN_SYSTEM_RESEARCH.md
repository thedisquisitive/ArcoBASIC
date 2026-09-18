# RFC-0052 WP-009 (Plugin System) — Research

**Date:** 2026-09-18

AP-0052-012 requires WP-009 to implement real plugin loading: discovery, enable/disable,
deterministic load order, capability enumeration, plugin diagnostics, and recovery launch with
plugins disabled, across at least the `command`, `completion`, `prompt.segment`, and `theme`
capability types (RFC-0052 section 19). Section 19.1's own conceptual API —
`Shell.RegisterCommand(...)`, `Shell.RegisterPromptSegment(...)`, `Shell.RegisterTheme(...)`,
`Shell.RegisterCompleter(...)` — implies a plugin registers a genuine, repeatedly-invokable
callback, not just static data. Before writing any of that, this pass had to answer one load-bearing
question: **can a plugin, loaded dynamically at runtime from an arbitrary `.abas` file, hand the
natively-compiled arcosh program a callback it can invoke later, more than once?**

## Finding: yes, but only through one specific, non-obvious path

ArcoSH is natively-compiled (RFC-0052 section 6); a plugin's source is arbitrary text discovered on
disk, so loading it necessarily means running it through the embedded interpreter via
`Runtime.RunString`/`Runtime.EvalImmediate` (`src/runtime/runtime.cpp`) — there is no other way to
turn unknown-at-compile-time `.abas` text into running behavior. ArcoBASIC's own first-class
callables (RFC-0027, `ADDRESSOF Name` → a `CALLABLE` value, invoked via `runtime(args...)`) are the
obvious mechanism for a plugin to hand over a repeatable callback. Three things had to each
independently work for this to be viable, and none of them could be assumed from reading the code
alone — each was confirmed (or refuted) by a small, throwaway native-compiled `.abas` test file
built and run directly, never by inspection alone:

1. **Does a plain VALUE set via `Runtime.SetGlobal` inside a `RunString`/`EvalImmediate`-executed
   script reach the native host's own `Runtime.GetGlobal`?** No, in either direction. Confirmed by
   direct testing: even a plain string set by native code before the call was read back as `NULL`
   from inside the interpreted script, and vice versa. Root cause, found by reading the source
   directly: `Runtime.SetGlobal`/`GetGlobal`, when the calling program is bytecode/interpreted, read
   and write `this->globals_` — a member of whichever specific `arco::Runtime` C++ object is
   executing (a fresh, disposable `Runtime nested;` for every single `RunString` call, a
   function-local `static Runtime session;` for `EvalImmediate`). Native code's OWN
   `Runtime.SetGlobal`/`GetGlobal` compile to dedicated calls into `arco_global_set`/`arco_global_get`
   (`src/native/runtime_abi.cpp`), backed by a completely separate process-wide
   `global_store()` map. These are two independent storage mechanisms that were never unified,
   because nothing before this pass ever needed to move a value from an ad hoc interpreted script
   back into the native host.

2. **Does an ordinary (non-callable) VALUE cross that same boundary through a NEW, purpose-built
   host primitive with real process-wide static storage instead?** Yes, confirmed directly, once an
   actual bug in the first attempt (see below) was found and fixed. A brand-new pair of primitives
   sharing ONE `static std::unordered_map<std::string, Value>` declared once (not duplicated inside
   each lambda — a real mistake made and caught in this same pass: two lambdas each declaring their
   own `static` map are NOT the same map) correctly carried a string value from native code into an
   interpreted `RunString` script and back out again, both directions, confirmed empirically.

3. **Does a genuine `CALLABLE` value survive being stored that way and get invoked later, from a
   different execution context, more than once?** No — not with `Runtime.RunString`, and this is
   the real finding. `Runtime::call_callable` (`src/runtime/runtime.cpp`) resolves a callable's
   underlying `CallableDescriptor` by NAME against `this` Runtime instance's OWN function table
   (`call_host_function`) — `this` being whichever specific Runtime object is doing the invoking.
   A callable produced by compiling a plugin's source into `RunString`'s own throwaway `nested`
   Runtime becomes a dangling reference the instant `RunString` returns and `nested` is destroyed:
   trying to invoke it afterward, from a different Runtime instance (native code's own persistent
   host-bridge Runtime, `src/native/host_bridge.cpp`), fails with a deliberate, explicit runtime
   error — **"value is not a live CALLABLE"** — not a crash and not a silent wrong answer, matching
   RFC-0027's own explicit non-forgeability/non-persistence intent for callables. This is a real,
   confirmed architectural wall for the literal "load via `RunString`, store the callable, invoke it
   whenever" design.
   Retrying with a **dedicated, process-lifetime `Runtime` instance used ONLY for plugin
   loading** (a free function returning a function-local `static Runtime`, exactly
   `Runtime.EvalImmediate`'s own already-proven `static Runtime session` pattern, generalized so
   more than one host function can reach the same instance) fixed it completely: a callable
   produced by running a plugin's source through that one persistent instance stayed genuinely live
   and was invoked correctly, repeatedly, from native code, well after the load call had returned.
   One real implementation pitfall hit and fixed along the way: declaring that persistent instance
   as a plain `static Runtime plugin_runtime;` directly inside `Runtime::Runtime()`'s own
   constructor body deadlocks (`std::recursive_init_error`) — constructing that static requires
   calling `Runtime::Runtime()` again, which is the exact function already running. Wrapping it in
   its own free function (lazily constructed the first time a REGISTERED LAMBDA is actually
   *called*, long after any constructor has returned, not while one is still running) fixes this.

## Conclusion and design consequence

The plugin system RFC-0052 section 19 describes — plugins registering genuine, repeatable
callbacks for `command`/`prompt.segment`/`completion` — **is buildable within ArcoSH's existing
native-compiled architecture**, with no incompatible language change and no major compiler backend
work (AP-0052-018 conditions 1/3/4 do not apply). It requires exactly one specific, non-obvious
architectural choice that would not follow from a naive reading of the RFC: plugin source MUST be
loaded through one dedicated, process-lifetime `Runtime` instance (never the ordinary, disposable
one `Runtime.RunString` already provides for its own, different purpose — re-running the resident
numbered program from a clean slate on every `RUN`), and every later invocation of anything a
plugin registered MUST route through that exact same instance. `theme` (a plain `OBJECT`) and any
other pure-data capability don't strictly need this — finding 2 alone would suffice for those — but
`command`/`prompt.segment`/`completion` all fundamentally require finding 3's specific fix, so the
whole plugin system is built on the one persistent-runtime mechanism throughout, rather than mixing
two different loading strategies for different capability types.
