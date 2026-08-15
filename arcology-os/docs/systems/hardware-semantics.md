# ArcoBASIC Hardware Semantics

The systems target implements `CPU.Halt`, `CPU.HaltForever`, and the nonterminal `CPU.Pause`. They are language
semantics, not exposed inline assembly or ordinary calls.

## Source and A-MIR

Both operations use bare, case-insensitive statement syntax:

```basic
CPU.Halt
CPU.HaltForever
CPU.Pause
```

Parenthesized call syntax is not accepted. The parser emits `AstKind::HardwareSemantic`; A-MIR emits
`CPU.HALT` or `CPU.HALT_FOREVER`.

`CPU.Halt` may resume according to the target architecture and falls through. `CPU.Pause` is a
spin-wait hint and falls through. `CPU.HaltForever` is
non-returning and terminal: subsequent source statements are not lowered and no implicit function
return is appended.

The hosted bytecode backend rejects all three operations with an explicit diagnostic because executing
privileged hardware semantics in the hosted runtime would be incorrect.

## x86-64 Lowering

| Semantic | Bytes | Instruction sequence |
|---|---:|---|
| `CPU.Halt` | `F4` | `HLT` |
| `CPU.HaltForever` | `FA F4 EB FD` | `CLI; HLT; JMP -3` |
| `CPU.Pause` | `F3 90` | `PAUSE` |

The infinite form disables maskable interrupts, halts, and jumps back to `HLT` if a non-maskable
event resumes execution. Instruction selection belongs exclusively to the x86-64 backend.

Unit tests verify every byte against the architecture encoding; the systems smoke test verifies the
AST, A-MIR, hosted rejection, and generated machine-code sequence.
