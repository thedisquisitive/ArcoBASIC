# ArcoBASIC Systems Language: UEFI x86-64 Target

This is the entry point for the ArcoBASIC systems-language milestone: compiling a restricted,
freestanding subset of ArcoBASIC directly into a bootable UEFI x86-64 application, with no C, C++,
Rust, or handwritten assembly anywhere in the application's own source. It exists so the project is
usable without the agent that built it (Packet WP-012 "Leave the project usable without the
agent"). Each linked document below covers one piece in full depth, verified against primary
sources and real hardware/firmware where that mattered; this page is the map.

```text
docs/systems/uefi-target.md          Target identity, directives, types, ABI, PE/COFF strategy (RFC)
docs/systems/calling-conventions.md  Microsoft x64 argument/return registers, shadow space, alignment
docs/systems/uefi-bindings.md        EFI_SYSTEM_TABLE/EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL field offsets
docs/systems/utf16-encoding.md       UTF-8 -> null-terminated UTF-16 constant encoding
docs/systems/x86-64-codegen.md       The instruction encoder and single-function code generator
docs/systems/pe32-image.md           The PE32+ writer, including the RELOCS_STRIPPED pitfall
docs/systems/qemu-ovmf-harness.md    The reusable QEMU/OVMF boot-and-verify harness
```

`.agents/reports/` holds one completion report per work package (`WP-000-repository-audit.md`
through `WP-011-end-to-end-milestone.md`), each with a `TESTS RUN`/`ACCEPTANCE CRITERIA`/`RISKS`
section, plus `systems-language-milestone-report.md`, the overarching summary against the Packet's
full Definition of Done.

## Prerequisites

To build ArcoBASIC/ArcoFission itself and run everything except the real firmware boot check:

- A C++17 compiler and CMake >= 3.16 (the only prerequisites this project has ever had).

To also run the real QEMU/OVMF boot check (`scripts/run-uefi-hello.sh`,
`tests/systems_qemu_ovmf_harness_smoke.sh`):

- `qemu-system-x86_64` (Debian/Ubuntu: `apt install qemu-system-x86`; Fedora:
  `dnf install qemu-system-x86`; Arch: `pacman -S qemu-system-x86`)
- An OVMF UEFI firmware image under `/usr/share/ovmf` or `/usr/share/OVMF` (Debian/Ubuntu:
  `apt install ovmf`; Fedora: `dnf install edk2-ovmf`; Arch: `pacman -S edk2-ovmf`)

Neither is required to build ArcoFission or to compile a `.efi` file -- only to actually boot one.
If either is missing, `scripts/run-uefi-hello.sh` prints the exact install command and exits
nonzero; it never downloads anything automatically (docs/systems/qemu-ovmf-harness.md).

`nasm`, `objdump`, and Python's `pefile` were used during development to independently verify the
instruction encoder and PE writer against ground truth from tools that don't share this project's
own reasoning. **None of them are required** to build, test, or use ArcoFission -- they were
verification aids, not dependencies (docs/systems/x86-64-codegen.md, docs/systems/pe32-image.md).

## Build Commands

```sh
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

Then, to compile an ArcoBASIC systems program into a bootable UEFI application:

```sh
build/ArcoFission build tests/systems/uefi-hello/hello.abas -o hello.efi --target uefi-x86_64
```

`--target uefi-x86_64` is the only value this milestone supports; omitting it preserves
ArcoFission's existing (unrelated) Linux ELF64 bytecode-capsule build path exactly as before this
mission started. An optional `--entry NAME` selects which declared `FUNCTION` becomes the image's
entry point (default `Main`).

## Test Commands

```sh
cd build && ctest --output-on-failure
```

Runs every test, including the systems-specific ones added by this mission
(`systems_fixed_width_types_smoke`, `systems_freestanding_profile_smoke`,
`systems_amir_primitives_smoke`, `systems_calling_convention_smoke`, `systems_uefi_bindings_smoke`,
`systems_utf16_encoding_smoke`, `systems_x86_64_codegen_smoke`, `systems_pe_image_smoke`,
`systems_qemu_ovmf_harness_smoke`). The last one skips gracefully (not a failure) if QEMU/OVMF are
not installed. To run only the systems tests:

```sh
ctest --output-on-failure -R '^systems_'
```

To boot a built image directly, outside the test suite:

```sh
scripts/run-uefi-hello.sh hello.efi "Hello from ArcoBASIC"
```

## Target Syntax

The exact, complete hello-world program (`tests/systems/uefi-hello/hello.abas`,
`examples/uefi_hello.abas`):

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

- `#PROFILE UEFI` -- selects the systems profile. Only `UEFI` is accepted.
- `#TARGET X86_64` -- selects the architecture. Only `X86_64` is accepted. (`#TARGET` is an
  existing directive, extended for this meaning under a systems profile; see
  `docs/systems/uefi-target.md` section 2.)
- `#RUNTIME NONE` -- disables the hosted interpreter runtime; see "Unsupported Features" below for
  what this rules out.
- `#CALLCONV UEFI` -- selects the Microsoft x64 calling convention for the entry point.
- `#EXPORT "efi_main"` -- names the PE export symbol (informational at this stage; the PE writer
  always places the entry point at the start of `.text` regardless of the declared function name).
- `LET name AS Type = literal` -- typed local declarations, with compile-time range checking for
  the ten fixed-width types (`U8`/`U16`/`U32`/`U64`/`I8`/`I16`/`I32`/`I64`/`BOOL`/`PTR`).
- `UEFI.Handle`, `UEFI.SystemTable`, `UEFI.SystemTable.ConsoleOut`,
  `UEFI.SimpleTextOutputProtocol.Write` -- the bound UEFI surface (see below).

## Supported Systems Features

- Fixed-width integer/pointer types with exact-integer literal range checking
  (`docs/systems/uefi-target.md` section 3).
- The freestanding profile, rejecting hosted-runtime constructs (`PRINT`,
  `File.`/`Network.`/`System.`/etc. calls) with clear diagnostics when used under `#RUNTIME NONE`
  (`docs/systems/uefi-target.md` section 7).
- A-MIR representation of external/ABI-bound calls, distinct from ordinary host/stdlib calls
  (`.agents/reports/WP-004-amir-systems-primitives.md`).
- The Microsoft x64 calling convention, including automatic injection of the implicit `This`
  argument real UEFI protocol methods require (`docs/systems/x86-64-codegen.md`).
- `UEFI.SystemTable.ConsoleOut` (`EFI_SYSTEM_TABLE.ConOut`, offset `0x40`) and
  `UEFI.SimpleTextOutputProtocol.Write` (`EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString`, offset
  `0x08`), verified against the TianoCore EDK2 reference headers (`docs/systems/uefi-bindings.md`).
- UTF-16 string constant encoding with correct surrogate-pair handling
  (`docs/systems/utf16-encoding.md`).
- A single-function x86-64 code generator (prologue/epilogue, uniform spill-based value handling,
  UEFI field dereferencing, indirect calls) producing machine code independently verified against
  `nasm` and `objdump` (`docs/systems/x86-64-codegen.md`).
- A self-contained PE32+ writer producing images that boot under real QEMU/OVMF
  (`docs/systems/pe32-image.md`).
- `ArcoFission reveal FILE at {AST|A-MIR|BYTECODE|CALLCONV|X86_64}` -- every compiler stage is
  independently inspectable.

## Unsupported Features (Non-Goals, Not Gaps to Silently Assume Away)

- **Control flow**: the code generator supports exactly one straight-line A-MIR block. `IF`,
  `WHILE`, `FOR`, `TRY`, and any other multi-block construct produce a clear compile-time error
  under `--target uefi-x86_64`, not incorrect code (`docs/systems/x86-64-codegen.md`).
- **Classes, arrays, objects** under `#RUNTIME NONE`: not rejected at parse time today (a
  documented, narrower-than-planned scope decision -- see `.agents/reports/
  WP-003-freestanding-profile.md` DEVIATIONS), but not lowered by the code generator either, so a
  program using them under `--target uefi-x86_64` fails at the X86_64 codegen stage.
- **UEFI binding surface**: only `ConsoleOut`/`Write` are bound. Every other `EFI_SYSTEM_TABLE`
  field (`ConIn`, `RuntimeServices`, `BootServices`, ...) and every other
  `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL` method (`Reset`, `ClearScreen`, ...) is rejected with a
  diagnostic naming what is bound instead (`docs/systems/uefi-bindings.md`).
- **Floating point, SIMD, interrupts, MMIO, page tables, DMA, PCI, USB, networking, multicore,
  ARM64, legacy BIOS**: out of scope for this milestone by design (Packet section 4).
- **5th+ function parameters** (stack-passed, beyond the four register-passed arguments): rejected
  with a clear error; the hello-world's two-parameter entry point never needs one
  (`docs/systems/x86-64-codegen.md`).
- **Real PE relocations**: the writer has no `.reloc` section; every address the code generator
  produces is RIP-relative by construction, so none is needed today, but a future program requiring
  an absolute address fixup is not supported yet (`docs/systems/pe32-image.md`).

## Troubleshooting

| Symptom | Meaning | See |
|---|---|---|
| `Unknown compilation profile: X` / `Unsupported architecture for the UEFI profile: X` | Only `#PROFILE UEFI` and `#TARGET X86_64` are accepted this milestone | `docs/systems/uefi-target.md` |
| `... is not available under #RUNTIME NONE` | A hosted-runtime construct (`PRINT`, `File.*`, etc.) was used in a freestanding program | `docs/systems/uefi-target.md` section 7 |
| `UEFI.SystemTable has no bound field or method "X" in this milestone. Bound fields: ConsoleOut.` | Only the listed UEFI surface is bound; `X` is real UEFI but not implemented here, or not real at all | `docs/systems/uefi-bindings.md` |
| `string argument cannot be encoded as UTF-16: ...` | A string literal passed to a UEFI call had an embedded NUL byte or malformed UTF-8 | `docs/systems/utf16-encoding.md` |
| `function "X" has control flow beyond a single straight-line block, which this milestone's code generator does not support` | The program uses `IF`/`WHILE`/`FOR`/etc.; the code generator only handles one block | `docs/systems/x86-64-codegen.md` |
| `ERROR: qemu-system-x86_64 was not found` / `no OVMF UEFI firmware image was found` | Install guidance is printed directly to stderr; nothing is downloaded automatically | `docs/systems/qemu-ovmf-harness.md` |
| `arco_runtime_tests` crashes only with an explicit `-DCMAKE_BUILD_TYPE=Release`/`RelWithDebInfo` build | A **pre-existing, unrelated** bug in `shell/arcosh.cpp`'s `Process.Exists`, confirmed present before this mission started; does not occur with the project's standard build configuration | `.agents/reports/WP-000-repository-audit.md` section 7b |
| `undefined bytecode local: X` when running a typed-parameter function through `compile-run`/`run` | A **pre-existing, unrelated** bytecode-VM parameter-binding bug; irrelevant to the `--target uefi-x86_64` path, which does not use the bytecode VM | `.agents/reports/WP-000-repository-audit.md` section 7a |

## Architecture Overview

```text
ArcoBASIC source (.abas)
        |
    Lexer + shared preprocessor (#PROFILE/#RUNTIME/#TARGET/#CALLCONV/#EXPORT handled here)
        |
    Parser -- builds the AST AND performs every compile-time systems check:
        |     fixed-width literal ranges, freestanding-profile rejections,
        |     UEFI field-chain resolution, UTF-16 constant validation
        |
        +-- (for `reveal ... at AST`: rendered directly from here)
        |
    AmirBuilder -- ***re-derives structure from the raw token stream independently of the
        |          AST above*** (a real, load-bearing architectural fact discovered during
        |          this mission -- see .agents/reports/WP-000-repository-audit.md; any new
        |          grammar must be taught to both the parser AND this builder)
        |
        +-- A-MIR (typed function signatures, CallExternal for ABI-bound calls, ...)
        |
    generate_x86_64_function -- single-function, spill-based x86-64 lowering
        |
        +-- machine code (.text) + UTF-16 data (.rdata) + unpatched RIP-relative relocations
        |
    write_pe32plus_efi_image -- patches relocations, lays out a minimal PE32+ image
        |
        +-- a real, bootable .efi file
        |
    QEMU + OVMF (real UEFI firmware) -- boots it, runs it, "Hello from ArcoBASIC" appears
```

The existing (pre-mission) Linux ELF64 bytecode-capsule build path (`ArcoFission build FILE -o
OUT` without `--target`) is untouched and uses a completely different route (A-MIR -> bytecode ->
tree-walking VM, wrapped in an ELF64 launcher) -- it does not share any code with the path above
beyond the shared lexer, preprocessor, and parser-level validation.

## Generated Artifact Locations

| Command | Output |
|---|---|
| `cmake --build build` | `build/ArcoFission`, `build/arcosh`, `build/arco_cli`, `build/arco_tests`, `build/libarco.a` |
| `ArcoFission build FILE -o OUT.efi --target uefi-x86_64` | `OUT.efi`, wherever specified -- a self-contained PE32+ file; nothing else is written |
| `ArcoFission reveal FILE at X86_64` | printed to stdout only; no file is written (use shell redirection to save it) |
| `scripts/run-uefi-hello.sh` | no artifacts left behind -- its temporary FAT boot directory is created under `mktemp -d` and removed on exit, success or failure |
