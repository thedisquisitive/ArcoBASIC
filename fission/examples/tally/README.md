# Writing a language for Fission: the Tally example

A Fission language is an ordinary ArcoBASIC library. It reads program text and
builds Fission's semantic representation (SIR) with the SIR builder. It never
mentions a target: bytecode, native Linux executables, and every target added
later work for it unchanged.

Tally is a complete example in about 200 lines (`tally.abas`). Most languages
need even less. `../quest` declares its statements as patterns, and `../pulse`
declares its symbols. Neither one parses anything itself.

```text
say "Counting down"
let n = 5
repeat n
  say n
  let n = n - 1
end
if n = 0
  say "done"
end
```

## 1. The compile function

Fission calls it with the program text and a builder. Append one SIR statement
per statement of your language:

```basic
FUNCTION Tally_Compile(source, builder)
    ...
    ignored = builder.Append(builder.PrintValue(builder.LiteralTyped("hi", "String")))
    RETURN TRUE
END FUNCTION
```

The builder (`fission/sir/builder.abas`) speaks in programming concepts, not in
machine terms:

| You want                 | Builder call                                            |
|--------------------------|---------------------------------------------------------|
| a number / text value    | `Literal("42")`, `LiteralTyped("hi", "String")`         |
| read a variable          | `Variable("n")`                                         |
| arithmetic / comparison  | `Binary("+", left, right)` (`-`, `*`, `/`, `MOD`, `==`, `!=`, `<`, ...) |
| print                    | `PrintValue(value)`                                     |
| assign                   | `AssignValue("n", value)`                               |
| if / else                | `Conditional(condition, thenStatements, elseStatements)` |
| while                    | `WhileLoop(condition, statements)`                      |
| counted loop             | `ForRangeLoop("i", from, to, step, statements)`         |
| functions, calls, arrays, objects, classes, TRY... | `FunctionDecl`, `CallValue`, `ArrayValue`, `ObjectValue`, `ClassDecl`, `TryCatch`, ... |
| a compile error          | `builder.Error("message")`                              |

## 2. Registering the language

```basic
FUNCTION Tally_Register(host)
    tally = host.LanguageKit().BeginnerCompiledLanguage("tally", ".tally")
    ignored = tally.SourceToSir(ADDRESSOF Tally_Compile)
    RETURN host.Register(tally)
END FUNCTION
```

## 3. A compiler for it

`tallyc.abas` is the whole compiler:

```basic
#IMPORT "fission/cli/build_cli.abas"
#IMPORT "fission/examples/tally/tally.abas"

host = Fission_New()
ignored = Fission_RegisterBuiltinComponents(host)
ignored = Tally_Register(host)
ignored = Fission_RunBuildCli(host, Runtime.Args())
```

```text
tallyc build countdown.tally -o countdown                        # native executable
tallyc build countdown.tally -o countdown.arcof --target bytecode
tallyc build countdown.tally --target sir -o countdown.sir        # see the SIR
tallyc build countdown.tally --pipeline                           # see the route
```

`--pipeline` shows how Fission assembled the compiler from components:

```text
language.tally: Source.tally -> SIR
lower.sir-to-amir: SIR -> A-MIR
target.linux-x86_64: A-MIR -> Executable.linux-x86_64
```

## Swapping parts

Every stage is a registered component that consumes one artifact type and
produces another (`fission/components/builtin.abas`). The pipeline is resolved
from those types, so a component can be replaced or added without touching the
others. For example, a new target is one more component consuming `A-MIR`, and
every language gets it. Advanced languages can skip SIR and produce `A-MIR`
directly. A language can also run as a separate program: see
`fission/modules/arcobasic_frontend` and `FissionExecutableModule`.
