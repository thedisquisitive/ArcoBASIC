# Systems Target RFC: UEFI x86-64

Status: draft (WP-001)
Mission: `arcology-os/Arcology_ArcoBASIC_Systems_Agent_Packet.md`
Depends on: `.agents/reports/WP-000-repository-audit.md`

This document is the binding specification for the first ArcoBASIC systems
target. It exists so that WP-002 through WP-011 can be implemented without
any of those work packages having to choose architecture on the project's
behalf. Where the Packet's illustrative syntax could not be preserved
verbatim without either colliding with existing language surface or
inventing new syntax beyond the minimum required, this document records the
decision and the reason.

---

## 1. Target Identity

```text
Architecture:            x86-64 (AMD64)
Firmware:                UEFI
Binary format:           PE32+ (COFF)
Machine type:            IMAGE_FILE_MACHINE_AMD64 (0x8664)
Image subsystem:         IMAGE_SUBSYSTEM_EFI_APPLICATION (10)
Execution environment:   QEMU (qemu-system-x86_64) + OVMF
Legacy BIOS:             not implemented (Packet §3.4)
ARM64:                   not implemented (Packet §3.4)
```

`qemu-system-x86_64` and OVMF firmware (`/usr/share/OVMF/OVMF_CODE_4M.fd` and
variants, `/usr/share/ovmf/OVMF.fd`) are already present on the development
host, so WP-010's environment policy ("if OVMF is unavailable, document
installation requirements... do not download firmware silently") is
satisfied without action.

---

## 2. Freestanding Profile Directives

The shared preprocessor (`Runtime::preprocess_source`, `runtime/runtime.cpp`)
is the single hook point used by both the interpreter and every ArcoFission
entry point (confirmed in WP-000 §3). New systems directives are added here,
not duplicated in `compiler/fission.cpp`.

```basic
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT "efi_main"
```

| Directive | Status | Decision |
|---|---|---|
| `#PROFILE` | new | Selects the compilation profile. `UEFI` is the only accepted value for this milestone. Any other profile value is rejected with a diagnostic naming the accepted set. |
| `#TARGET` | **existing, reused** | `runtime.cpp` already implements `#TARGET` as `metadata_.targets = split_words(args)` (a list of package-metadata platform names), currently unused by any shipped example, stdlib module, or test. Per the project decision recorded for this RFC, `#TARGET` is **extended** rather than shadowed by a second directive: when the active profile is a systems profile (`#PROFILE UEFI` or later systems profiles), the first word of `#TARGET`'s argument list is additionally interpreted as the codegen architecture (`X86_64` for this milestone). Outside a systems profile, `#TARGET`'s existing platform-list behavior is unchanged. This avoids the Packet §7 rule against "multiple competing spellings for the same concept" while leaving every existing use of `#TARGET` untouched (there are none in the repository today, so there is no compatibility risk in practice). |
| `#RUNTIME` | new | `NONE` disables the hosted runtime: no implicit `Runtime` object, no stdlib auto-import, no dynamic `Value` heap features that need it (arrays/objects backed by `shared_ptr` remain legal as *host-side* compiler data structures but must not be required by the *generated program* — see §7). Any other value is reserved and rejected for this milestone. |
| `#CALLCONV` | new | Declares the ABI used for the exported entry point. `UEFI` selects the Microsoft x64 convention (§4). This directive is only meaningful together with `#EXPORT`. |
| `#EXPORT` | new | Declares the symbol name the linker/image-writer must expose as the PE entry point. Takes exactly one quoted string. |

