# RFC-0023: Hosted Evolutionary Workload Support

**RFC Number:** RFC-0023  
**Title:** Hosted Evolutionary Workload Support  
**Status:** Implemented  
**Category:** Language / Hosted Runtime / Tooling  
**Authors:** Arcology Project  
**Created:** 2026-08-14  
**Last Updated:** 2026-08-14  
**Supersedes:** None  
**Superseded By:** None  
**Related RFCs:** RFC-0000, RFC-0007, RFC-0012, RFC-0015, RFC-0017, RFC-0021, RFC-0022

------------------------------------------------------------------------

# 1. Executive Summary

ArcoBASIC now has the ordinary values, collections, control flow, functions, file operations, and
deterministic pseudorandom operations needed to express evolutionary simulations. Three remaining
hosted-runtime gaps prevent a faithful, recognizable port of PetriBrain and similar workloads:

- library code cannot deliberately reject invalid input with a source-defined runtime error;
- an explicit pseudorandom generator cannot be cloned at its current state into an independent
  stream; and
- long-running compute programs cannot request an instruction budget above the fixed hosted
  default from ArcoBASIC source or first-party command-line tools.

This RFC closes those gaps as one hosted evolutionary-workload milestone. It:

1. makes RFC-0022 user-defined runtime errors a required dependency of this milestone;
2. amends RFC-0021 with `Random.Clone(generator)`, which copies current generator state into a new
   independently owned `RANDOM` resource; and
3. adds a host-authorized `#INSTRUCTION_LIMIT` source directive plus matching first-party tool
   options for bounded or explicitly unlimited trusted execution.

The expected outcome is that a PetriBrain port can validate genomes, preserve its independent
crossover stream behavior, and complete realistic multi-generation simulations entirely in
ArcoBASIC. A human using the port should recognize the same genome operations, evolutionary loop,
game behavior, persisted brains, and interactive play experience.

Exact pseudorandom sequences from Python, Python exception class names, source-level syntactic
similarity to Python, and bit-for-bit output parity with another language are not required.

------------------------------------------------------------------------

# 2. Motivation

PetriBrain is representative of a useful class of hosted applications: genetic algorithms,
procedural simulations, game-playing agents, randomized tests, and optimization tools. Such
programs combine ordinary computation with three contracts that are easy to overlook in a small
language runtime.

First, reusable library functions must reject invalid caller input. Returning a sentinel changes
the function's contract and can allow corrupted genomes or invalid probability values to continue
through an evolution run. Provoking an unrelated built-in failure produces misleading diagnostics.
RFC-0022 specifies the necessary `THROW` statement, but the PetriBrain milestone is incomplete
until that RFC is implemented through every hosted execution path.

Second, PetriBrain crossover deliberately advances a caller-provided generator, clones its current
state, and consumes the clone. Assignment cannot express this behavior. RFC-0021 correctly defines
ordinary `RANDOM` assignment as identity sharing, so changing assignment would break the handle
model and existing programs. A distinct cloning operation is required.

Third, the hosted runtime defaults to an instruction budget of 100,000. That is useful protection
for embedded and interactive programs, but a full evolutionary run executes deeply nested loops
over generations, populations, games, turns, and candidate moves. The C++ embedding API can change
the limit, while an ArcoBASIC author using `arco_cli`, ArcoSH, `ArcoFission compile-run`, serialized
bytecode, or a hosted runtime capsule cannot express the same requirement. Requiring a custom C++
host contradicts the goal of implementing the application in ArcoBASIC.

These are not requests to make ArcoBASIC imitate Python. They are application-level semantic and
execution contracts shared by many simulation workloads.

------------------------------------------------------------------------

# 3. Goals

- Make source-defined, catchable validation failures available by requiring RFC-0022.
- Add an explicit operation that clones the current state of an explicit `RANDOM` generator.
- Preserve RFC-0021 handle assignment and lifecycle semantics.
- Allow trusted long-running programs to request a larger finite instruction budget.
- Allow an operator to authorize unlimited execution explicitly from first-party tools.
- Keep embedding hosts authoritative over maximum resource use.
- Preserve equivalent behavior across the interpreter, ArcoSH, hosted bytecode VM, serialized
  bytecode, and hosted native runtime capsules.
