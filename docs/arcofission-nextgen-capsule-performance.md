# Next-Generation ArcoFission Capsule Performance Notes

This document captures performance observations from the current ArcoCapsule
executable format and implementation. It is intended as planning input for the
next generation of the Fission compiler.

## Current Capsule Shape

The default ArcoCapsule path is a native executable on the outside and the hosted
ArcoFission bytecode VM on the inside.

Current Linux capsule construction:

1. ArcoFission preprocesses, parses, and lowers source to A-MIR.
2. A-MIR is lowered to hosted bytecode.
3. Hosted bytecode is serialized into the binary `ARCOFBC1` capsule payload.
4. ArcoFission writes a temporary C++ launcher source file.
5. The launcher embeds the bytecode as a C++ string literal.
6. The system C++ compiler links the launcher against ArcoBASIC runtime/compiler
   archives.
7. At process startup, the launcher reconstructs the bytecode string and calls
   `arco::fission::run_bytecode_binary`.
8. `run_bytecode_binary` parses the binary bytecode into runtime structures,
   prepares the module, registers capsule globals/functions, and executes `Main`.

The current implementation already includes several important optimizations:

- binary bytecode payloads for capsules, instead of textual `.arcof`;
- prepared operands and resolved targets during `prepare_bytecode_module`;
- fused bytecode operations such as local/local arithmetic, local/constant
  arithmetic, local/local branch comparison, constant stores, and local/constant
  indexing;
- reusable bytecode frame buffers to reduce per-function-call allocations;
- reusable call/index scratch vectors on bytecode instructions;
- cached call-site resolution for stable user-function and host-function calls;
- cached lowercased call keys for eligible host calls;
- a narrow hot numeric `FOR` loop JIT;
- opt-in per-function capsule profiling through `ARCOFISSION_DEBUG=1`;
- opt-in JIT diagnostics through `ARCOFISSION_JIT_DIAG=1`;
- `ARCOFISSION_NO_JIT=1` for correctness/performance comparisons.

## Measured Local Baseline

A minimal lean capsule built from:

```basic
PRINT "hello"
```

with:

```sh
build-rivet/ArcoFission native hello.abas -o hello
```

produced these local artifact sizes:

| Artifact | Size |
| --- | ---: |
| unstripped capsule | 20,906,880 bytes |
| stripped capsule | 2,088,920 bytes |

The ELF `text` and `data` sizes were unchanged by stripping:

```text
text=2,072,051 data=11,912 bss=1,393 dec=2,085,356
```

Local process startup for that trivial capsule was already very low, around
`0.002s` wall time on this workstation. That suggests the highest-value format
work is binary size, page-load pressure, startup preparation for larger programs,
and hot runtime dispatch, not basic process creation latency.

## Priority Improvements

### 1. Strip Release Capsules by Default

The quickest win is a release/packaging profile that strips capsules after a
successful build, or a first-class `--strip` / `--release` option on ArcoFission
capsule builds.

Why:

- A minimal lean capsule dropped from about 20 MB to about 2.1 MB after `strip`.
- Smaller capsules reduce disk I/O, package size, cold page faults, cache churn,
  copy time, install time, and distribution cost.
- This does not change ArcoBASIC semantics.

Current source of the size issue:

- The Rivet root build uses `-O2` and `-g` for core targets.
- Capsules link runtime/compiler objects built with debug information.
- Debug information is valuable for development but should not be shipped by
  default in production capsules.

Suggested design:

- Keep debug-symbol capsules available for compiler/runtime debugging.
- Add a release capsule mode that strips by default.
- Optionally emit external debug files later, if symbolized production crash
  analysis becomes important.

### 2. Split Capsule Runtime Out of the Compiler Translation Unit

Default capsules currently need `run_bytecode_binary`, which lives in
`src/compiler/fission.cpp`. Linking that symbol through `libarco_compiler.a` or
`libarco_compiler_core.a` risks pulling in broad compiler implementation code:
front-end reveal paths, bytecode renderers, native builders, UEFI image emission,
Windows/web builders, and support code that capsules do not need to run an
already-compiled payload.

Why:

- Smaller runtime-only capsules.
- Fewer linked objects and relocations.
- Less cold code in the executable.
- Cleaner boundary for next-generation Fission: compiler outputs a capsule;
  capsule runtime executes it.

Suggested design:

- Create a dedicated `arco_capsule_runtime` or `arco_bytecode_vm` library.
- Move these runtime-only pieces out of `fission.cpp`:
  - binary bytecode parser;
  - bytecode module preparation;
  - bytecode executor;
  - capsule launcher entry API;
  - VM/JIT support needed at run time.
- Keep compiler-only features in compiler libraries:
  - source preprocessing orchestration;
  - A-MIR lowering;
  - bytecode generation;
  - textual reveal/rendering;
  - native image builders;
  - target toolchain orchestration.
- Make `ArcoFission.CompileRunSource(source)` optional. It is valuable for web
  tools such as ArcoFlow, but most production capsules should not embed the full
  compiler just to execute precompiled bytecode.

### 3. Introduce a Typed Capsule Bytecode Format

The current `ARCOFBC1` format is binary, but its instruction operands are still
stored as strings. On every capsule launch, the runtime reparses those operand
strings into prepared operands, numeric op enums, local indexes, constant indexes,
and target cursors.

Why:

- String-heavy bytecode increases capsule payload size.
- Startup does extra parsing and allocation before `Main`.
- Large GUI/tool capsules pay this cost every launch.
- Prepared runtime structures already exist; the format should encode more of
  that work directly.

Suggested `ARCOFBC2` direction:

- Header:
  - magic/version;
  - feature flags;
  - endianness marker;
  - source/debug metadata offset;
  - constant table offset;
  - function table offset.
- Constants:
  - typed constant tags;
  - numeric, bool, null, string, tuple/range metadata where applicable.
- Functions:
  - interned function name index;
  - return type index or compact type tag;
  - parameter descriptors or prepared parameter metadata;
  - local count;
  - temp count;
  - block count;
  - instruction stream offset.
- Instructions:
  - opcode byte;
  - compact operand count or opcode-specific fixed layout;
  - operand kind tags;
  - local/temp/constant indexes as integers;
  - inline immediate values where useful;
  - branch targets as block/instruction indexes or block indexes;
  - prepared numeric op enum for arithmetic/compare opcodes.
- Optional debug section:
  - source name;
  - source line mapping;
  - textual local/parameter names;
  - diagnostics.

Compatibility:

- Keep `ARCOFBC1` parser for old capsules during transition.
- Emit `ARCOFBC2` for new capsules by default once stable.
- Provide a reveal tool to dump typed binary bytecode in readable form.

### 4. Avoid Startup Copy of Embedded Bytecode

The generated launcher currently constructs a `std::string` from the embedded
payload before calling `run_bytecode_binary`. That copies the full bytecode
payload at startup.

Why:

- Wasteful for large capsules.
- Avoidable without semantic risk.

Suggested design:

- Add a runtime API accepting `std::string_view` or `(const std::uint8_t*, size)`.
- Emit the payload as a static byte array or static string literal view.
- Parse directly from that immutable memory.

This pairs naturally with `ARCOFBC2`, where the reader should already be a
span/view over bytes rather than an owning string.

### 5. Add Capsule Build Profiles

Capsule builds currently inherit a mixture of development and runtime concerns.
The next-generation compiler should expose explicit profiles.

Suggested profiles:

| Profile | Behavior |
| --- | --- |
| `debug` | Keep symbols, no strip, enable diagnostics, preserve maximum source/debug metadata. |
| `release` | Optimized runtime, strip by default, keep only necessary metadata. |
| `size` | Optimize for smaller artifact, prefer lean runtime, strip, optional compressed sections. |
| `profile` | Optimized build plus runtime profiling support and symbol retention. |
| `web-release` | Separate `.wasm` by default for HTTP/CDN deployment, optimized for browser load/cache. |
| `web-single-file` | Single `.html` artifact for direct open and simple distribution. |

Rivet should expose these as first-class `ArcoCapsuleTarget` options rather than
forcing every caller to know ArcoFission flags and environment variables.

### 6. Specialize or Remove Instruction Counting for Release Capsules

The bytecode executor checks instruction-counting state in the hot dispatch loop.
This is necessary for bounded hosted execution, but many production capsules run
trusted source and do not need an instruction budget.

