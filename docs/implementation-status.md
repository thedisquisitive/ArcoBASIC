# Implementation Status

This repository now contains the first runnable slice of ArcoBASIC.

Implemented:

* C++ runtime object with sandbox-style instruction limits
* Lexer and recursive descent parser
* Interpreter for modern BASIC syntax
* Values: null, boolean, number, string
* Composite values: arrays and objects
* Statements: `PRINT`, assignment, `LET`, `IF ... THEN ... ELSE ... END IF`, `WHILE ... WEND`, `FOR ... TO ... STEP ... NEXT`
* Collection iteration: `FOR item IN array ... NEXT`
* Expressions: arithmetic, comparison, equality, grouping, unary minus
* `CONTAINS` for string and array membership checks
* Literals: arrays (`[1, 2]`) and objects (`{"Name": "Ada"}`)
* Object property reads such as `person.Name`
* Host function calls from expressions
* Core helpers: `LEN`, `Upper`, `Lower`
* Public C++ header
* Public C API matching the draft embedding shape
* CLI runner
* ArcoSH script runner and REPL
* Executable `.abas` / `.arcsh` scripts with `#!/usr/bin/env arcosh` shebang support
* Initial ArcoSH host built-ins: `ENV`, `RUN`, `Host.OSName`, `Host.Hostname`, `Host.IsWindows`, `Host.Processes`, `File.Exists`, `File.ReadText`, `File.Find`
* In-shell documentation via `HELP [topic]`, `arcosh --help [topic]`, `Help.Topic()`, and `Help.Topics()`
* Interactive ArcoSH sysadmin tutorial launched with `TUTORIAL` or `arcosh --tutorial`; lesson flow is written in `tutorials/arcosh_sysadmin.abas` with an embedded fallback
* ArcoSH profile directory support through `~/.arcosh` or `ARCOSH_HOME`, including rc files, plugin loading, reusable scripts, and profile path helpers
* Configurable ArcoSH prompts through `ArcoSH.SetPrompt()` / `ArcoSH.Prompt` with user, host, cwd, shell, and last-status tokens
* ArcoSH persistent history at `~/.arcosh/history` plus dependency-free interactive line editing with arrows, Tab completion, cursor movement, delete/backspace, Ctrl-A/E/C/D
* ArcoSH aliases, `source` / `.` script loading, script `Args` / `Script.*` metadata, and `arcosh --init-profile`
* ArcoSH command introspection through `type NAME` and `which NAME`
* ArcoSH recovery/startup modes: `--no-rc`, `--norc`, `--safe`, and explicit `--rc FILE`
* ArcoSH `--doctor` self-check for profile paths, stdlib importability, host tools, terminal readiness, and process support
* Alpha smoke test coverage for doctor, safe mode, profile initialization/loading, history, reusable scripts, executable scripts, `STOP`, help/version output, and staged install asset discovery
* ArcoSH shell-side variable expansion for `$NAME`, `${NAME}`, and `$?`
* ArcoSH environment handling with `export`, `unset`, `env`, and temporary `NAME=value command` assignment prefixes
* ArcoSH bare host command pipelines and redirection through the host shell
* ArcoSH alpha smoke coverage for command chaining with `&&`, `||`, `;`, quoted separators, and `$?`
* ArcoSH alpha background job control with trailing `&`, `jobs`, `fg`, `bg`, `kill`, `disown`, process-group signaling, cleanup, and script-level `ArcoSH.*Job` helpers
* File, Directory, and Path helpers for shell automation: write/append text, create/check directories, and common path transforms
* String escape sequences (`\n`, `\r`, `\t`, `\"`, `\\`, `\0`) for practical script output and file generation
* Interpolated strings with `$"..."` and `{expression}` placeholders, including escaped `{{` / `}}` literal braces
* Expanded `Array.*` and `Object.*` helpers for shell automation data structures
* Time helpers (`Time.Now`, `Time.Timestamp`) and `Sleep(ms)`
* Additional string/script helpers: `String.StartsWith`, `String.EndsWith`, `String.Lines`, and `Format`
* ArcoSH process helpers: `Process.List`, `Process.Exists`, and POSIX `Process.Kill`
* ANSI color output in the REPL, CLI version output, and scripts through `Color.*` helpers
* External command color preservation for common tools (`ls`, `grep`, `rg`, `git`, `diff`) when ArcoSH color mode is enabled
* REPL `oops <correct-command>` retry after unknown system commands
* Classic line-numbered ArcoBASIC scripts and ArcoSH REPL program buffer with `LIST`, `RUN`, and `NEW`
* Expanded in-shell syntax help topics such as `HELP if`, `HELP for`, `HELP arrays`, and `HELP lines`
* Single-line `IF condition THEN statement`, `==` equality, and ArcoSH `Exit()`/`ExitTheProgram()` helpers
* Compound assignment (`+=`, `-=`, `*=`, `/=`, bitwise variants) and bitwise operations via symbols, word operators, and `Bit.*` helpers
* Human-friendly bitwise layer: binary/hex literals, `SHIFT`, `BIT`, bit mutation helpers, bit/hex conversion helpers, `HAS`/`ADD`/`REMOVE`/`TOGGLE`, and `FLAGS` blocks
* Compiler directive preprocessing: metadata directives, defines, conditionals, warnings/errors, notes/todos, executable imports/includes, binary layout metadata, and accepted `@` attributes
* Named stdlib import resolution for `#IMPORT "text"`, `#IMPORT "files"`, `#IMPORT "shell"`, and `#IMPORT "sysadmin"` via local paths, `stdlib/`, `../stdlib/`, installed share directories, and `ARCOBASIC_STDLIB`
* Viewable stdlib `.abas` modules under `stdlib/` for text helpers, file helpers, shell command helpers, and sysadmin helper functions
* Viewable stdlib example scripts under `examples/` for sysadmin checks and log writing
* CMake install rules for `arcosh`, `arco_cli`, stdlib modules, ArcoSH tutorials, examples, and docs
* Alpha Debian package generation through `scripts/build-deb.sh`
* Source-line diagnostics for parser errors with carets, source-line context for lexer errors, and statement context for top-level runtime errors
* Alpha known limitations documented in `docs/alpha-known-limitations.md`
* User-defined functions with parameters, default argument values, `RETURN`, and local call scope
* Case-insensitive function calls for built-ins, host helpers, stdlib helpers, and user-defined functions
* Classic `:` statement separators, including multi-statement single-line `IF ... THEN`
* BASIC `REM` comments, including numbered comment lines
* Array indexing and assignment plus object property assignment
* Type/conversion helpers: `TYPEOF`, `ISNULL`, `NUMBER`, `STRING`
* `TRY` / `CATCH err` / `END TRY` runtime error handling
* ArcoSH unnumbered multiline REPL blocks for `IF`, `WHILE`, `FOR`, `FUNCTION`, `TRY`, and `FLAGS`
* Core `Array.*` and `String.*` helper functions
* Classic `GOTO lineNumber` support for line-numbered programs
* Classic `STOP` support to end the current ArcoBASIC program without exiting ArcoSH
* Runtime tests

Not implemented yet:

* Classes
* Namespaced module isolation beyond simple executable `.abas` imports
* Bytecode VM
* Debugger and full package/build systems beyond the alpha `.deb` builder
* Bindings beyond C and C++ headers
* Full ArcoSH object pipeline model, terminal handoff/Ctrl-Z job control, richer shell parsing, and permission prompts