- Support a recognizable PetriBrain port without requiring C, C++, Python, or a new dependency at
  application runtime.
- Add deterministic tests for generator independence, limit precedence, diagnostics, and hosted
  execution parity.

------------------------------------------------------------------------

# 4. Non-Goals

This RFC does not define:

- compatibility with Python's Mersenne Twister engine or seeded output sequences;
- compatibility with Python exception classes such as `ValueError` or `KeyError`;
- user-defined exception hierarchies, typed catches, arbitrary thrown values, or stack traces;
- generator state serialization, persistence, inspection, export, or import;
- cloning of the runtime-owned default generator;
- cryptographically secure randomness;
- parallel execution, worker processes, tasks, coroutines, or threads;
- wall-clock timeouts, memory quotas, allocation quotas, or cancellation tokens;
- a general capability or permission system redesign;
- automatic detection of compute-heavy programs;
- removal of the default instruction limit;
- a guarantee that every source-requested limit will be authorized by every host;
- freestanding or `#RUNTIME NONE` error unwinding, pseudorandom generation, or hosted limit
  directives;
- new bit-array, tuple, comprehension, lambda, sorting-key, enum, or pattern-matching syntax; or
- correction of defects or incompatible interfaces in an application being ported.

------------------------------------------------------------------------

# 5. Terminology

**Evolutionary Workload**  
A hosted computation that repeatedly evaluates, selects, mutates, or recombines a population over
multiple generations. The term is descriptive and does not create a new runtime profile.

**Source-Defined Error**  
A runtime failure deliberately originated by ArcoBASIC source using RFC-0022 `THROW`.

**Generator Clone**  
A new `RANDOM` resource whose initial private state is identical to an explicit source generator's
state at the instant of cloning and whose subsequent state changes are independent.

**Source Limit Request**  
The finite instruction count requested by `#INSTRUCTION_LIMIT`. It is a request subject to host
policy, not permission to override an embedding sandbox.

**Tool Limit Override**  
An instruction limit explicitly supplied by the operator to a first-party command-line tool.

**Host Hard Maximum**  
The maximum instruction budget an embedding host permits for source and tool requests. A host may
set no finite maximum for trusted execution.

**Effective Instruction Limit**  
The instruction budget selected after applying host policy, an operator override, a source request,
and the existing runtime default in the precedence order defined by this RFC. Zero denotes
explicitly authorized unlimited execution in the existing `RuntimeLimits` representation.

------------------------------------------------------------------------

# 6. Requirements

## 6.1 Required error dependency

A conforming implementation of this RFC MUST implement RFC-0022 in full before claiming this RFC
as Implemented.

PetriBrain-style library validation MUST be expressible as a source `THROW` with a string message.
The interpreter, hosted bytecode VM, serialized bytecode execution, and hosted native runtime
capsule MUST preserve RFC-0022 control transfer and diagnostics.

This RFC does not change RFC-0022's public `UserError` and `RuntimeError` categories. A port MAY use
messages such as `Invalid nucleotide: B` to preserve recognizable diagnostics without reproducing
host-language exception class names.

## 6.2 Random generator cloning

The hosted runtime MUST expose:

```basic
clone = Random.Clone(generator)
```

`generator` MUST be a valid, live, explicit `RANDOM` handle. `NULL`, an omitted argument, the
runtime-owned default generator, a stale handle, a destroyed handle, or a handle of another type
MUST produce a deterministic runtime error.

`Random.Clone` MUST:

- create a distinct `RANDOM` resource and handle;
- copy all private engine state needed to continue the exact RFC-0021 stream;
- consume no pseudorandom outputs;
- leave the source generator unchanged;
- return the clone in the `Ready` lifecycle state;
- register the clone through the RFC-0015 and RFC-0017 resource mechanisms used by
  `Random.Create`; and
- give the clone an independent lifecycle, including independent destruction.

