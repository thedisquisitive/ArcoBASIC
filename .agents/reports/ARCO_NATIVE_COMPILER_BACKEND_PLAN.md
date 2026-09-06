# Native (No-VM) ArcoBASIC Compiler Backend — Phased Plan

**Renamed 2026-09-05 from `ARCO_SH_NATIVE_COMPILER_BACKEND_PLAN.md`.** This is general Arcology
platform infrastructure, not part of ArcoSH — see **RFC-0049**
(`arcology-os/rfcs/RFC-0049_Native_Hosted_ArcoBASIC_Compilation_and_System_Runtime.md`), the
authoritative design/scope document. This file remains the detailed phased *plan*; RFC-0049 is the
short, stable summary; `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` is the entry-by-entry history of
what actually happened, in order, including the real bugs found along the way.

**Status:** Phase 1 (System V/Linux: PRINT + arithmetic + the ArcoValue runtime ABI) implemented
and tested — **v2, supersedes v1 in full**
**v1 was wrong.** It proposed building a new AMIR-to-native-x86-64 lowering pass and a new encoder from
scratch. That infrastructure **already exists** (`generate_x86_64_function`/`generate_x86_64_program`,
`src/compiler/fission.cpp:4485-5830`, ~1250 lines, used by the UEFI target since RFC-0044-0047, QEMU-proven).
v1 was written without reading that code first — a real process failure, not a judgment call; see
`.agents/ARCO_SH_PROGRESS.md` Entry 3 for the direct account. This version is grounded in what that code
actually does, read line-by-line, not inferred from doc comments about a *different* encoder
(`include/arco/jit_x86_64.hpp`, this session's own narrow loop-JIT, which is NOT the relevant prior art here).

---

## 1. What already exists (verified by reading the code, not assumed)

`generate_x86_64_function(const AmirModule&, const std::string& function_name)` compiles one ArcoBASIC
function's AMIR straight to real x86-64 machine code:

- Real per-function stack frame (slot allocation for every local/temp, 16-byte-aligned, sized to fit
  outgoing call arguments) — `fission.cpp:4504-4552`.
- Real control flow: `AmirInstruction::Kind::Branch`/`Jump`/`Label` lower to real `Jcc`/`JMP`, not a bytecode
  cursor bump. Covers `IF`/`WHILE`/`FOR`/loop control already, because AMIR itself already expresses those as
  branches/jumps (confirmed: this is the *same* AMIR the bytecode compiler consumes, just a different backend
  reading it).
- Real function-to-function calls: `AmirInstruction::Kind::CallValue` for calling *other compiled ArcoBASIC
  functions* — direct `CALL rel32`, patched by `generate_x86_64_program`'s internal-call resolution pass
  (`fission.cpp:5812-5827`) once every function's final offset in the combined image is known. Fully generic,
  not UEFI-specific.
- `generate_x86_64_program` links multiple functions' fragments into one combined `.text`/`.rdata` image with
  resolved internal calls — this combining/linking logic has **zero** UEFI-specific content.
- Scalar arithmetic (`Binary`/`Unary`/`Const`/`Load`/`Store`/`Return`) already works for the numeric/fixed-
  width types the freestanding profile supports.
- Output today: `render_x86_64` (a text listing) or, via `write_pe32plus_efi_image`
  (`arcology-os/include/arco/pe_image.hpp`), a real hand-written PE32+ image — because bare UEFI has no
  linker to hand off to.

**What's genuinely UEFI-specific** (confirmed by reading these exact spots, not assumed from the freestanding
docs alone):

1. **Calling convention is hardcoded Microsoft x64** (`fission.cpp:4522` uses `systems::kShadowSpaceBytes`;
   argument registers are the MS x64 set) inside `generate_x86_64_function` itself — not parameterized.
2. **`CallExternal` (host/OS calls) is hardwired to UEFI protocol dispatch**
   (`fission.cpp:5561-5600`: `systems::lookup_uefi_type`, walking UEFI struct field tables by name). This is
   how `PRINT` etc. reach the outside world on that target. Nothing else in this pass has an equivalent for
   "call the host runtime" on any other target.
3. **String representation is a raw fixed-width UTF-16 buffer pointer**, not `arco::Value`'s heap-backed
   `std::string` — `LEN`/`MID` are hand-assembled unit-by-unit buffer walks
   (`fission.cpp:5375-5460`, the "Freestanding LEN/MID" work from earlier project history) because the
   freestanding profile has no heap allocator to back a real dynamic string with. **This constraint is a
   freestanding-target fact, not a Linux one** — see §2.
4. **Output**: hand-written PE32+, because bare UEFI has no linker to call.

## 2. The key correction: Linux is not freestanding, and already has what freestanding lacks

