# RFC-0021: Deterministic Pseudorandom Number Generation

**RFC Number:** RFC-0021  
**Title:** Deterministic Pseudorandom Number Generation  
**Status:** Implemented  
**Category:** Language / Hosted Runtime  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0012, RFC-0015

------------------------------------------------------------------------

# 1. Executive Summary

ArcoBASIC documents `Math.Random()` but the hosted runtime does not currently provide a random
number generator. Programs that perform simulation, games, procedural generation, randomized
testing, sampling, or evolutionary computation therefore cannot be implemented using the
published language surface.

This RFC defines a deterministic pseudorandom number generation service for the hosted ArcoBASIC
runtime. The service provides:

- explicit, independently seeded `RANDOM` generator handles;
- a runtime-owned default generator for convenient scripts;
- stable PCG32 output for reproducible programs and tests;
- unbiased inclusive integer ranges;
- floating-point values in the half-open interval `[0, 1)`;
- choice, sampling, and non-mutating shuffle helpers; and
- `Math.Random()` as a compatibility alias for the default generator.

The expected outcome is that ArcoBASIC programs can choose between convenient nondeterministic
execution and exactly reproducible seeded execution without depending on host-language random
APIs. This service is not cryptographic randomness and does not authorize freestanding or UEFI
random-number lowering.

------------------------------------------------------------------------

# 2. Motivation

Randomized applications require more than a single undocumented source of host randomness. They
need a stable contract for range boundaries, seeding, independent streams, error behavior, and
reproducibility.

The current repository creates a misleading compatibility gap: `README.md` lists
`Math.Random()`, while calling that function produces an `unknown host function` runtime error.
There is no alternative `Random.*` API in the interpreter, standard library, or hosted runtime.

Using a host implementation such as C++ standard-library distributions directly would leave
observable sequences implementation-defined. The same seeded ArcoBASIC program could then produce
different results across standard libraries, operating systems, compiler versions, interpreter
execution, and native runtime capsules. That is unacceptable for deterministic tests, saved
simulations, evolutionary workloads, and reproducible bug reports.

This RFC therefore specifies the engine and distribution behavior as part of the ArcoBASIC runtime
contract.

------------------------------------------------------------------------

# 3. Goals

- Make the documented zero-argument `Math.Random()` call operational.
- Provide explicit generator objects with independent mutable state.
- Produce identical seeded sequences on every conforming hosted runtime.
- Support common random operations without modulo bias.
- Make invalid ranges, empty choices, stale handles, and invalid seeds fail deterministically.
- Preserve the existing lexer, parser, AST, and A-MIR language syntax.
- Add no third-party dependency.
- Provide enough functionality for games, simulation, evolutionary computation, and randomized
  tests.

------------------------------------------------------------------------

# 4. Non-Goals

This RFC does not define:

- cryptographically secure random numbers;
- keys, salts, nonces, authentication tokens, or security-sensitive identifiers;
- entropy quality suitable for security decisions;
- a freestanding, UEFI, firmware, or kernel random service;
- hardware random-number instructions;
- a new literal, statement, operator, or grammar production;
- exact compatibility with Python, C++, JavaScript, or another language's seeded sequences;
- parallel generator access or a concurrency memory model;
- persistence or serialization of generator state;
- weighted choice, probability distributions, or statistical modeling APIs.

------------------------------------------------------------------------

# 5. Terminology

**Pseudorandom Number Generator (PRNG)**  
A deterministic state machine that produces values which are useful as random-looking data but
are not suitable for cryptographic use.

**Generator**  
An identity-bearing hosted runtime resource containing one PRNG state and stream selector.

**Default Generator**  
A generator owned by each `Runtime` instance and used when an API call omits its generator.

**Seed**  
A caller-provided non-negative safe integer used to initialize a reproducible generator state.

**Sequence**  
A caller-provided non-negative safe integer selecting a PCG stream independently of the seed.

**Safe Integer**  
An integral ArcoBASIC `Number` in the inclusive range `0` through `9007199254740991` (`2^53 - 1`).