Immediately after cloning, applying the same ordered random operations to source and clone MUST
produce equal values. Applying an operation to only one MUST NOT advance or otherwise change the
other.

Assignment remains identity sharing:

```basic
same = generator                 ' same handle and stream
copy = Random.Clone(generator)   ' new handle and independent stream
```

`Random.Clone` is a `Returned` ownership operation. It borrows the source handle and does not
consume it. The caller owns the returned handle and SHOULD eventually call `Random.Destroy(copy)`.

## 6.3 Instruction-limit directive

The compiler directive processor MUST accept:

```basic
#INSTRUCTION_LIMIT positiveInteger
```

The operand MUST be a decimal integer literal from `1` through `9007199254740991` (`2^53 - 1`),
matching the language's safe-integer ceiling. Expressions, identifiers, interpolated values,
negative values, zero, fractional values, larger values, duplicate directives, and missing operands
MUST produce source-located diagnostics.

The directive is hosted metadata. It MUST NOT itself change a runtime limit without host
authorization. The requested value MUST be preserved through preprocessing, canonical compile
metadata, bytecode preparation, serialized bytecode, and hosted native capsule construction.

A `#RUNTIME NONE` source using `#INSTRUCTION_LIMIT` MUST receive a deterministic diagnostic that
the directive applies only to hosted execution. A backend MUST NOT silently ignore it.

## 6.4 First-party tool options

First-party hosted execution tools MUST accept:

```text
--instruction-limit COUNT
```

for `arco_cli`, ArcoSH script execution, `ArcoFission compile-run`, and `ArcoFission run`.

`COUNT` MUST accept a decimal integer from `1` through `9007199254740991` or `unlimited` as a
case-insensitive operator spelling for the existing runtime value zero. Numeric zero MUST be
rejected so unlimited execution cannot be selected accidentally by an empty or default-initialized
numeric value. All listed first-party tools MUST document that `unlimited` disables
instruction-count termination for that invocation.

`ArcoFission build` and `ArcoFission native` MUST accept the same `--instruction-limit COUNT`
option. Hosted native capsule construction MUST embed the authorized effective value so the
resulting program does not silently return to the 100,000-instruction default.

Invalid option values MUST fail before source or bytecode execution.

## 6.5 Limit authorization and precedence

The effective limit MUST be selected in this order, from highest authority to lowest:

1. host hard maximum and host permission policy;
2. explicit operator tool override;
3. source `#INSTRUCTION_LIMIT` request; and
4. existing runtime default.

An operator override supersedes a source request when host policy permits it.

An embedding host MUST be able to reject source limit requests, cap the maximum authorized value,
and reject unlimited execution. Source text MUST NOT be able to raise the limit established as a
hard maximum by the embedding host.

The standalone first-party `arco_cli`, ArcoSH script runner, and ArcoFission hosted execution tools
MUST authorize representable finite source requests by default. They have no finite hard maximum
unless their operator supplies or system packaging configures one. Unlimited execution still
requires the explicit tool option and can never be selected by source.

When a requested value exceeds host policy, execution MUST fail before the first program statement
with a deterministic diagnostic. The runtime MUST NOT silently clamp the request, because silent
clamping would make a program terminate unexpectedly at a value different from both its request
and the documented default.

The absence of a directive and tool override MUST retain the existing default behavior.

## 6.6 Instruction accounting

This RFC does not redefine what constitutes an instruction tick. Interpreter statements, hosted
function calls, and bytecode instructions MUST continue using their existing accounting rules.

Changing the effective limit MUST reset the instruction count before program execution. Imports and
program initialization performed as part of the same execution MUST count under the effective
limit according to existing rules.

A caught `instruction limit exceeded` error MUST NOT allow a program to reset or evade the budget
and continue indefinitely. Existing terminal limit behavior remains authoritative. Implementations
MUST NOT expose an unrestricted source-level `ResetInstructionCount()` operation.

## 6.7 Hosted execution parity

The interpreter, ArcoSH script runner, hosted bytecode VM, serialized bytecode runner, and hosted
native runtime capsule MUST agree on:

- `Random.Clone` state and independence;
- validation and lifecycle errors;
- source directive parsing and metadata;
- effective limit selection;
- the instruction at which an exhausted finite budget terminates; and
- diagnostics for rejected, malformed, or unsupported requests.

Equivalent programs MUST NOT complete in one hosted path and fail at the unchanged default limit in
another because metadata or tool options were dropped.

------------------------------------------------------------------------

# 7. Architecture

## 7.1 Component relationship

```text
ArcoBASIC source
  |-- THROW -----------------------> RFC-0022 hosted unwinding
  |
  |-- Random.Clone(rng) -----------> RuntimeHandleTable
  |                                      |
  |                                      +--> source RANDOM state
  |                                      +--> cloned RANDOM state
  |
  +-- #INSTRUCTION_LIMIT N
           |
           v
     CompileMetadata request
           |
           v
     host authorization <----------- CLI option / embedding policy
           |
           v
     effective RuntimeLimits
           |
           +--> interpreter
           +--> hosted bytecode VM
           +--> serialized bytecode
           +--> native runtime capsule
```

The three branches are separate implementation mechanisms joined by one acceptance milestone.
User errors use the RFC-0022 language pipeline. Generator cloning extends the existing RFC-0021
host namespace. Limit requests extend directive metadata and hosted tool configuration.

## 7.2 Random clone state

RFC-0021 defines each PCG32 generator by private `state` and `increment` values. Cloning copies both
values without running initialization or an engine step:

```text
source = { state: S, increment: I }
clone  = { state: S, increment: I }
```

After construction, each resource owns its own mutable pair. The implementation MAY use a private
PCG32 copy constructor or explicit state copy. It MUST NOT implement cloning by reseeding, deriving
a new seed, hashing visible outputs, or consuming source outputs.

## 7.3 Limit policy boundary

The directive processor records intent; the execution host grants authority:

```text
requested by source ----+
                       |
operator override ------+--> resolve against host policy --> effective limit
                       |
runtime default --------+
```

An embedding API may represent policy through an expanded `RuntimeLimits`, a distinct execution
policy object, or private host configuration. The public ArcoBASIC behavior and precedence are
normative; the C++ type layout is not.

## 7.4 Compile metadata

Canonical compile metadata MUST have an optional instruction-limit request distinguishable from
absence. A numeric zero MUST NOT ambiguously mean both absent and unlimited source authorization,
because this RFC does not permit unlimited execution to be requested from source.

Textual A-MIR and bytecode reveal output SHOULD display the request in a stable metadata section.
Serialized bytecode MUST retain it in a versioned, backward-detectable form.

Older serialized bytecode without this metadata behaves as if no source request were present.

## 7.5 Native capsule behavior

A hosted capsule embeds either:

- the source-requested finite value authorized at build time;
- an explicit build-tool override; or
- no request, which preserves the runtime default.

The capsule MUST initialize its runtime limit before executing the first bytecode instruction. A
build tool MUST NOT encode `unlimited` from source metadata because source syntax cannot request it.

------------------------------------------------------------------------

# 8. User Experience

Genome validation is direct:

```basic
FUNCTION DecodeGenome(bits)
    IF LEN(bits) MOD 2 <> 0 THEN
        THROW "Genome bit length must be a multiple of 2"
    END IF
    ' decode pairs
END FUNCTION
```

Crossover can preserve independent stream behavior:

```basic
FUNCTION OnePointCrossover(parentA, parentB, rng AS RANDOM)
    ignored = Random.Float(rng)
    LET localRng AS RANDOM = Random.Clone(rng)

    point = Random.Integer(1, LEN(parentA) - 1, localRng)
    children = BuildChildren(parentA, parentB, point)

    Random.Destroy(localRng)
    RETURN children
END FUNCTION
```

A trusted evolution entry point can request an adequate finite budget:

```basic
#INSTRUCTION_LIMIT 10000000

#IMPORT "petri/genome"
#IMPORT "ttt/evolve"

RunAllDifficulties()
```

An operator may override it for a larger local run:

```text
arcosh --instruction-limit 50000000 petri_evolve.abas
```

