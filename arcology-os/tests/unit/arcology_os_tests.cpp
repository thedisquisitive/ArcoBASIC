#include "arco/calling_convention.hpp"
#include "arco/graphics.hpp"
#include "arco/pe_image.hpp"
#include "arco/utf16.hpp"
#include "arco/uefi_bindings.hpp"
#include "arco/x86_64_encoder.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main() {
    using namespace arco::graphics;
    {
        const auto surface_result = CreateSurface(16, 12, PixelFormat::RedGreenBlueReserved8);
        require(static_cast<bool>(surface_result), "graphics surface allocation succeeds");
        auto surface = surface_result.surface;
        require(PackColor(Color::RGB(0x12, 0x34, 0x56), PixelFormat::RedGreenBlueReserved8).value() == 0x00563412,
                "RGB GOP packing is little-endian BGR byte order");
        require(PackColor(Color::RGB(0x12, 0x34, 0x56), PixelFormat::BlueGreenRedReserved8).value() == 0x00123456,
                "BGR GOP packing reverses red and blue channels");
        require(PutPixel(surface, 2, 3, Color::Magenta()), "in-bounds pixel write succeeds");
        require(!PutPixel(surface, -1, 0, Color::White()), "out-of-bounds pixel write is clipped");
        FillRect(surface, -4, -3, 10, 10, Color::White());
        require(surface.Pixels[0] == 255 && surface.Pixels[1] == 255 && surface.Pixels[2] == 255,
                "partially offscreen rectangle clips to the surface");
        DrawLine(surface, -5, 0, 20, 11, Color::RGB(1, 2, 3));
        DrawCircle(surface, 8, 6, 4, Color::RGB(4, 5, 6));
        FillCircle(surface, 8, 6, 2, Color::RGB(7, 8, 9));
        const auto metrics = MeasureText("ARCOLOGY\nOS");
        require(metrics.width == 48 && metrics.height == 16, "bitmap text metrics are deterministic");
        DrawText(surface, -3, -2, "ARCOLOGY", Color::White());
        auto other_result = CreateSurface(16, 12);
        require(static_cast<bool>(other_result), "second graphics surface allocation succeeds");
        auto other = other_result.surface;
        FillRect(other, 0, 0, 16, 12, Color::Black());
        require(Blit(surface, other, -2, -2), "clipped blit succeeds");
        require(Present(surface, other), "same-sized surfaces can be presented");
        require(Centered({0, 0, 100, 80}, 40, 20).x == 30, "centered layout is resolution-independent");
        require(Align({0, 0, 100, 80}, 20, 10, HorizontalAlign::Right, VerticalAlign::Bottom).x == 80,
                "edge alignment uses parent-relative bounds");
        Panel panel{{1, 1, 10, 8}}; Render(other, panel);
        Button button{{2, 2, 8, 6}, "OK"}; Render(other, button);
        ProgressBar progress{{0, 0, 10, 2}, 50}; Render(other, progress);
        Label label{{0, 0, 10, 8}, "UI"}; Render(other, label);
        Unbind();
        require(Bind(surface) && CurrentSurface() == &surface, "binding selects the active surface");
        FillRect(0, 0, 2, 2, Color::Magenta());
        require(PushSurface(other) && CurrentSurface() == &other, "nested binding redirects rendering");
        FillRect(0, 0, 2, 2, Color::White());
        require(PopSurface() && CurrentSurface() == &surface, "pop restores the previous surface");
        require(PopSurface() && CurrentSurface() == nullptr, "final pop clears the active binding");
    }
    {
        require(CreateSurface(0, 4).error == SurfaceError::InvalidDimensions, "zero-sized surface is rejected");
        require(CreateSurface(4, 4, static_cast<PixelFormat>(99)).error == SurfaceError::UnsupportedPixelFormat,
                "unsupported pixel format is rejected");
        require(FromFramebuffer(nullptr, 4, 4, 4, PixelFormat::RedGreenBlueReserved8).error == SurfaceError::NullPixels,
                "null framebuffer is rejected");
        std::uint8_t pixels[16]{};
        require(FromFramebuffer(pixels, 4, 4, 2, PixelFormat::RedGreenBlueReserved8).error == SurfaceError::InvalidStride,
                "invalid framebuffer stride is rejected");
    }
    {
        const auto gop = arco::systems::lookup_uefi_type("UEFI.GraphicsOutputMode");
        require(gop.has_value(), "minimal GOP mode binding is registered");
        const auto* base = gop->find_field("FrameBufferBase");
        require(base != nullptr && base->result_type == "PHYSICALPTR" && base->offset_bytes == 0x18,
                "GOP framebuffer base is a PHYSICALPTR at the verified mode offset");
        const auto boot = arco::systems::lookup_uefi_type("UEFI.BootServices");
        const auto* locate = boot->find_field("LocateProtocol");
        require(locate != nullptr && locate->is_method && locate->offset_bytes == 0x140,
                "BootServices.LocateProtocol is bound at the verified table offset");
        const auto* exit_boot = boot->find_field("ExitBootServices");
        require(exit_boot != nullptr && exit_boot->is_method && exit_boot->offset_bytes == 0xE8,
                "BootServices.ExitBootServices is bound at the verified table offset");
        const auto* memory_map = boot->find_field("GetMemoryMap");
        require(memory_map != nullptr && memory_map->is_method && memory_map->offset_bytes == 0x38,
                "BootServices.GetMemoryMap is bound at the verified table offset");
        const auto* allocate_pool = boot->find_field("AllocatePool");
        require(allocate_pool != nullptr && allocate_pool->is_method && allocate_pool->offset_bytes == 0x40,
                "BootServices.AllocatePool is bound at the verified table offset");
    }
    // Microsoft x64 calling convention (Packet WP-005, arcology-os/docs/systems/calling-conventions.md).
    require(arco::systems::assign_argument_locations(0).empty(), "zero arguments assigns no locations");
    {
        const auto one = arco::systems::assign_argument_locations(1);
        require(one.size() == 1 && one[0].in_register && one[0].register_name == "RCX",
                "first argument assigns to RCX");
    }
    {
        const auto two = arco::systems::assign_argument_locations(2);
        require(two.size() == 2 && two[0].register_name == "RCX" && two[1].register_name == "RDX",
                "two arguments assign to RCX, RDX in order");
    }
    {
        const auto four = arco::systems::assign_argument_locations(4);
        require(four.size() == 4 && four[0].register_name == "RCX" && four[1].register_name == "RDX" &&
                    four[2].register_name == "R8" && four[3].register_name == "R9",
                "four arguments assign to RCX, RDX, R8, R9");
    }
    {
        const auto five = arco::systems::assign_argument_locations(5);
        require(five.size() == 5 && !five[4].in_register && five[4].stack_offset_bytes == 40,
                "fifth argument spills to the stack at shadow-space-plus-return-address offset 40");
    }
    {
        const auto six = arco::systems::assign_argument_locations(6);
        require(!six[5].in_register && six[5].stack_offset_bytes == 48,
                "sixth argument follows the fifth at offset 48");
    }
    require(arco::systems::integer_return_register() == "RAX", "integer/pointer return register is RAX");
    require(arco::systems::kShadowSpaceBytes == 32, "shadow space is always 32 bytes");
    require(arco::systems::kStackAlignmentAtCallBytes == 16, "stack must be 16-byte aligned at CALL");
    {
        const auto& callee_saved = arco::systems::callee_saved_registers();
        const auto& caller_saved = arco::systems::caller_saved_registers();
        bool disjoint = true;
        for (const auto& reg : callee_saved) {
            for (const auto& other : caller_saved) {
                if (reg == other) {
                    disjoint = false;
                }
            }
        }
        require(disjoint, "callee-saved and caller-saved register sets do not overlap");
    }

    // UTF-16 constant encoding (Packet WP-007, arcology-os/docs/systems/utf16-encoding.md).
    {
        const auto units = arco::systems::encode_utf16_null_terminated("Hello from ArcoBASIC");
        const std::u16string expected = u"Hello from ArcoBASIC";
        require(units.size() == expected.size() + 1, "hello string encodes to 21 code units plus a null terminator");
        bool matches = true;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            if (units[i] != expected[i]) {
                matches = false;
            }
        }
        require(matches, "hello string's UTF-16 code units match their ASCII values exactly");
        require(units.back() == u'\0', "hello string's UTF-16 encoding ends with a null terminator");
    }
    {
        const auto empty_units = arco::systems::encode_utf16_null_terminated("");
        require(empty_units.size() == 1 && empty_units[0] == u'\0', "empty string encodes to just a null terminator");
    }
    {
        // U+00E9 (LATIN SMALL LETTER E WITH ACUTE), UTF-8: 0xC3 0xA9 -- a single UTF-16 code unit.
        const auto units = arco::systems::encode_utf16_null_terminated("\xC3\xA9");
        require(units.size() == 2 && units[0] == 0x00E9 && units[1] == u'\0',
                "a BMP character outside ASCII encodes to a single UTF-16 code unit");
    }
    {
        // U+1F600 (GRINNING FACE), UTF-8: 0xF0 0x9F 0x98 0x80 -- a UTF-16 surrogate pair.
        const auto units = arco::systems::encode_utf16_null_terminated("\xF0\x9F\x98\x80");
        require(units.size() == 3 && units[0] == 0xD83D && units[1] == 0xDE00 && units[2] == u'\0',
                "a character outside the BMP encodes to a correct UTF-16 surrogate pair");
    }
    const auto encoding_rejects = [](const std::string& text) {
        try {
            (void)arco::systems::encode_utf16_null_terminated(text);
            return false;
        } catch (const std::exception&) {
            return true;
        }
    };
    require(encoding_rejects(std::string("bad\0null", 8)), "embedded NUL byte is rejected");
    require(encoding_rejects("\xFF"), "an invalid UTF-8 lead byte is rejected");
    require(encoding_rejects("\xC3"), "a truncated UTF-8 sequence is rejected");
    require(encoding_rejects("\xC3\x28"), "an invalid UTF-8 continuation byte is rejected");
    require(encoding_rejects("\xED\xA0\x80"), "a UTF-8 encoding of a surrogate code point is rejected");

    // x86-64 instruction encoder (Packet WP-008, arcology-os/docs/systems/x86-64-codegen.md). Every expected
    // byte sequence below was independently verified with `nasm -f bin` (see the completion
    // report for the exact .asm source used); this test only checks that the C++ encoder
    // reproduces those same bytes, not that the bytes are correct x86-64 in the first place.
    {
        using namespace arco::systems::x86_64;
        const auto bytes_equal = [](const std::vector<std::uint8_t>& actual, std::initializer_list<int> expected) {
            if (actual.size() != expected.size()) {
                return false;
            }
            std::size_t i = 0;
            for (int value : expected) {
                if (actual[i++] != static_cast<std::uint8_t>(value)) {
                    return false;
                }
            }
            return true;
        };

        {
            Assembler asm_;
            asm_.sub_rsp_imm8(0x38);
            require(bytes_equal(asm_.bytes(), {0x48, 0x83, 0xEC, 0x38}), "sub rsp, imm8 matches nasm");
        }
        {
            Assembler asm_;
            asm_.add_rsp_imm8(0x38);
            require(bytes_equal(asm_.bytes(), {0x48, 0x83, 0xC4, 0x38}), "add rsp, imm8 matches nasm");
        }
        {
            Assembler asm_;
            asm_.mov_store_disp8(Reg::RSP, 0x28, Reg::RCX);
            require(bytes_equal(asm_.bytes(), {0x48, 0x89, 0x4C, 0x24, 0x28}), "mov [rsp+disp8], rcx matches nasm");
        }
        {
            Assembler asm_;
            asm_.mov_store_disp8(Reg::RSP, 0x20, Reg::RDX);
            require(bytes_equal(asm_.bytes(), {0x48, 0x89, 0x54, 0x24, 0x20}), "mov [rsp+disp8], rdx matches nasm");
        }
        {
            Assembler asm_;
            asm_.mov_load_disp8(Reg::RAX, Reg::RSP, 0x20);
            require(bytes_equal(asm_.bytes(), {0x48, 0x8B, 0x44, 0x24, 0x20}), "mov rax, [rsp+disp8] matches nasm");
        }
        {
            Assembler asm_;
            asm_.mov_load_disp8(Reg::RAX, Reg::RAX, 0x40);
            require(bytes_equal(asm_.bytes(), {0x48, 0x8B, 0x40, 0x40}), "mov rax, [rax+disp8] (non-RSP base, no SIB) matches nasm");
        }
        {
            Assembler asm_;
            asm_.mov_load32_disp8(Reg::RAX, Reg::RAX, 0x20);
            require(bytes_equal(asm_.bytes(), {0x8B, 0x40, 0x20}),
                    "mov eax, [rax+disp8] performs an exact-width U32 load and zero-extends");
        }
        {
            Assembler asm_;
            asm_.mov_reg32_reg32(Reg::RAX, Reg::RAX);
            require(bytes_equal(asm_.bytes(), {0x89, 0xC0}),
                    "mov eax, eax zero-extends U32 values without the imm32 sign-extension trap");
        }
        {
            Assembler asm_;
            asm_.mov_rax_cr3();
            require(bytes_equal(asm_.bytes(), {0x0F, 0x20, 0xD8}), "mov rax, cr3 matches x86-64 encoding");
            Assembler write;
            write.mov_cr3_rax();
            require(bytes_equal(write.bytes(), {0x0F, 0x22, 0xD8}), "mov cr3, rax matches x86-64 encoding");
            Assembler invalidate;
            invalidate.invlpg_rax();
            require(bytes_equal(invalidate.bytes(), {0x0F, 0x01, 0x38}), "invlpg [rax] matches x86-64 encoding");
        }
        {
            Assembler asm_;
            asm_.mov_rax_cr2();
            require(bytes_equal(asm_.bytes(), {0x0F, 0x20, 0xD0}), "mov rax, cr2 matches x86-64 encoding");
        }
        {
            Assembler asm_;
            asm_.mov_rax_cs();
            require(bytes_equal(asm_.bytes(), {0x48, 0x8C, 0xC8}), "mov rax, cs matches x86-64 encoding");
        }
        {
            Assembler lgdt;
            lgdt.lgdt_rax();
            require(bytes_equal(lgdt.bytes(), {0x0F, 0x01, 0x10}), "lgdt [rax] matches x86-64 encoding");
            Assembler lidt;
            lidt.lidt_rax();
            require(bytes_equal(lidt.bytes(), {0x0F, 0x01, 0x18}), "lidt [rax] matches x86-64 encoding");
            Assembler ltr;
            ltr.ltr_rax();
            require(bytes_equal(ltr.bytes(), {0x66, 0x0F, 0x00, 0xD8}), "ltr ax matches x86-64 encoding");
        }
        {
            Assembler asm_;
            asm_.int3();
            require(bytes_equal(asm_.bytes(), {0xCC}), "INT3 breakpoint matches x86-64 encoding");
        }
        {
            Assembler asm_;
            asm_.mov_reg_reg(Reg::RCX, Reg::RAX);
            require(bytes_equal(asm_.bytes(), {0x48, 0x89, 0xC1}), "mov rcx, rax matches nasm");
        }
        {
            Assembler asm_;
            const std::size_t disp_offset = asm_.lea_rip_relative(Reg::RDX);
            require(disp_offset == 3, "lea rip-relative disp32 field starts right after the 3-byte prefix/opcode/modrm");
            require(bytes_equal(asm_.bytes(), {0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00}),
                    "lea rdx, [rip+disp32] prefix/opcode/modrm matches nasm (disp32 left as an unpatched placeholder)");
            asm_.patch_u32(disp_offset, 0x0A);
            require(bytes_equal(asm_.bytes(), {0x48, 0x8D, 0x15, 0x0A, 0x00, 0x00, 0x00}), "patch_u32 writes little-endian");
        }
        {
            Assembler asm_;
            asm_.call_indirect_disp8(Reg::RAX, 0x08);
            require(bytes_equal(asm_.bytes(), {0xFF, 0x50, 0x08}), "call qword [rax+disp8] matches nasm");
        }
        {
            Assembler asm_;
            asm_.call_indirect_disp32(Reg::RAX, 0x100);
            require(bytes_equal(asm_.bytes(), {0xFF, 0x90, 0x00, 0x01, 0x00, 0x00}),
                    "call qword [rax+disp32] matches nasm");
        }
        {
            Assembler asm_;
            const std::size_t disp_offset = asm_.call_rel32_placeholder();
            require(disp_offset == 1 && bytes_equal(asm_.bytes(), {0xE8, 0x00, 0x00, 0x00, 0x00}),
                    "call rel32 emits a patchable near-call placeholder");
            asm_.patch_i32(disp_offset, 1);
            require(bytes_equal(asm_.bytes(), {0xE8, 0x01, 0x00, 0x00, 0x00}),
                    "call rel32 displacement is patched little-endian");
        }
        {
            Assembler asm_;
            asm_.hlt();
            require(bytes_equal(asm_.bytes(), {0xF4}), "hlt matches nasm");
        }
        {
            Assembler asm_;
            asm_.cmp_rax_imm8(0);
            require(bytes_equal(asm_.bytes(), {0x48, 0x83, 0xF8, 0x00}), "cmp rax, 0 matches nasm");
        }
        {
            Assembler asm_;
            asm_.cli();
            asm_.hlt();
            asm_.jmp_rel8(-3);
            require(bytes_equal(asm_.bytes(), {0xFA, 0xF4, 0xEB, 0xFD}),
                    "CPU.HaltForever sequence matches nasm");
        }
        {
            Assembler asm_;
            asm_.in_al_dx();
            asm_.in_ax_dx();
            asm_.in_eax_dx();
            asm_.out_dx_al();
            asm_.out_dx_ax();
            asm_.out_dx_eax();
            asm_.pause();
            require(bytes_equal(asm_.bytes(), {0xEC, 0x66, 0xED, 0xED, 0xEE, 0x66, 0xEF, 0xEF, 0xF3, 0x90}),
                    "IN/OUT DX forms and PAUSE match x86-64 encodings");
        }
        {
            Assembler asm_;
            asm_.lfence();
            asm_.sfence();
            asm_.mfence();
            require(bytes_equal(asm_.bytes(), {0x0F, 0xAE, 0xE8, 0x0F, 0xAE, 0xF8, 0x0F, 0xAE, 0xF0}),
                    "LFENCE/SFENCE/MFENCE match x86-64 encodings");
        }
        {
            Assembler asm_;
            asm_.mov_load8_rax();
            asm_.mov_load16_rax();
            asm_.mov_load32_rax();
            asm_.mov_load64_rax();
            asm_.mov_store8_rax_from_cl();
            asm_.mov_store16_rax_from_cx();
            asm_.mov_store32_rax_from_ecx();
            asm_.mov_store64_rax_from_rcx();
            require(bytes_equal(asm_.bytes(), {0x0F, 0xB6, 0x00, 0x66, 0x0F, 0xB7, 0x00, 0x8B, 0x00,
                                                0x48, 0x8B, 0x00, 0x88, 0x08, 0x66, 0x89, 0x08, 0x89, 0x08,
                                                0x48, 0x89, 0x08}),
                    "volatile memory load/store forms match x86-64 encodings");
        }
        {
            Assembler asm_;
            asm_.mov_reg_imm64(Reg::RAX, 0);
            require(bytes_equal(asm_.bytes(), {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0}),
                    "mov rax, imm64(0) uses the full 10-byte form, matching nasm's `strict qword` output "
                    "(no attempt to use the shorter reg32,imm32 form -- Packet WP-008 non-goal: optimization)");
        }
        {
            Assembler asm_;
            asm_.mov_reg_imm64(Reg::RAX, 0x123456789ABCDEF0ULL);
            require(bytes_equal(asm_.bytes(), {0x48, 0xB8, 0xF0, 0xDE, 0xBC, 0x9A, 0x78, 0x56, 0x34, 0x12}),
                    "mov rax, imm64(nonzero) matches nasm's `strict qword` output, byte order included");
        }
        {
            Assembler asm_;
            asm_.ret();
            require(bytes_equal(asm_.bytes(), {0xC3}), "ret matches nasm");
        }
        // Common exception-entry stub primitives (arcology-os/.agents/reports/aps-owned-idt.md's
        // "remaining entry-ABI gate": normalize error-code frames, preserve registers, IRETQ).
        {
            Assembler asm_;
            asm_.push_reg(Reg::RAX);
            asm_.push_reg(Reg::RBP);
            asm_.push_reg(Reg::R8);
            asm_.push_reg(Reg::R15);
            require(bytes_equal(asm_.bytes(), {0x50, 0x55, 0x41, 0x50, 0x41, 0x57}),
                    "push r64 matches nasm for both legacy and REX.B-extended registers");
        }
        {
            Assembler asm_;
            asm_.pop_reg(Reg::R15);
            asm_.pop_reg(Reg::R8);
            asm_.pop_reg(Reg::RBP);
            asm_.pop_reg(Reg::RAX);
            require(bytes_equal(asm_.bytes(), {0x41, 0x5F, 0x41, 0x58, 0x5D, 0x58}),
                    "pop r64 matches nasm for both legacy and REX.B-extended registers");
        }
        {
            Assembler asm_;
            asm_.push_imm8(0);
            asm_.push_imm8(31);
            require(bytes_equal(asm_.bytes(), {0x6A, 0x00, 0x6A, 0x1F}), "push imm8 matches nasm");
        }
        {
            Assembler asm_;
            asm_.iretq();
            require(bytes_equal(asm_.bytes(), {0x48, 0xCF}), "iretq matches nasm");
        }
        {
            Assembler asm_;
            asm_.inc_rax();
            require(bytes_equal(asm_.bytes(), {0x48, 0xFF, 0xC0}), "inc rax matches nasm");
        }
        {
            Assembler asm_;
            asm_.nop();
            require(bytes_equal(asm_.bytes(), {0x90}), "nop matches nasm");
        }
        {
            Assembler asm_;
            asm_.retfq();
            require(bytes_equal(asm_.bytes(), {0x48, 0xCB}), "retfq matches nasm");
        }
        {
            Assembler asm_;
            asm_.mov_ss_rax();
            require(bytes_equal(asm_.bytes(), {0x8E, 0xD0}), "mov ss, ax matches nasm");
        }
    }

    // PE32+ image writer (Packet WP-009, arcology-os/docs/systems/pe32-image.md). Field offsets below were
    // verified against Microsoft's PE/COFF format reference and cross-checked with `pefile` and
    // `objdump -p` on real output before being hardcoded here.
    {
        using arco::systems::MachineCodeImage;
        using arco::systems::MachineCodeRelocation;

        MachineCodeImage image;
        image.text = {0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00};  // a 7-byte placeholder LEA
        image.rdata = {0xAA, 0xBB, 0xCC, 0xDD};
        image.relocations.push_back(MachineCodeRelocation{3, 7, 0});
        image.entry_symbol = "efi_main";

        const auto pe = arco::systems::write_pe32plus_efi_image(image);

        const auto u16_at = [&](std::size_t offset) {
            return static_cast<unsigned>(pe[offset]) | (static_cast<unsigned>(pe[offset + 1]) << 8);
        };
        const auto u32_at = [&](std::size_t offset) {
            return static_cast<std::uint32_t>(pe[offset]) | (static_cast<std::uint32_t>(pe[offset + 1]) << 8) |
                   (static_cast<std::uint32_t>(pe[offset + 2]) << 16) | (static_cast<std::uint32_t>(pe[offset + 3]) << 24);
        };

        require(pe[0] == 'M' && pe[1] == 'Z', "PE image starts with the MZ DOS signature");
        require(u32_at(0x3C) == 0x40, "e_lfanew points to offset 0x40, right after the 64-byte DOS header");
        require(pe[0x40] == 'P' && pe[0x41] == 'E' && pe[0x42] == 0 && pe[0x43] == 0, "PE signature at e_lfanew");
        require(u16_at(0x44) == 0x8664, "Machine is IMAGE_FILE_MACHINE_AMD64");
        require(u16_at(0x46) == 2, "NumberOfSections is 2 (.text, .rdata)");
        require(u16_at(0x54) == 0xF0, "SizeOfOptionalHeader is 240 bytes (standard + windows-specific + 16 data directories)");
        require(u16_at(0x56) == 0x0022,
                "Characteristics is EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE only -- IMAGE_FILE_RELOCS_STRIPPED "
                "must NOT be set (arcology-os/docs/systems/pe32-image.md: OVMF refuses to load an image with this flag set "
                "and no .reloc section, discovered by booting a real image under QEMU/OVMF)");
        require(u16_at(0x58) == 0x020B, "Optional header Magic is PE32+");
        require(u16_at(0x9C) == 10, "Subsystem is IMAGE_SUBSYSTEM_EFI_APPLICATION");
        require(u32_at(0x68) == 0x1000, "AddressOfEntryPoint is the start of .text, right after one page of headers");
        require(u32_at(0xD0) == 0 && u32_at(0xD4) == 0,
                "Import Directory (DataDirectory[1]) is zero -- Packet WP-009 \"absence of host runtime imports\"");

        // The relocation must be patched: displacement = rdata_rva - (text_rva + instruction_end_offset).
        // text_rva = 0x1000 (one page of headers); rdata_rva = 0x1000 + round_up(7, 0x1000) = 0x2000.
        const std::size_t patched_field_offset = 0x1000 + 3;  // text_file_offset (== text_rva) + text_offset
        const std::uint32_t expected_displacement = 0x2000 - (0x1000 + 7);
        require(u32_at(patched_field_offset) == expected_displacement,
                "RIP-relative relocation is patched to the correct displacement between .text and .rdata");

        require(pe.size() == 0x3000, "image size is headers(1 page) + text(1 page) + rdata(1 page) with 4096-byte alignment");
    }
    return 0;
}