**Seeded Execution**  
Execution using an explicitly supplied seed and sequence. Its output is reproducible.

**Automatic Seeding**  
Initialization without a caller-supplied seed. Its output is intentionally not reproducible.

------------------------------------------------------------------------

# 6. Requirements

## 6.1 Hosted runtime

The hosted runtime MUST expose the `Random.*` API defined by this RFC.

Each `Runtime` instance MUST own a distinct default generator. Creating or using a generator in
one runtime MUST NOT change another runtime's state.

The runtime MUST use fixed-width unsigned arithmetic for generator state. It MUST NOT implement
the specified state transitions using floating-point arithmetic.

## 6.2 Determinism

Given the same explicit seed, sequence, and ordered series of API calls, conforming
implementations MUST return the same values and reach the same generator state.

The implementation MUST NOT use `std::uniform_*_distribution`, an implementation-defined host
engine, or a platform-dependent distribution to produce seeded results.

## 6.3 Generator ownership

Explicit generators MUST use RFC-0015 opaque runtime handles with the type name `RANDOM`.

Creating a generator is a `Returned` ownership operation. Random operations borrow a generator.
Destroying a generator is a `Consumed` ownership operation.

Copying a `RANDOM` value MUST copy handle identity, not generator state. Two variables containing
the same handle therefore consume one shared sequence.

The default generator is runtime-owned, MUST NOT be exposed as a destroyable handle, and MUST live
until its owning runtime is destroyed.

## 6.4 Profile boundary

The API in this RFC is a hosted runtime service. A `#RUNTIME NONE` program that references it MUST
receive a deterministic unsupported-service diagnostic. A compiler or backend MUST NOT silently
substitute a time source, firmware service, CPU instruction, or constant sequence.

------------------------------------------------------------------------

# 7. Architecture

## 7.1 Component relationship

```text
ArcoBASIC source
      |
      v
Random.* host-call boundary -----> RuntimeHandleTable
      |                                  |
      |                                  v
      +--------------------------> RANDOM resource
                                           |
                                           v
                                    PCG32 state/stream
```

No lexer, parser, AST, or expression syntax change is required. `Random.*` functions follow the
existing case-insensitive hosted function-call model. ArcoFission-hosted runtime capsules resolve
the same public calls through the runtime boundary.

## 7.2 Generator state

Each explicit generator contains exactly the logical state needed by the specified PCG32 engine:

```text
state:     unsigned 64-bit integer
increment: unsigned 64-bit odd integer
```

This representation is private runtime state. ArcoBASIC programs cannot inspect or mutate it.

## 7.3 PCG32 initialization

Initialization uses the PCG XSH RR 64/32 procedure. All arithmetic wraps modulo `2^64`.

For `seed` and `sequence`:

```text
state = 0
increment = (sequence << 1) | 1
advance once and discard the output
state = state + seed
advance once and discard the output
```

The default explicit sequence is `54`.

## 7.4 PCG32 transition and output

One engine step is defined as:

```text
oldState = state
state = oldState * 6364136223846793005 + increment
xorshifted = ((oldState >> 18) XOR oldState) >> 27
rotation = oldState >> 59
output = rotate_right_32(low_32(xorshifted), rotation)
```

The result is an unsigned 32-bit integer. Shifts and rotation operate on unsigned fixed-width
values. An implementation MUST mask intermediate output values to the widths shown above rather
than rely on host signed-shift behavior.

The first six raw outputs after initialization with `seed = 42` and `sequence = 54` MUST be:

```text
a15c02b7
7b47f409
ba1d3330
83d2f293
bfa4784b
cbed606e
```

These hexadecimal values are a normative known-answer vector for engine conformance. They are
raw engine outputs, not the decimal results of `Random.Float` or `Random.Integer`.

## 7.5 Automatic seeding

When no seed is provided, the runtime SHOULD use operating-system entropy. When operating-system
entropy is unavailable, it MAY combine wall-clock time, monotonic time, process-local state, and a
monotonically increasing runtime counter.