An embedded host that does not authorize the request reports that fact before evolution begins.

------------------------------------------------------------------------

# 9. Developer Experience

Application developers use ordinary `TRY` / `CATCH` and `THROW` for contract failures. They do not
construct host exception objects or depend on C++ exception names.

`Random.Clone` makes identity intent explicit. Assignment is still the correct operation when two
variables should share a stream; cloning is correct when an algorithm needs a snapshot-derived
independent stream.

Developers should request a finite instruction budget based on representative workloads and leave
headroom for diagnostics and input variation. Libraries SHOULD NOT contain
`#INSTRUCTION_LIMIT`; the application entry point owns whole-program execution policy.

Tool help and in-shell help MUST explain:

- the unchanged 100,000 default;
- source requests versus operator overrides;
- host rejection behavior;
- the security consequence of unlimited execution; and
- that higher limits do not increase memory or wall-clock quotas.

The error for a rejected source request should be actionable, for example:

```text
requested instruction limit 10000000 exceeds host maximum 1000000
```

The error for unsupported freestanding use should identify the directive or API by name.

------------------------------------------------------------------------

# 10. Security Considerations

Instruction limits protect hosts from accidental infinite loops and hostile compute exhaustion.
Source code is not an authority boundary. An embedding host MUST retain the ability to reject or
bound source requests, and source MUST NOT request unlimited execution.

First-party command-line tools may treat an explicit operator option as authorization because the
operator launches the local process. Documentation MUST warn that `unlimited` permits infinite
loops and unbounded CPU use until externally interrupted.

Increasing an instruction limit does not authorize filesystem, network, process, GUI, or other
capabilities. Existing service availability and permission checks remain unchanged.

`Random.Clone` exposes no state bytes and adds no predictive capability beyond the source
generator already held by the program. RFC-0021 remains non-cryptographic. Cloned streams MUST NOT
be presented as independent cryptographic entropy.

User-thrown messages may contain sensitive input. RFC-0022's information-disclosure rules remain
authoritative.

Malformed directives and CLI values MUST use checked integer parsing and MUST NOT wrap the host
counter type.

------------------------------------------------------------------------

# 11. Privacy Considerations

This RFC collects no personal information and introduces no telemetry.

Generator clones exist only as runtime resources unless future functionality explicitly persists
them. This RFC provides no state export.

Instruction requests may appear in source, bytecode metadata, capsule metadata, diagnostics, or
build logs. They reveal only a requested compute budget.

Error messages follow RFC-0022 and SHOULD avoid embedding secrets or unnecessary personal data.

------------------------------------------------------------------------

# 12. Accessibility Considerations

All new interactions are textual and do not depend on color, timing, audio, animation, or pointer
input.

Limit diagnostics MUST name the requested value, relevant policy value when safe to disclose, and
a corrective action. Tool help MUST remain understandable with ANSI color disabled.

Long-running command-line programs SHOULD continue printing meaningful generation progress so
users can distinguish active computation from a stalled process. This is application guidance, not
a new runtime requirement.

------------------------------------------------------------------------

# 13. Performance Considerations

`Random.Clone` uses constant time and constant memory because RFC-0021 PCG32 state is fixed-size.
Cloning MUST NOT scale with the number of values previously generated.

Checking a finite instruction limit remains constant time per tick. Resolving a limit happens once
before execution and must not add per-instruction policy parsing.

A larger limit permits more work but does not itself allocate memory. Unlimited execution may
consume CPU indefinitely and therefore requires explicit operator or host authorization.

`THROW` performance remains governed by RFC-0022. Normal validation branches and non-throwing
execution should retain existing characteristics.

The PetriBrain acceptance workload is intentionally compute-heavy enough to prove that metadata is
preserved and the unchanged default no longer terminates an authorized run.

------------------------------------------------------------------------

# 14. Compatibility

This RFC is additive for source programs that do not use its new API or directive.

RFC-0021 remains authoritative for PCG32, distributions, handle identity, default generators, and
resource lifecycle. This RFC amends its public API with `Random.Clone` but does not change seeded
sequences or the outputs consumed by existing calls.

