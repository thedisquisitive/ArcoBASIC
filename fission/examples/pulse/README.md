# The shortest way to write a language for Fission: Pulse

Pulse (`pulse.abas`) is written with only Fission's Level 1 language
construction API. It has no tokenizer and no parser. The language declares its
symbols and says what each one means:

```basic
pulse = host.LanguageKit().BeginnerCompiledLanguage("pulse", ".pulse")
ignored = pulse.Symbol("+", "Up")
ignored = pulse.Symbol("!", "Show")
ignored = pulse.Pair("(", ")", "Loop")
ignored = pulse.On("Up", ADDRESSOF Pulse_Up)
ignored = pulse.On("Show", ADDRESSOF Pulse_Show)
ignored = pulse.On("Loop", ADDRESSOF Pulse_Loop)
ignored = host.Register(pulse)
```

A handler receives the symbol and the SIR builder. It returns what the symbol
does: one statement, an array of statements, or `NULL`.

```basic
FUNCTION Pulse_Up(node, builder)
    RETURN builder.AssignValue("pulse", builder.Binary("+", builder.Variable("pulse"), builder.Literal("1")))
END FUNCTION

' For a Pair, node.Body already holds the statements built for everything
' between the open and the close symbol.
FUNCTION Pulse_Loop(node, builder)
    RETURN builder.WhileLoop(builder.Binary(">", builder.Variable("pulse"), builder.Literal("0")), node.Body)
END FUNCTION
```

What Fission does for such a language (`fission/language/api/symbol_compiler.abas`):

- It finds the declared symbols in the source. The longest match wins, and
  every other character is a comment.
- It matches pairs, including nested ones, and reports unbalanced ones with a
  line and column.
- It runs the optional `Start` handler before the first symbol (set up state
  there) and `Finish` after the last one.
- It reports a symbol with no handler as a compile error.

The rest is the same as for any language. `pulsec.abas` is the whole compiler,
and every target works unchanged:

```text
pulsec build countdown.pulse -o countdown                        # native executable
pulsec build countdown.pulse -o countdown.arcof --target bytecode
pulsec build countdown.pulse --pipeline
```

When a language needs real syntax, such as names, expressions, and nested
statements, write a compile function instead. See `../tally`.
