```text
STATUS
Complete

OBJECTIVE
Produce a valid UEFI-loadable executable (Packet WP-009).

SUMMARY
Built a minimal PE32+ image writer (include/arco/pe_image.hpp, compiler/pe_image.cpp) that places
WP-008's generated .text/.rdata into a PE32+ EFI application, patching the RIP-relative relocations
WP-008 deliberately left unresolved. Every header field offset and size was verified against
Microsoft's PE/COFF format reference before being hardcoded. SectionAlignment == FileAlignment ==
4096 bytes throughout, so RVA always equals file offset -- no separate translation logic exists
anywhere in the writer, eliminating a whole class of potential bugs. Exposed as
`ArcoFission build FILE -o OUT.efi --target uefi-x86_64 [--entry NAME]`, extending the existing
`build` subcommand per the decision already recorded in docs/systems/uefi-target.md section 10
rather than adding a new command.

The resulting image was independently validated by three separate tools before any real
firmware testing: `file` (libmagic), `objdump -p`, and Python's `pefile` library (installed via pip
for this verification step only, not a build dependency) -- all three parsed it as a fully
well-formed PE32+ EFI application with no structural errors.

That still was not sufficient. Booting the image under real QEMU + OVMF failed with a bare
"Not Found" from both the automatic boot manager and the UEFI Shell's own LoadImage path, with no
further diagnostic available (the Debian OVMF package ships a RELEASE build with DEBUG() output
compiled out). A control test -- booting a real, known-good EFI binary (grubx64.efi from the
grub-efi-amd64-bin package) through the byte-identical test harness -- succeeded immediately,
proving the QEMU/OVMF harness itself was sound and the defect was specific to the generated image.
Comparing the two images' headers found the difference: this project's image set
IMAGE_FILE_RELOCS_STRIPPED (a reasonable-looking choice given available documentation described it
as the standard way to ship a PE with no .reloc section), while grubx64.efi did not set that flag
and carried a real .reloc section despite not needing per-load relocation any more than this
project's position-independent code does. Removing the flag (still with a zero BASERELOC data
directory and no .reloc section) was sufficient -- the byte-identical image otherwise then booted
successfully and printed "Hello from ArcoBASIC" to the real UEFI console before returning control to
firmware, which proceeded normally to its next boot option exactly as expected for an application
that returned EFI_SUCCESS.

This is the first point in the mission where the full pipeline (ArcoBASIC source -> lexer -> parser
-> A-MIR -> x86-64 machine code -> PE32+ image) was verified working end to end under real UEFI
firmware, not just structurally/statically correct.

FILES CHANGED
include/arco/pe_image.hpp (created)
compiler/pe_image.cpp (created)
CMakeLists.txt (registered the new source file and the new test)
compiler/fission.cpp (build_efi_image/build_efi_image_file)
include/arco/fission.hpp (declarations)
tools/arcofission_main.cpp (build FILE -o OUT.efi --target uefi-x86_64 [--entry NAME])
tests/runtime_tests.cpp (unit tests for write_pe32plus_efi_image against known-correct field
  offsets and a hand-verified relocation-patching case)
tests/systems_pe_image_smoke.sh (created)
docs/systems/pe32-image.md (created)
docs/systems/uefi-target.md (traceability section updated)
.agents/reports/WP-009-pe32-image.md (this report)

PUBLIC BEHAVIOR
New: `arcofission build FILE -o OUT.efi --target uefi-x86_64` produces a real, bootable UEFI
application. No existing `build`/`native`/`bytecode` behavior changed -- the new --target flag is
purely additive; omitting it preserves every prior behavior exactly (verified: argc handling was
relaxed from argc==5 to argc>=5, but the flag-parsing loop only changes behavior when --target or
--entry are actually present).

TESTS RUN
cmake --build build -j$(nproc)  -> clean, no warnings
./build/arco_tests (direct run)  -> exit 0, all require() assertions passed including the new PE
  writer unit tests (DOS/PE signatures, Machine, NumberOfSections, SizeOfOptionalHeader,
  Characteristics without RELOCS_STRIPPED, Magic, Subsystem, AddressOfEntryPoint, empty Import
  Directory, and correct relocation patching arithmetic)
ctest --test-dir build --output-on-failure -> 11/11 passed
  arco_runtime_tests (includes the new unit tests), arcosh_alpha_smoke, arcofission_alpha_smoke,
  systems_fixed_width_types_smoke, systems_freestanding_profile_smoke,
  systems_amir_primitives_smoke, systems_calling_convention_smoke, systems_uefi_bindings_smoke,
  systems_utf16_encoding_smoke, systems_x86_64_codegen_smoke: unchanged, still passing
  systems_pe_image_smoke: new, passing both its structural checks and (since qemu-system-x86_64 and
  an OVMF firmware image are present on this host) the real boot check -- confirmed "Hello from
  ArcoBASIC" appears in the captured serial console output
Manual verification beyond the automated suite:
  - Cross-checked the built image with `file`, `objdump -p`, and `pefile` (all three independently
    confirm the structure) before attempting any real boot.
  - Control test with a real-world EFI binary (grubx64.efi) through the identical harness to isolate
    the RELOCS_STRIPPED defect from a harness problem.
  - Repeated the successful boot multiple times to confirm it was not a fluke.

ACCEPTANCE CRITERIA (Packet "Required validation")
PE32+ format: PASS (pefile full parse, file/libmagic identification)
AMD64 machine type: PASS (Machine = 0x8664)
EFI application subsystem: PASS (Subsystem = 10)
Entry point: PASS (AddressOfEntryPoint at start of .text; confirmed by actual execution)
Section alignment: PASS (SectionAlignment == FileAlignment == 0x1000 throughout)
Image size: PASS (SizeOfImage matches actual file size, 0x3000 for the hello-world)
Absence of host runtime imports: PASS (Import Directory is {0,0})
OVMF loads the image without an invalid-image error: PASS -- and beyond the letter of this
  criterion, the image actually runs correctly and produces the exact expected output

ASSUMPTIONS
- ImageBase (0x140000000) and stack/heap reserve/commit sizes follow common Windows/EFI PE32+
  convention; the real boot test confirms these values do not prevent successful loading, but no
  documentation was found asserting these specific values are required (recorded as an assumption,
  not a verified requirement, in docs/systems/pe32-image.md).
- pip-installed `pefile` and the host's `objdump`/`file` were used only as verification aids during
  this work package, matching the "no new dependency" policy already established for nasm in
  WP-008 -- none of them are required to build or run ArcoBASIC/ArcoFission itself.

DEVIATIONS
None from the strategy recorded in docs/systems/uefi-target.md section 10 (self-contained PE32+
writer, no external linker). The IMAGE_FILE_RELOCS_STRIPPED finding is a correction of an
assumption made mid-implementation, not a deviation from any prior locked decision.

REGRESSIONS
None (11/11 tests passing, including all 10 pre-existing tests unchanged).

RISKS
- The RELOCS_STRIPPED finding is specific to this OVMF build's loader behavior; it was verified
  empirically (real boot, real control test) rather than derived from an authoritative statement in
  the UEFI/PE-COFF specification that this exact flag combination must be rejected. If a future
  work package targets different firmware, this should be re-verified rather than assumed universal
  -- though removing the flag is a conservative choice unlikely to cause new problems elsewhere.
  Documented prominently in docs/systems/pe32-image.md specifically so it is not silently
  rediscovered or reverted by a future change that looks "more standard" on paper.
- No .reloc section exists, so this writer cannot yet support a program whose code needs real
  absolute-address relocations. Not an issue for any A-MIR shape WP-008 currently generates.

NEXT SAFE WORK PACKAGE
WP-010: QEMU/OVMF Harness. No architectural decision blocks starting it -- this work package's ad
hoc boot check (systems_pe_image_smoke.sh's optional QEMU section) is a proof of concept for
exactly what WP-010 should formalize into a proper, reusable, always-on test harness with a
dedicated FAT image build step and structured pass/fail reporting.
```
