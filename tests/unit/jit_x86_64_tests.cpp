#include "arco/jit_x86_64.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace arco::fission::jit;

bool bytes_equal(const std::vector<std::uint8_t>& actual, std::initializer_list<int> expected) {
    if (actual.size() != expected.size()) return false;
    std::size_t i = 0;
    for (int value : expected) {
        if (actual[i++] != static_cast<std::uint8_t>(value)) return false;
    }
    return true;
}

// Every expected byte sequence below was independently verified with `nasm -f bin` (see
// tests/unit/jit_x86_64_tests.cpp's own history for the exact .asm source used, matching the
// verification discipline arcology-os/tests/unit/arcology_os_tests.cpp already established for
// its own x86-64 encoder). This test only checks that JitAssembler reproduces those same bytes,
// not that the bytes are correct x86-64 in the first place -- nasm is the ground truth for that.

void test_gpr_instructions() {
    {
        JitAssembler asm_;
        asm_.mov_reg_imm64(Gpr::RAX, 0x1122334455667788ULL);
        assert(bytes_equal(asm_.bytes(), {0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11}));
    }
    {
        JitAssembler asm_;
        asm_.add_reg_imm32(Gpr::RCX, 0x100);
        assert(bytes_equal(asm_.bytes(), {0x48, 0x81, 0xC1, 0x00, 0x01, 0x00, 0x00}));
    }
    {
        JitAssembler asm_;
        asm_.cmp_reg_reg(Gpr::RDX, Gpr::RBX);
        assert(bytes_equal(asm_.bytes(), {0x48, 0x39, 0xDA}));
    }
    {
        JitAssembler asm_;
        asm_.ret();
        assert(bytes_equal(asm_.bytes(), {0xC3}));
    }
}

void test_sse2_instructions() {
    {
        JitAssembler asm_;
        asm_.movsd_load(Xmm::XMM0, Gpr::RDI, 0x400);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x10, 0x87, 0x00, 0x04, 0x00, 0x00}));
    }
    {
        JitAssembler asm_;
        asm_.movsd_store(Gpr::RSI, 0x200, Xmm::XMM1);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x11, 0x8E, 0x00, 0x02, 0x00, 0x00}));
    }
    {
        JitAssembler asm_;
        asm_.movsd_reg_reg(Xmm::XMM2, Xmm::XMM3);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x10, 0xD3}));
    }
    {
        JitAssembler asm_;
        asm_.addsd(Xmm::XMM0, Xmm::XMM1);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x58, 0xC1}));
    }
    {
        JitAssembler asm_;
        asm_.subsd(Xmm::XMM0, Xmm::XMM1);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x5C, 0xC1}));
    }
    {
        JitAssembler asm_;
        asm_.mulsd(Xmm::XMM0, Xmm::XMM1);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x59, 0xC1}));
    }
    {
        JitAssembler asm_;
        asm_.divsd(Xmm::XMM0, Xmm::XMM1);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x5E, 0xC1}));
    }
    {
        JitAssembler asm_;
        asm_.ucomisd(Xmm::XMM4, Xmm::XMM5);
        assert(bytes_equal(asm_.bytes(), {0x66, 0x0F, 0x2E, 0xE5}));
    }
    {
        JitAssembler asm_;
        asm_.movq_xmm_reg(Xmm::XMM0, Gpr::RAX);
        assert(bytes_equal(asm_.bytes(), {0x66, 0x48, 0x0F, 0x6E, 0xC0}));
    }
    {
        JitAssembler asm_;
        asm_.movq_xmm_reg(Xmm::XMM3, Gpr::RCX);
        assert(bytes_equal(asm_.bytes(), {0x66, 0x48, 0x0F, 0x6E, 0xD9}));
    }
    // RSP as a memory-operand base needs an explicit SIB byte (0x24: base=RSP, no index); RBP
    // needs no such special case for the mod=10 (disp32) form used here.
    {
        JitAssembler asm_;
        asm_.movsd_load(Xmm::XMM6, Gpr::RSP, 0x300);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x10, 0xB4, 0x24, 0x00, 0x03, 0x00, 0x00}));
    }
    {
        JitAssembler asm_;
        asm_.movsd_load(Xmm::XMM7, Gpr::RBP, 0x380);
        assert(bytes_equal(asm_.bytes(), {0xF2, 0x0F, 0x10, 0xBD, 0x80, 0x03, 0x00, 0x00}));
    }
}