RFC-0022 remains authoritative for `THROW` syntax, AST, A-MIR, bytecode, error objects, diagnostics,
and hosted unwinding. This RFC makes its implementation a milestone dependency without changing
its string-only error model.

`INSTRUCTION_LIMIT` becomes a recognized compiler directive name. Source that previously used an
unknown directive with that spelling changes from an unknown-directive diagnostic to the behavior
defined here.

Existing source without the directive retains the 100,000-instruction default unless an operator
or embedding host already configures another value.

Existing serialized bytecode remains runnable. New readers treat missing limit metadata as no
request. Old readers may reject a newer bytecode version containing the metadata but MUST NOT
silently misparse it.

Exact output parity with Python's random module is intentionally unnecessary. ArcoBASIC's stable
PCG32 behavior is the native contract for the port.

------------------------------------------------------------------------

# 15. Reference Implementation

## 15.1 Generator clone behavior

The following output is normative even though the numeric values depend on RFC-0021:

```basic
LET rng AS RANDOM = Random.Create(20260814)
ignored = Random.Float(rng)
LET clone AS RANDOM = Random.Clone(rng)

a = Random.Integer(0, 1000, rng)
b = Random.Integer(0, 1000, clone)
PRINT a = b

advancedSource = Random.Integer(0, 1000, rng)
advancedClone = Random.Integer(0, 1000, clone)
PRINT advancedSource = advancedClone

c = Random.Integer(0, 1000, clone)
d = Random.Integer(0, 1000, rng)
PRINT c = d

Random.Destroy(clone)
Random.Destroy(rng)
```

Expected output:

```text
TRUE
TRUE
TRUE
```

The first equality proves identical initial state. The second proves equal operations retain equal
state. Before the third comparison, `clone` advances first; if the handles shared state then `rng`
would observe the following value instead of the same value. The third equality therefore proves
the handles do not share mutable state.

## 15.2 Validation behavior

```basic
FUNCTION MutationRate(value)
    IF value < 0 ORELSE value > 1 THEN
        THROW "Mutation rate must be between 0 and 1"
    END IF
    RETURN value
END FUNCTION

TRY
    ignored = MutationRate(2)
CATCH err
    PRINT err.Type
    PRINT err.Message
END TRY
```

Expected output:

```text
UserError
Mutation rate must be between 0 and 1
```

## 15.3 Limit precedence behavior

For source containing:

```basic
#INSTRUCTION_LIMIT 500000
```

the normative cases are:

| Host maximum | Tool override | Effective result |
|---|---:|---|
| no finite maximum | absent | `500000` |
| no finite maximum | `750000` | `750000` |
| `600000` | absent | `500000` |
| `400000` | absent | reject before execution |
| `600000` | `700000` | reject before execution |
| unlimited permitted | `unlimited` | unlimited |
| unlimited forbidden | `unlimited` | reject before execution |

## 15.4 PetriBrain acceptance shape

A conforming port can perform this conceptual flow without host-language code:

```text
validate configuration with THROW
create one seeded or automatically seeded RANDOM generator
generate a population of genome arrays
for each generation:
    evaluate genomes through game simulations
    rank and select parents
    clone current RNG state for crossover-local draws
    mutate offspring
    persist the best genome
destroy explicit generators
```

The port need not reproduce Python's exact random choices. It must remain recognizably the same
application and preserve deterministic behavior within ArcoBASIC when given the same ArcoBASIC
seed and call sequence.

------------------------------------------------------------------------

# 16. Testing Strategy

## 16.1 RFC-0022 dependency tests

All RFC-0022 lexer, parser, AST, interpreter, compiler, bytecode, capsule, profile, and regression
tests MUST pass before this RFC can be marked Implemented.

## 16.2 Random clone unit tests

Tests MUST cover:

- cloning immediately after creation;
- cloning after several mixed random operations;
- equal subsequent values under equal call sequences;
- independent advancement under unequal call sequences;
- source state unchanged by cloning;
- distinct handle identity;
- destruction of either handle leaving the other valid;
- resource registration and unregistration;
- wrong-type, null, stale, and destroyed-handle rejection;
- no acceptance of an omitted argument;
- interpreter and hosted bytecode parity; and
- no change to existing RFC-0021 known-answer vectors.

