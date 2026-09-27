# A language with real syntax, no parser: Quest

Quest (`quest.abas`) is a small rules language for game designers:

```text
# A character's attack
damage = strength * 2 + weaponBonus
repeat 2
  show "swing"
end
if damage > 15
  show "critical"
otherwise
  show "normal"
end
```

It is written with only Fission's Level 1 construction API. The language lists
its statements as patterns and says what each one means. It contains no
tokenizer, no expression parser, no block matching, and no target code.

```basic
quest = host.LanguageKit().BeginnerCompiledLanguage("quest", ".quest")
ignored = quest.Comment("#")
ignored = quest.Statement("Set", "{Target:name} = {Value}")
ignored = quest.Statement("Show", "show {Value}")
ignored = quest.Block("Repeat", "repeat {Count}", "end")
ignored = quest.Block("When", "if {Condition}", "end")
ignored = quest.Alternative("When", "otherwise")
ignored = quest.On("Set", ADDRESSOF Quest_Set)
...
```

## Patterns

| In a pattern     | Matches                                   | The handler gets              |
|------------------|-------------------------------------------|-------------------------------|
| `{Value}`        | an expression                             | `node.Value`, a SIR value     |
| `{Target:name}`  | a plain name                              | `node.Target`, a String       |
| anything else    | exactly that word or symbol               |                               |

- Statements are one per line.
- Patterns are tried in the order they were declared. The first one that
  matches the whole line wins.
- `Block(name, pattern, endWord)` makes the following lines, up to `endWord`,
  the statement's `node.Body`.
- `Alternative(block, word)` adds an else part, which arrives as
  `node.ElseBody`.

Expressions come ready-made: numbers, `"text"`, names, `( )`, unary minus, and
`+ - * / %` and `= <> < <= > >=` with the usual precedence. `Infix(symbol,
precedence, sirOperator)` declares your own operator set instead, for example
`Infix("and", 0, "AND")`.

## Handlers

```basic
FUNCTION Quest_Set(node, builder)
    RETURN builder.AssignValue(node.Target, node.Value)
END FUNCTION

FUNCTION Quest_When(node, builder)
    RETURN builder.Conditional(node.Condition, node.Body, node.ElseBody)
END FUNCTION
```

`node.Line` is the line number. `node.Id` is unique for each statement, which
is useful for naming hidden variables such as loop counters.

## Errors come for free

```text
ERROR FISSION_LANGUAGE_ERROR: line 2: not a statement of this language: shout x
ERROR FISSION_LANGUAGE_ERROR: line 3: Repeat is never closed with end
```

## Build

`questc.abas` is the whole compiler, and every Fission target works for Quest:

```text
questc build rules.quest -o rules                         # native executable
questc build rules.quest -o rules.arcof --target bytecode
questc build rules.quest --target sir -o rules.sir        # see the SIR
```

## The three ways to write a language

- **Symbols only** (`../pulse`): `Symbol`, `Pair`, `On`.
- **Line patterns** (this one): `Statement`, `Block`, `On`.
- **Your own compile function** (`../tally`): `SourceToSir(callback)`, with
  full control of the syntax.