// Runs the exact instruction sequence nasm was given (see the .asm source referenced above) as one
// combined program, checking the assembler's own internal state (code offsets) stays correct
// across multiple instructions in sequence, not just each one tested alone.
void test_combined_sequence_matches_nasm() {
    JitAssembler asm_;
    asm_.mov_reg_imm64(Gpr::RAX, 0x1122334455667788ULL);
    asm_.add_reg_imm32(Gpr::RCX, 0x100);
    asm_.cmp_reg_reg(Gpr::RDX, Gpr::RBX);
    asm_.movsd_load(Xmm::XMM0, Gpr::RDI, 0x400);
    asm_.movsd_store(Gpr::RSI, 0x200, Xmm::XMM1);
    asm_.movsd_reg_reg(Xmm::XMM2, Xmm::XMM3);
    asm_.addsd(Xmm::XMM0, Xmm::XMM1);
    asm_.subsd(Xmm::XMM0, Xmm::XMM1);
    asm_.mulsd(Xmm::XMM0, Xmm::XMM1);
    asm_.divsd(Xmm::XMM0, Xmm::XMM1);
    asm_.ucomisd(Xmm::XMM4, Xmm::XMM5);
    asm_.ret();
    asm_.movsd_load(Xmm::XMM6, Gpr::RSP, 0x300);
    asm_.movsd_load(Xmm::XMM7, Gpr::RBP, 0x380);
    assert(bytes_equal(asm_.bytes(), {
        0x48, 0xB8, 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
        0x48, 0x81, 0xC1, 0x00, 0x01, 0x00, 0x00,
        0x48, 0x39, 0xDA,
        0xF2, 0x0F, 0x10, 0x87, 0x00, 0x04, 0x00, 0x00,
        0xF2, 0x0F, 0x11, 0x8E, 0x00, 0x02, 0x00, 0x00,
        0xF2, 0x0F, 0x10, 0xD3,
        0xF2, 0x0F, 0x58, 0xC1,
        0xF2, 0x0F, 0x5C, 0xC1,
        0xF2, 0x0F, 0x59, 0xC1,
        0xF2, 0x0F, 0x5E, 0xC1,
        0x66, 0x0F, 0x2E, 0xE5,
        0xC3,
        0xF2, 0x0F, 0x10, 0xB4, 0x24, 0x00, 0x03, 0x00, 0x00,
        0xF2, 0x0F, 0x10, 0xBD, 0x80, 0x03, 0x00, 0x00,
    }));
}

// Labels: a backward branch (target already bound, common shape of a loop back-edge) and a
// forward branch (target bound later, the shape of a loop's exit jump) must both patch a correct
// rel32 relative to the END of the jump instruction, not its start.
void test_label_backward_and_forward_jumps() {
    {
        // Backward: bind a label, emit a few bytes, then jmp back to it.
        JitAssembler asm_;
        JitAssembler::Label top;
        asm_.bind(top);                 // position 0
        asm_.ret();                     // 1 byte -> position 1
        asm_.jmp(top);                  // E9 + rel32, instruction is 5 bytes, ends at position 6
        // rel32 = target(0) - end_of_instruction(6) = -6
        const auto& bytes = asm_.bytes();
        assert(bytes.size() == 6);
        assert(bytes[0] == 0xC3);
        assert(bytes[1] == 0xE9);
        const std::int32_t rel = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(bytes[2]) | (static_cast<std::uint32_t>(bytes[3]) << 8) |
            (static_cast<std::uint32_t>(bytes[4]) << 16) | (static_cast<std::uint32_t>(bytes[5]) << 24));
        assert(rel == -6);
    }
    {
        // Forward: jcc to a label bound later (a loop's exit branch).
        JitAssembler asm_;
        JitAssembler::Label exit_label;
        asm_.jcc(Condition::Equal, exit_label); // 0F 84 + rel32 = 6 bytes, ends at position 6
        asm_.ret();                             // 1 byte -> position 7
        asm_.bind(exit_label);                  // position 7
        const auto& bytes = asm_.bytes();
        assert(bytes.size() == 7);
        assert(bytes[0] == 0x0F && bytes[1] == 0x84);
        const std::int32_t rel = static_cast<std::int32_t>(
            static_cast<std::uint32_t>(bytes[2]) | (static_cast<std::uint32_t>(bytes[3]) << 8) |
            (static_cast<std::uint32_t>(bytes[4]) << 16) | (static_cast<std::uint32_t>(bytes[5]) << 24));
        // rel32 = target(7) - end_of_instruction(6) = 1
        assert(rel == 1);
    }
}

} // namespace

int main() {
    test_gpr_instructions();
    test_sse2_instructions();
    test_combined_sequence_matches_nasm();
    test_label_backward_and_forward_jumps();
    std::cout << "JIT x86-64 encoder tests passed\n";
    return 0;
}
