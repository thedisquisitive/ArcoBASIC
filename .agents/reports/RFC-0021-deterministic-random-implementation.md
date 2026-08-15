# RFC-0021 Deterministic Pseudorandom Generation — Implementation Report

## Status

Complete.

## Objective

Implement RFC-0021's hosted deterministic pseudorandom service without new grammar, a third-party
dependency, implementation-defined standard distributions, or freestanding fallback behavior.

## Baseline

The repository contained unrelated in-progress systems, graphics, runtime-handle, and resource
registry changes before this work. Those changes were preserved.

Baseline command:

```text
ctest --test-dir build --output-on-failure
```

Baseline result: 35 of 35 tests passed in 118.39 seconds.

## Summary

- Added an isolated PCG XSH RR 64/32 engine with RFC-known-answer behavior.
- Added automatic non-cryptographic seed construction without a third-party dependency.
- Added runtime-owned default generators scoped to each `Runtime` instance.
- Added explicit generation-checked `RANDOM` handles and resource registry metadata.
- Added `Random.Create`, `Destroy`, `Reseed`, `Float`, `Integer`, `Choice`, `Sample`, and `Shuffle`.
- Added the zero-argument `Math.Random()` compatibility alias.
- Added deterministic rejection of `Random.*` and `Math.Random()` under `#RUNTIME NONE`.
- Added interpreter, bytecode, hosted native capsule, lifecycle, profile, and diagnostic coverage.
- Added `HELP random`, a runnable example, API documentation, and implementation status updates.

## Files Changed for RFC-0021

```text
CMakeLists.txt
README.md
arcology-os/rfcs/RFC-0015_Runtime_Object_Handles.md
arcology-os/rfcs/RFC-0021_Deterministic_Pseudorandom_Number_Generation.md
arcology-os/tests/systems/systems_freestanding_profile_smoke.sh
cmake/Testing.cmake
docs/implementation-status.md
docs/random.md
docs/runtime-object-handles.md
examples/random.abas
include/arco/random.hpp
include/arco/runtime.hpp
src/frontend/parser.cpp
src/runtime/random.cpp
src/runtime/runtime.cpp
src/shell/arcosh.cpp
tests/fixtures/random/deterministic.abas
tests/fixtures/random/expected-output.txt
tests/integration/arcosh_alpha_smoke.sh
tests/integration/random_smoke.sh
tests/unit/random_tests.cpp
```

## Public Behavior

```basic
PRINT Math.Random()
PRINT Random.Integer(1, 6)

rng = Random.Create(42)
PRINT Random.Float(rng)
PRINT Random.Choice(["north", "south"], rng)
PRINT Random.Sample([1, 2, 3, 4], 2, rng)
PRINT Random.Shuffle([1, 2, 3, 4], rng)
Random.Reseed(rng, 42)
Random.Destroy(rng)
```

Seeded output is stable across hosted execution paths. Explicit handle copies share state. Sample
and shuffle do not mutate their source arrays. Integer bounds are inclusive and use rejection
sampling. Invalid values and handles fail with deterministic diagnostics.

The API remains unavailable under `#RUNTIME NONE`; no firmware, CPU, clock, or constant fallback is
substituted.

## Validation

Build:

```text
cmake --build build -j2
```

Result: passed.

Targeted command:

```text
ctest --test-dir build -R 'random_tests|random_integration_smoke|arcosh_alpha_smoke|systems_freestanding_profile_smoke' --output-on-failure
```

Result: 4 of 4 tests passed in 1.99 seconds.

Final regression command:

```text
ctest --test-dir build --output-on-failure
```

Result: 37 of 37 tests passed in 118.61 seconds.

Additional manual execution:

```text
./build/arco_cli examples/random.abas
```

Result: default and seeded examples executed successfully.

## Acceptance Criteria

- PASS — PCG32 known-answer vector and fixed-width engine behavior.
- PASS — all RFC-0021 public hosted functions.
- PASS — default runtime generator and `Math.Random()` compatibility behavior.
- PASS — equal seed/sequence reproducibility and distinct sequence streams.
- PASS — copied-handle shared state and reseeding behavior.
- PASS — stale, destroyed, null, and wrong-type handle rejection.
- PASS — safe-integer, range, empty-choice, and sample-count diagnostics.
- PASS — non-mutating choice/sample/shuffle collection behavior.
- PASS — exact seeded output across interpreter, bytecode VM, and hosted native capsule.
- PASS — deterministic `#RUNTIME NONE` rejection.
- PASS — user documentation, shell help, example, and security warning.
- PASS — complete regression suite with no new failures.

## Assumptions

- The existing `RuntimeHandleTable`, `ResourceRegistry`, case-insensitive host-function registry,
  hosted `Number`, and array semantics remain authoritative.
- Automatic seeding is variation-only and may use the RFC-permitted clock/counter fallback when
  operating-system entropy is unavailable.

## Deviations

None from the public RFC contract.

Explicit generators also receive substrate resource records using the existing registry. The
records expose lifecycle metadata only and do not expose or persist generator state.

## Regressions

None observed.

## Remaining Deferred Work

The RFC's future extensions remain deferred: cryptographic randomness, state serialization, wider
integer ranges, weighted distributions, synchronization semantics, and freestanding entropy/PRNG
services.
