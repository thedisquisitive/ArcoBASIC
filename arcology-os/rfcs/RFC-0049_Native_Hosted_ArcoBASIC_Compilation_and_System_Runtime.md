
# RFC-0049: Native Hosted ArcoBASIC Compilation and System Runtime

**RFC Number:** RFC-0049
**Title:** Native Hosted ArcoBASIC Compilation and System Runtime
**Status:** Draft — Phase 1 (System V/Linux target: PRINT of a string/number/bool, `+ - * /`, string
concatenation via `+`, `MOD` (real IEEE-754 `fmod`), bitwise/shift ops (`& | ^ << >>` and unary
`~`/`NOT`), unary `-`, all six comparisons, real IEEE-754 double semantics matching `arco::Value`,
and general user-declared function calls with real System V argument/return classification) and
Phase 2 (arrays, objects, and classes/methods, always boxed through `ArcoValue`: construction,
indexing/property get+set (including a mixed index-then-property chain like `arr[i].field`), LEN,
nesting, arithmetic on an element/field, passing/returning through function calls, and instance
method call dispatch under `EXTENDS` polymorphism) and Phase 3 (script-scope globals and `SHARED`
class fields via `Runtime.SetGlobal`/`GetGlobal`; a generic bridge, `arco_call_host`, into a real
`arco::Runtime`'s own ~244-entry host-function library — string/array/formatting utilities and
everything else this backend doesn't hand-roll dedicated native codegen for — gracefully degraded
when the build tree hasn't opted in to it), Phase 4 (`TRY`/`CATCH`/`THROW` via real
`setjmp`/`longjmp` and a global handler stack; `ADDRESSOF`/callable-variable dispatch via a boxed-
string runtime comparison against every `ADDRESSOF` target in the module), Phase 5 (real
reference-counted lifetime tracking for every Boxed local -- retain-on-copy/release-on-overwrite-or-
scope-exit, closing the leak every prior phase disclosed; five independently-found real bugs fixed
along the way, see Entry 14), Phase 6 (chained indexed assignment -- `a.b.c = x`,
`arr[i].field = x`, `arr[i][j] = x`, and every mix; a parser grammar fix shared by every backend
plus a native `Kind::StoreIndex` codegen generalization), Phase 7 (chained method-call receivers,
`a.b.Method(...)`; dynamic method calls, `f().Method(...)`/`arr[0].Method(...)`, via a new
`DynamicMethodCallExpr` lowering shared by every backend; and a general two-pass argument-marshal
fix for a real, pre-existing, silent wrong-answer bug in multi-argument calls needing more than one
boxing/unboxing call, see Entry 16), Phase 8 (tuples; string codepoint indexing; range/
bit-vector LEN and indexing -- construction for the latter two already worked via the generic
host-function bridge, see Entry 17), and Phase 9 (uncaught-error stderr text now matches
`compile-run`'s own exactly; the host-function bridge's own build-time opt-in requirement was
investigated and found already correctly implemented, no fix needed; see Entry 18), and Phase 10
(a full marshaling-loop audit closing out Entry 16's own open question -- one more real, if
currently latent, register-clobber instance found and fixed in ordinary two-Boxed-operand Binary
arithmetic; every other marshaling site in the file individually traced and confirmed already safe;
see Entry 19), and Phase 11 (compiling Arconaut — `arcfs-utils/apps/arconaut/arconaut.abas`, a
real, substantial, pre-existing ~970-line ArcoBASIC GUI program, not a purpose-built fixture — as a
direct stress test of this backend; found and fixed nine distinct real bugs (an arbitrary frame-
size cap; `Runtime.Args()` a null-pointer stub; no `==`/`!=` codegen for Boxed operands; the
classifier misclassifying `GUI.Window`'s real `int` return as Boxed; AND/OR/XOR-of-Bool codegen
missing entirely, plus two further Number-vs-BOOL disambiguation bugs it exposed in `Kind::Branch`
and `Kind::Load`; an inline numeric literal used as a comparison operand losing its double
representation; default parameter values omitted at a call site never being filled in, plus a
`declared_parameter_type` trimming bug it exposed; and `ExitTheProgram()` producing a spurious
crash-looking panic instead of a clean exit — see Entry 20 for the full incident-by-incident
writeup) implemented and tested; Arconaut now compiles successfully to a native ELF64 binary and
its non-GUI code paths run byte-for-byte identical to `arco_cli`), and Phase 12 (making the GUI
backend REAL and running Arconaut against an actual display, not just compiling it — the gap
Phase 11 disclosed and left alone is now closed: a native `--target linux-x86_64` GUI program links
the full, GLFW-capable runtime `arco_cli` itself already builds by default, chosen per-program via
a new `program_calls_gui_function` AMIR scan, no new build-tree opt-in needed — confirmed with a
real screenshot that this real GLFW linkage renders Arconaut's actual UI correctly on a live
display (the full Volumes tab, the real block-device list, all 7 tabs, the Activity log); that
screenshot is of the bytecode capsule (`arco_cli`'s own execution path, which the asset-path fix
below benefits identically), not the native binary specifically — the native binary's own window
creation and GUI queries are separately, individually confirmed correct, but Arconaut's own full
native first frame is not yet independently screenshotted, currently blocked end-to-end by the
open bug below every time it's attempted. Eight more real bugs found by actually running it: a
stale root-level `assets/` directory that silently broke icon loading for EVERY Arconaut
invocation, interpreted or compiled; the `GUI.Window` classifier fix from Phase 11 was itself
wrong (reverted, fixed the right way this time with `AS NUMBER` annotations); "==" and "+"
both had the identical missing-codegen gap for operands ambiguously Boxed on BOTH sides (Phase 11
only covered one side provably Boxed, the other provably String); a Boxed array index built from
one of those "+" results; a genuine SEGV (not just a wrong answer) from a string literal's own
storage width being corrupted by frontend type-hint noise it was never meant to carry; and a
STRING-typed parameter's ambiguous representation corrupting values passed through multiple levels
of function calls — see Entry 21 for the full incident-by-incident writeup, including the
AddressSanitizer-based investigation technique that found the SEGV and the still-open bug below).
The native GUI-linking mechanism itself is no longer a gap, but Arconaut's own full native run is
not yet clean end to end (see the open bug below). A dotted field
can't hold or call a callable (only a plain variable can) — a confirmed, genuinely shared
interpreter/bytecode-VM limitation, not owned by this RFC, see Entry 18. The Windows target and
freestanding/AOS support not started. **A real, confirmed, UNRESOLVED reference-counting bug in
FOR-EACH loop codegen** was found in Phase 12 (a Boxed value produced outside a loop, compared
against a loop-iterated array element inside it, non-deterministically leaks or double-frees,
reduced to a minimal repro independent of any GUI code) — root-caused to `lower_for_each`'s own
loop-variable binding as the likely locus but not yet fixed; see Entry 21's own "UNRESOLVED"
section for the full repro and elimination process. See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md`
for the full, entry-by-entry implementation history (this document records the design and scope;
that one records what actually happened, in what order, including the real bugs found along the
way).
**Category:** Compiler / Systems Runtime
**Related RFCs:** RFC-0012 (ArcoFission Frontend to AMIR Contract — the shared IR this backend
reads), RFC-0007 (ArcoBASIC Interactive Program Model — the first intended consumer's own
foundation), RFC-0046 (Arcology Executable Assembly — a related but distinct native executable
*format*; this RFC is about *compiling to* native code, not about the container it ships in)

---

## 1. Executive Summary

This RFC defines a native compilation path for hosted ArcoBASIC — Linux and, later, Windows —
where an ArcoBASIC program's control flow (branches, loops, function calls) compiles to real
x86-64 machine code, not a bytecode array walked by a resident interpreter loop. Every other
`ArcoFission build`/`native` output today (the "arcocapsule" format) embeds exactly such an
interpreter; this is a second, narrower, opt-in output shape (`--target linux-x86_64`) that does
not.

**This is not part of ArcoSH.** It is a general Arcology/ArcoBASIC platform capability, living in
this repository's shared compiler (`src/compiler/fission.cpp`) and a small new runtime library
(`src/native/runtime_abi.cpp`), used by whatever needs genuinely native ArcoBASIC execution.
ArcoSH's own clean-room reimplementation mission is the first and motivating consumer — a real
interactive shell cannot be "the arcocapsule format" without recreating the exact class of
always-on bytecode-interpretation overhead this whole effort exists to avoid — but this RFC's
scope, and the code it governs, stand on their own. Nothing here depends on ArcoSH; ArcoSH depends
on this.

## 2. Motivation

`ArcoFission build`/`native` (no `--target`) is genuinely useful and stays the default: it embeds a
bytecode-compiled program plus the VM that interprets it (`execute_function`,
`src/compiler/fission.cpp`), giving every ArcoBASIC program access to the full dynamic language —
arrays, objects, classes, the complete stdlib — with no compilation-scope restrictions. That
generality has a real, measured cost (this session's own earlier work quantified and then narrowed
it via a hot-loop JIT), and it means every such binary carries an always-resident dispatch loop
regardless of what the program actually does.

A real interactive shell — the kind of thing users spend hours a day inside, where every keystroke
round-trips through the program — is exactly the case where that overhead is least acceptable and
most avoidable, since a shell's own hot path (read a line, dispatch a command, print a result) does
not need arbitrary dynamic language generality to be fast. More generally: any Arcology-level tool
that wants to *be* native, not merely *claim* to be, needs a real answer to "how does ArcoBASIC
compile to code with no interpreter in it," independent of what that tool happens to be.

## 3. Architecture

Two necessary, complementary pieces. Neither replaces the other; a system built from only one of
them cannot do anything beyond what that one piece alone provides.

```
ArcoBASIC source
       |
       v