`#PROFILE`, `#RUNTIME`, `#CALLCONV`, and `#EXPORT` do not collide with any
existing directive name (`DEFINE`, `UNDEF`, `VERSION`, `AUTHOR`,
`DESCRIPTION`, `ENTRY`, `TARGET`, `REQUIRE`, `FEATURE`, `STRICT`,
`EXPERIMENTAL`, `DEPRECATED`, `WARNING`, `ERROR`, `TODO`, `NOTE`, `REGION`,
`ENDREGION`, `INCLUDE`, `IMPORT`, `IFDEF`/`IFNDEF`/`IF`/`ELSEIF`/`ELSE`/`ENDIF`).
Note `ENTRY` already exists as a metadata directive (`metadata_.entry = args`)
distinct in purpose from `#EXPORT`: `ENTRY` is unused elsewhere in the
codebase today and is a documentation-style metadata field, not a linker
symbol name. `#EXPORT` is kept as a separate, new directive because reusing
`ENTRY` for a materially different purpose (real PE export naming) would be
the same kind of ambiguity `#TARGET` reuse is deliberately avoiding — the
difference is `#TARGET` was reused because both meanings are "target
platform/architecture selection" (compatible in spirit), whereas `ENTRY` and
"linker export symbol" are not the same concept.

---

## 3. Required Types (Packet §8 / WP-002 contract)

The compiler must represent, for the freestanding profile only:

| Type | Size | Alignment | Signed | Notes |
|---|---|---|---|---|
| `U8`  | 1 byte | 1 | no | |
| `U16` | 2 bytes | 2 | no | |
| `U32` | 4 bytes | 4 | no | |
| `U64` | 8 bytes | 8 | no | |
| `I8`  | 1 byte | 1 | yes | two's complement |
| `I16` | 2 bytes | 2 | yes | two's complement |
| `I32` | 4 bytes | 4 | yes | two's complement |
| `I64` | 8 bytes | 8 | yes | two's complement |
| `BOOL` | 1 byte | 1 | n/a | `0`/`1` only; distinct type from `Uxx`, no implicit int coercion |
| `PTR` | 8 bytes | 8 | n/a | opaque address-sized value; for this milestone, UEFI handles and pointers use this opaque representation (Packet §8), with no address arithmetic exposed to ArcoBASIC source (consistent with the existing `REF()` safety model in `docs/references.md`, which already forbids raw address arithmetic) |

Literal range checking is required at parse/semantic-analysis time (not
runtime): `DIM`/`LET`-style declarations with an out-of-range literal must
fail to compile with a diagnostic, not throw at execution. No implicit
narrowing conversions are permitted (Packet §8) — an explicit conversion
helper (name TBD in WP-002, e.g. `CAST(value, "U8")`) is required to narrow.

**Declaration syntax decision:** the Packet's illustrative WP-002 examples
use `DIM a AS U8 = 255`. ArcoBASIC has no `DIM` keyword today; local
variable declaration is `LET name = expr` (`core/parser.cpp`,
`assignment_statement`), and `AS Type` annotations already exist for
function parameters, function/method return types, and class fields
(`Parser::parse_type_name`, `core/parser.cpp:1532`) but not for `LET`.
Introducing a new `DIM` keyword would create a second spelling for "declare
a local variable" alongside the existing `LET`, which Packet §7 forbids.
**Decision: extend `LET` to accept an optional `AS Type` clause** —
`LET a AS U8 = 255` — rather than adding `DIM`. This is a small, additive
grammar change in the same place function parameters already parse `AS
Type` (`match(TokenType::As)` then `parse_type_name(...)`). WP-002's
acceptance tests should use `LET`, not `DIM`.

**Type-name grammar (corrected during WP-002):** WP-001 originally assumed
`Parser::parse_type_name` needed to be extended to accept dotted type names.
That assumption was wrong and was disproved empirically while implementing
WP-002: the lexer's `identifier()` scanner already treats `.` as a valid
identifier-continuation character (`core/lexer.cpp:251`), so `UEFI.Handle`
lexes as a single `Identifier` token with lexeme `"UEFI.Handle"` — the same
mechanism that already makes `System.Capabilities`, `Math.PI`, etc. work as
plain identifiers today. `parse_type_name` therefore already accepts
`UEFI.Handle`/`UEFI.SystemTable` verbatim with **no grammar change**,
confirmed by running `ArcoFission reveal ... at AST` against `FUNCTION
Greet(x AS UEFI.Handle) AS U64` before writing any parser code. No action
was needed here.

