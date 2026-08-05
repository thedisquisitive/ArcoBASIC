# PE32+ EFI Image

Status: WP-009 (PE32+ EFI Image)
Depends on: `arcology-os/docs/systems/x86-64-codegen.md`, `arcology-os/docs/systems/uefi-target.md` section 9 (strategy
decision)

`include/arco/pe_image.hpp` / `src/compiler/pe_image.cpp` write a minimal PE32+ EFI application
containing exactly the code and data WP-008's code generator produced. Exposed via
`ArcoFission build FILE -o OUT.efi --target uefi-x86_64 [--entry NAME]`.

## Layout

Headers, `.text`, and `.rdata` are each aligned to 4096 bytes (`SectionAlignment ==
FileAlignment == 0x1000`), so RVA and file offset are always identical — there is no separate
RVA-to-file-offset translation anywhere in the writer, eliminating an entire class of possible
bugs at the cost of some wasted padding (acceptable for a hello-world-sized image; Packet WP-009
has no size-optimization requirement).

```text
0x0000  IMAGE_DOS_HEADER (64 bytes: only e_magic="MZ" and e_lfanew=0x40 are meaningful)
0x0040  PE signature ("PE\0\0")
0x0044  IMAGE_FILE_HEADER (COFF header, 20 bytes)
0x0058  IMAGE_OPTIONAL_HEADER64 (240 bytes: standard + windows-specific fields + 16 data directories)
0x0148  IMAGE_SECTION_HEADER x2 (.text, .rdata; 40 bytes each)
0x1000  .text   (AddressOfEntryPoint; the generated function's first instruction)
0x2000  .rdata  (UTF-16 string constants)
0x3000  end of image
```

Every field offset and size above was verified against Microsoft's PE/COFF format reference before
being hardcoded (not derived from memory), and the resulting header was independently re-parsed and
confirmed correct by three separate tools before any real hardware/firmware testing: `file`
(libmagic), `objdump -p`, and Python's `pefile` library.

## Relocation Patching

WP-008 left every string constant's `LEA` instruction with an unpatched 4-byte placeholder plus a
recorded `MachineCodeRelocation{text_offset, instruction_end_offset, rdata_offset}`. This work
package resolves them once real section RVAs are known:

```text
displacement = (rdata_rva + relocation.rdata_offset) - (text_rva + relocation.instruction_end_offset)
```

matching the x86-64 RIP-relative addressing rule (displacement measured from the address of the
*next* instruction). Verified both by a unit test (`tests/unit/runtime_tests.cpp`) computing the exact
expected displacement for a hand-crafted image, and by the hello-world image actually working under
real UEFI firmware (see "Verification" below) — a wrong relocation would have produced a crash or
garbage output, not a plausible-looking success.

## No `.reloc` Section, No `IMAGE_FILE_RELOCS_STRIPPED`

The image has no base-relocation table (`DataDirectory[5]` is zero) because nothing in it needs
one: every address reference the code generator produces is RIP-relative (position-independent by
construction — see `arcology-os/docs/systems/x86-64-codegen.md`), so the image behaves correctly no matter
where the loader places it in memory.

**This does not mean `IMAGE_FILE_RELOCS_STRIPPED` should be set, and setting it was a real bug this
work package found and fixed.** The first working version of this writer set that flag (a
reasonable-looking choice: "no relocations exist, so mark them stripped," and multiple public
sources describe this as the standard way to ship a PE without a `.reloc` section). It produced a
PE32+ image that `file`, `objdump -p`, and `pefile` all parsed as fully well-formed -- and that real
OVMF firmware under QEMU refused to load, failing with a bare `Not Found` and no further diagnostic
(both from the automatic boot manager and from the UEFI Shell's own `LoadImage` path). A control
test in the identical harness with a known-good real-world EFI binary (`grubx64.efi` from the
`grub-efi-amd64-bin` package) booted successfully, proving the test harness itself was sound and the
problem was specific to this image. Comparing the two images' headers showed the difference:
`grubx64.efi` does **not** set `IMAGE_FILE_RELOCS_STRIPPED` and carries a real (non-empty)
`.reloc` section, even though nothing about its own correctness required per-load relocation any
more than this project's hello-world does. Removing the flag (while leaving `DataDirectory[5]`
zero and adding no `.reloc` section at all) was sufficient: the same image, unchanged in every other
byte, then booted and printed `Hello from ArcoBASIC` correctly.