generate_x86_64_function/_program   <-- the compiler backend. Turns control flow (IF/FOR/WHILE,
  (src/compiler/fission.cpp)            direct function-to-function calls, arithmetic on
       |                                statically-provable doubles) into real x86-64: real
       |                                CALL/JMP/Jcc, a real per-function stack frame. No bytecode
       |                                array, no cursor, no dispatch switch anywhere in the
       |                                output. Already existed for the UEFI/freestanding target
       |                                (RFC-0044-0047's own work); extended here with a System V
       |                                calling-convention variant and hosted-double (SSE2) support
       |                                rather than duplicated as a second pass.
       | CALL (real, named, resolved by the system linker at build time)
       v
ArcoValue system runtime             <-- the runtime library. What generated code calls into for
  (include/arco/native_runtime_abi.h,   anything a static compile-time proof can't reduce to a raw
   src/native/runtime_abi.cpp)          scalar: strings, bools (today), arrays/objects/general
                                         dynamic dispatch (planned). A thin, explicitly
                                         reference-counted box around the *existing*,
                                         already-correct `arco::Value` — not a second value
                                         representation to keep in sync with the first.
```

**The governing design rule**, stated once here since every future extension should be checked
against it: `ArcoValue` is the fallback for values whose type cannot be proven statically, not the
default representation for everything. A provably-numeric local or temp stays a raw IEEE-754
double in an XMM register or an 8-byte stack slot, with zero `ArcoValue` involvement — exactly the
hosted-number fast path already implemented. Boxing is reached for only at the edges: a value
whose type isn't known until runtime (an unconstrained function parameter, an array element, an
object field), or a genuine formatting/presentation boundary (`PRINT`, which was never on a hot
arithmetic path to begin with). A change that boxes something the compiler could otherwise prove
is a regression against this rule, not a simplification.

Output for the Linux target reaches a real ELF64 executable by generating the machine code as text
(`render_x86_64_linux_asm`, real GNU-assembler `.byte` sequences with genuine `call`/`lea` mnemonics
spliced in at the specific spans that need linker-resolved symbols or section-relative addressing)
and handing it to the system C++ compiler/linker — the exact mechanism `ArcoFission native` already
uses to build its own (bytecode-VM-embedding) capsules, reused rather than duplicated. No ELF
writer was built for this; none was needed.

## 4. Scope and current status

**Implemented and tested (Phase 1, System V / Linux):**
- `PRINT` of a string literal, a number (literal, variable, or a straight-line arithmetic
  expression), or a boolean — dispatched through `ArcoValue`/`arco_value_print`, so formatting is
  never re-implemented outside `arco::Value::to_string()` itself.
- `+ - * /` and unary `-` on numbers, via real SSE2 (`addsd`/`subsd`/`mulsd`/`divsd`), matching
  `arco::Value`'s own double-only number model — there is no separate integer type for this backend
  to diverge from.
- All six comparisons, including real IEEE-754 NaN semantics (`!=` true for NaN, every other
  comparison false) rather than a simplified "always false."
- `MOD` (real IEEE-754 remainder — called directly as an external libm `fmod` symbol, whose System
  V `(double,double)->double` signature already matches this backend's own XMM0/XMM1-in-XMM0-out
  convention with zero marshaling — not a truncating integer modulo) and bitwise/shift ops (`&`,
  `|`, `^`, `<<`, `>>`, unary `~`/`NOT` — converted to int64 via a truncating SSE2 conversion,
  operated on with ordinary GPR instructions, converted back — matching `eval_binary`'s own
  `value_to_int`-based semantics exactly, including `>>` being a signed/arithmetic shift, not a
  logical one). A runtime-zero MOD divisor panics with the exact text the interpreter throws; a NaN
  divisor correctly does not panic, matching `fmod`'s own IEEE-754 behavior byte-for-byte against
  `compile-run`. See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 10.
- Byte-identical output against both the bytecode VM (`compile-run`) and the tree-walking
  interpreter (`arco_cli`) for every case above, checked directly, not assumed.
- General user-declared function calls (`Foo(x)`), with real System V argument *classification*:
  each parameter is independently classified, by the callee's own declared type (or "untyped
  defaults to hosted-number," the common case), as integer/pointer class (GPR: RDI/RSI/RDX/RCX/
  R8/R9) or floating-point class (XMM0-7) — not by raw position — and the callee's return value
  comes back in XMM0 or RAX depending on the same straight-line classification PRINT itself uses.
  Real recursion works (each call is a real stack frame, no VM call-depth bookkeeping). A parameter
  whose actual type genuinely varies by call site (e.g. an untyped parameter sometimes called with a
  number, sometimes a string) is explicitly rejected with a clear compile error rather than silently
  misclassified — real support needs boxing through `ArcoValue` at the call boundary, not attempted
  here. No stack-spilled arguments/parameters yet (more than 6 integer-class or 8 float-class fails
  cleanly). See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 8 for the three real bugs found
  building this (a `Kind::Call`/`Kind::CallValue` mixup caused by `ArcoFission reveal` rendering both
  identically; a missing `Kind::DeclareFunction` codegen case; and the untyped-parameter
  misclassification that silently printed a string pointer's raw bits as a double before the
  caller-side safety check above was added).
- Arrays and objects (Phase 2), always boxed through `ArcoValue` — there is no unboxed
  representation for a dynamic, heterogeneous collection the way a provably-numeric scalar has one,
  so this isn't a fallback path, it's simply the only representation. Covers construction (`[1, 2,
  3]`, `{key: value}`), element/field get and set (`arr[i]`, `obj.field`, both directions), `LEN`,
  PRINT (arrays/objects format via the same `arco::Value::to_string()` every other value does, so
  nested/mixed-type collections print correctly with zero new formatting code), nesting
  (array-of-object, object-of-array, array-of-array), arithmetic directly on an array element or
  object field (real System V-classified `arco_value_as_number` unboxing — a real bug this exact
  case caught, see the ledger), and passing/returning arrays/objects through function calls (an
  explicit `AS ARRAY`/`AS OBJECT`-style parameter annotation routes through the ordinary GPR/pointer
  path already built for STRING; an untyped parameter still assumes hosted-number and rejects a
  Boxed argument with a clear error, the same rule already applied to STRING/BOOL). Out-of-range
  array access and a missing object property panic (stderr message + nonzero exit) rather than
  reading/writing out of bounds. A mixed index-then-property chain (`arr[i].field`, reading) works
  too — this needed a real, PRE-EXISTING frontend fix (`src/frontend/parser.cpp`'s
  `DynamicGetExpr` previously had no `AMIR` lowering at all, failing identically on the bytecode VM,
  not something Phase 2 introduced; fixed by giving it a `canonical_ast()` that reuses the existing
  generic `AstKind::Index` lowering, needing zero `fission.cpp` changes — see
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 10). Reference lifetime tracking, both
  assignment-side gaps noted here originally (`arr[i].field = x` failing at parse time; chained
  indexed assignment, `a.b.c = x`), and string codepoint/tuple/bit-vector/range indexing are ALL now
  implemented — see Phases 5, 6, and 8 below. Still real, disclosed, unattempted future work: the
  dedicated `BITS "..."` literal TOKEN (as opposed to the equivalent `Bits.FromString(...)` host
  function, which already works). See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 9 for the
  full original detail.
- Classes and methods (SELF, instance dispatch, EXTENDS/inheritance) — a class instance needed NO
  new representation at all (it's exactly an Object with a conventional `"__class"` field, and
  field construction/get/set already worked via the array/object machinery above); the one
  genuinely new piece is instance method call dispatch (`receiver.Method(...)`), resolved via a new
  `resolve_class_method` compile-time walk of the static class hierarchy (`module.class_parents`)
  plus a runtime check of the receiver's own `"__class"` string content
  (`arco_value_string_equals_utf16`) — single-level `EXTENDS`, a 3-level chain with a
  method-less middle class, and double dispatch (a base method calling `SELF.OverriddenMethod()`)
  all verified. `SELF` itself needed special-casing by name (it's always untyped, which collided
  with this backend's own "untyped parameter defaults to hosted-number" convention). String
  concatenation via `+` (a real Phase 1 gap, but pulled into this work since classes are far less
  useful without it — `"Hello, " + SELF.Name`-style patterns are extremely common) was implemented
  alongside. A chained method-call receiver (`a.b.Method(...)`) and a dynamic one
  (`f().Method(...)`/`arr[0].Method(...)`) are both now implemented — see Phase 7 below. A
  GLOBAL-only receiver (a SHARED class field host) is no longer a gap, see Phase 3 below. See
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 11 for the full detail, including a real
  segmentation fault this work found and fixed (two independent static-analysis functions silently
  disagreeing on whether a Boxed numeric field's `+` result was a number or a string) and a real
  regression the full test suite (not ad hoc testing) caught before this entry was considered done
  (a fix meant only for the System V target briefly broke an unrelated Microsoft x64/UEFI fixture
  until properly convention-gated).
- **Phase 3** — script-scope globals, `SHARED` class fields, and a generic host-function bridge
  ("finish off Linux support", prompted directly by the project owner naming ArcoSH as this
  backend's primary target right now). `apply_script_global_scoping`'s own `Runtime.SetGlobal`/
  `GetGlobal` AMIR calls are not a class-only mechanism — they back ANY top-level ArcoBASIC variable
  referenced from inside a FUNCTION, an extremely common pattern this backend previously couldn't
  compile at all; a REAL, confirmed-identical-to-`compile-run` ArcoBASIC language semantic was
  found along the way (a plain assignment INSIDE a function only ever shadows its own local copy,
  never writing back to the script-scope global — not a bug). Separately, a new generic bridge
  (`arco_call_host`, `src/native/host_bridge.cpp`) reaches a real, process-lifetime `arco::Runtime`'s
  own ~244-entry host-function library (`UPPER`, `String.Split`, `String.Join`, `Format`, and
  everything else this backend doesn't hand-roll dedicated native codegen for) instead of failing to
  compile outright — gracefully degraded (a new `ArcoNativeRuntimeCoreProbe` CMake probe target,
  paralleling the bytecode-capsule format's own established `ArcoFissionCapsuleCoreProbe`
  precedent) rather than hard-required: a program that never calls a host function has zero
  dependency on any of it. An unrecognized host-function name now fails at RUNTIME (a clean panic)
  rather than compile time, matching how the interpreter/bytecode VM themselves only ever discover
  this at runtime too. See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 12 for the full detail,
  including a real linker wall this work found and routed around (attempting to compile the lean
  runtime from raw source hit an undefined-reference wall at `arco::graphics::*`, a whole separate
  subsystem the hand-picked file list didn't include) and two real regressions the full test suite
  and a deliberate "move the library aside and rebuild" trial caught before this entry was
  considered done.
- **Phase 4** — `TRY`/`CATCH`/`THROW`, and `ADDRESSOF`/callable-variable dispatch (same pass,
  prompted by the project owner picking both, in that order, from an open "what else is useful"
  question). `TRY`/`CATCH` uses real `setjmp`/`longjmp` (generated native code has no C++ unwind
  tables) plus a global, process-wide LIFO handler stack — a deliberate simplification versus the
  bytecode VM's own per-call `try_stack`, observably identical for every case this backend needs.
  Each `TryBegin` AMIR site gets its own dedicated `jmp_buf` stack slot so nested `TRY` blocks are
  safe; `arco_value_panic` now raises a catchable `RuntimeError` object when a handler is active,
  falling back to its original fatal behavior only when uncaught. `ADDRESSOF Foo` boxes the plain
  string `"Foo"`; a call through a variable resolves at runtime by comparing that boxed string
  against every distinct `ADDRESSOF` target in the module, reusing the same per-candidate marshal-
  and-call lambda instance-method dispatch already established — deliberately not a thunk/raw-
  function-pointer design. See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 13 for the full
  detail, including four real bugs found and fixed by direct testing/disassembly: two classifier/
  codegen disagreements on a callable call's own return kind (one causing a segfault, one causing a
  silent garbage-double misclassification when same-arity candidates had different parameter types),
  a hard compile error from dispatching against arity-mismatched candidates, and — found only by the
  test suite's own negative case — a silent fallthrough to the wrong error message when zero
  candidates could ever match a given call site, fixed by making the "not a callable" panic
  unconditional once ADDRESSOF dispatch's own gating condition is met.
- **Phase 5** — real reference-counted lifetime tracking, closing the leak every prior arrays/
  objects/classes-touching entry back to Entry 9 explicitly disclosed as future work. A zero-init
  sweep for every Boxed-kind local's stack slot at function entry; `store_result` (the ~50-call-site
  shared helper behind every fresh Boxed construction/call-result) now releases a named local's OLD
  value before overwriting it; `Kind::Store`/`Kind::Load` (plain slot-to-slot copies in either
  direction) both retain the source and release the destination's old value, since a copy aliases
  two slots to the same underlying box; `Kind::Return` releases every Boxed local still alive at
  that specific return site except the one actually being returned; every ABI function confirmed to
  copy a value BY VALUE without taking ownership (`arco_value_array_push`/`_set`,
  `arco_value_object_set`, `arco_value_concat`, `arco_global_set`, `arco_call_host`) now has its own
  freshly-boxed marshaling temporaries released right after the call. Parameters are permanently
  excluded — borrowed for their whole lifetime, never retained or released by the callee. See
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 14 for the full detail, including five
  independently-found real bugs (a `Kind::Load`/`Kind::Store` asymmetry causing a real
  segfault/use-after-free in every class constructor; a classifier blind spot for a temp that's
  never a `Kind::Store` target, found via measured linear RSS growth and confirmed via a
  hand-written C++ ABI-level harness showing zero growth in isolation, proving the bug was in
  codegen, not the runtime; the six by-value-copy marshaling-temporary leaks above; a SEVENTH such
  leak in instance-method dispatch's own `"__class"` field read, found the same way; and an
  unrelated pre-existing `arco::Runtime` default instruction-limit of 100,000 that any native
  program calling an ordinary host function past that many times over its own lifetime would hit,
  fixed in `host_bridge.cpp`). Disclosed, deliberate non-fix: a `longjmp`-based exception unwinding
  through intermediate native function calls does not run their own cleanup, an inherent
  consequence of Phase 4's own setjmp/longjmp design, bounded to the error path only.
- **Phase 6** — chained indexed assignment (`a.b.c = x`, `arr[i].field = x`, `arr[i][j] = x`, and
  every mix). Never actually an interpreter/bytecode-VM gap at all: `assign_indexed` already
  recurses through an arbitrary-length key list on both (confirmed identical — `a.b.c = x` already
  worked on `compile-run` before this backend could compile it). Two separate real pieces: (1)
  `parser.cpp`'s `assignment_statement` — shared by every backend, not native-only — never accepted
  a `.field` continuation mixed with `[index]` brackets (a lone `Dot` token after a `]`, the same
  shape the expression grammar's own postfix-chaining loop already handled on the read side); fixed
  by extending its index-collection loop to also match `Dot` and push each dotted segment as an
  ordinary string-literal index, making `arr[i].field = x` parse identically to the
  already-working `arr[i]["field"] = x`. (2) This backend's own `Kind::StoreIndex` codegen was
  hardcoded to exactly one key plus one value; generalized to descend through every key but the
  last via an ordinary `INDEX` read (the same shape `assign_indexed`'s own recursion produces,
  unrolled since the key count is fixed at compile time), releasing each intermediate reference
  once its own one-level-deeper read is done with it — a real use of Phase 5's own lifetime-
  tracking machinery, not a parallel one. One real bug found by direct testing: the first draft
  spilled the new intermediate receiver to its scratch slot AFTER calling `arco_value_release` on
  the old one, but that release call (an ordinary System V call) is free to clobber RAX internally,
  corrupting the new receiver before it was ever stored — fixed by spilling it to a scratch register
  across the release call first, the same "spill before releasing" discipline `store_result`'s own
  `tracks_lifetime` logic already follows elsewhere. Verified against `compile-run` across a 2-level
  dot chain, a 2-level bracket chain, a 4-level all-dot chain, a 3-level all-bracket chain, and
  every dot/bracket mix — plus a 300,000-iteration reassignment loop confirming no leak from the new
  descent path's own intermediate releases.
- **Phase 7** — chained method-call receivers (`a.b.Method(...)`) and dynamic method calls
  (`f().Method(...)`, `arr[0].Method(...)` — the receiver is an arbitrary expression, not a name at
  all). The chained-receiver path resolves the method name from the LAST dot and reads every field
  before it via ordinary `INDEX` reads (mirroring Phase 6's own `Kind::StoreIndex` descent, and
  reusing its lifetime-tracking machinery for real); the dynamic case needed a genuinely new AST
  shape (`DynamicMethodCallExpr` previously had no `canonical_ast()` at all, unsupported on EVERY
  backend) lowered by storing the receiver into a fresh hidden local and dispatching on
  `"<that local>.<method>"` — a fix that lives in shared AMIR lowering, so `compile-run` benefits
  too. Also fixed, found while testing this: a general, pre-existing, silent WRONG-ANSWER bug where
  a multi-argument call (plain function OR instance method) needing more than one boxing/unboxing
  call could clobber an already-finalized earlier argument's register, or (a separate root cause in
  the plain-call path specifically) never unbox an explicitly-typed `AS NUMBER` argument at all —
  fixed with a general two-pass marshal (spill every argument to memory first, load into real
  registers second) now the standing pattern for this backend's call-marshaling code. See
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 16 for the full detail, including a real bug where
  a bare `%tN` receiver name collided with bytecode-prep's own, unrelated ADDRESSOF/CALLABLE
  convention for a `%`-prefixed call target.
- **Phase 8** — tuples, string codepoint indexing, and range/bit-vector LEN+indexing. Never actually
  a semantic gap: the interpreter/bytecode VM already supported all of it via one shared,
  already-proven dispatch (`index_value()`); this backend just had no native codegen for any of it
  yet. A new `arco_value_new_tuple` (every element available up front on one instruction, unlike
  `Kind::Array`'s own incremental push loop) and `arco_value_index_get` (a general sibling to the
  array-only `arco_value_array_get`, mirroring `index_value()`'s own array/tuple/bit-vector/range/
  string dispatch) cover construction and indexing; `Range(...)`/`Bits.FromString(...)` needed no
  new codegen at all, already reachable via Phase 3's generic host-function bridge. Two real bugs
  found by direct testing: `infer_hosted_value_kind` never recognized a `Kind::Tuple` result as
  Boxed (an oversight parallel to the existing Array/Object check right next to it); and LEN's own
  classifier always assumes a Number return regardless of dispatch path, so a plain hosted STRING
  variable's own `LEN` — falling through to the generic host-function bridge, which always returns
  Boxed — printed a garbage denormal double instead of the real length, a real, pre-existing,
  unrelated-to-tuples bug surfaced while verifying this phase's own extended LEN precedence. See
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 17 for the full detail. Explicitly NOT attempted:
  the dedicated `BITS "..."` literal token (`Kind::Const` has no case for a bit-vector-valued
  literal) — the equivalent host function already covers the same construction need. Tuple/string/
  bit-vector/range WRITE-indexing needed no attention: the interpreter itself already rejects
  `t[0] = x` ("value is not index-assignable"), confirmed directly.
- **Phase 9** — closing the last two items from the "full Linux support" ranked list.
  `arco_value_panic`'s own uncaught-error stderr text now matches `compile-run`'s wrapper text
  exactly (`BYTECODE RUN FAILED\n\n<message>\n`) — deliberately NOT also matching that command's
  own stdout-discarding behavior on failure, a dev-CLI-tool characteristic of that one subcommand,
  not a language semantic (the tree-walking interpreter and this backend both correctly flush
  `PRINT` output before an uncaught error). The host-function bridge's own build-time opt-in
  requirement, the list's last item, was investigated directly (the same "move
  `libarco_runtime_core.a` aside and rebuild" trial Entry 12 originally used) and found ALREADY
  correctly implemented: a program needing the bridge in an unopted-in build tree fails with a
  clear, actionable error at COMPILE time, never a confusing runtime crash — the original ranked-
  list item was simply mischaracterized, not a real gap; no code change was needed. See
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 18 for the full detail, including the direct
  three-way (`compile-run`/`arco_cli`/native) comparison that confirmed a dotted field holding a
  callable (`obj.handler()` where `obj.handler` was set via `ADDRESSOF`) is a genuinely shared
  interpreter/bytecode-VM limitation — identical failure on all three — and therefore explicitly
  out of this RFC's own scope, not attempted.
- **Phase 10** — the marshaling-loop audit Entry 16 itself left open: every `load_double_operand`/
  `box_operand_into_rax` call site in `generate_x86_64_function` individually traced for the same
  register-clobber bug shape (an earlier already-loaded value sitting in a caller-saved register
  when a later value's own unboxing/construction makes an external call). One more real instance
  found, in ordinary two-hosted-number Binary arithmetic (`+`/`-`/`*`/`/`/comparisons/`MOD`/
  bitwise): operand 1 is unboxed into XMM1 before operand 0 into XMM0 (a deliberate, pre-existing
  ordering), and if operand 0 is ALSO Boxed, its own unboxing call could clobber XMM1 — not observed
  to misbehave with this toolchain's own simple `arco_value_as_number`, but not a real correctness
  guarantee either, fixed the same spill-then-reload way regardless. Every other marshaling site
  (string concatenation, the host-function bridge, `Kind::Array`/`Object`/`Tuple` construction,
  `Kind::StoreIndex`) was individually confirmed already safe, each for a different concrete reason
  — see `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 19 for the full site-by-site detail. Closes
  out this whole "full Linux support" investigation with no open marshaling-safety questions
  remaining.