Automatic seeding MUST NOT be described as cryptographically secure. Tests MUST NOT assert a
specific automatically seeded sequence or assume that two automatic seeds always differ.

## 7.6 Distribution construction

Higher-level operations MUST be derived from raw PCG32 output as specified here so that
distribution behavior remains portable.

### Unit interval

`Random.Float` consumes two consecutive 32-bit outputs, `a` and `b`, and returns:

```text
((a >> 5) * 67108864 + (b >> 6)) / 9007199254740992
```

This produces a 53-bit-resolution `Number` in `[0, 1)`.

### Inclusive integer range

`Random.Integer(minimum, maximum, generator)` returns an integer in the inclusive range
`[minimum, maximum]`.

The runtime MUST use rejection sampling over one or more raw 32-bit outputs. It MUST NOT use a
simple modulo reduction unless the requested range size evenly divides the raw sample space.

The first implementation MUST accept only safe-integer bounds whose inclusive range size is no
larger than `2^32`. Wider ranges are deferred and MUST fail with a deterministic range error.

### Choice, sample, and shuffle

`Random.Choice` selects an index using the inclusive integer-range operation.

`Random.Sample` performs sampling without replacement and MUST NOT mutate the input array. A
partial Fisher-Yates algorithm is RECOMMENDED.

`Random.Shuffle` returns a shuffled shallow copy and MUST NOT mutate the input array. It MUST use
Fisher-Yates with the inclusive integer-range operation.

------------------------------------------------------------------------

## 7.7 Public API Contract

### 7.7.1 `Random.Create`

```basic
generator = Random.Create(seed = NULL, sequence = 54)
```

Creates and returns a `RANDOM` handle. An omitted or `NULL` seed requests automatic seeding. An
explicit seed requests reproducible initialization.

`seed` and `sequence` MUST be safe integers. `sequence` MUST NOT be `NULL`.

### 7.7.2 `Random.Destroy`

```basic
Random.Destroy(generator)
```

Consumes an explicit `RANDOM` handle. Destroying an invalid, stale, null, or already destroyed
handle MUST fail safely with a deterministic runtime error.

### 7.7.3 `Random.Reseed`

```basic
Random.Reseed(generator, seed, sequence = 54)
```

Reinitializes an explicit generator. `seed` and `sequence` MUST be safe integers. This operation
MUST NOT accept an omitted or `NULL` generator and MUST NOT reseed the default generator.

### 7.7.4 `Random.Float`

```basic
value = Random.Float(generator = NULL)
```

Returns a `Number` greater than or equal to zero and strictly less than one. An omitted or `NULL`
generator uses the runtime's default generator.

### 7.7.5 `Random.Integer`

```basic
value = Random.Integer(minimum, maximum, generator = NULL)
```

Returns an integral `Number` in the inclusive range. Bounds MUST be safe integers and `minimum`
MUST be less than or equal to `maximum`.

### 7.7.6 `Random.Choice`

```basic
value = Random.Choice(values, generator = NULL)
```

Returns one element from `values`. `values` MUST be a non-empty array. The returned value follows
ordinary ArcoBASIC value-copy and reference semantics for that element's type.

### 7.7.7 `Random.Sample`

```basic
values = Random.Sample(source, count, generator = NULL)
```

Returns a new array containing `count` elements selected without replacement. `count` MUST be a
safe integer from zero through `LEN(source)`, inclusive. Duplicate values at different source
indices remain distinct sampling candidates.

### 7.7.8 `Random.Shuffle`

```basic
values = Random.Shuffle(source, generator = NULL)
```

Returns a shuffled shallow copy of `source`. The source array is unchanged.

### 7.7.9 `Math.Random`

```basic
value = Math.Random()
```

`Math.Random()` MUST be a zero-argument compatibility alias for `Random.Float()` using the default
generator. It MUST consume the same default sequence as a direct call to `Random.Float()`.

No overloaded `Math.Random` signatures are introduced by this RFC.

