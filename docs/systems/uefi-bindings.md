# UEFI Bindings

Status: WP-006 (UEFI Bindings)
Depends on: `docs/systems/uefi-target.md`, `docs/systems/calling-conventions.md`

This document is the verification trail for `include/arco/uefi_bindings.hpp`. Packet section 9
requires ABI details to be verified from primary technical specifications rather than guessed;
this document records exactly what was verified, against what, and how each byte offset was
derived, so a future agent (or a human) can re-check it without re-deriving everything from
scratch.

Per Packet WP-006, this binds **only the smallest surface needed for text output** -- it does not
attempt to bind the rest of the UEFI specification. Any UEFI field not listed below is not bound in
this milestone; ArcoBASIC source that references it is rejected at compile time with a diagnostic
naming what actually is bound (`core/parser.cpp::Parser::validate_uefi_field_chain`), not silently
accepted or miscompiled.

## Sources

Fetched directly from the TianoCore EDK2 repository (`https://github.com/tianocore/edk2`), the
reference implementation of the UEFI Specification that essentially all real UEFI firmware and
bootloader development is built against:

- `MdePkg/Include/Uefi/UefiSpec.h` -- `EFI_SYSTEM_TABLE` field list
- `MdePkg/Include/Uefi/UefiMultiPhase.h` -- `EFI_TABLE_HEADER` field list
- `MdePkg/Include/Protocol/SimpleTextOut.h` -- `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` field list and the
  `EFI_TEXT_STRING` (`OutputString`) function pointer signature
- `MdePkg/Include/Base.h` -- base type sizes on a 64-bit (X64) target

Cross-checked against the public UEFI Specification's own field-order description (search results
summarizing `https://uefi.org/specs/UEFI/2.10/04_EFI_System_Table.html` and
`https://uefi.org/specs/UEFI/2.10/12_Protocols_Console_Support.html`; direct fetch of uefi.org
returned HTTP 403 in this environment, so the edk2 reference headers -- which implement the same
spec verbatim -- were used as the primary source instead).

## Byte-Offset Derivation

All UEFI structures use natural C alignment on x86-64 (8-byte pointers, no `#pragma pack`). Offsets
below are computed field-by-field from the verified type list, not taken from a secondary source
that might already have made an arithmetic error.

### `EFI_TABLE_HEADER` (24 bytes)

| Field | Type | Size | Offset |
|---|---|---|---|
| `Signature` | `UINT64` | 8 | 0 |
| `Revision` | `UINT32` | 4 | 8 |
| `HeaderSize` | `UINT32` | 4 | 12 |
| `CRC32` | `UINT32` | 4 | 16 |
| `Reserved` | `UINT32` | 4 | 20 |

Total: 24 bytes (already 8-byte aligned; no trailing padding needed).

### `EFI_SYSTEM_TABLE` (120 bytes) -> ArcoBASIC `UEFI.SystemTable`

| Field | Type | Size | Offset |
|---|---|---|---|
| `Hdr` | `EFI_TABLE_HEADER` | 24 | 0 |
| `FirmwareVendor` | `CHAR16*` | 8 | 24 |
| `FirmwareRevision` | `UINT32` | 4 | 32 |
| *(padding)* | -- | 4 | 36 |
| `ConsoleInHandle` | `EFI_HANDLE` | 8 | 40 |
| `ConIn` | `EFI_SIMPLE_TEXT_INPUT_PROTOCOL*` | 8 | 48 |
| `ConsoleOutHandle` | `EFI_HANDLE` | 8 | 56 |
| **`ConOut`** | **`EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*`** | **8** | **64 (0x40)** |
| `StandardErrorHandle` | `EFI_HANDLE` | 8 | 72 |
| `StdErr` | `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*` | 8 | 80 |
| `RuntimeServices` | `EFI_RUNTIME_SERVICES*` | 8 | 88 |
| `BootServices` | `EFI_BOOT_SERVICES*` | 8 | 96 |
| `NumberOfTableEntries` | `UINTN` | 8 | 104 |
| `ConfigurationTable` | `EFI_CONFIGURATION_TABLE*` | 8 | 112 |

`FirmwareRevision` (a `UINT32`) leaves a 4-byte gap before `ConsoleInHandle` (a pointer, requiring
8-byte alignment) -- this padding is a real, necessary part of the layout, not an omission.

**Bound in this milestone:** `ConOut` only, at offset `0x40`, exposed to ArcoBASIC as
`UEFI.SystemTable.ConsoleOut` (renamed for readability; the underlying offset and pointee type are
unchanged from the spec). This offset is the well-known `gST->ConOut` access pattern used throughout
public UEFI hello-world tutorials, cross-checked here rather than assumed correct because it matched
memory.

### `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` (80 bytes) -> ArcoBASIC `UEFI.SimpleTextOutputProtocol`