- **Phase 11** — compiling Arconaut (`arcfs-utils/apps/arconaut/arconaut.abas`), a real,
  substantial (~970-line), pre-existing ArcoBASIC GUI admin tool, as a direct stress test of this
  backend against actual production code rather than a purpose-built fixture. Found and fixed nine
  distinct real bugs: an arbitrary 4095-byte frame-size cap (not a real x86-64 encoding limit,
  raised to 1,000,000); `Runtime.Args()` a null-pointer stub, now a real argv-backed
  implementation; no codegen at all for `==`/`!=` between a Boxed operand and a string/number
  (a new `arco_value_equals` ABI function, mirroring `arco::values_equal()`); the classifier's
  generic "unrecognized host function returns Boxed" fallback misclassifying `GUI.Window`/
  `GUI.WindowShaped`'s real `int` handle return; AND/OR/XOR-of-BOOL codegen missing entirely
  (ArcoBASIC's AND/OR/XOR lower to the same `&`/`|`/`^` AMIR shape as bitwise operators; the result
  is a real NUMBER, confirmed against `compile-run`'s own ground truth, never a "true" Bool), which
  once fixed exposed two further "does this instruction's own metadata match its actual physical
  representation" bugs in `Kind::Branch` and `Kind::Load`, plus a distinct chained-`AND`
  (`a AND b AND c`) case where the inner AND's own Number-classified result needed accepting
  alongside a plain Bool operand; a `Kind::Const` integer literal used directly as a comparison
  operand losing its double representation when no frontend type hint was available; default
  parameter values omitted at a call site never being filled in at all (fixed at AST-lowering time
  via a new `function_declarations_` lookup, not codegen, so an arbitrary default expression --not
  just a literal-- lowers correctly), which exposed a `declared_parameter_type` bug (a typed
  parameter that ALSO has a default returned its type string with the default's own text still
  attached, a real segfault); and `ExitTheProgram()` falling through the generic host-bridge
  exception handler into a spurious crash-looking panic instead of the clean process exit every
  other backend already gives it. Arconaut now compiles successfully to a native ELF64 binary, and
  its reachable non-GUI code paths (the `--smoke` and no-display-session early exits) run byte-for-
  byte identical to `arco_cli`. Actually opening a GUI window from a native-compiled binary was
  not reached: the generic host-function bridge links `arco_runtime_core`, the same lean/
  `EXCLUDE_FROM_ALL` library the bytecode-capsule format's own "lean runtime" deliberately builds
  with the stub GUI backend, never real GLFW — so `GUI.Available()` is always false for a native
  binary today, a real, disclosed, PRE-EXISTING limitation from earlier phase work (not introduced
  by this phase), separately scoped future work if native GUI support is ever wanted. Also required
  ~20 explicit `AS STRING`/`AS BOOL`/`AS NUMBER` parameter annotations directly in Arconaut's own
  source (each one a case where a real call site provably always passes one concrete type,
  confirmed by reading the call site before annotating) — inert on every other backend, purely
  additive, matching this backend's own established "untyped parameter assumed hosted-number,
  proven otherwise must say so explicitly" convention. See
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 20 for the full incident-by-incident writeup.
- **Phase 12** — making the GUI backend REAL and running Arconaut against an actual display, not
  just compiling it. `build_linux_native_image` now links the full, GLFW-capable runtime
  `arco_cli` itself already builds by default (reusing `arco_cli`'s own executable link line — a
  static library has no link.txt of its own — via the same link.txt-probing trick every other
  native-link-dependency helper in this file already uses) instead of the lean/stub-GUI one,
  chosen per-program via a new `program_calls_gui_function` AMIR scan; a program that never calls
  `GUI.*` is completely unaffected, and this capability needed no new build-tree opt-in since
  `arco_cli` is already an ordinary default target. Confirmed with a real screenshot that this
  linkage renders Arconaut's actual UI correctly on a live display (the Volumes tab, the real
  block-device list, all 7 tabs, the Activity log) -- that screenshot is of the bytecode capsule
  (`arco_cli`'s own execution path), not the native binary specifically; native window
  creation/GUI queries are separately confirmed correct in isolation, but Arconaut's own full
  native first frame is not yet independently screenshotted, currently blocked end-to-end by the
  open bug below every time it's attempted. Getting this far found eight more real bugs, every
  one of them unreachable from compilation alone: a stale root-level `assets/` directory (an
  arcfs-utils-restructuring leftover) that silently broke icon loading for every Arconaut
  invocation, interpreted or compiled, not this backend's own bug at all; Phase 11's own
  `GUI.Window`/`GUI.WindowShaped` classifier fix was itself wrong (claimed Number while the actual
  codegen still produced a Boxed pointer, the exact "classifier and codegen disagree" pattern this
  project's memory warns about) — reverted, then fixed the way Phase 11's own
  SelectDevice/SelectSnapshot fix already established (`AS NUMBER` annotations on the receiving
  parameters); "==" and "+" both had the identical gap Phase 11's own Boxed-equality/concat fixes
  left uncovered — an operand ambiguously Boxed on BOTH sides (not just one side provably String),
  fixed for "==" by widening the existing `arco_value_equals` dispatch and for "+" with a genuine
  RUNTIME `arco_value_is_string` check (no static way to decide it, matching `eval_binary`'s own
  ground truth exactly) that boxes its result on EITHER branch so one instruction's result always
  has one consistent representation; a Boxed array index built from one of those "+" results,
  needing `Kind::Index` to accept Boxed the same way it already accepts Number; a genuine SEGV (not
  a wrong answer) from a string literal's own storage width being corrupted by a frontend BOOL type
  hint it was never meant to carry (`AND RAX, 0xFF` on a raw 64-bit pointer — ordinary, valid
  amd64, invisible without actually running it, found via AddressSanitizer); and a STRING-typed
  parameter's own ambiguous physical representation (a raw literal pointer vs a real boxed
  pointer) corrupting values passed through multiple levels of function calls, fixed by always
  boxing a STRING-typed argument at the call site and classifying every STRING-typed parameter as
  Boxed inside the callee to match. `PRINT` now flushes stdout immediately (kept permanently,
  found mid-investigation: neither an uncaught exception nor a SIGSEGV runs atexit flush handlers,
  so buffered PRINT trace output was silently vanishing and making crashes look earlier than they
  really were). **One real bug found this phase remains open and unresolved**: a genuine
  reference-counting bug in FOR-EACH loop codegen, reduced via AddressSanitizer to a minimal
  repro independent of Arconaut, GUI code, or any fix in this RFC (a Boxed value from outside a
  loop, compared against a loop-iterated array element inside it, non-deterministically leaks or
  double-frees) — root-caused to `lower_for_each`'s own loop-variable binding as the likely locus
  but not yet fixed or confirmed by a line-by-line audit; the single highest-priority remaining
  item for this backend. See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 21 for the full
  incident-by-incident writeup, including the repro and the AddressSanitizer/gdb techniques used.

**Explicitly not attempted yet (disclosed, not silently missing):**
- The Windows target (`--target windows-x86_64` for this backend does not exist; the *bytecode
  capsule* format's own `native ... --target windows-x86_64` is unrelated and unaffected). Needs a
  PE object-format equivalent of "shell out to a real linker" and the Microsoft x64 convention
  `generate_x86_64_function` already partially supports for the UEFI target.
- Freestanding/AOS support. The freestanding profile has no heap allocator by explicit, documented
  design (`arcology-os/docs/systems/uefi-target.md`) — whether to extend that profile with a real
  allocator so `ArcoValue` can exist there too, or accept that AOS gets this capability later than
  Linux/Windows, is a real open decision, not resolved by this RFC.
- **A real, confirmed, UNRESOLVED reference-counting bug in FOR-EACH loop codegen** (found in
  Phase 12): a Boxed value produced OUTSIDE a `FOR ... IN <array>` loop, then compared against a
  loop-iterated array element INSIDE the loop body, non-deterministically leaks (AddressSanitizer:
  a `arco_value_index_get` result, the loop's own `item := INDEX items, index` fetch, never
  released) or double-frees (`arco_value_release` heap-use-after-free) depending on the exact run.
  Reduced to a minimal repro with NO dependency on Arconaut, GUI code, or any Phase 11/12 fix —
  needs the compared value's own source expression to involve a PARAMETER (an identical repro using
  a plain local instead does not reproduce it) and the comparison to happen inside the loop body
  specifically (comparing outside the loop, or iterating without touching the value, does not
  reproduce it either). Most likely locus is `lower_for_each`'s own loop-variable binding
  interacting with the reference-lifetime tracking Phase 5 added, but this was narrowed to by
  elimination, not confirmed by a line-by-line audit the way Phase 10's marshaling-loop audit
  covered every OTHER site. The single highest-priority remaining item before this backend is
  genuinely production-ready for a real, stateful, loop-heavy program — see
  `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 21 for the full repro and investigation.

## 5. Relationship to ArcoSH

ArcoSH's clean-room reimplementation packet originally assumed "the smallest reusable execution
bridge" between ArcoBASIC source and a session loop — implicitly, something built on the existing
bytecode VM, with "compile the shell itself with no embedded interpreter" left as vague future
work. Investigating what that future work would actually require (this session, in direct
response to the project owner questioning whether the compiler-backend investment was real or a
detour) surfaced that no backend in this repository could compile a dynamic-`Value`-using
ArcoBASIC program to native code at all, on any target — making it its own real, separately-scoped
prerequisite rather than an incidental detail of ArcoSH's own session/RPM/protocol design.

This RFC is that prerequisite, standing on its own. ArcoSH's own mission packet and progress ledger
(`.agents/reports/ARCO_SH_WP000_REPOSITORY_AUDIT.md`, and ArcoSH's own future WP-001 onward once
resumed) should treat this RFC as a dependency to build against, the same way any other Arcology
subsystem depends on RFCs it doesn't own.

## 6. Non-goals

- A garbage collector. `ArcoValue` reuses `arco::Value`'s own existing `shared_ptr`-backed
  reference semantics for arrays/objects; reference counting at the ABI boundary is the only new
  memory-management mechanism this introduces.
- A general register allocator. Every local/temp is stack-resident (spilled and reloaded around
  every use), matching the existing freestanding backend's own approach — a real, deliberate,
  correctness-first simplification, not an oversight; a register allocator is plausible future work
  but changes nothing about this RFC's scope.
- Replacing the bytecode-VM capsule format. That format remains the default, general-purpose
  native/build output for anything that doesn't specifically need this narrower, faster, no-VM
  path.

## 7. References

- `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` — full implementation ledger (renamed from
  `.agents/ARCO_SH_PROGRESS.md`; the early entries predate this RFC and record the ArcoSH-mission
  framing this section's own history describes).
- `.agents/reports/ARCO_NATIVE_COMPILER_BACKEND_PLAN.md` — the phased technical plan (v2; v1 is
  preserved only in this same file's own superseded-content note, not as a separate document).
- `include/arco/native_runtime_abi.h`, `src/native/runtime_abi.cpp` — the ArcoValue ABI.
- `src/compiler/fission.cpp` — `generate_x86_64_function`/`generate_x86_64_program`
  (`CallingConvention` parameter), the `Kind::Call`/`Kind::Binary`/`Kind::Const`/`Kind::Unary`
  hosted-number/System-V additions.
- `arcology-os/include/arco/calling_convention.hpp`, `arcology-os/include/arco/x86_64_encoder.hpp`
  — the System V calling convention and SSE2 encoder additions (shared with the pre-existing UEFI
  target's own use of the same files).
- `tests/integration/linux_native_backend_smoke.sh` — the permanent regression suite for this
  target.
