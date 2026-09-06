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

// Scalar-double (SSE2) support, low 8 XMM registers only -- added for the Linux/System V hosted
// backend (ArcoBASIC numbers are always doubles; unlike the freestanding profile this file was
// originally scoped to, which represents values as fixed-width integers because it has nothing
// else to lower "AS U64"-style hardware-facing declarations to). XMM0-7 need no REX.R/X/B
// extension bit, so every encoding below is a fixed, simple shape -- byte-for-byte the same as
// include/arco/jit_x86_64.hpp's already nasm-cross-verified encoder (this session's earlier,
// narrower loop-JIT work), ported here rather than duplicated as a third copy since this file's
// Assembler is the one generate_x86_64_function actually already threads through for both targets.
enum class Xmm : std::uint8_t {
    XMM0 = 0, XMM1 = 1, XMM2 = 2, XMM3 = 3, XMM4 = 4, XMM5 = 5, XMM6 = 6, XMM7 = 7,
};
inline std::uint8_t xmm_low3(Xmm r) { return static_cast<std::uint8_t>(r) & 0x7; }

class Assembler {
public:
    // sub rsp, imm8 -- REX.W 83 /5 ib
    void sub_rsp_imm8(std::uint8_t imm8) {
        emit(0x48);
        emit(0x83);
        emit(0xEC);
        emit(imm8);
    }

    void sub_rsp_imm32(std::uint32_t imm32) { emit(0x48); emit(0x81); emit(0xEC); emit_u32(imm32); }

    // add rsp, imm8 -- REX.W 83 /0 ib
    void add_rsp_imm8(std::uint8_t imm8) {
        emit(0x48);
        emit(0x83);
        emit(0xC4);
        emit(imm8);
    }

    void add_rsp_imm32(std::uint32_t imm32) { emit(0x48); emit(0x81); emit(0xC4); emit_u32(imm32); }

    // mov [base+disp8], src -- REX.W 89 /r, disp8 (SIB byte added automatically when base is
    // RSP or R12, whose low 3 bits collide with the "SIB follows" ModRM.rm encoding)
    void mov_store_disp8(Reg base, std::uint8_t disp8, Reg src) {
        emit_modrm_disp8(0x89, src, base, disp8);
    }

    // mov dst, [base+disp8] -- REX.W 8B /r, disp8
    void mov_load_disp8(Reg dst, Reg base, std::uint8_t disp8) {
        emit_modrm_disp8(0x8B, dst, base, disp8);
    }

    // mov r32, [base+disp] -- 8B /r. A 32-bit destination write zero-extends into the
    // corresponding 64-bit register, which is exactly what unsigned U32 loads require.
    void mov_load32_disp8(Reg dst, Reg base, std::uint8_t disp8) {
        emit_modrm32_disp8(0x8B, dst, base, disp8);
    }

    void mov_store_disp32(Reg base, std::uint32_t disp32, Reg src) { emit_modrm_disp32(0x89, src, base, disp32); }
    void mov_load_disp32(Reg dst, Reg base, std::uint32_t disp32) { emit_modrm_disp32(0x8B, dst, base, disp32); }
    void mov_load32_disp32(Reg dst, Reg base, std::uint32_t disp32) { emit_modrm32_disp32(0x8B, dst, base, disp32); }

    void lea_rsp_disp8(Reg dst, std::uint8_t disp8) { emit_modrm_disp8(0x8D, dst, Reg::RSP, disp8); }
    void lea_rsp_disp32(Reg dst, std::uint32_t disp32) { emit_modrm_disp32(0x8D, dst, Reg::RSP, disp32); }

