# Systems Semantic Aliases

Status: proposed
Applies to: parser, semantic analysis, canonical AST, documentation, diagnostics

## 1. Purpose

ArcoBASIC systems code may provide multiple source spellings for one exact operation when the alternative names improve readability without changing behavior.

The primary use is allowing developers to choose between width-oriented and data-oriented names:

```basic
PORT.Write8(port, value)
PORT.WriteByte(port, value)
```

Both forms mean precisely the same thing. Neither is a wrapper around the other.

## 2. Definition

A semantic alias is an alternate source-level name that resolves to the same canonical semantic before A-MIR generation.

Aliases must have identical:

- argument count;
- argument types;
- return type;
- side effects;
- volatility and ordering rules;
- privilege and capability requirements;
- diagnostics for invalid operands;
- target support requirements;
- backend lowering.

If any of those differ, the names are separate operations rather than aliases.

## 3. Canonicalization

The parser retains the original spelling and source location for diagnostics. Semantic analysis resolves the spelling to one canonical operation identifier.

```text
source spelling
    -> canonical AST semantic
    -> canonical A-MIR opcode
    -> target backend lowering
```

A-MIR must not contain separate opcodes for aliases.

Example:

```text
PORT.WriteByte -> PORT.WRITE8
PORT.Write8    -> PORT.WRITE8
```

## 4. Width aliases

The standard width aliases are:

| Width form | Data form |
|---|---|
| `Read8` | `ReadByte` |
| `Read16` | `ReadWord` |
| `Read32` | `ReadDWord` |
| `Read64` | `ReadQWord` |
| `Write8` | `WriteByte` |
| `Write16` | `WriteWord` |
| `Write32` | `WriteDWord` |
| `Write64` | `WriteQWord` |

A namespace only exposes aliases for widths that the underlying semantic actually supports. For example, x86 port I/O supports 8-, 16-, and 32-bit transfers, so `PORT.Read64` and `PORT.ReadQWord` do not exist.

## 5. Documentation rule

Documentation presents aliases together rather than as separate features:

```text
PORT.Write8(port, value)
Alias: PORT.WriteByte(port, value)
```

The width spelling is the canonical documentation name unless another RFC explicitly chooses a different canonical form.

## 6. Diagnostics

Diagnostics should use the spelling written by the developer when identifying source, then mention the canonical operation when useful.

Example:

```text
PORT.WriteByte expects a U8 value; received U16.
PORT.WriteByte is an alias of PORT.Write8.
```

## 7. Reveal output

`ArcoFission reveal FILE at AST` may show both the original spelling and canonical semantic:

```text
HardwareCall spelling=PORT.WriteByte semantic=PORT.WRITE8
```

`at A-MIR` shows only the canonical opcode.

## 8. Non-goals

Semantic aliases do not provide:

- compatibility macros;
- user-defined aliases;
- overloads with different types;
- automatic unit conversions;
- synonyms whose meaning is merely similar.

`CPU.Halt` and `CPU.Sleep`, for example, are not aliases because “sleep” may imply scheduler or timer behavior absent from `HLT`.

## 9. Acceptance

The implementation is complete when:

1. the alias registry has one source of truth;
2. alias and canonical spellings produce identical canonical AST semantics;
3. alias and canonical spellings produce byte-identical machine code;
4. invalid calls produce equivalent diagnostics;
5. hosted targets reject both spellings identically;
6. tests prove that no duplicate A-MIR opcode or backend path exists for an alias.
