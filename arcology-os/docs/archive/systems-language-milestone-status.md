ARCOBASIC SYSTEMS LANGUAGE MILESTONE -- WHERE WE STAND
=======================================================

ARCHIVED SNAPSHOT
-----------------

This file preserves the status at the end of the original systems-language milestone. It is not
the current project status. Later work implemented RFC-0012's canonical frontend-to-A-MIR contract,
Packet 002 hardware bring-up, and the `arcology-os/` subsystem layout.

WHERE THINGS STAND
-------------------

The milestone (Packet: arcology-os/agent-packets/Arcology_ArcoBASIC_Systems_Agent_Packet.md) is complete and
verified: `ArcoFission build ... --target uefi-x86_64` compiles pure ArcoBASIC into a self-contained
PE32+ EFI binary that boots under real QEMU/OVMF and prints "Hello from ArcoBASIC". All 11
Definition-of-Done items pass (.agents/reports/systems-language-milestone-report.md), re-verified
on a clean git clone, 12/12 tests passing. 12 work packages, 12 commits (c128f2b -> 90b9028).

The new pipeline: ArcoBASIC source -> lexer/parser (+AST, +compile-time systems checks) ->
AmirBuilder -> A-MIR -> single-function x86-64 codegen -> self-contained PE32+ writer -> real UEFI
boot. It's fully separate from the pre-existing Linux ELF64 bytecode-VM path -- they only share the
lexer/preprocessor/parser layer.

WHAT CHANGED FROM THE PLAN, AND WHY
------------------------------------

Two small syntax deviations from the packet's illustrative source (docs/systems/uefi-target.md
section 8, WP-001):
  - systemTable.ConsoleOut.Write("...") needs parentheses (packet showed bare-word call style) --
    matches existing ArcoBASIC call syntax rather than inventing a second one.
  - Dotted type names (UEFI.Handle, UEFI.SystemTable) required extending parse_type_name, since
    ArcoBASIC's type grammar didn't previously support namespacing.

One deliberate scope narrowing (WP-003): the RFC wanted #RUNTIME NONE to reject class instantiation
and array/object literals outright. That wasn't enforced -- rejecting classes requires knowing
whether an identifier resolves to a user-defined CLASS in the same file, which is real complexity
with no acceptance-test payoff for a program that uses neither. Left as a documented gap rather than
guessed at. (These constructs still fail today, just later -- at the codegen stage instead of the
parser.)

Two docs:systems: files from the packet's suggested layout were never created
(baremetal-profile.md, hardware-semantics.md): freestanding-profile content folded into
uefi-target.md section 2 instead, and no bare-metal hardware work (interrupts/MMIO/etc.) was ever
in scope, so there was nothing to put in the second file. Recorded in the milestone report's
path-mapping table rather than silently dropped.

Four findings mid-build that reshaped implementation (not the plan's architecture, but real course
corrections):

  1. AmirBuilder doesn't consume the parser's AST -- it independently re-derives structure from the
     raw token stream (WP-000/002, discovered when WP-002's new syntax parsed fine but silently
     no-opped downstream). This is now a standing constraint: any future grammar addition must be
     taught to both the parser and AmirBuilder, or it'll parse and then silently do nothing.
  2. Real UEFI protocol methods take an implicit "This" pointer that ArcoBASIC's one-argument call
     surface doesn't show -- ConsoleOut.Write(...) needed a synthesized first argument to match the
     real two-arg OutputString(This, String) ABI. Flagged in WP-006, closed in WP-008.
  3. IMAGE_FILE_RELOCS_STRIPPED made OVMF reject an otherwise structurally perfect PE32+ image
     (WP-009) -- file, objdump -p, and pefile all said the image was fine; only a real boot plus a
     control-binary comparison found it. This was the first successful end-to-end boot.
  4. A pre-existing, unrelated crash in shell/arcosh.cpp's Process.Exists (WP-011), reproducible on
     the pre-mission baseline commit, optimization-build-only. Documented, not fixed -- out of this
     mission's scope by the packet's own rules.

No stop conditions were ever triggered, no architecture decisions were revisited, no new public
syntax beyond the two documented deviations above.

WHAT'S EXPLICITLY NOT THERE YET (THE REAL BOUNDARY FOR "NEXT SECTION")
-------------------------------------------------------------------------

This is the part that matters most for planning -- these aren't gaps, they're documented non-goals
with a specific reason each is blocked:

  - No control flow at all. The codegen only lowers a single straight-line A-MIR block -- IF/WHILE/
    FOR/TRY all fail at compile time with a clear error, not wrong codegen. This is why the boot
    demo couldn't be made to loop/wait -- it's a hard current ceiling, not a bug.
  - UEFI binding surface is exactly ConsoleOut/Write. Every other EFI_SYSTEM_TABLE field (ConIn,
    BootServices, RuntimeServices...) and every other text-output method (ClearScreen, Reset...) is
    rejected by name.
  - Classes/arrays/objects parse but fail at the x86-64 codegen stage under the systems target.
  - No real PE relocations (.reloc section) -- fine today since everything's RIP-relative, but
    blocks anything needing an absolute-address fixup.
  - 5th+ function parameters (beyond the four register-passed args) aren't supported.
  - Floating point, SIMD, interrupts, MMIO, page tables, DMA, PCI, USB, networking, multicore,
    ARM64, legacy BIOS, secure boot -- all out of scope by the packet's original section 4,
    untouched.

Given that ceiling, the most natural next work package is control flow (IF first, then loops) in
the A-MIR/codegen -- it's the one gap that blocks almost everything else (you can't usefully expand
the UEFI binding surface, do real I/O, or write a non-trivial freestanding program without it), and
it's explicitly flagged rather than silently missing. Whether that's actually next is a product
call -- an RFC/work-package for it could follow the same process WP-001 used, if that process is
worth keeping.
