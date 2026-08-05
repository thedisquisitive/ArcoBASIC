# Implementation Status

This repository now contains the first runnable slice of ArcoBASIC.

Implemented:

* C++ runtime object with sandbox-style instruction limits
* Lexer and recursive descent parser
* Interpreter for modern BASIC syntax
* Values: null, boolean, number, string
* Composite values: arrays and objects
* Statements: `PRINT`, assignment, `LET`, `IF ... THEN ... ELSE ... END IF`, `SELECT CASE ... CASE ... CASE start TO finish ... CASE ELSE ... END SELECT`, `WHILE ... WEND`, `DO WHILE` / `DO UNTIL` / `LOOP WHILE` / `LOOP UNTIL`, `FOR ... TO ... STEP ... NEXT`, `EXIT FOR`, `CONTINUE FOR`, `EXIT WHILE`, `CONTINUE WHILE`, `EXIT DO`, and `CONTINUE DO`
* Collection iteration: `FOR item IN array ... NEXT`
* Expressions: arithmetic including BASIC `MOD` and symbolic `%` modulo, comparison, equality, grouping, unary minus, symbolic boolean `!`, and short-circuit boolean `ANDALSO` / `ORELSE` / `&&` / `||`
* `CONTAINS` for string and array membership checks
* Literals: arrays (`[1, 2]`) and objects (`{"Name": "Ada"}`), including multiline delimited arrays/objects/call argument lists with trailing commas
* Object property reads such as `person.Name`
* Host function calls from expressions
* Core helpers: `LEN`, `Upper`, `Lower`
* Math helpers for interpreter and ArcoFission-hosted code: `SIN`, `COS`, `TAN`, `ASIN`, `ACOS`, `ATAN`, `ATAN2`, `SQRT`, `FLOOR`, `CEIL`, `ROUND`, `ABS`, `MIN`, `MAX`, `CLAMP`, `LERP`, `POW`, `EXP`, `LOG`, `LOG10`, `PI`, `TAU`, plus `Math.*` aliases and `Math.Constants()`
* Core file/text/document helpers available to interpreter and ArcoFission-hosted code: `File.Exists`, `File.ReadText`, `File.WriteText`, `File.AppendText`, `File.ReadBytes`, `File.WriteBytes`, `Bytes.New`, `Bytes.Length`, `Bytes.GetU8`, `Bytes.SetU8`, `Bytes.FromText`, `Bytes.ToText`, `String.Insert`, `String.Delete`, `String.Join`, `Document.New`, `Document.InsertText`, `Document.DeleteRange`, `Document.ReplaceRange`, `Document.LineColumnAt`, `Document.OffsetAtLineColumn`, `Document.ApplyFormat`, `Document.Runs`, `Document.PlainText`, `Document.Serialize`, `Document.Parse`, `Document.Save`, `Document.Load`, `Document.Text`, and `Document.LineAt`; document edits now preserve and normalize formatting runs across insert/delete/replace and `.arwrite` roundtrips
* Public C++ header
* Public C API matching the draft embedding shape
* CLI runner
* Initial `ArcoFission` native compiler prototype command with `reveal FILE at AST` parsed tree output, `reveal FILE at A-MIR` source validation plus structured A-MIR model/rendering for hosted expressions, calls, arrays, objects, indexing, indexed stores, compound assignment, expression-call statements, line labels, `GOTO`, `IF`, `SELECT CASE`, `WHILE`, `DO`, `FOR`, `FOR IN`, loop control, user functions, `TRY` / `CATCH`, class/interface declarations, compiled class method bodies, `STOP`, and `RETURN`; A-MIR reveal also reports unsupported lowering, unresolved branch/jump targets, and unterminated blocks as diagnostics; `reveal FILE at BYTECODE`, `bytecode FILE -o OUT.arcof`, and `build FILE -o OUT.arcof` emit the initial `.arcof-text` bytecode-prep format with stable opcodes, constants, locals, explicit blocks, and terminators; `compile-run FILE` and `run FILE.arcof` execute the first hosted bytecode VM subset, including straight-line code, arrays/objects/indexed stores, user function calls, GUI/window host API calls, low-level GUI pixel/fill/column drawing, key-state polling, pointer position reads, `IF`, `SELECT CASE`, `WHILE`, `DO`, `FOR`, `FOR IN`, loop control, `TRY` / `CATCH`, compiled class-method calls by full name, and numbered `GOTO`; by default, `build FILE -o OUT` and `native FILE -o OUT` build a Linux ELF64 runtime capsule that embeds the prepared bytecode and links it against the ArcoFission VM from the active CMake build tree
* ArcoSH script runner and REPL
* Executable `.abas` / `.arcsh` scripts with `#!/usr/bin/env arcosh` shebang support
* Initial ArcoSH host built-ins: `ENV`, `RUN`, `Host.OSName`, `Host.Hostname`, `Host.IsWindows`, `Host.Processes`, `Host.Printers`, `File.Exists`, `File.ReadText`, `File.Find`
* Core runtime optional libcurl-backed networking helpers available to interpreter, ArcoSH, and ArcoFission-hosted code: `Network.Available`, `Network.Get`, `Network.Post`, `Network.Download`
* Core runtime network utility helpers: `Network.UrlEncode`, `Network.UrlDecode`, `Network.QueryString`, `Network.ResolveDNS`, `Net.ResolveDNS`
* Core runtime synchronous TCP client helpers: `Network.TcpConnect`, `Network.TcpSend`, `Network.TcpRead`, `Network.TcpClose` plus `Net.*` aliases
* Minimal core runtime static HTTP server: `Web.ServeStatic(root, port, host, maxRequests)` / `Web.Static(...)`, with safe path normalization, common MIME types, and blocking one-process serving for generated sites
* ArcoBASIC utility examples now include networking probes for URL helpers, DNS, HTTP fetch/download/POST, raw TCP client checks, plus `ArcoNav`, a lightweight terminal web browser built on `Network.Get`
* GUI examples include a rotating projected-3D cube demo driven by ArcoBASIC math, arrays/objects, keyboard input, and line/circle drawing primitives
* ArcoWrite example app started as a WordPad-style GUI editor with toolbar actions, document load/save/save-as, clipboard operations, undo/redo stacks, caret editing, mouse click caret placement, drag selection, selection-aware cut/copy/paste/delete, arrow/home/end/page keyboard navigation with Shift/Ctrl variants, font-size controls, bold/italic formatting runs, paragraph alignment controls, `.arwrite` persistence, wrapped visual-line layout, scroll-aware caret/selection rendering, formatted text rendering, in-app find/replace bar, wrap toggle, file dialogs, and document stats
* System-level runtime probes and OS integration helpers: `System.Capabilities`, `System.CommandExists`, `System.Open`, `System.Launch`, `Printer.List`, `Printer.Default`, and `Printer.PrintFile`
* In-shell documentation via `HELP [topic]`, `arcosh --help [topic]`, `Help.Topic()`, and `Help.Topics()`
* Retro-styled in-shell manual panels with `HELP search <text>` and scriptable TUI helpers for boxes, rules, headers, badges, status lines, progress bars, lists, menus, key-value panels, tables, ANSI clear/cursor control, named themes, and ASCII-art scroll panels
* Interactive ArcoBASIC tutorials launched with `TUTORIAL`, `arcosh --tutorial`, `arcosh --tutorial game`, `arcosh --tutorial tool`, or the `arcosh --tutorial adventure*` ArcoAdventures series; lesson flows are viewable `.abas` files under `tutorials/`, and practice prompts support `hint`, `skip`, and `quit`
* ArcoSH profile directory support through `~/.arcosh` or `ARCOSH_HOME`, including rc files, plugin loading, reusable scripts, and profile path helpers
* User-space ArcoSH mods under `~/.arcosh/mods`, with install/list/activate/deactivate/load helpers and persisted active state in `enabled.txt`
* Shipped `arcogotchi` mod adds a small Tamagotchi-style terminal pet with persisted state
* ArcoBASIC login shell installation wizard via `arcosh --install-shell` or the in-session `INSTALL-LOGIN` command, shipped as `scripts/arcosh/install-login-shell.abas`
* Configurable ArcoSH prompts through `ArcoSH.SetPrompt()` / `ArcoSH.Prompt` with user, host, cwd, shell, and last-status tokens
* ArcoSH persistent history at `~/.arcosh/history` plus dependency-free interactive line editing with arrows, Tab completion, cursor movement, delete/backspace, Ctrl-A/E/C/D
* Context-aware ArcoSH Tab completion for commands, help topics, profile scripts, paths, `@script` launches, and `LOAD` / `RUN` script targets
* ArcoSH aliases, `source` / `.` script loading, script `Args` / `Script.*` metadata, and `arcosh --init-profile`
* ArcoSH REPL script launching with `@script.abas args...`, `RUN script.abas args...`, and classic `LOAD script.abas` / `RUN`
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
* Dynamic mixed-value arrays backed by vector-style storage, with `Array.New`, `Array.Add` / `Append` / `Push`, `Pop`, `Shift`, `Unshift`, `Insert`, `RemoveAt`, `Remove`, `Resize`, `Extend`, `Clear`, `First`, `Last`, `Length`, and `Empty` helpers
* Expanded `Object.*` helpers for shell automation data structures
* Time helpers (`Time.Now`, `Time.Timestamp`) and `Sleep(ms)`
* Additional string/script helpers: `String.StartsWith`, `String.EndsWith`, `String.Lines`, and `Format`
* ArcoSH process helpers: `Process.List`, `Process.Exists`, and POSIX `Process.Kill`
* ANSI color output in the REPL, CLI version output, and scripts through `Color.*` helpers
* External command color preservation for common tools (`ls`, `grep`, `rg`, `git`, `diff`) when ArcoSH color mode is enabled
* REPL `oops <correct-command>` retry after unknown system commands
* Classic line-numbered ArcoBASIC scripts and ArcoSH REPL program buffer with `LIST`, `RUN`, and `NEW`
* Expanded in-shell syntax help topics such as `HELP if`, `HELP select`, `HELP for`, `HELP arrays`, and `HELP lines`
* Single-line `IF condition THEN statement`, `==` equality, and ArcoSH `Exit()`/`ExitTheProgram()` helpers
* Compound assignment (`+=`, `-=`, `*=`, `/=`, bitwise variants) and bitwise operations via symbols, word operators, and `Bit.*` helpers
* Human-friendly bitwise layer: binary/hex literals, `SHIFT`, `BIT`, bit mutation helpers, bit/hex conversion helpers, `HAS`/`ADD`/`REMOVE`/`TOGGLE`, and `FLAGS` blocks
* Compiler directive preprocessing: metadata directives, defines, conditionals, warnings/errors, notes/todos, executable imports/includes, binary layout metadata, and accepted `@` attributes
* Named stdlib import resolution for `#IMPORT "text"`, `#IMPORT "files"`, `#IMPORT "shell"`, `#IMPORT "sysadmin"`, `#IMPORT "compy"`, `#IMPORT "compydb"`, and `#IMPORT "arcodb"` via local paths, `stdlib/`, `../stdlib/`, installed share directories, and `ARCOBASIC_STDLIB`
* Import alias namespaces with `#IMPORT "module" AS Alias`, which generate case-insensitive `Alias.Function` wrappers for imported functions while preserving current global import compatibility
* Viewable `.abas` modules under `stdlib/`, plus the Arcology-owned module under `arcology-os/stdlib/`, for text helpers, file helpers, shell command helpers, sysadmin helper functions, ArcoCompy value packing with safe `TryUnpack` corruption/depth/count checks, ArcoCompyDB schema-aware compact records, alpha ArcoDB single-file object storage with persisted field catalogs, durable object pointers, class-backed command/query registration, sidecar journal recovery, and full-store compaction, the first `commons` application-framework helpers for route records, validation, explainable feeds, moderation actions, and audit entries, and `arcology` v0.1a domain helpers for users, communities, memberships, posts, events, reports, moderation actions, audit entries, explainable feeds, ArcoDB persistence, static HTML export, and static-site serving examples
* Storage architecture direction documented for ArcoCompy, ArcoCompyDB, alpha ArcoDB, future ArcoDB paging/catalogs, and ArcoCompress
* Viewable stdlib example scripts under `examples/` for sysadmin checks, log writing, storage demos, and the ArcoMart storefront/inventory demo
* CMake install rules for `arcosh`, `arco_cli`, stdlib modules, ArcoSH tutorials, examples, and docs
* Alpha Linux package generation through `scripts/build/build-deb.sh` and `scripts/build/build-linux-packages.sh` for `.deb`, portable `.tar.gz`, `.rpm` when `rpmbuild` is installed, and Void Linux `xbps-src` template archives; release Void Linux `.xbps` packages are built natively through `scripts/build/build-void-native-package.sh` and `xbps-src`, plus an interactive `scripts/install/install-deb-wizard.sh` that installs a `.deb` and configures profile prompt, ArcoGotchi, and login shell options
* Source-line diagnostics for parser errors with carets, source-line context for lexer errors, and statement context for top-level runtime errors
* Alpha known limitations documented in `docs/alpha-known-limitations.md`
* Alpha class usage documented in `docs/classes.md`
* User-defined functions with parameters, default argument values, `RETURN`, and local call scope
* Alpha classes with fields, explicit `CONSTRUCTOR` blocks, compatibility `Init` constructor method, methods using `SELF`, object-backed instances, inheritance via `EXTENDS`, `SUPER.Method()`, `CLASSOF()`, `ISA()`, `SHARED` class fields/methods, `PUBLIC` / `PROTECTED` / `PRIVATE` access modifiers, interfaces with typed signature validation through `IMPLEMENTS`, abstract methods, and runtime-checked `AS Type` annotations for fields, function/method parameters, and returns
* Case-insensitive function calls for built-ins, host helpers, stdlib helpers, and user-defined functions
* Classic `:` statement separators, including multi-statement single-line `IF ... THEN`
* BASIC `REM` comments, including numbered comment lines
* Array indexing and assignment plus object property assignment
* Type/conversion helpers: `TYPEOF`, `ISNULL`, `NUMBER`, `STRING`
* Safe runtime references through `REF(value)` and typed `REF(value, "TypeName")`, with `.Value`, `.Set(value)`, `.Exists()`, and `.Clear()`; no raw memory pointer access
* `TRY` / `CATCH err` / `END TRY` runtime error handling
* ArcoSH unnumbered multiline REPL blocks for `IF`, `WHILE`, `FOR`, `FUNCTION`, `TRY`, and `FLAGS`
* Core `Array.*` and `String.*` helper functions
* Classic `GOTO lineNumber` support for line-numbered programs
* Classic `STOP` support to end the current ArcoBASIC program without exiting ArcoSH
* Runtime tests

Not implemented yet:

* Hard module isolation beyond compatibility import aliases
* Complete bytecode VM coverage for every interpreter feature
* Debugger and full package/build systems beyond the alpha Linux package builders
* Bindings beyond C and C++ headers
* Full ArcoSH object pipeline model, Ctrl-Z job suspension, richer shell parsing, and permission prompts
