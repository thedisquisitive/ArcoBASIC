# Freestanding STRING Equality Fix

## Scope delivered

The real, general compiler bug flagged (not fixed) in `.agents/reports/aps-block-storage.md`'s
"documented deviations" section is now fixed at the root: **freestanding `STRING` equality
(`=`/`<>`) now genuinely compares text content**, not pointer identity, and a second, independent
encoder bug this investigation uncovered along the way (`mov_load16_rax`'s incorrect `0x66`
prefix) is fixed too.

## Two bugs, found in sequence, both real and both fixed

### 1. STRING `=`/`<>` compared pointers, not content (`src/compiler/fission.cpp`)

Freestanding `STRING` values are pointers to a UTF-16, null-terminated buffer (a literal is
encoded once into `.rdata` at its `CONST` site; a parameter/local holds whatever pointer its
caller/assignment supplied). The generic `Binary`-operator codegen case treats every operand type
identically: load both sides into registers, `cmp_reg_reg`, `setcc`. For `STRING`, that compares
the two *pointers* — two content-identical strings from different `CONST` sites (or a parameter
vs. a literal) have different addresses and silently compare as never-equal, exactly the bug
`aps-block-storage.md` found and routed around (`path = "HELLO"` returning `FALSE` for an exact
match, confirmed under real QEMU execution, not just at compile time).

Fixed by special-casing `STRING` operands in the `Binary` case for `==`/`!=`: a hand-assembled
loop walks both UTF-16 buffers unit-by-unit until either a mismatch or a shared null terminator is
found, using only encoder primitives this backend already exercises elsewhere for exactly this
kind of hand-assembled control flow (the `jcc_rel32_placeholder`-then-`patch_i32` forward-branch
idiom, and a `jmp_rel8` backward branch, both already established by the exception-entry table and
RFC-0036's IRQ dispatch). No new encoder primitive was needed for the control flow itself.

### 2. `mov_load16_rax()` had a genuinely wrong `0x66` prefix (`x86_64_encoder.hpp`)

Building the comparison loop above surfaced a second, independent, pre-existing bug: the fix in
(1) initially still produced wrong answers even for an *exact* match. Disassembly of the generated
code (`objdump -M intel`) showed why: `mov_load16_rax()` emitted `66 0F B7 00`, which decodes as
`MOVZX AX, WORD PTR [RAX]` — a 16-bit *destination* — not the intended `MOVZX EAX, WORD PTR [RAX]`
(32-bit destination, architecturally zero-extending into all of RAX). Confirmed against a real
assembler: `nasm` encodes `movzx eax, word [rax]` as `0F B7 00` (no `0x66`), and outright *refuses*
to encode `movzx ax, word [rax]` at all ("mismatch in operand sizes" — MOVZX requires a strictly
wider destination than its source). The `0x66` prefix here was a copy-paste-shaped mistake:
correct for the *store* forms right below it in the same primitive family (`mov_store16_rax`,
where both operands genuinely are 16-bit and the prefix is required), wrong for this *load*
(`MOVZX`) form, where the whole point is a size-mismatched, zero-extending load.

**Why this was never caught before**, and why it does not indicate any other prior work was
actually broken: every existing caller of `mov_load16_rax()` (all of them via `MEMORY.Read16`,
used throughout RFC-0036/0037/0038's FAT32 boot-sector/IDT-gate/PIT-register parsing) immediately
passes the result through this codegen's own `store_result`/`normalize` path for a 16-bit result
type, which unconditionally masks the register down to 16 bits (`AND reg, 0xFFFF`) regardless of
what garbage the buggy encoding left in bits 16-63. That mask is unconditional, not
probabilistic — every prior `MEMORY.Read16` call was always correct, for the same reason every
time, not "usually right by luck." The bug was real but fully dormant until a caller (the new
STRING-comparison loop) used the loaded value directly across multiple loop iterations without an
intervening re-mask, at which point stale upper bits from two different preceding register states
made even exact character matches compare unequal.

Fixed by removing the incorrect `0x66` prefix (`0F B7 00`, verified against real `nasm` output).

## Validation

- Real `nasm`-verified encoding for the corrected `mov_load16_rax()`, and confirmation that the
  old encoding's alternate reading is not even a form `nasm` will produce.
- Existing byte-for-byte encoder unit test (`arcology_os_tests.cpp`) updated to the corrected
  bytes; full suite green.
- **Real proof, executed under QEMU/OVMF**: ten STRING-equality/inequality edge cases in one
  fixture — exact match, same-length mismatch, left-is-prefix-of-right, right-is-prefix-of-left,
  both empty, empty-vs-non-empty (both directions), `<>` on both a match and a mismatch, and a
  single-trailing-character difference — all pass, including the specifically tricky prefix cases
  that a naive "compare until either hits null" loop could get wrong in either direction if the
  termination check were misplaced. Verified deterministic across repeated runs.
- Full existing suite: 47/47 passing, including every RFC-0036/0037/0038 QEMU fixture that
  depends on `MEMORY.Read16` — confirms the encoder fix changes no prior behavior, only removes
  the previously-dormant risk.

## Remaining activation gate

- STRING ordering comparisons (`<`, `<=`, `>`, `>=`) are still unimplemented for `STRING` operands
  (fall through to the generic pointer-compare path, which is likely meaningless for strings and
  was never a documented requirement — ArcoBASIC has no stated lexicographic-string-ordering use
  case on this profile today). Worth revisiting if one appears.
- RFC-0038's own shipped code (`stdlib/fat32_policy.abas`, `volumes_policy.abas`, the block-storage
  fixtures) was **not** reverted back to `STRING` now that the bug is fixed — the 11-byte raw 8.3
  buffers and 16-byte mount-point buffers remain the shipped implementation, since they work,
  are already proven under QEMU, and reverting them now would be pure unforced churn. This fix
  means *future* freestanding work no longer needs to avoid `STRING` the way that RFC did, not
  that RFC-0038 needed to be revisited.
- `DirectoryEntry[]`-shaped array returns are still unverified on this backend (RFC-0038's other
  documented deviation) — untouched by this fix, a separate investigation if it's ever wanted.
