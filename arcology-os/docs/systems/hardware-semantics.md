# ArcoBASIC Hardware Semantics

Packet 002 implements the RFC-0005 subset `CPU.Halt` and `CPU.HaltForever`. They are language
semantics, not exposed inline assembly or ordinary calls.

## Source and A-MIR

Both operations use bare, case-insensitive statement syntax:

```basic
CPU.Halt
CPU.HaltForever
```

Parenthesized call syntax is not accepted. The parser emits `AstKind::HardwareSemantic`; A-MIR emits
`CPU.HALT` or `CPU.HALT_FOREVER`.

`CPU.Halt` may resume according to the target architecture and falls through. `CPU.HaltForever` is
non-returning and terminal: subsequent source statements are not lowered and no implicit function
return is appended.

The hosted bytecode backend rejects both operations with an explicit diagnostic because executing
privileged hardware semantics in the hosted runtime would be incorrect.

## x86-64 Lowering

| Semantic | Bytes | Instruction sequence |
|---|---:|---|
| `CPU.Halt` | `F4` | `HLT` |
| `CPU.HaltForever` | `FA F4 EB FD` | `CLI; HLT; JMP -3` |

The infinite form disables maskable interrupts, halts, and jumps back to `HLT` if a non-maskable
event resumes execution. Instruction selection belongs exclusively to the x86-64 backend.

Unit tests verify every byte against the architecture encoding; the systems smoke test verifies the
AST, A-MIR, hosted rejection, and generated machine-code sequence.

`CPU.Pause` appears in RFC-0005 but is intentionally deferred because Agent Packet 002 authorizes
only the two halt operations.