------------------------------------------------------------------------

# 8. User Experience

Convenient scripts can use the default generator:

```basic
PRINT Math.Random()
PRINT Random.Integer(1, 6)
PRINT Random.Choice(["north", "south", "east", "west"])
```

Reproducible programs create an explicit generator:

```basic
rng = Random.Create(12345)

FOR i = 1 TO 5
    PRINT Random.Integer(0, 100, rng)
NEXT

Random.Destroy(rng)
```

Independent simulations use separate handles:

```basic
training = Random.Create(42, 1)
evaluation = Random.Create(42, 2)

PRINT Random.Float(training)
PRINT Random.Float(evaluation)
```

Users MUST be able to rerun a seeded workload and receive the same results without knowing the
host operating system or standard-library implementation.

------------------------------------------------------------------------

# 9. Developer Experience

The runtime implementation should isolate engine mechanics from public function registration.
One internal generator type should own state transition, initialization, raw output, and bounded
sampling. Public host functions should validate ArcoBASIC values, resolve optional handles, and
delegate to that type.

The `RANDOM` handle must use the existing runtime handle table rather than adding a second resource
registry or exposing state in ordinary ArcoBASIC objects.

Adding this service does not authorize parser special cases. Calls are ordinary qualified function
calls and remain case-insensitive under existing language rules.

Errors should name the called API and the invalid contract. Examples:

```text
Random.Integer requires minimum <= maximum
Random.Choice requires a non-empty array
Random.Sample count must be between 0 and the source length
Random.Float received an invalid or destroyed RANDOM handle
Random.Create seed must be a non-negative safe integer
```

------------------------------------------------------------------------

# 10. Security Considerations

PCG32 is not cryptographically secure. Its state can be inferred from output, and a seed does not
provide secrecy.

The documentation and runtime API help MUST warn users not to use `Random.*` or `Math.Random()` for
passwords, session identifiers, authentication tokens, cryptographic keys, salts, nonces, or any
security decision.

Automatic seeding improves variation, not security. A future cryptographic API must use a distinct
namespace and contract so that security-sensitive code cannot accidentally select this PRNG.

Opaque handles prevent accidental state manipulation but do not turn the generator into a secure
resource. Handle validation MUST reject wrong-type and stale handles at every boundary.

------------------------------------------------------------------------

# 11. Privacy Considerations

The deterministic generator collects and stores no user information.

Automatic seeding SHOULD prefer operating-system entropy and SHOULD avoid incorporating usernames,
hostnames, file paths, network addresses, or other stable identifiers. If a fallback seed is used,
it should use transient process and clock state only.

Generator state is process-local and is not persisted, exported, or logged by this RFC.

------------------------------------------------------------------------

# 12. Accessibility Considerations

The API is usable through source code, the REPL, and non-graphical tooling. Correct use does not
depend on color, pointer interaction, animation, timing, or audio.

Runtime errors MUST be textual and identify the invalid argument or handle. Documentation examples
MUST not communicate random outcomes solely through color or position.

Randomized user interfaces SHOULD provide deterministic or manual alternatives when randomness
would impede users who rely on predictable navigation or repeatable output.

------------------------------------------------------------------------

# 13. Performance Considerations

PCG32 uses constant memory per generator and constant time per raw output. `Random.Float` consumes
two raw outputs. Rejection sampling has expected constant time for supported ranges.

`Random.Choice` is constant time. `Random.Sample` is expected `O(n)` memory and time when implemented
through a copied array and partial Fisher-Yates. `Random.Shuffle` is `O(n)` time and memory because
its non-mutating contract requires a shallow copy.

The first implementation need not be thread-safe. It MUST NOT add a global process-wide lock to
unrelated runtime operations.

------------------------------------------------------------------------

# 14. Compatibility

This RFC fills the missing implementation behind the already documented `Math.Random()` name. A
program that currently fails with `unknown host function: Math.Random` will begin returning a
`Number`; no previously successful behavior changes.