## 16.3 Directive parser tests

Tests MUST cover:

- a valid finite request;
- the minimum value `1`;
- the maximum representable value;
- zero, negative, fractional, overflowing, missing, expression, and duplicate values;
- case-insensitive directive spelling according to existing directive rules;
- stable compile metadata and reveal output;
- imports not overriding the entry point's request; and
- deterministic freestanding rejection.

## 16.4 Policy and CLI tests

Tests MUST cover every precedence row in section 15.3, plus:

- default behavior when no request exists;
- operator override parsing in each required tool;
- `unlimited` support and numeric-zero rejection in every required tool;
- host rejection before the first source statement;
- instruction count reset before execution;
- no silent clamping;
- malformed tool values;
- serialized bytecode metadata roundtrip; and
- native capsule preservation of its authorized limit.

## 16.5 Workload tests

One integration fixture MUST intentionally execute more than 100,000 accounted instructions.

The fixture MUST:

- fail with `instruction limit exceeded` under the unchanged default;
- complete with a sufficiently large authorized finite request;
- produce deterministic final output;
- run through the interpreter and hosted bytecode VM; and
- run through a hosted native capsule when capsule tests are supported on the host.

A PetriBrain-shaped smoke fixture SHOULD combine genome arrays, mutation, selection, generator
cloning, and multiple generations. It need not import the external PetriBrain repository.

## 16.6 Regression tests

The complete runtime, shell, compiler, Arcology OS, and Arcology Commons suites MUST pass. Existing
programs without the new directive MUST retain their prior limit behavior.

------------------------------------------------------------------------

# 17. AI Implementation Guidance

## 17.1 Implementation order

An implementation agent SHALL:

1. inspect and run the unchanged repository baseline tests;
2. implement and verify RFC-0022 completely if it is not already Implemented;
3. inspect RFC-0021 PCG32 state, runtime handles, and resource registration;
4. add `Random.Clone` without changing assignment semantics or existing sequences;
5. add clone unit, lifecycle, interpreter, compiler, and capsule tests;
6. add `instruction_limit` as optional compile metadata;
7. parse and validate `#INSTRUCTION_LIMIT` through the existing directive processor;
8. define a single limit-resolution helper shared by first-party hosted execution paths where
   architecture permits;
9. add consistent first-party tool options and help text;
10. preserve limit metadata through bytecode and capsule construction;
11. add policy, precedence, exhaustion, and long-workload tests;
12. update implementation status, directive documentation, random documentation, shell help, and
    compiler help; and
13. produce an implementation report mapping every normative requirement to code and tests.

## 17.2 Implementation boundaries

The agent MAY select private C++ representations, CLI parser helpers, metadata serialization keys,
and bytecode version changes when they preserve this RFC's public behavior.

The agent SHOULD reuse the existing `Pcg32`, `RuntimeHandleTable`, `ResourceRegistry`,
`CompileMetadata`, `RuntimeLimits`, preprocessing, and first-party tool structures.

The agent SHALL keep changes scoped to hosted error completion, random cloning, instruction-limit
metadata and policy, tests, documentation, and directly necessary build registration.

## 17.3 Prohibited implementation choices

An implementation agent SHALL NOT:

- change `RANDOM` assignment into state copying;
- clone by reseeding or deriving a seed from generated output;
- expose PCG state fields to ArcoBASIC;
- add Python-compatible random behavior;
- add typed Python-like exception classes;
- simulate `THROW` through an unrelated runtime failure;
- allow source to request unlimited execution;
- allow source metadata to override an embedding host maximum;
- silently clamp an unauthorized limit;
- reset the instruction counter from unrestricted source code;
- disable instruction accounting to make a test pass;
- ignore limit metadata in bytecode or capsules;
- make hosted services available to `#RUNTIME NONE`; or
- add a third-party dependency.

## 17.4 Required deliverables