The freestanding profile's "no heap allocator, no dynamic `Value` runtime" (WP-000 audit §5) is true *because
it's targeting bare UEFI firmware with no OS underneath it*. A Linux-native ArcoSH backend has none of that
problem: it can link against `arco_runtime` (the same C++ library the bytecode VM already links against) and
get a real `malloc`-backed heap, a real already-correct `arco::Value` (shared_ptr-backed arrays/objects/
strings), and every existing host function (`String.*`, `Array.*`, `File.*`, ...) for free — **without**
reimplementing any of it, and **without** the freestanding profile's restrictions applying at all. This was
the one part v1 got right in spirit (a shim around `arco::Value`) but wrongly scoped as "build a new pipeline
to call it from"; the pipeline already exists, it just needs a *Linux-shaped* `CallExternal` lowering instead
of a UEFI-shaped one.

Also newly confirmed: `ArcoFission native` does **not** hand-write ELF today either.
`fission.cpp:8380-8430` generates a small `launcher.cpp` and shells out to the system C++ compiler/linker
(`std::system("c++ ... -o output ...")`) to produce the final ELF64 capsule. The exact same mechanism —
already proven, already in this codebase — is the right way to turn this backend's generated machine code
into a real Linux ELF64 binary: emit an object file (or a `.s` with `.byte`-encoded machine code, or hand a
minimal relocatable `.o` to `ld`) and let the system toolchain do the ELF work, rather than hand-rolling ELF
format. **No new ELF writer needs to be built.**

## 3. Corrected Phase 1 scope

Goal is unchanged from v1: a genuinely native Linux ELF64 binary, no bytecode-dispatch loop anywhere in it,
correctly running numbers/strings/variables/`IF`/`FOR`/`WHILE`/user function calls/`PRINT`. What changes is
*how much of this is new work*:

1. **New**: a SysV-AMD64 calling-convention variant of `generate_x86_64_function`'s argument-marshaling logic
   (real change, bounded to that concern — the frame-layout/slot-allocation/control-flow logic around it does
   not need to change).
2. **New**: a Linux `CallExternal` lowering path, parallel to the existing UEFI one, that instead emits a real
   `CALL` into a new small `extern "C"` shim — e.g. `arco_native_print(ArcoValueHandle)`,
   `arco_native_call_host(const char* name, ArcoValueHandle* args, size_t count)` — thin wrappers around the
   *existing* `arco::Runtime`/`arco::Value` C++ API (confirmed `include/arco/c/arco_c_api.h` doesn't already
   provide this granularity — genuinely new, but small, and it's calling *into* already-correct code, not
   reimplementing semantics).
3. **New (design decision, not yet made)**: local/temp representation. Two real options, not mutually
   exclusive:
   - raw native scalars (double/int64) for provably-numeric locals — reuses the exact type-narrowing
     discipline this session's loop-JIT already established;
   - an opaque `Value` handle (pointer to a heap or stack-resident `arco::Value`) for anything else, read/
     written only through shim calls. This is what makes strings/arrays/objects "just work" via the real
     runtime instead of hand-assembling buffer walks the way the freestanding LEN/MID code had to.
4. **Not new, reused as-is**: control flow, function-to-function calls, multi-function linking
   (`generate_x86_64_program`), and the "shell out to system `cc`/`ld`" ELF-production path.
5. **Not new, reused with care**: the existing SSE2 scalar encoder (`include/arco/jit_x86_64.hpp`) for the
   provably-numeric fast path within this backend, exactly as it's already used by the loop-JIT — no reason
   to duplicate that encoder a third time.

Verification, unchanged from v1's discipline: three-way byte-identical output (interpreter / bytecode-VM /
this backend) per test fixture; an explicit check that no `execute_function`-shaped dispatch loop exists in
the produced binary; new encoder paths (SysV argument marshaling) nasm-cross-verified the same way
`jit_x86_64_tests.cpp` already does.

## 4. Explicit non-goals, unchanged from v1

Not a garbage collector (reuses `arco::Value`'s existing shared_ptr refcounting). Not a general register
allocator in Phase 1 (stack-slot-everywhere, matching the existing UEFI backend's own approach — it does the
same thing). Not Windows in Phase 1 (Phase 4, needs a PE-object-format equivalent of the ELF-via-system-
toolchain trick — MSVC's `link.exe`/`clang-cl`, or the same MS x64 convention this pass already has, minus the
bare-metal PE-writing since Windows has a real linker too). Not freestanding/AOS changes in Phase 1 — whether
to extend the freestanding profile with a real allocator is still a separate, later owner decision, now with
much better information: Linux doesn't need that decision resolved at all to proceed, since it isn't
freestanding.

## 5. Immediate next action

Given how much less new-from-scratch work this now is, propose starting directly with the `CallExternal`
Linux-lowering + shim (§3.2) against the *existing* `generate_x86_64_function`, using `PRINT "hello"` as the
first end-to-end target (closest to RFC-0007's own UEFI console "hello" milestone, and exercises calling
convention + shim + linking all at once with minimal surface area). SysV calling-convention work (§3.1) is a
prerequisite for that same first milestone, so the two land together rather than sequentially.
