#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace arco::fission::jit {

// A small, deliberately non-general x86-64 instruction encoder for ArcoFission's hot-numeric-loop
// JIT -- implements exactly the instruction shapes that codegen needs and nothing else, the same
// discipline arcology-os/include/arco/x86_64_encoder.hpp already established for its own
// (unrelated) kernel-bring-up needs. Deliberately a SEPARATE class rather than an extension of
// that one: that header is explicit about its own scope ("a small, deliberately non-general
// x86-64 instruction encoder... implements exactly the instruction shapes arcology-os's lowering
// needs and nothing else") and has zero floating-point support, which a numeric-loop JIT
// fundamentally requires (ArcoBASIC numbers are doubles throughout) -- growing that kernel-owned
// class to cover SSE2 for a completely different consumer would blur a boundary it draws on
// purpose. This file follows the same verification discipline instead: every encoding below is
// checked byte-for-byte against real `nasm -f bin` output in tests/unit/jit_x86_64_tests.cpp.
//
// Registers are restricted to the low 8 of each file (no R8-R15, no XMM8-XMM15) so no REX.R/X/B
// extension bits are ever needed -- ample for a straight-line numeric loop body, and it keeps
// every encoding below a fixed, simple shape.

enum class Gpr : std::uint8_t {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3, RSP = 4, RBP = 5, RSI = 6, RDI = 7,
};

// SysV AMD64 makes all XMM registers caller-saved, so a JIT-compiled loop (itself a leaf call from
// the interpreter's perspective) never needs to save/restore any of these across its own call
// boundary.
enum class Xmm : std::uint8_t {
    XMM0 = 0, XMM1 = 1, XMM2 = 2, XMM3 = 3, XMM4 = 4, XMM5 = 5, XMM6 = 6, XMM7 = 7,
};

inline std::uint8_t reg_bits(Gpr r) { return static_cast<std::uint8_t>(r); }
inline std::uint8_t reg_bits(Xmm r) { return static_cast<std::uint8_t>(r); }

// Standard Jcc condition codes (the 4-bit tttn field in the 0F 8x rel32 encoding). Named for the
// signed/unsigned-agnostic flag tests codegen actually issues after ucomisd -- see that
// instruction's own comment for why PF must be checked first.
enum class Condition : std::uint8_t {
    Equal = 0x4,        // ZF=1
    NotEqual = 0x5,      // ZF=0
    Below = 0x2,          // CF=1 (unsigned/ucomisd "less than")
    BelowOrEqual = 0x6,    // CF=1 or ZF=1
    Above = 0x7,           // CF=0 and ZF=0
    AboveOrEqual = 0x3,    // CF=0
    ParityEven = 0xA,      // PF=1 (ucomisd: operands unordered, i.e. either was NaN)
};

class JitAssembler {
public:
    // A forward or backward jump target. bind() records the current position; jmp()/jcc() either
    // resolve immediately (target already bound -- the common case for a loop's back-edge, always
    // emitted after the label it jumps to) or emit a placeholder and record a fixup, patched once
    // bind() is later called (a loop's forward exit jump, whose target isn't known until codegen
    // reaches the code right after the loop).
    struct Label {
        bool bound = false;
        std::size_t position = 0;
        std::vector<std::size_t> pending_fixups;
    };

    void bind(Label& label) {
        label.bound = true;
        label.position = code_.size();
        for (const auto fixup_offset : label.pending_fixups) {
            patch_rel32(fixup_offset, label.position);
        }
        label.pending_fixups.clear();
    }

    // --- General-purpose (64-bit) ---------------------------------------------------------

    // mov reg, imm64 -- REX.W B8+r io
    void mov_reg_imm64(Gpr dst, std::uint64_t imm64) {
        emit(0x48);
        emit(static_cast<std::uint8_t>(0xB8 + reg_bits(dst)));
        for (int i = 0; i < 8; ++i) emit(static_cast<std::uint8_t>((imm64 >> (8 * i)) & 0xFF));
    }

    // add reg, imm32 -- REX.W 81 /0 id
    void add_reg_imm32(Gpr dst, std::uint32_t imm32) {
        emit(0x48);
        emit(0x81);
        emit(static_cast<std::uint8_t>(0xC0 | reg_bits(dst)));
        emit_u32(imm32);
    }

