#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace arco::systems::x86_64 {

// A small, deliberately non-general x86-64 instruction encoder (Packet WP-008 non-goal:
// "general-purpose instruction selection"). It implements exactly the instruction shapes
// arcology-os/docs/systems/x86-64-codegen.md's lowering needs and nothing else. Every primitive here is
// verified byte-for-byte against `nasm -f bin` output (see tests/unit/runtime_tests.cpp) rather than
// trusted from manual bit-twiddling alone.

enum class Reg : std::uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11, R12 = 12, R13 = 13, R14 = 14, R15 = 15,
};

inline std::uint8_t reg_index(Reg r) { return static_cast<std::uint8_t>(r); }
inline std::uint8_t reg_low3(Reg r) { return reg_index(r) & 0x7; }
inline bool reg_needs_rex_extension(Reg r) { return reg_index(r) >= 8; }

class Assembler {
public:
    // sub rsp, imm8 -- REX.W 83 /5 ib
    void sub_rsp_imm8(std::uint8_t imm8) {
        emit(0x48);
        emit(0x83);
        emit(0xEC);
        emit(imm8);
    }

    // add rsp, imm8 -- REX.W 83 /0 ib
    void add_rsp_imm8(std::uint8_t imm8) {
        emit(0x48);
        emit(0x83);
        emit(0xC4);
        emit(imm8);
    }

    // mov [base+disp8], src -- REX.W 89 /r, disp8 (SIB byte added automatically when base is
    // RSP or R12, whose low 3 bits collide with the "SIB follows" ModRM.rm encoding)
    void mov_store_disp8(Reg base, std::uint8_t disp8, Reg src) {
        emit_modrm_disp8(0x89, src, base, disp8);
    }

    // mov dst, [base+disp8] -- REX.W 8B /r, disp8
    void mov_load_disp8(Reg dst, Reg base, std::uint8_t disp8) {
        emit_modrm_disp8(0x8B, dst, base, disp8);
    }

    // mov dst, src (register to register) -- REX.W 89 /r, register-direct
    void mov_reg_reg(Reg dst, Reg src) {
        emit_rex(src, dst);
        emit(0x89);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_low3(src) << 3) | reg_low3(dst)));
    }

    // mov dst, imm64 -- REX.W B8+rd io (always the full 10-byte form; Packet WP-008 non-goal
    // "optimization" -- no attempt is made to use the shorter 5-byte reg32,imm32 form real
    // assemblers pick for small values)
    void mov_reg_imm64(Reg dst, std::uint64_t imm64) {
        emit(static_cast<std::uint8_t>(0x48 | (reg_needs_rex_extension(dst) ? 0x01 : 0)));
        emit(static_cast<std::uint8_t>(0xB8 + reg_low3(dst)));
        for (int i = 0; i < 8; ++i) {
            emit(static_cast<std::uint8_t>((imm64 >> (8 * i)) & 0xFF));
        }
    }

    // lea dst, [rip+disp32]. Returns the buffer offset of the 4-byte disp32 field; the caller
    // must patch it (patch_u32) once both the target address and this instruction's own end
    // address are known, since RIP-relative displacement is measured from the address of the
    // *next* instruction.
    std::size_t lea_rip_relative(Reg dst) {
        emit(static_cast<std::uint8_t>(0x48 | (reg_needs_rex_extension(dst) ? 0x04 : 0)));
        emit(0x8D);
        emit(static_cast<std::uint8_t>(0x05 | (reg_low3(dst) << 3)));  // mod=00 rm=101: RIP-relative
        const std::size_t disp_offset = code_.size();
        emit(0);
        emit(0);
        emit(0);
        emit(0);
        return disp_offset;
    }

    // call qword [base+disp8] -- FF /2, disp8
    void call_indirect_disp8(Reg base, std::uint8_t disp8) {
        if (reg_needs_rex_extension(base)) {
            emit(0x41);  // REX.B only
        }
        emit(0xFF);
        emit(static_cast<std::uint8_t>(0x50 | reg_low3(base)));  // mod=01 reg=010(/2)
        if (reg_low3(base) == 0x4) {                             // RSP or R12 needs a SIB byte
            emit(0x24);
        }
        emit(disp8);
    }

    // call qword [base+disp32] -- FF /2, disp32
    void call_indirect_disp32(Reg base, std::uint32_t disp32) {
        if (reg_needs_rex_extension(base)) {
            emit(0x41);  // REX.B only
        }
        emit(0xFF);
        emit(static_cast<std::uint8_t>(0x90 | reg_low3(base)));  // mod=10 reg=010(/2)
        if (reg_low3(base) == 0x4) {                             // RSP or R12 needs a SIB byte
            emit(0x24);
        }
        emit(static_cast<std::uint8_t>(disp32 & 0xFF));
        emit(static_cast<std::uint8_t>((disp32 >> 8) & 0xFF));
        emit(static_cast<std::uint8_t>((disp32 >> 16) & 0xFF));
        emit(static_cast<std::uint8_t>((disp32 >> 24) & 0xFF));
    }

    void cli() { emit(0xFA); }
    void hlt() { emit(0xF4); }

    // The displacement is relative to the instruction following this two-byte jump.
    void jmp_rel8(std::int8_t displacement) {
        emit(0xEB);
        emit(static_cast<std::uint8_t>(displacement));
    }

    void ret() { emit(0xC3); }

    std::size_t size() const { return code_.size(); }
    const std::vector<std::uint8_t>& bytes() const { return code_; }

    void patch_u32(std::size_t offset, std::uint32_t value) {
        code_[offset] = static_cast<std::uint8_t>(value & 0xFF);
        code_[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        code_[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        code_[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    }

private:
    void emit(std::uint8_t byte) { code_.push_back(byte); }

    void emit_rex(Reg reg_field, Reg rm_field) {
        emit(static_cast<std::uint8_t>(0x48 | (reg_needs_rex_extension(reg_field) ? 0x04 : 0) |
                                        (reg_needs_rex_extension(rm_field) ? 0x01 : 0)));
    }

    void emit_modrm_disp8(std::uint8_t opcode, Reg reg_field, Reg base, std::uint8_t disp8) {
        emit_rex(reg_field, base);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0x40 | (reg_low3(reg_field) << 3) | reg_low3(base)));  // mod=01
        if (reg_low3(base) == 0x4) {  // RSP or R12 needs a SIB byte
            emit(0x24);
        }
        emit(disp8);
    }

    std::vector<std::uint8_t> code_;
};

} // namespace arco::systems::x86_64