---

## 4. UEFI Calling Convention (Microsoft x64)

Verify all details below against the primary specifications before
implementation (Packet §9 — "do not guess ABI details"): the *Microsoft x64
calling convention* is documented by Microsoft's x64 software conventions
reference, and the *UEFI entry point contract* is documented by the UEFI
Specification (current version) §4.1 ("EFI Image Entry Point"). The table
below states the well-established shape of both as background for WP-005;
it is not a substitute for checking the authoritative documents during
implementation.

- Integer/pointer arguments 1–4 passed in `RCX, RDX, R8, R9`; arguments 5+ on
  the stack.
- Floating-point arguments in `XMM0–XMM3` (not required for this milestone —
  no floating-point ABI work is in scope per Packet WP-002 non-goals).
- Return value in `RAX`.
- Caller allocates 32 bytes of "shadow space" on the stack before `CALL`,
  even when the callee has fewer than 4 arguments.
- Stack pointer (`RSP`) must be 16-byte aligned immediately before a `CALL`
  instruction (i.e. `RSP % 16 == 8` at function entry, after the return
  address is pushed).
- Callee-saved (non-volatile): `RBX, RBP, RDI, RSI, R12, R13, R14, R15`, plus
  `XMM6–XMM15`.
- Caller-saved (volatile): `RAX, RCX, RDX, R8, R9, R10, R11`, plus
  `XMM0–XMM5`.

`EFI_HANDLE` and pointer-to-struct parameters (`EFI_SYSTEM_TABLE*`) are
8-byte values passed by this convention like any other integer/pointer
argument — no special handling beyond the `PTR`/opaque-handle representation
in §3.

---

## 5. Entry-Point Mapping

```basic
FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64
```

maps to the C-equivalent UEFI entry point:

```c
EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
```

- The compiled function's PE/COFF export name is taken from `#EXPORT
  "efi_main"`, not from the ArcoBASIC function name `Main`. This indirection
  exists so the ArcoBASIC-facing name can stay idiomatic (`Main`) while the
  binary satisfies whatever symbol name the firmware/loader path expects.
- `#CALLCONV UEFI` selects the Microsoft x64 convention (§4) for this
  function's generated prologue/epilogue and argument marshalling.
- The function's `AS U64` return type maps to `EFI_STATUS` at the ABI level
  (`EFI_STATUS` is `UINTN`, 8 bytes on x86-64); returning `0` corresponds to
  `EFI_SUCCESS`.
- OVMF loads the PE32+ image directly as an EFI application and calls its
  entry point per the image's `AddressOfEntryPoint`; there is no separate
  CRT startup stub in this milestone (§6).

---

## 6. Runtime Assumptions

- No libc, no host OS API, no C runtime startup (`_start`, `crt0`, etc.) —
  the PE entry point *is* the compiled `Main` function directly.
- No heap allocator, no garbage collector, no dynamic `Value` runtime (the
  `arco::Value` variant used by the interpreter/bytecode VM is a host-side
  compiler data structure during compilation; it must not appear in, or be
  required by, the generated machine code for this milestone).
- No multicore, no interrupts, no page tables, no DMA, no PCI/USB/networking
  — all explicitly out of scope (Packet §4 "Out of scope").
- The only permitted "system call" for this milestone is the UEFI Simple
  Text Output Protocol call described in §5/§8 — reached via the
  `systemTable.ConsoleOut.Write(...)` binding, not any interpreter host
  function.

---

## 7. Error Behavior for Unsupported Features

Under `#RUNTIME NONE`, any construct that implicitly depends on the hosted
runtime must fail to compile with a diagnostic identifying the construct and
the required alternative, per Packet §13's diagnostic quality bar. Examples
of constructs that must be rejected for this milestone, with the required
diagnostic shape:

```text
UEFI target does not provide the standard console runtime.
Use UEFI.SystemTable.ConsoleOut or select a hosted runtime profile.
```

