#include "arco/runtime.hpp"
#include "arco/shell.hpp"
#include "arco/calling_convention.hpp"
#include "arco/pe_image.hpp"
#include "arco/utf16.hpp"
#include "arco/x86_64_encoder.hpp"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
std::string run_shell_capture(const std::string& code) {
    arco::Runtime runtime;
    arco::shell::register_shell_builtins(runtime);
    std::ostringstream output;
    runtime.set_output(output);
    const auto result = runtime.run_string(code);
    require(result.ok, result.error);
    return output.str();
}

} // namespace

int main() {
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcology-v01a-runtime-test.arcodb";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        require(run_shell_capture(
            "#IMPORT \"arcology\"\n"
            "app = Arcology.Open(\"" + db_file.string() + "\")\n"
            "PRINT Arcology.Version()\n"
            "manifest = Arcology.Manifest()\n"
            "PRINT manifest.Name\n"
            "ada = Arcology.CreateUser(app, \"Ada\", \"Ada Lovelace\", \"Steward\", \"moderator\")\n"
            "grace = Arcology.CreateUser(app, \"Grace\", \"Grace Hopper\")\n"
            "photo = Arcology.CreateCommunity(app, \"Photography\", \"Photography\", \"Local photo walks\")\n"
            "PRINT ada.Ok\n"
            "PRINT grace.Value.Handle\n"
            "PRINT photo.Value.Slug\n"
            "joined = Arcology.JoinCommunity(app, \"Grace\", \"photography\")\n"
            "PRINT joined.Ok\n"
            "post = Arcology.Post(app, \"photography\", \"Grace\", \"Sunset walk\", \"Meet by the library\", [\"local\"])\n"
            "event = Arcology.Event(app, \"photography\", \"Open studio\", \"2026-08-12 18:00\", \"Community center\")\n"
            "feed = Arcology.FeedForUser(app, \"grace\")\n"
            "PRINT feed.Title\n"
            "PRINT feed.Explanation CONTAINS \"No hidden\"\n"
            "PRINT LEN(feed.Items)\n"
            "PRINT Object.Get(feed.Items[0], \"Reason\") CONTAINS \"photography\"\n"
            "report = Arcology.ReportContent(app, \"post:\" + STRING(post.Value.__id), \"Ada\", \"Spam\", \"test report\")\n"
            "PRINT report.Value.Status\n"
            "action = Arcology.Moderate(app, \"post:\" + STRING(post.Value.__id), \"spam\", \"remove_content\", \"transparent removal\", \"Ada\")\n"
            "PRINT action.Value.Action\n"
            "after = Arcology.CommunityFeed(app, \"photography\")\n"
            "PRINT LEN(after.Items)\n"
            "info = Arcology.Inspect(app)\n"
            "PRINT info.Users\n"
            "PRINT info.Communities\n"
            "PRINT info.Posts\n"
            "PRINT info.Reports\n"
            "PRINT Arcology.Save(app)\n"
            "reopened = Arcology.Open(\"" + db_file.string() + "\")\n"
            "loaded = Arcology.User(reopened, \"grace\")\n"
            "PRINT loaded.DisplayName\n") == "ARCOLOGY_0.1A\nThe Arcology Commons\nTRUE\ngrace\nphotography\nTRUE\nYour Commons\nTRUE\n2\nTRUE\nopen\nremove_content\n1\n2\n1\n1\n1\nTRUE\nGrace Hopper\n", "runs Arcology v0.1a community, feed, report, moderation, and persistence helpers");
        require(std::filesystem::exists(db_file), "writes Arcology ArcoDB file");
    }
    {
        const auto db_file = std::filesystem::temp_directory_path() / "arcology-static-runtime-test.arcodb";
        const auto site_dir = std::filesystem::temp_directory_path() / "arcology-static-runtime-test-site";
        std::filesystem::remove(db_file);
        std::filesystem::remove(db_file.string() + ".journal");
        std::filesystem::remove_all(site_dir);
        require(run_shell_capture(
            "#IMPORT \"arcology\"\n"
            "app = Arcology.Open(\"" + db_file.string() + "\")\n"
            "ignored = Arcology.CreateUser(app, \"Ada\", \"Ada Lovelace\")\n"
            "ignored = Arcology.CreateCommunity(app, \"Photography\", \"Photography\", \"Local <photos> & walks\")\n"
            "ignored = Arcology.JoinCommunity(app, \"Ada\", \"photography\")\n"
            "ignored = Arcology.Post(app, \"photography\", \"ada\", \"Sunset & shadows\", \"Meet <outside>\")\n"
            "result = Arcology.ExportSite(app, \"" + site_dir.string() + "\")\n"
            "PRINT result.Ok\n"
            "PRINT result.Files\n"
            "index = File.ReadText(Arcology.SitePath(result.Path, \"index.html\"))\n"
            "community = File.ReadText(Arcology.SitePath(result.Path, \"community-photography.html\"))\n"
            "style = File.ReadText(Arcology.SitePath(result.Path, \"style.css\"))\n"
            "PRINT index CONTAINS \"The Arcology Commons\"\n"
            "PRINT community CONTAINS \"Local &lt;photos&gt; &amp; walks\"\n"
            "PRINT community CONTAINS \"Sunset &amp; shadows\"\n"
            "PRINT style CONTAINS \"topbar\"\n") == "TRUE\n3\nTRUE\nTRUE\nTRUE\nTRUE\n", "exports Arcology v0.1a static HTML site");
        require(std::filesystem::exists(site_dir / "index.html"), "writes Arcology static index");
        std::filesystem::remove_all(site_dir);
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
            asm_.hlt();
            require(bytes_equal(asm_.bytes(), {0xF4}), "hlt matches nasm");
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