| Field | Type | Size | Offset |
|---|---|---|---|
| `Reset` | `EFI_TEXT_RESET` (fn ptr) | 8 | 0 |
| **`OutputString`** | **`EFI_TEXT_STRING` (fn ptr)** | **8** | **8 (0x08)** |
| `TestString` | `EFI_TEXT_TEST_STRING` (fn ptr) | 8 | 16 |
| `QueryMode` | `EFI_TEXT_QUERY_MODE` (fn ptr) | 8 | 24 |
| `SetMode` | `EFI_TEXT_SET_MODE` (fn ptr) | 8 | 32 |
| `SetAttribute` | `EFI_TEXT_SET_ATTRIBUTE` (fn ptr) | 8 | 40 |
| `ClearScreen` | `EFI_TEXT_CLEAR_SCREEN` (fn ptr) | 8 | 48 |
| `SetCursorPosition` | `EFI_TEXT_SET_CURSOR_POSITION` (fn ptr) | 8 | 56 |
| `EnableCursor` | `EFI_TEXT_ENABLE_CURSOR` (fn ptr) | 8 | 64 |
| `Mode` | `SIMPLE_TEXT_OUTPUT_MODE*` | 8 | 72 |

**Bound in this milestone:** `OutputString` only, at offset `0x08`, exposed to ArcoBASIC as
`UEFI.SimpleTextOutputProtocol.Write`.

## `OutputString` Signature and the Implicit `This` Argument

Verified prototype (`EFI_TEXT_STRING` in `SimpleTextOut.h`):

```c
typedef
EFI_STATUS
(EFIAPI *EFI_TEXT_STRING)(
  IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
  IN CHAR16 *String
);
```

This takes **two** arguments in the real C ABI: `This` (the protocol pointer itself -- the same
value `UEFI.SystemTable.ConsoleOut` resolves to) and `String` (a UTF-16 string pointer). ArcoBASIC
source calls this as `systemTable.ConsoleOut.Write("Hello from ArcoBASIC")` with only **one**
explicit argument. `This` is implicit, matching the method-call convention every other ArcoBASIC
call already uses (`SELF` is passed implicitly to class methods).

`UefiField::implicit_this_argument` in `include/arco/uefi_bindings.hpp` records this fact for
`Write`. **This work package does not yet act on it** -- A-MIR's `CallExternal` instruction for
`systemTable.ConsoleOut.Write("...")` still shows exactly one argument (the string), matching what
WP-004 already produces. Injecting the implicit `This` pointer as a real leading argument is left
for WP-008, when a call actually needs to be lowered to a real x86-64 `CALL` instruction with a
real argument list built via `include/arco/calling_convention.hpp::assign_argument_locations`.
Recorded here explicitly so it is a known, flagged gap rather than a silent one.

`String` is a `CHAR16*` (UTF-16, null-terminated) in the real ABI; ArcoBASIC source currently passes
a plain (non-UTF-16) string literal. Producing a real UTF-16 constant is WP-007's job
(`docs/systems/uefi-target.md` roadmap section 11).

## `EFI_STATUS` and `EFI_HANDLE`

Both are simple typedefs, not structures with fields, so neither needs a `UefiType` registry entry
with bound fields:

- `EFI_HANDLE` is `VOID*` -- an opaque pointer. `UEFI.Handle` is registered in
  `include/arco/uefi_bindings.hpp` with zero fields, so any attempt to access a field on a
  `UEFI.Handle`-typed value is correctly rejected (verified in
  `tests/systems_uefi_bindings_smoke.sh`).
- `EFI_STATUS` is `UINTN` (8 bytes on x86-64), with `EFI_SUCCESS = 0` and error codes having the
  high bit set. This is already captured by `docs/systems/uefi-target.md` section 5's mapping of
  the entry point's `AS U64` return type to `EFI_STATUS`/`RAX`; no separate binding entry is needed.

## Compile-Time Field Resolution

`Parser::validate_uefi_field_chain` (`core/parser.cpp`) walks a dotted call chain
(`systemTable.ConsoleOut.Write`) against this registry whenever the chain's root identifier is a
declared function parameter with a known `UEFI.*` type (tracked per-function via
`current_function_parameter_types_`, populated in `Parser::function_statement`). Each segment must
resolve to a bound field; the first segment that does not produces a diagnostic naming the type it
failed to resolve against and every field that *is* bound, for example:

```text
UEFI.SystemTable has no bound field or method "ConIn" in this milestone. Bound fields: ConsoleOut.
See docs/systems/uefi-bindings.md.
```

This satisfies Packet WP-006's acceptance criterion ("the compiler can type-check the hello-world
source and resolve the required UEFI fields") without requiring a general type-inference system:
the check only activates for identifiers whose declared parameter type is a recognized UEFI type,
and is a no-op for every other type name (ordinary hosted types, class instances, user-defined
types), so it cannot produce false positives outside the systems surface it is scoped to.

## What This Work Package Does Not Do

- Does not bind `ConIn`, `StdErr`, `RuntimeServices`, `BootServices`, or any other `EFI_SYSTEM_TABLE`
  field beyond `ConOut`.
- Does not bind `Reset`, `TestString`, `QueryMode`, `SetMode`, `SetAttribute`, `ClearScreen`,
  `SetCursorPosition`, `EnableCursor`, or `Mode` on `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` beyond
  `OutputString`.
- Does not inject the implicit `This` argument into A-MIR (see above -- WP-008).
- Does not produce UTF-16 constants (WP-007).
- Does not validate field chains through variables that are not themselves a function parameter
  (e.g. a local variable reassigned from a parameter) -- only direct parameter-rooted chains are
  checked, matching WP-004's `CallExternal` classification scope.
