#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace arco::systems {

// A RIP-relative reference within `text` that needs patching once `text` and `rdata`'s final
// virtual addresses are known (produced by the x86-64 code generator, docs/systems/
// x86-64-codegen.md; consumed here to finish what that work package deliberately left open).
struct MachineCodeRelocation {
    std::size_t text_offset = 0;             // offset of the 4-byte disp32 field within .text
    std::size_t instruction_end_offset = 0;   // offset within .text right after that field
    std::size_t rdata_offset = 0;             // offset within .rdata the field should point to
};

struct MachineCodeImage {
    std::vector<std::uint8_t> text;
    std::vector<std::uint8_t> rdata;
    std::vector<MachineCodeRelocation> relocations;
    std::string entry_symbol;  // informational only; the entry point is always the start of .text
};

// Writes a minimal PE32+ EFI application image containing exactly the given code and data, with
// the entry point at the start of .text (docs/systems/pe32-image.md). Patches every relocation in
// `image.relocations` against the final section layout before returning.
std::vector<std::uint8_t> write_pe32plus_efi_image(const MachineCodeImage& image);

} // namespace arco::systems