    // mov dst, src (register to register) -- REX.W 89 /r, register-direct
    void mov_reg_reg(Reg dst, Reg src) {
        emit_rex(src, dst);
        emit(0x89);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_low3(src) << 3) | reg_low3(dst)));
    }

    // mov r32, r32. Any 32-bit register write clears the upper 32 bits of the destination.
    void mov_reg32_reg32(Reg dst, Reg src) {
        emit_rex32(src, dst);
        emit(0x89);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_low3(src) << 3) | reg_low3(dst)));
    }

    void add_reg_reg(Reg dst, Reg src) { emit_binary_reg(0x01, dst, src); }
    void sub_reg_reg(Reg dst, Reg src) { emit_binary_reg(0x29, dst, src); }
    void and_reg_reg(Reg dst, Reg src) { emit_binary_reg(0x21, dst, src); }
    void or_reg_reg(Reg dst, Reg src) { emit_binary_reg(0x09, dst, src); }
    void xor_reg_reg(Reg dst, Reg src) { emit_binary_reg(0x31, dst, src); }

    // Two-operand signed multiply: dst = dst * src.
    void imul_reg_reg(Reg dst, Reg src) {
        emit_rex(dst, src);
        emit(0x0F);
        emit(0xAF);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_low3(dst) << 3) | reg_low3(src)));
    }

    void not_reg(Reg reg) { emit_unary_reg(0x02, reg); }
    void neg_reg(Reg reg) { emit_unary_reg(0x03, reg); }

    void cmp_reg_reg(Reg left, Reg right) { emit_binary_reg(0x39, left, right); }
    void cmp_reg_imm32(Reg reg, std::uint32_t value) { emit_imm_reg(0x81, 7, reg, value); }

    void shl_reg_imm8(Reg reg, std::uint8_t count) { emit_shift_imm(reg, 4, count); }
    void shr_reg_imm8(Reg reg, std::uint8_t count) { emit_shift_imm(reg, 5, count); }
    void sar_reg_imm8(Reg reg, std::uint8_t count) { emit_shift_imm(reg, 7, count); }
    void shl_reg_cl(Reg reg) { emit_shift_cl(reg, 4); }
    void shr_reg_cl(Reg reg) { emit_shift_cl(reg, 5); }
    void sar_reg_cl(Reg reg) { emit_shift_cl(reg, 7); }

    void xor_reg_imm32(Reg reg, std::uint32_t value) { emit_imm_reg(0x81, 6, reg, value); }
    void and_reg_imm32(Reg reg, std::uint32_t value) { emit_imm_reg(0x81, 4, reg, value); }
    void movzx_eax_al() { emit(0x0F); emit(0xB6); emit(0xC0); }
    void setcc_al(std::uint8_t condition_code) { emit(0x0F); emit(static_cast<std::uint8_t>(0x90 | (condition_code & 0x0F))); emit(0xC0); }
    void cqo() { emit(0x48); emit(0x99); }
    // Privileged address-space primitives used by the substrate policy layer.
    void mov_rax_cr3() { emit(0x0F); emit(0x20); emit(0xD8); }
    void mov_cr3_rax() { emit(0x0F); emit(0x22); emit(0xD8); }
    void mov_rax_cr2() { emit(0x0F); emit(0x20); emit(0xD0); }
    void mov_rax_cr0() { emit(0x0F); emit(0x20); emit(0xC0); }
    void mov_cr0_rax() { emit(0x0F); emit(0x22); emit(0xC0); }
    // RDMSR/WRMSR (0F 32 / 0F 30) -- fixed-register ABI per the x86 architecture itself, not a
    // convention this encoder invents: ECX selects the MSR, EDX:EAX is the 64-bit value split into
    // two 32-bit halves (RDMSR loads it, WRMSR reads it). No ModRM byte for either.
    void rdmsr() { emit(0x0F); emit(0x32); }
    void wrmsr() { emit(0x0F); emit(0x30); }
    void wbinvd() { emit(0x0F); emit(0x09); }
    void lgdt_rax() { emit(0x0F); emit(0x01); emit(0x10); }
    void lidt_rax() { emit(0x0F); emit(0x01); emit(0x18); }
    void ltr_rax() { emit(0x66); emit(0x0F); emit(0x00); emit(0xD8); }
    void invlpg_rax() { emit(0x0F); emit(0x01); emit(0x38); }
    void mov_rax_rsp() { emit(0x48); emit(0x89); emit(0xE0); }

    // mov rax, cs -- REX.W 8C /r (MOV r/m64, Sreg; reg field 001 selects CS), zero-extended into
    // RAX. Needed to discover the currently-loaded code selector so a newly built IDT's gates
    // reference a segment that is actually valid right now, rather than assuming a specific GDT
    // layout is loaded (loading a *replacement* GDT safely also requires reloading CS via a far
    // jump, which this backend does not yet support -- see aps-owned-gdt.md's "Segment-selector
    // reload" remaining-activation-gate note).
    void mov_rax_cs() { emit(0x48); emit(0x8C); emit(0xC8); }
    void xor_rdx_rdx() { xor_reg_reg(Reg::RDX, Reg::RDX); }
    void div_reg(Reg divisor) { emit_unary_reg(0x06, divisor); }
    void idiv_reg(Reg divisor) { emit_unary_reg(0x07, divisor); }

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

    // cmp rax, imm8 -- 48 83 F8 ib. Used for firmware status checks.
    void cmp_rax_imm8(std::uint8_t imm8) { emit(0x48); emit(0x83); emit(0xF8); emit(imm8); }

    // --- Scalar double (SSE2), System V hosted backend only -- see Xmm's own comment above ---

    // movsd xmm, [base+disp32] -- F2 0F 10 /r, mod=10 (disp32 form always; this backend's stack
    // frames can be up to 4095 bytes -- see generate_x86_64_function's own frame_size check -- so
    // there is no correctness reason to also add a disp8 form purely to save a few code bytes).
    // RSP (and R12) as base needs an explicit SIB byte, matching every other disp-addressed load/
    // store in this file.
    void movsd_load_disp32(Xmm dst, Reg base, std::uint32_t disp32) { emit_sse_mem_disp32(0xF2, 0x10, xmm_low3(dst), base, disp32); }
    // movsd [base+disp32], xmm -- F2 0F 11 /r
    void movsd_store_disp32(Reg base, std::uint32_t disp32, Xmm src) { emit_sse_mem_disp32(0xF2, 0x11, xmm_low3(src), base, disp32); }
    // movsd xmm, xmm -- F2 0F 10 /r, register-direct
    void movsd_reg_reg(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x10, xmm_low3(dst), xmm_low3(src)); }

    void addsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x58, xmm_low3(dst), xmm_low3(src)); }
    void subsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x5C, xmm_low3(dst), xmm_low3(src)); }
    void mulsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x59, xmm_low3(dst), xmm_low3(src)); }
    void divsd(Xmm dst, Xmm src) { emit_sse_reg_reg(0xF2, 0x5E, xmm_low3(dst), xmm_low3(src)); }

    // ucomisd left, right -- 66 0F 2E /r. Sets ZF/PF/CF the same way an unsigned integer compare
    // would, EXCEPT PF=1 additionally means "unordered" (either operand was NaN) -- callers must
    // check that before trusting ZF/CF, matching how every native double comparison in this
    // codebase (C++'s own `==`/`!=`/`<`/etc, which the tree-walking interpreter and bytecode VM
    // both compile down to) already treats a NaN comparison: never equal to anything (including
    // itself), always "!=" true, always "<"/"<="/">"/">=" false.
    void ucomisd(Xmm left, Xmm right) { emit_sse_reg_reg(0x66, 0x2E, xmm_low3(left), xmm_low3(right)); }

    // movq xmm, r64 -- 66 REX.W 0F 6E /r. Reinterprets a GPR's raw 64 bits as the destination XMM
    // register's low qword -- the standard way to get an arbitrary double bit pattern (e.g. a
    // compile-time-constant literal loaded via mov_reg_imm64) into a form addsd/mulsd/etc. can
    // operate on, with no dependency on any data-section address staying valid.
    void movq_xmm_reg(Xmm dst, Reg src) {
        emit(0x66);
        emit(static_cast<std::uint8_t>(0x48 | (reg_needs_rex_extension(src) ? 0x01 : 0)));
        emit(0x0F);
        emit(0x6E);
        emit(static_cast<std::uint8_t>(0xC0 | (xmm_low3(dst) << 3) | reg_low3(src)));
    }

    // cvttsd2si r64, xmm -- F2 REX.W 0F 2C /r. TRUNCATING double->int64 conversion (toward zero) --
    // matches C++'s own `static_cast<long long>(double)` exactly, NOT cvtsd2si's round-per-current-
    // rounding-mode behavior. This backend's hosted MOD/bitwise codegen (AND/OR/XOR/shifts, which
    // have no floating-point equivalent) needs precisely this truncating conversion to match
    // eval_binary's own value_to_int helper (the interpreter/bytecode VM's shared ground truth for
    // exactly this operation).
    void cvttsd2si_reg_xmm(Reg dst, Xmm src) {
        emit(0xF2);
        emit(static_cast<std::uint8_t>(0x48 | (reg_needs_rex_extension(dst) ? 0x04 : 0)));
        emit(0x0F);
        emit(0x2C);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_low3(dst) << 3) | xmm_low3(src)));
    }

    // cvtsi2sd xmm, r64 -- F2 REX.W 0F 2A /r. Exact int64->double conversion, the inverse of
    // cvttsd2si_reg_xmm above -- converts a bitwise op's integer result back to a real double
    // before it's stored the same way every other hosted-number result is.
    void cvtsi2sd_xmm_reg(Xmm dst, Reg src) {
        emit(0xF2);
        emit(static_cast<std::uint8_t>(0x48 | (reg_needs_rex_extension(src) ? 0x01 : 0)));
        emit(0x0F);
        emit(0x2A);
        emit(static_cast<std::uint8_t>(0xC0 | (xmm_low3(dst) << 3) | reg_low3(src)));
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

    // call reg -- FF /2, register-direct. Used for AEX native lifecycle activation once the
    // loader has resolved a CODE chunk base plus lifecycle entry offset into a concrete address.
    void call_reg(Reg target) {
        if (reg_needs_rex_extension(target)) {
            emit(0x41);  // REX.B only
        }
        emit(0xFF);
        emit(static_cast<std::uint8_t>(0xD0 | reg_low3(target))); // mod=11 reg=010(/2)
    }

    void cli() { emit(0xFA); }
    void sti() { emit(0xFB); }
    void hlt() { emit(0xF4); }
    void pause() { emit(0xF3); emit(0x90); }
    void int3() { emit(0xCC); }

    // retfq -- REX.W CB. Far return: pops an 8-byte RIP then an 8-byte-slot CS from the stack and
    // jumps there, reloading CS in the process -- the only way to change CS in 64-bit mode (there
    // is no direct "MOV CS, imm" or "JMP FAR imm" available to this encoder). The standard
    // "push CS; push RIP; far-return" trick: the caller pushes the target CS selector, then the
    // target RIP, then executes this. Needed to safely activate a newly loaded GDT: after LGDT,
    // CS still holds its old selector value, which now indexes a possibly-unrelated descriptor in
    // the new table until something reloads it (aps-owned-gdt.md's "Segment-selector reload" gate).
    void retfq() { emit(0x48); emit(0xCB); }

    // mov SS, ax -- 8E /r (MOV Sreg, r/m16; reg field 010 selects SS), reading the low 16 bits of
    // the source register regardless of its full width. After loading a new GDT, SS still holds
    // whatever selector the firmware assigned -- if that index no longer names a valid descriptor
    // in the new (typically much smaller) table, it isn't a fault yet, but IRETQ re-validates and
    // reloads SS from every interrupt frame it pops, and that validation *does* fault (#GP) on a
    // stale selector. Reloading SS to a selector valid in the new GDT (its data descriptor) is
    // therefore required for interrupt delivery to keep working after a GDT switch, not just
    // hygiene the way DS/ES/FS/GS reloads are (those aren't touched by interrupt frame push/pop).
    void mov_ss_rax() { emit(0x8E); emit(0xD0); }

    // mov DS/ES/FS/GS, ax -- 8E /r, same encoding family as mov_ss_rax with a different reg field
    // (ES=0, DS=3, FS=4, GS=5). None of these are touched by interrupt frame push/pop the way SS
    // is, so a stale post-GDT-swap selector here isn't load-bearing the way SS's was -- reloading
    // them is hygiene, not a correctness requirement this encoder has a proof forcing yet.
    void mov_ds_rax() { emit(0x8E); emit(0xD8); }
    void mov_es_rax() { emit(0x8E); emit(0xC0); }
    void mov_fs_rax() { emit(0x8E); emit(0xE0); }
    void mov_gs_rax() { emit(0x8E); emit(0xE8); }

    // int imm8 -- CD ib. Software interrupt: raises the given vector directly, the same trap-class
    // delivery as INT3 (the pushed RIP already points past this two-byte instruction, no handler
    // adjustment needed to resume). Requires a compile-time constant vector, matching this
    // backend's existing PORT.Offset precedent for operands that must be statically known.
    void int_imm8(std::uint8_t vector) { emit(0xCD); emit(vector); }

    // push/pop r64 -- (REX.B) 50+rd / (REX.B) 58+rd. Operand size is always 64-bit in long mode,
    // so unlike most instructions here these never take a REX.W bit, only REX.B when the register
    // is R8-R15. Needed by the exception-entry common handler to save/restore general-purpose
    // registers around a hardware interrupt (arcology-os/.agents/reports/aps-owned-idt.md's
    // "remaining entry-ABI gate").
    void push_reg(Reg r) {
        if (reg_needs_rex_extension(r)) emit(0x41);
        emit(static_cast<std::uint8_t>(0x50 + reg_low3(r)));
    }
    void pop_reg(Reg r) {
        if (reg_needs_rex_extension(r)) emit(0x41);
        emit(static_cast<std::uint8_t>(0x58 + reg_low3(r)));
    }

    // push imm8 -- 6A ib. Sign-extended to 64 bits and pushed as a single qword, exactly what the
    // exception-entry stubs need to normalize the CPU's inconsistent error-code push behavior
    // (some vectors push a hardware error code, most don't) into a uniform stack shape.
    void push_imm8(std::uint8_t imm8) { emit(0x6A); emit(imm8); }

    // iretq -- REX.W CF. Pops RIP/CS/RFLAGS/RSP/SS (long mode always saves RSP/SS, regardless of
    // whether the interrupt changed privilege level) and resumes. The only way back out of an
    // interrupt/exception handler.
    void iretq() { emit(0x48); emit(0xCF); }

    // inc rax -- REX.W FF /0. Used to step the saved RIP past a one-byte INT3 opcode when
    // recovering from a deliberate breakpoint.
    void inc_rax() { emit(0x48); emit(0xFF); emit(0xC0); }
    void lfence() { emit(0x0F); emit(0xAE); emit(0xE8); }
    void sfence() { emit(0x0F); emit(0xAE); emit(0xF8); }
    void mfence() { emit(0x0F); emit(0xAE); emit(0xF0); }

    void mov_load8_rax() { emit(0x0F); emit(0xB6); emit(0x00); }
    // MOVZX EAX, WORD PTR [RAX] -- 0F B7 /r, no 0x66 prefix. The 16-bit *source* width is
    // already encoded by the B7 opcode itself; the destination is the full 32-bit EAX (which
    // architecturally zero-extends into all of RAX), giving a genuinely clean zero-extending
    // 16-bit load with no leftover garbage in the upper bits. A 0x66 prefix here does not mean
    // "zero-extend to 32 bits" the way it correctly does for the 8-bit MOVZX form's non-prefixed
    // baseline -- it instead toggles the *destination* operand size down to 16-bit (AX, not
    // EAX/RAX), which real assemblers refuse to encode at all (confirmed with `nasm`: `movzx ax,
    // word [rax]` is rejected as a source/destination size mismatch -- MOVZX requires a strictly
    // *wider* destination than its source). This was a real, live bug: every existing caller
    // (MEMORY.Read16) happened to escape it only because its result always passed through this
    // codegen's own normalize()/store_result masking down to 16 bits immediately afterward, which
    // coincidentally discards whatever garbage the wrong encoding left in bits 16-63 of RAX --
    // but any caller that uses the loaded value directly, without that immediate re-mask (as a
    // hand-written UTF-16 string-comparison loop needs to), gets genuine garbage in the upper
    // bits and silently wrong comparisons. Found and fixed implementing that exact string-compare
    // loop (fission.cpp's Binary-operator STRING-equality case); see
    // .agents/reports/aps-block-storage.md and the follow-up report on this fix for the full story.
    void mov_load16_rax() { emit(0x0F); emit(0xB7); emit(0x00); }
    void mov_load32_rax() { emit(0x8B); emit(0x00); }
    void mov_load64_rax() { emit(0x48); emit(0x8B); emit(0x00); }
    void mov_store8_rax() { emit(0x88); emit(0x00); }
    void mov_store16_rax() { emit(0x66); emit(0x89); emit(0x00); }
    void mov_store32_rax() { emit(0x89); emit(0x00); }
    void mov_store64_rax() { emit(0x48); emit(0x89); emit(0x00); }
    void mov_store8_rax_from_cl() { emit(0x88); emit(0x08); }
    void mov_store16_rax_from_cx() { emit(0x66); emit(0x89); emit(0x08); }
    void mov_store32_rax_from_ecx() { emit(0x89); emit(0x08); }
    void mov_store64_rax_from_rcx() { emit(0x48); emit(0x89); emit(0x08); }

    // x86 port-I/O instructions using DX. These forms cover the complete 16-bit port range.
    void in_al_dx() { emit(0xEC); }
    void in_ax_dx() { emit(0x66); emit(0xED); }
    void in_eax_dx() { emit(0xED); }
    void out_dx_al() { emit(0xEE); }
    void out_dx_ax() { emit(0x66); emit(0xEF); }
    void out_dx_eax() { emit(0xEF); }

    // The displacement is relative to the instruction following this two-byte jump.
    void jmp_rel8(std::int8_t displacement) {
        emit(0xEB);
        emit(static_cast<std::uint8_t>(displacement));
    }

    // Near unconditional jump with a patchable signed rel32 displacement.
    std::size_t jmp_rel32_placeholder() {
        emit(0xE9);
        const std::size_t offset = code_.size();
        emit_u32(0);
        return offset;
    }

    // Near relative CALL with a patchable signed rel32 displacement.
    std::size_t call_rel32_placeholder() {
        emit(0xE8);
        const std::size_t offset = code_.size();
        emit_u32(0);
        return offset;
    }

    // Near conditional jump with a patchable signed rel32 displacement. The condition code uses
    // the standard x86 Jcc low nibble (for example 0x4 = JE, 0x5 = JNE).
    std::size_t jcc_rel32_placeholder(std::uint8_t condition_code) {
        emit(0x0F);
        emit(static_cast<std::uint8_t>(0x80 | (condition_code & 0x0F)));
        const std::size_t offset = code_.size();
        emit_u32(0);
        return offset;
    }

    void ret() { emit(0xC3); }
    void nop() { emit(0x90); }

    std::size_t size() const { return code_.size(); }
    const std::vector<std::uint8_t>& bytes() const { return code_; }

    void append_bytes(const std::vector<std::uint8_t>& bytes) {
        code_.insert(code_.end(), bytes.begin(), bytes.end());
    }

    void patch_u32(std::size_t offset, std::uint32_t value) {
        code_[offset] = static_cast<std::uint8_t>(value & 0xFF);
        code_[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
        code_[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
        code_[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    }

    void patch_i32(std::size_t offset, std::int32_t value) {
        patch_u32(offset, static_cast<std::uint32_t>(value));
    }

private:
    void emit(std::uint8_t byte) { code_.push_back(byte); }
    void emit_u32(std::uint32_t value) {
        emit(static_cast<std::uint8_t>(value & 0xFF));
        emit(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        emit(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        emit(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    }

    void emit_rex(Reg reg_field, Reg rm_field) {
        emit(static_cast<std::uint8_t>(0x48 | (reg_needs_rex_extension(reg_field) ? 0x04 : 0) |
                                        (reg_needs_rex_extension(rm_field) ? 0x01 : 0)));
    }

    void emit_rex32(Reg reg_field, Reg rm_field) {
        const std::uint8_t rex = static_cast<std::uint8_t>(0x40 |
            (reg_needs_rex_extension(reg_field) ? 0x04 : 0) |
            (reg_needs_rex_extension(rm_field) ? 0x01 : 0));
        if (rex != 0x40) emit(rex);
    }

    void emit_modrm32_disp8(std::uint8_t opcode, Reg reg_field, Reg base, std::uint8_t disp8) {
        emit_rex32(reg_field, base);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0x40 | (reg_low3(reg_field) << 3) | reg_low3(base)));
        if (reg_low3(base) == 0x4) emit(0x24);
        emit(disp8);
    }

    void emit_modrm32_disp32(std::uint8_t opcode, Reg reg_field, Reg base, std::uint32_t disp32) {
        emit_rex32(reg_field, base);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0x80 | (reg_low3(reg_field) << 3) | reg_low3(base)));
        if (reg_low3(base) == 0x4) emit(0x24);
        emit_u32(disp32);
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

    void emit_modrm_disp32(std::uint8_t opcode, Reg reg_field, Reg base, std::uint32_t disp32) {
        emit_rex(reg_field, base);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0x80 | (reg_low3(reg_field) << 3) | reg_low3(base)));
        if (reg_low3(base) == 0x4) emit(0x24);
        emit_u32(disp32);
    }

    void emit_binary_reg(std::uint8_t opcode, Reg left, Reg right) {
        emit_rex(right, left);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_low3(right) << 3) | reg_low3(left)));
    }

    void emit_unary_reg(std::uint8_t group, Reg reg) {
        emit_rex(Reg::RAX, reg);
        emit(0xF7);
        emit(static_cast<std::uint8_t>(0xC0 | ((group & 0x07) << 3) | reg_low3(reg)));
    }

    void emit_shift_imm(Reg reg, std::uint8_t group, std::uint8_t count) {
        emit_rex(Reg::RAX, reg);
        emit(0xC1);
        emit(static_cast<std::uint8_t>(0xC0 | ((group & 0x07) << 3) | reg_low3(reg)));
        emit(count);
    }

    void emit_shift_cl(Reg reg, std::uint8_t group) {
        emit_rex(Reg::RAX, reg);
        emit(0xD3);
        emit(static_cast<std::uint8_t>(0xC0 | ((group & 0x07) << 3) | reg_low3(reg)));
    }

    // mod=10 (disp32 form), matching emit_modrm_disp32's own SIB handling for RSP/R12 as base.
    void emit_sse_mem_disp32(std::uint8_t mandatory_prefix, std::uint8_t opcode, std::uint8_t reg_field, Reg base, std::uint32_t disp32) {
        emit(mandatory_prefix);
        if (reg_needs_rex_extension(base)) emit(0x41); // REX.B only -- XMM0-7 never needs REX.R here
        emit(0x0F);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0x80 | (reg_field << 3) | reg_low3(base)));
        if (reg_low3(base) == 0x4) emit(0x24); // SIB: base=RSP, no index, scale=1
        emit_u32(disp32);
    }

    void emit_sse_reg_reg(std::uint8_t mandatory_prefix, std::uint8_t opcode, std::uint8_t reg_field, std::uint8_t rm_field) {
        emit(mandatory_prefix);
        emit(0x0F);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0xC0 | (reg_field << 3) | rm_field)); // mod=11, register-direct
    }

    void emit_imm_reg(std::uint8_t opcode, std::uint8_t group, Reg reg, std::uint32_t value) {
        emit_rex(Reg::RAX, reg);
        emit(opcode);
        emit(static_cast<std::uint8_t>(0xC0 | ((group & 0x07) << 3) | reg_low3(reg)));
        emit(static_cast<std::uint8_t>(value & 0xFF));
        emit(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        emit(static_cast<std::uint8_t>((value >> 16) & 0xFF));
        emit(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    }

    std::vector<std::uint8_t> code_;
};

} // namespace arco::systems::x86_64