The practical rule this work package is leaving behind: **do not set
`IMAGE_FILE_RELOCS_STRIPPED` unless a real `.reloc` section backs it up**, regardless of what a
structural validator says about the image otherwise being fine — OVMF's loader treats the
combination of "flag set, no usable relocation data" as a hard rejection, independent of whether
the image would have needed relocating at all. This is the kind of ABI/loader behavior Packet
section 9 asks to be verified against real tools rather than assumed from documentation, and in
this case the documentation available during development was genuinely ambiguous
("OVMF *may* run an EFI with relocs stripped if it can load it at its nominal address") right up
until a real boot test resolved it definitively.

## Verification (Packet WP-009 "Required validation")

| Requirement | How verified |
|---|---|
| PE32+ format | `pefile` full structural parse (no errors); `file` identifies it as "PE32+ executable for EFI (application), x86-64, 2 sections" |
| AMD64 machine type | `Machine = 0x8664`, confirmed by `objdump -p` and `pefile` |
| EFI application subsystem | `Subsystem = 10` (`IMAGE_SUBSYSTEM_EFI_APPLICATION`), confirmed by `objdump -p` and `pefile` |
| Entry point | `AddressOfEntryPoint` = start of `.text` (`0x1000`), matching `BaseOfCode`; the function actually executes when loaded (see below) |
| Section alignment | `SectionAlignment == FileAlignment == 0x1000`; every section's `VirtualAddress`/`PointerToRawData` is a multiple of 4096 |
| Image size | `SizeOfImage` and the actual file size agree (`0x3000` for the hello-world), both `pefile`- and manually-verified |
| Absence of host runtime imports | `DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]` is `{0, 0}` -- verified directly and by the fact `pefile` finds no import table to parse |
| **OVMF loads the image without an invalid-image error** | **Booted for real**: `qemu-system-x86_64 -bios <OVMF.fd> -drive file=fat:rw:<dir>,format=raw ...` with the built image at `EFI/BOOT/BOOTX64.EFI` -- OVMF's boot manager loads and starts it automatically (`BdsDxe: starting Boot0001 "UEFI QEMU HARDDISK..."`), and it prints `Hello from ArcoBASIC` to the UEFI console before returning control to the firmware, which then proceeds to its next boot option exactly as a well-behaved EFI application that returned `EFI_SUCCESS` should. |

`arcology-os/tests/systems/systems_pe_image_smoke.sh` automates the structural checks (dependency-free, using only
`od`, so it always runs regardless of what else is installed). The real boot check now lives in
`arcology-os/tests/systems/systems_qemu_ovmf_harness_smoke.sh` via the reusable `arcology-os/scripts/run/run-uefi-hello.sh` harness --
see `arcology-os/docs/systems/qemu-ovmf-harness.md` (Packet WP-010).

## What This Work Package Does Not Do

- Does not itself run the real boot check as part of its own test (moved to WP-010's dedicated,
  reusable harness after this work package proved the approach worked -- see
  `arcology-os/docs/systems/qemu-ovmf-harness.md`). The boot verification described in this document was
  performed manually while implementing this work package, establishing that it worked, before
  WP-010 formalized it into `arcology-os/scripts/run/run-uefi-hello.sh`.
- Does not handle programs whose code needs real relocations (any absolute 64-bit address
  reference). The current code generator (WP-008) never produces one, so this has not been an
  issue in practice, but it means this PE writer would need updating (real `.reloc` section
  emission) before it could be used for a program that does.
- Does not sign, checksum-validate, or otherwise prepare the image for Secure Boot. `CheckSum` is
  written as `0`, matching common practice for EFI applications where the field is not validated by
  the loader.