The new `Random.*` namespace does not conflict with existing syntax or core functions. `RANDOM` is
added to the hosted opaque handle type set defined by RFC-0015.

RFC-0012 remains authoritative for compiler structure. Because this RFC adds hosted library calls
rather than syntax, the lexer, parser, and AST require no random-specific nodes. Hosted native
capsules and the interpreter must expose equivalent call behavior. Unsupported profiles must
reject the service explicitly.

Changing the PCG32 engine, initialization, floating construction, or integer distribution in a
future release would change seeded observable behavior and therefore requires a superseding RFC or
an explicitly versioned generator API.

------------------------------------------------------------------------

# 15. Reference Implementation

The following is conceptual pseudocode. Fixed-width types are required internally even though
hosted ArcoBASIC `Number` values are floating-point values.

```text
function next_u32(generator):
    old = generator.state
    generator.state = old * 6364136223846793005 + generator.increment
    xorshifted = u32(((old >> 18) xor old) >> 27)
    rotation = u32(old >> 59)
    return rotate_right_32(xorshifted, rotation)
```

Illustrative ArcoBASIC use:

```basic
FUNCTION MakeGenome(length, rng)
    genome = []
    FOR index = 1 TO length
        ignored = Array.Add(genome, Random.Integer(0, 1, rng))
    NEXT
    RETURN genome
END FUNCTION

rng = Random.Create(20260814)
genome = MakeGenome(18, rng)
PRINT genome
Random.Destroy(rng)
```

------------------------------------------------------------------------

# 16. Testing Strategy

## 16.1 Engine unit tests

Tests MUST validate PCG32 initialization and raw-output fixtures against independently calculated
known-answer vectors for multiple seeds and sequences, including zero and the maximum accepted
safe integer.

Tests MUST validate wraparound, unsigned shifts, and rotation without depending solely on the same
production helper used to compute expected values.

## 16.2 Runtime API tests

Tests MUST cover:

- `Math.Random()` returns a `Number` in `[0, 1)`;
- `Math.Random()` and `Random.Float()` consume one shared default sequence;
- equal seeds and sequences produce equal results;
- different sequence values produce independently initialized streams;
- copied handles share state;
- reseeding reproduces the initial sequence;
- stale, destroyed, null, and wrong-type handles fail safely;
- integer endpoints can both be returned;
- equal integer bounds return that bound;
- invalid, fractional, negative, non-finite, and out-of-safe-range seed values fail;
- reversed and over-wide integer ranges fail;
- choice rejects an empty array;
- sample validates its count and does not mutate its input;
- shuffle preserves all input elements and does not mutate its input; and
- zero- and one-element sample/shuffle cases behave deterministically.

## 16.3 Distribution sanity tests

Statistical tests MAY check gross implementation mistakes, but they MUST use fixed seeds, generous
tolerances, and bounded workloads. Statistical tests MUST NOT replace known-answer tests and MUST
NOT be flaky release gates.

## 16.4 Integration tests

At least one `.abas` fixture MUST run the same seeded workload through the interpreter and the
hosted runtime capsule path and compare exact output.

A `#RUNTIME NONE` fixture MUST verify the deterministic unsupported-service diagnostic.

The full existing runtime and compiler test suites MUST remain green.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

## 17.1 Implementation boundaries

An implementation agent SHALL:

1. inspect the current runtime function-registration and handle-table conventions;
2. record and run the unchanged baseline test suites;
3. add one isolated internal PCG32 generator implementation using explicit unsigned widths;
4. add `RANDOM` to the existing opaque hosted handle type set;
5. register the public functions exactly as specified in section 7.7;
6. reuse the runtime-owned default generator for both `Random.Float()` and `Math.Random()`;
7. add deterministic validation and diagnostics at the public boundary;
8. add known-answer, API, handle-lifecycle, integration, and profile-rejection tests;
9. update implementation status, language reference, help text, and examples; and
10. run the complete affected test suites and report exact commands and results.

## 17.2 Prohibited implementation choices

An implementation agent SHALL NOT:

- add random-specific grammar, tokens, AST nodes, or parser branches;
- use `std::rand`, a global C/C++ engine, or implementation-defined standard distributions;
- expose raw state as an ArcoBASIC object, pointer, number, or string;
- implement seeded state with floating-point arithmetic;
- treat automatic seeding as cryptographically secure;
- add a third-party PRNG dependency;
- silently enable this API in `#RUNTIME NONE`; or
- create an alternate handle table or resource lifetime system.

## 17.3 Required deliverables

- internal PCG32 engine and deterministic fixtures;
- `RANDOM` runtime-handle integration;
- all section 7.7 host functions;
- `Math.Random()` compatibility behavior;
- runtime, compiler-profile, and integration tests;
- an ArcoBASIC example using both default and seeded generators;
- updated `README.md`, implementation status, runtime-handle documentation, and shell help; and
- an implementation report mapping requirements to files and tests.

## 17.4 Acceptance criteria

Implementation is complete only when:

- all normative API behavior is present;
- known-answer vectors pass on supported hosted platforms;
- interpreter and hosted capsule output match for the seeded fixture;
- wrong-type and stale handles are rejected;
- freestanding use fails with the specified profile boundary;
- `Math.Random()` no longer produces an unknown-function error;
- no existing tests regress; and
- documentation clearly distinguishes pseudorandom and cryptographic use.

## 17.5 Stop conditions

The agent MUST stop and report instead of guessing if:

- RFC-0015 handles cannot represent a mutable `RANDOM` resource without changing their contract;
- hosted native capsules cannot call the same runtime service as the interpreter;
- profile validation cannot distinguish hosted from `#RUNTIME NONE` calls;
- the accepted ArcoBASIC `Number` model differs from the safe-integer assumptions in this RFC;
- known-answer vectors disagree across independently implemented checks;
- an existing public `Random.*` API is discovered with conflicting semantics; or
- implementation would require new syntax, a third-party dependency, or cryptographic claims.

## 17.6 Assumptions and dependencies

This RFC assumes the existing case-insensitive host-call mechanism, RFC-0015 handle table, hosted
`Number` representation, array value semantics, and RFC-0012 pipeline remain authoritative.

An implementation packet MAY divide the work into engine, runtime API, handle integration,
compiler-profile validation, tests, and documentation, but MUST NOT alter the public contract in
this RFC.

------------------------------------------------------------------------

# 18. Future Extensions

- A distinct cryptographically secure random-byte API.
- Versioned generator algorithms when a second deterministic engine is justified.
- Generator state export/import with an explicit versioned format.
- Weighted choice and additional statistical distributions.
- Wider integer ranges assembled from multiple raw outputs.
- Thread-local or synchronization-defined default generators.
- A separately specified freestanding entropy and PRNG service.
- Hardware entropy adapters behind a security-reviewed API.

------------------------------------------------------------------------

# 19. Open Questions

1. Should an accepted revision expose a read-only algorithm identifier such as
   `Random.Algorithm(generator)` before generator-state persistence is introduced?
2. Should future generator-state export be text-based for diagnostics or byte-based for compact
   checkpoints?
3. Should automatic seeding failure be observable through diagnostics, or is the defined
   non-security fallback sufficient for the hosted alpha?

None of these questions blocks the first implementation defined by this RFC.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology Request for Comments Process.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.
- RFC-0015, Runtime Object Handles.
- Melissa E. O'Neill, *PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms
  for Random Number Generation*, 2014, <https://www.pcg-random.org/paper.html>.
- PCG Project, *Using the Full C Implementation*,
  <https://www.pcg-random.org/using-pcg-c.html>.
- RFC 2119, *Key words for use in RFCs to Indicate Requirement Levels*.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---------|------|---------|
| 0.1 | 2026-08-14 | Initial draft defining hosted deterministic pseudorandom generators. |
| 0.2 | 2026-08-14 | Implemented the hosted engine, APIs, handles, profile rejection, tests, help, and documentation. |