Rejected under `#RUNTIME NONE` (non-exhaustive; WP-003 owns the authoritative
list): `PRINT` (implicit console runtime), stdlib `#IMPORT`s that assume a
hosted `Runtime`, `File.*`/`Network.*`/`System.*`/GUI host-function calls,
class instantiation that relies on the interpreter's object runtime,
dynamic array/object literals (`[1,2]`, `{...}`) unless/until WP-004 defines
a freestanding lowering for them. None of these are needed by the hello-world
program in §8.

---

## 8. Exact Hello-World Source and Expected Output

`examples/uefi_hello.abas` (see also `tests/systems/uefi-hello/hello.abas`,
identical content, used as the test fixture):

```basic
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
#CALLCONV UEFI
#EXPORT "efi_main"

FUNCTION Main(imageHandle AS UEFI.Handle, systemTable AS UEFI.SystemTable) AS U64
    systemTable.ConsoleOut.Write("Hello from ArcoBASIC")
    RETURN 0
END FUNCTION
```

Only two deviations from the Packet's illustrative §7 syntax:

1. `systemTable.ConsoleOut.Write(...)` uses parentheses. ArcoBASIC's
   existing call grammar always uses parentheses for calls with arguments
   (`Person()`, `File.Exists(path)`, `Math.SIN(x)`); there is no existing
   bare-command call-with-argument grammar to reuse, and adding one would be
   new public syntax beyond the minimum required (Packet §14 — must-stop
   list includes "new public ArcoBASIC syntax beyond the minimum required").
   Using the existing parenthesized-call convention avoids that.
2. Dotted type names (`UEFI.Handle`, `UEFI.SystemTable`) require no grammar
   change at all — see the corrected note in §3.

Expected output (exact bytes, printed to the UEFI console / captured via
QEMU serial or display text extraction per WP-010):

```text
Hello from ArcoBASIC
```

`tests/systems/uefi-hello/expected-output.txt` holds exactly this line
(with a trailing newline) as the golden fixture for WP-010's harness.

---

## 9. PE/COFF Strategy (Packet §10)

Packet §10 gives a priority order: (1) an existing in-repo compiler/linker
component, (2) an object format supported by an already-approved host
linker, (3) a small isolated PE32+ writer, (4) a new external linker only if
unavoidable.

Toolchain survey on the development host (recorded here so WP-009 does not
have to re-derive it):

```text
x86_64-w64-mingw32-gcc / -ld    not found
lld / lld-link / ld.lld         not found
clang                           found (19.1.7) — no PE-capable linker paired with it here
nasm                            found (2.16.03)
objcopy                         found
qemu-system-x86_64, OVMF        found
```

There is no PE32+-capable linker already present in the repository or
verified available on the host. Options (1) and (2) are therefore not
available without adding a new external dependency (installing a
mingw-w64 cross-linker or LLVM `lld`), which Packet §5 requires to be
justified (why needed, license, build/runtime, why nothing existing
suffices) and which Packet §10 treats as last resort.

**Decision: Strategy 3 — a small, isolated PE32+ image writer implemented
inside ArcoFission itself.** This also keeps `arcofission build hello.abas
--target uefi-x86_64` dependency-free for end users (no external linker
needs to be installed to use the ArcoBASIC systems target), consistent with
Packet §10's requirement that "the developer should not need to invoke a
linker manually." For the same reason (avoid a new required external
dependency), **WP-008's x86-64 code generation should emit machine-code
bytes directly from ArcoFission's own minimal instruction encoder** for the
small required instruction subset (function prologue/epilogue, `mov`,
`lea`, `call`, `ret`, stack adjustment), rather than shelling out to `nasm`
or `clang` to assemble generated text. `nasm`/`clang`/`objcopy` remain
available on this host as optional tools for producing *diagnostic*
disassembly/comparison output (Packet §3.2 allows generated assembly for
diagnostics), not as build-time requirements.

The public developer workflow stays:

```text
arcofission build hello.abas -o hello.efi --target uefi-x86_64
```

(`--target` is a new CLI flag on the existing `build` subcommand in
`tools/arcofission_main.cpp`; it does not change the meaning of `build`
without `--target`, so existing invocations are unaffected.)

---

## 10. Summary of Net-New Language/Compiler Surface

Recorded here for traceability against Packet §14 ("must stop and report" /
"may decide without stopping"). Everything below is additive; nothing
existing is removed or redefined except `#TARGET`'s reuse (§2), which was
an explicit project decision rather than an agent-chosen redesign.

- New directives: `#PROFILE`, `#RUNTIME`, `#CALLCONV`, `#EXPORT`.
- Extended directive: `#TARGET` (adds architecture-selection meaning under a
  systems profile; platform-list meaning unchanged elsewhere).
- New types: `U8, U16, U32, U64, I8, I16, I32, I64, BOOL, PTR` (WP-002).
- Extended grammar: `LET name AS Type = expr` for local variables. (Dotted
  type names like `UEFI.Handle` require no grammar change — see §3.)
- New UEFI bindings (WP-006): `UEFI.Handle`, `UEFI.SystemTable`,
  `UEFI.SystemTable.ConsoleOut`, `.Write(text)` — minimal surface only,
  not a full UEFI binding set.
- New CLI flag: `--target uefi-x86_64` on `ArcoFission build`.
- New backend stage (WP-008/WP-009): A-MIR → x86-64 machine code → PE32+
  image, added alongside (not replacing) the existing A-MIR → bytecode → VM
  path, isolated behind its own backend interface per Packet §3.3.
- New A-MIR instruction (WP-004): `CallExternal`, distinguishing a call
  through a declared function parameter from an ordinary namespaced
  host/stdlib call. See `.agents/reports/WP-004-amir-systems-primitives.md`.
- New calling-convention model (WP-005): `include/arco/calling_convention.hpp`
  plus `ArcoFission reveal FILE at CALLCONV`. See
  `docs/systems/calling-conventions.md`.
- New UEFI bindings registry and compile-time field-chain validation
  (WP-006): `include/arco/uefi_bindings.hpp`,
  `Parser::validate_uefi_field_chain`. See `docs/systems/uefi-bindings.md`.
- New UTF-16 constant encoder and compile-time validation (WP-007):
  `include/arco/utf16.hpp`, `Parser::validate_utf16_arguments`. See
  `docs/systems/utf16-encoding.md`.
- New x86-64 instruction encoder and single-function code generator
  (WP-008): `include/arco/x86_64_encoder.hpp`,
  `generate_x86_64_function` / `ArcoFission reveal FILE at X86_64`. See
  `docs/systems/x86-64-codegen.md`.
- New PE32+ EFI image writer (WP-009): `include/arco/pe_image.hpp` /
  `compiler/pe_image.cpp`, `ArcoFission build FILE -o OUT.efi --target
  uefi-x86_64`. The built hello-world image boots under real QEMU/OVMF
  and prints `Hello from ArcoBASIC`. See `docs/systems/pe32-image.md`.
- New reusable QEMU/OVMF boot harness (WP-010): `scripts/run-uefi-hello.sh`.
  See `docs/systems/qemu-ovmf-harness.md`.

## Acceptance

This document specifies target triple, freestanding profile behavior,
required types (with the `LET`/dotted-type-name grammar decisions needed to
make them expressible), UEFI calling convention, entry-point mapping,
PE/COFF strategy (with toolchain survey and justification), runtime
assumptions, exact hello-world source, exact expected output, and error
behavior for unsupported features. No architecture choice is left for a
later work package to invent.

## Stop Conditions Check

The one real conflict identified in WP-000 (`#TARGET` semantics) has an
explicit resolution recorded in §2, per user decision. No other required
behavior conflicts with existing accepted ArcoBASIC syntax or compiler
architecture. **WP-001 is COMPLETE; WP-002 (Fixed-Width Types) may begin.**