- every RFC-0022 deliverable;
- `Random.Clone` runtime API and documentation;
- clone lifecycle and parity tests;
- `#INSTRUCTION_LIMIT` directive metadata and diagnostics;
- consistent hosted CLI execution options;
- host-policy and limit-precedence implementation;
- bytecode and capsule metadata preservation;
- a workload fixture exceeding the old default;
- updated in-shell help and implementation status; and
- an implementation report with exact build and test commands and results.

## 17.5 Acceptance criteria

Implementation is complete only when:

- RFC-0022 is Implemented and its full suite passes;
- the section 15.1 clone program prints the expected output on every hosted path;
- clone handles have independent state and lifecycle;
- no existing RFC-0021 sequence changes;
- valid finite source requests complete above the old default when authorized;
- unauthorized requests fail before source execution;
- tool overrides and host policy follow section 6.5 exactly;
- serialized bytecode and capsules preserve effective behavior;
- the long-workload fixture behaves as required in section 16.5;
- freestanding uses fail explicitly;
- all prior tests pass; and
- no application runtime dependency outside ArcoBASIC is introduced.

## 17.6 Stop conditions

The agent MUST stop and report instead of guessing if:

- RFC-0022 cannot be implemented without changing its accepted public contract;
- PCG32 state cannot be copied without exposing or changing RFC-0021 behavior;
- cloned resources cannot receive independent RFC-0015/RFC-0017 lifecycle records;
- the directive processor cannot preserve an optional exact integer request;
- a hosted execution path cannot apply its limit before the first accounted instruction;
- bytecode or capsule formats cannot preserve metadata without an explicit compatibility decision;
- host policy and source request authority cannot be kept separate;
- existing instruction-limit errors are recoverable in a way that permits budget evasion and the
  correction requires a broader control-flow decision;
- a first-party tool cannot support the common option contract without redesigning unrelated CLI
  behavior; or
- completing the RFC requires parallelism, Python compatibility, or a third-party dependency.

## 17.7 Assumptions and dependencies

This RFC assumes RFC-0012 remains authoritative for shared frontend and hosted compiler parity,
RFC-0015 and RFC-0017 remain authoritative for runtime resources, RFC-0021 remains authoritative
for PCG32 behavior, and RFC-0022 remains authoritative for user-defined errors.

The current runtime convention that instruction limit zero means unlimited may remain internal.
Only explicitly authorized tool or embedding policy may select it.

------------------------------------------------------------------------

# 18. Future Extensions

- Versioned random generator state export and import.
- Named execution-policy profiles for trusted simulations, interactive scripts, and sandboxes.
- Wall-clock, allocation, and memory budgets coordinated with instruction limits.
- Cooperative cancellation and progress callbacks for long-running hosted computations.
- A standard evolutionary-computation library built after the required primitives stabilize.
- Stable user-defined error codes if applications require machine-readable categories.
- Per-module instruction diagnostics for profiling without changing enforcement.

------------------------------------------------------------------------

# 19. Open Questions

None. Numeric zero is invalid in tool syntax, `unlimited` requires explicit operator authority,
standalone first-party tools authorize representable finite source requests by default, embedding
hosts retain their hard policy boundary, and hosted capsules embed the build-time authorized
effective limit.

------------------------------------------------------------------------

# 20. References

- RFC-0000, Arcology Request for Comments Process.
- RFC-0007, ArcoBASIC Interactive Program Model.
- RFC-0012, ArcoFission Frontend to A-MIR Contract.
- RFC-0015, Runtime Object Handles.
- RFC-0017, Substrate Resource Model.
- RFC-0021, Deterministic Pseudorandom Number Generation.
- RFC-0022, User-Defined Runtime Errors.
- RFC 2119, *Key words for use in RFCs to Indicate Requirement Levels*.
- PetriBrain genetic operations, Tic-Tac-Toe evolution, and interactive play source audited on
  2026-08-14.

------------------------------------------------------------------------

# 21. Revision History

| Version | Date | Summary |
|---------|------|---------|
| 0.1 | 2026-08-14 | Initial draft defining the hosted PetriBrain/evolutionary-workload milestone. |