Why:

- One branch per bytecode instruction is small but hot.
- The current design already supports unlimited execution; the next step is to
  make that cheaper in release capsules.

Suggested design options:

- A separate no-count executor entry point.
- A compile-time template or policy object for counting versus non-counting.
- A module flag in `ARCOFBC2` that routes to the correct executor once before
  dispatch begins.

Correctness requirement:

- Keep instruction limits available for hosted tools, untrusted source, tests,
  and explicitly budgeted capsules.

### 7. Broaden JIT Coverage

The current hot numeric loop JIT is deliberately narrow: canonical counting
`FOR` loops with numeric slots and a straight-line body. That made it safe to
ship, but it leaves many real programs in interpreter dispatch.

Promising next targets:

- numeric `WHILE` loops;
- more `FOR` loop body shapes;
- local/constant branch forms;
- array numeric loops;
- simple induction-variable patterns;
- additional inlinable leaf functions;
- straight-line basic blocks without calls;
- selected arithmetic expression trees rather than only fused statement shapes.

Constraints:

- Preserve dynamic ArcoBASIC semantics.
- Keep runtime type guards.
- Fall back to interpreter execution on type mismatch or unsupported shape.
- Keep `ARCOFISSION_NO_JIT=1` parity tests.
- Expand `ARCOFISSION_JIT_DIAG=1` coverage so tests prove the native path is
  actually used.

### 8. Reduce Per-Instruction Dynamic Work Further

The current executor already prepares operands, targets, call keys, numeric ops,
and some call-site resolution. Remaining candidates:

- Replace more string comparisons in dispatch with prepared enum values.
- Add opcode-specific instruction structs or a compact decoded instruction union.
- Pre-size object literals where field count is known.
- Preserve prepared object-field keys instead of splitting `key:value` operands
  on every `OBJECT` instruction execution.
- Add more fused operations for common GUI/game patterns.
- Cache class method lookup more aggressively where receiver class is stable.

These are medium-risk changes: they touch runtime semantics and need broad
interpreter/bytecode/capsule parity tests.

### 9. Separate Web Performance Modes

Web capsules currently optimize for "open the `.html` directly and it works."
That is the right default for demos and local distribution, but not always for
production web deployment.

Suggested split:

- Direct-open mode:
  - single-file HTML;
  - embedded wasm;
  - custom shell;
  - maximum portability.
- HTTP deployment mode:
  - separate `.wasm`;
  - cacheable binary payload;
  - no base64 decode cost;
  - optional no-Asyncify mode for console/compute capsules.

Asyncify should remain available for GUI capsules that rely on blocking-style
`GUI.WaitEvent`, but console-only or callback-oriented web capsules should be
able to opt out.

## Fissure Regression Needs

Before changing the format or runtime split, add regression probes that measure
or assert:

- minimal capsule size before and after strip/release profile;
- startup output parity for `compile-run`, textual bytecode, and native capsule;
- `Args` propagation in capsules;
- `Exit()` / `ExitTheProgram()` exit-code behavior;
- instruction-limit preservation and override behavior;
- JIT enabled/disabled output parity;
- JIT diagnostic proof for fixtures intended to take the native path;
- lean versus full runtime selection;
- web capsule output shape selection;
- Windows PE32+ capsule validity;
- `ARCOFBC1` backward compatibility after `ARCOFBC2` lands.

Add performance smoke tests carefully. They should not fail on noisy timing alone;
prefer artifact-size ceilings, structural checks, and optional benchmark output
that Fissure can collect without making CI flaky.

## Recommended Development Order

1. Add release/strip capsule profile.
2. Add Fissure size/startup/performance reporting probes.
3. Split runtime-only bytecode execution out of `fission.cpp`.
4. Make `ArcoFission.CompileRunSource` optional per capsule target/profile.
5. Add zero-copy bytecode runtime API.
6. Design and implement `ARCOFBC2`.
7. Expand JIT coverage with parity and diagnostic tests.
8. Add web deployment profiles.

The first two items are low-risk and provide immediate feedback. The runtime
split and typed bytecode format are the core architectural work for the next
generation of Fission.