    // cmp left, right -- REX.W 39 /r (register-direct)
    void cmp_reg_reg(Gpr left, Gpr right) {
        emit(0x48);
        emit(0x39);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_bits(right) << 3) | reg_bits(left)));
    }

    void jmp(Label& target) { emit_jump(0xE9, std::nullopt, target); }
    void jcc(Condition condition, Label& target) {
        emit_jump(static_cast<std::uint8_t>(0x80 + static_cast<std::uint8_t>(condition)), std::uint8_t{0x0F}, target);
    }

    void ret() { emit(0xC3); }

    // movq xmm, r64 -- 66 REX.W 0F 6E /r. Reinterprets a GPR's raw 64 bits as the destination XMM
    // register's low qword (the standard way to get an arbitrary double bit-pattern -- e.g. a
    // compile-time-constant literal loaded via mov_reg_imm64 -- into a form addsd/mulsd/etc. can
    // operate on, without needing a memory location for it at all).
    void movq_xmm_reg(Xmm dst, Gpr src) {
        emit(0x66);
        emit(0x48); // REX.W
        emit(0x0F);
        emit(0x6E);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_bits(dst) << 3) | reg_bits(src)));
    }

    // --- Scalar double-precision (SSE2) ---------------------------------------------------

    // movsd xmm, [base+disp32] -- F2 0F 10 /r
    void movsd_load(Xmm dst, Gpr base, std::int32_t disp32) { emit_sse_mem(0xF2, 0x10, reg_bits(dst), base, disp32); }
    // movsd [base+disp32], xmm -- F2 0F 11 /r
    void movsd_store(Gpr base, std::int32_t disp32, Xmm src) { emit_sse_mem(0xF2, 0x11, reg_bits(src), base, disp32); }
    // movsd xmm, xmm -- F2 0F 10 /r (register-direct)
    void movsd_reg_reg(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x10, reg_bits(dst), reg_bits(src)); }

    void addsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x58, reg_bits(dst), reg_bits(src)); }
    void subsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x5C, reg_bits(dst), reg_bits(src)); }
    void mulsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x59, reg_bits(dst), reg_bits(src)); }
    void divsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x5E, reg_bits(dst), reg_bits(src)); }

    // ucomisd left, right -- 66 0F 2E /r. Sets ZF/PF/CF per the unordered double comparison --
    // PF=1 means "unordered" (either operand was NaN), which codegen must check before trusting
    // ZF/CF, exactly matching the IEEE-754 comparison semantics the interpreter's own numeric
    // comparisons already rely on (a NaN compares false against everything, including itself).
    void ucomisd(Xmm left, Xmm right) { emit_sse_reg_reg(0x66, 0x2E, reg_bits(left), reg_bits(right)); }

    const std::vector<std::uint8_t>& bytes() const { return code_; }

private:
    void emit(std::uint8_t byte) { code_.push_back(byte); }
    void emit_u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) emit(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
    }

    // mod=10 (disp32 form). RSP as a base needs a SIB byte (its low 3 bits, 100, collide with the
    // ModRM.rm encoding that would otherwise mean "SIB follows"); RBP needs no such special case
    // here since that only applies to mod=00 (would-be RIP-relative), not mod=10 with an explicit
    // disp32.
    void emit_modrm_disp(std::uint8_t reg_field, Gpr base, std::int32_t disp32) {
        emit(static_cast<std::uint8_t>(0x80 | (reg_field << 3) | reg_bits(base)));
        if (reg_bits(base) == reg_bits(Gpr::RSP)) emit(0x24); // SIB: base=RSP, no index, scale=1
        emit_u32(static_cast<std::uint32_t>(disp32));
    }

    void emit_sse_mem(std::uint8_t mandatory_prefix, std::uint8_t opcode, std::uint8_t reg_field, Gpr base, std::int32_t disp32) {
        emit(mandatory_prefix);
        emit(0x0F);
        emit(opcode);
        emit_modrm_disp(reg_field, base, disp32);
    }

    void emit_sse_reg_reg(std::uint8_t mandatory_prefix, std::uint8_t opcode, std::uint8_t reg_field, std::uint8_t rm_field) {
        emit(mandatory_prefix);
        emit(0x0F);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_field << 3) | rm_field)); // mod=11, register-direct
    }

    void emit_jump(std::uint8_t opcode, std::optional<std::uint8_t> prefix, Label& target) {
        if (prefix) emit(*prefix);
        emit(opcode);
        const std::size_t rel32_offset = code_.size();
        emit_u32(0); // placeholder; patched immediately below if the target is already bound
        if (target.bound) {
            patch_rel32(rel32_offset, target.position);
        } else {
            target.pending_fixups.push_back(rel32_offset);
        }
    }

    void patch_rel32(std::size_t rel32_offset, std::size_t target_position) {
        const std::size_t instruction_end = rel32_offset + 4;
        const std::int64_t rel = static_cast<std::int64_t>(target_position) - static_cast<std::int64_t>(instruction_end);
        const auto rel32 = static_cast<std::uint32_t>(static_cast<std::int32_t>(rel));
        code_[rel32_offset + 0] = static_cast<std::uint8_t>(rel32 & 0xFF);
        code_[rel32_offset + 1] = static_cast<std::uint8_t>((rel32 >> 8) & 0xFF);
        code_[rel32_offset + 2] = static_cast<std::uint8_t>((rel32 >> 16) & 0xFF);
        code_[rel32_offset + 3] = static_cast<std::uint8_t>((rel32 >> 24) & 0xFF);
    }

    std::vector<std::uint8_t> code_;
};

} // namespace arco::fission::jit
