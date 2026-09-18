# Arcology Shell (arcosh)

This is a fresh, Linux-focused restart of ArcoSH, now governed by
[RFC-0052 "The Arcology Shell"](rfcs/RFC-0052_The_Arcology_Shell.md). It is developed as its own
standalone CMake project independent of the umbrella ArcoBASIC build (it is not
`add_subdirectory`'d from the root `CMakeLists.txt`).

Per RFC-0052 section 6, `arcosh` is authored **in ArcoBASIC itself** and compiled through
ArcoFission, not hand-written in C++.

## Status: WP-001 (Shell Skeleton) through WP-012 (Job Control), plus WP-008 (Display and Themes),
a built-in HELP command, profile/theme/prompt customization (RFC-0052 sections 20/22), and
interactive theme/prompt editors on top of it

`src/arcosh.abas` implements RFC-0052's AP-0052-004 (WP-001):

* an interactive read loop with a basic prompt (`arcosh:<arcology-cwd>> `);
* `--version` and `--diagnostic` flags;
* clean exit (`exit`/`quit`, or EOF/Ctrl-D);
* current directory (`pwd`, and shown in the prompt);
* terminal capability query (`Console.IsTTY`) with graceful degradation for piped/non-interactive
  input and dumb terminals;
* explicit no-color mode (`--no-color`, or the `NO_COLOR` environment variable).

and AP-0052-005 (WP-002), the single Arcology colon-path <-> Linux host-path translation service
(`Path.IsArcology`/`Path.ToHost`/`Path.ToArcology`/`Path.Normalize` in `src/arcosh.abas` — RFC
section 7.3: "Path conversion MUST be implemented through one path service... Built-ins MUST NOT
each implement colon-path parsing independently"):

* `:` / `:home:` / `:home:user:` / `:home:user:Desktop:` <-> `/` / `/home/` / `/home/user/` /
  `/home/user/Desktop/`, including file paths (no trailing colon/slash), `.`/`..` collapsing,
  surrounding whitespace and one enclosing pair of quotes, and malformed input (`::`) degrading to
  root instead of erroring;
* ordinary Linux paths (no leading colon / no leading slash for the reverse direction) pass through
  unchanged;
* `cd` (accepting either an Arcology or a Linux path; bare `cd` goes to `Path.Home()`) and the
  prompt are the only current consumers — anything added later that needs colon-path handling must
  call these functions, not re-parse `:segments:` itself.

and AP-0052-006/007 (WP-003 Structured Command Parser + WP-004 Linux Process Layer):

* `TokenizeCommand`/`ParseCommand` in `src/arcosh.abas` parse a command line into
  `{Executable, Arguments, Redirections, PipelineStages, Background, Source}` — quote/escape-aware
  word splitting, with `|`/`>`/`>>`/`<`/trailing `&` recognized structurally;
* `Process.Execute` (`arco_runtime`, `src/runtime/runtime.cpp`) runs a plain foreground command for
  real — `fork`/`execvp`/`waitpid` with correct job-control terminal handoff (verified under a real
  pty: Ctrl-C during a foreground child interrupts the child, not the shell, which stays responsive
  afterward), `$PATH` lookup, and a clean "command not found" distinct from the program's own exit
  status (the standard self-pipe technique — see the function's own comment);
* a command with any pipeline stage, redirection, or trailing `&` is refused with a clear message
  naming the RFC section that will implement it (WP-011/WP-012), rather than being silently
  mis-run.

and AP-0052-008 (WP-005 Structured History + `oops`):

* every non-empty command (built-ins included) appends a `MakeHistoryEntry` record
  (`SourceText`/`Executable`/`Arguments`/`Redirections`/`PipelineStages`/`Background`/
  `WorkingDirectory`/`Timestamp`/`ResultKind`/`ExitCode` — RFC section 14's own field list) to a
  `history` array; `history` (the built-in) lists them;
* `oops <command>` replaces only the immediately-preceding `COMMAND_NOT_FOUND` command's
  executable, operating on its *parsed* `Arguments`/`Redirections`/`PipelineStages` — never its raw
  text — so a quoted argument that happens to contain the failed executable's own name survives
  untouched (RFC section 15.2's explicit requirement, and its own regression test: `gti commit -m
  "gti must remain inside this message"` then `oops git` really does `git commit` with that exact
  message, confirmed against a real repo, not just the `--selftest-oops` structural check).

and AP-0052-009 (WP-006 Resident ArcoBASIC Program + Immediate Input):

* a line typed at the prompt that starts with a number is classified before shell-command parsing
  even runs (`ParseLeadingLineNumber`) and inserted/replaced/deleted (empty text deletes) into an
  in-memory `resident_program` array kept in numeric order (`SetProgramLine`) — it is never
  executed immediately, matching classic BASIC line-editing semantics (RFC section 8/9);
* `LIST` prints the resident program back in order; `NEW` clears it;
* `RUN` (with no filename argument — `RUN <file>` is WP-007, refused with a clear message for now)
  reassembles the resident program into source text and hands it to the new `Runtime.RunString`
  host function, which compiles and executes it in a fresh, isolated `arco::Runtime` instance — so
  `GOTO`-based line-number control flow works exactly as classic BASIC, without arcosh's own REPL
  loop needing to interpret it itself;
* verified against the RFC section 9 worked example exactly (`10 PRINT "ONE"` / `20 PRINT "TWO"` /
  replace `20 PRINT "THREE"` / `LIST` / bare `20` deletes it / `LIST` / `RUN` / `NEW`), plus a
  multi-digit-line-number `GOTO` counting loop, both against the real native binary;
* RFC section 8's "Input Classification" also requires **ArcoBASIC immediate input** as its own
  category, distinct from a shell command — typing `PRINT "hello"` or `x = 5` directly at the
  prompt runs it right away (`IsArcoBasicImmediateInput`/`RunImmediateInput`), classified by shape
  (a leading ArcoBASIC statement keyword, or a plain/compound assignment) rather than by trying to
  parse the line and seeing if it happens to succeed — a bare `ls` is syntactically a valid
  ArcoBASIC expression-statement (an identifier reference) that would only fail at runtime
  ("undefined variable"), so parse-then-see-if-it-works would misclassify ordinary commands.
  Backed by the new `Runtime.EvalImmediate` host function, a SINGLE persistent interpreter for the
  whole session (unlike `Runtime.RunString`'s deliberately fresh one per `RUN`) — a variable set on
  one line stays visible on the next, real REPL/classic-BASIC-immediate-mode semantics, verified
  directly: `PRINT "hello"` prints `hello`, `x = 5` / `PRINT x` / `x = x + 1` / `PRINT x` prints
  `5` then `6`, and ordinary shell commands (`ls`, etc.) keep working unaffected in between.

and AP-0052-010 (WP-007 Script Execution):

* `RUN <file>` (`RunScriptFile` in `src/arcosh.abas`) reads the raw file text and hands it
  directly to `Runtime.RunString` — the SAME canonical compiler/parser path every other ArcoBASIC
  program goes through, never a hand-rolled preprocessor for numbered source (the RFC's own
  explicit requirement: "do not preprocess numbered source into a different language");
* numbered lines, unnumbered lines, and a mix of both in one file all work with zero special
  casing here, since `Parser::statement()` (`src/frontend/parser.cpp`) already accepts an
  optional leading line-number label on any statement, not only inside some special "classic
  mode";
* proven against three real, checked-in fixture files (`tests/fixtures/wp007_*.abas`) — pure
  unnumbered structured code, a classic numbered `GOTO` loop, and a file mixing an unnumbered
  `FUNCTION` declaration with numbered top-level statements that call it — each calling
  `ExitTheProgram` with its own distinctive code so the self-test can verify the RIGHT program
  actually ran, not just that some program didn't error;
* a missing file produces a clean `arcosh: run: no such file: ...` message rather than an
  uncaught panic.

and AP-0052-014 (WP-011 Pipelines and Redirection):

* `ParseCommand` now splits a real pipeline into per-stage `{Executable, Arguments}` objects
  (`PipelineStages`) instead of only recording that a pipe token was present — the first stage
  stays in the existing `Executable`/`Arguments` fields, unchanged, for the plain-command case;
* `Process.ExecutePipeline` (`src/runtime/runtime.cpp`) does the real fork/pipe/`dup2` plumbing —
  one process group for the WHOLE pipeline (so Ctrl-C stops every stage, not just the last), real
  files opened for `<`/`>`/`>>` (the same primitive handles plain redirection with no pipe at all,
  as a one-stage "pipeline"); returns one result per stage so an earlier stage's
  command-not-found can be reported without changing the pipeline's own exit status (which always
  reflects the LAST stage, matching real shell `$?` semantics);
* records exactly one history entry per typed pipeline/redirection command, classified from the
  last stage (now `RunForegroundJob`, see AP-0052-015 below — WP-012 folded this into the same
  job-control-aware execution path every command uses);
* verified against real `cat`/`grep`/`wc` processes and real files on disk — a pipeline with output
  redirected to a file, plain `>`/`>>`/`<` redirection, `<` and `>` combined in one command, and a
  pipeline surviving an earlier stage's command-not-found — per AP-0052-014's own explicit rule:
  "Tests MUST inspect actual execution/output behavior. Parser-only tests are insufficient."

and AP-0052-015 (WP-012 Job Control — full research write-up at `.agents/reports/
ARCO_SH_RFC0052_WP012_JOB_CONTROL_RESEARCH.md`, written before any implementation per the RFC's
own explicit requirement):

* `Process.SetupShellJobControl`/`StartJob`/`WaitJob`/`PollJob`/`ContinueJob` (`src/runtime/
  runtime.cpp`) replace `Process.Execute`/`ExecutePipeline` as arcosh's own real execution path —
  every job (single command or pipeline, foreground or background) gets its own process group, and
  every foreground wait uses `WUNTRACED` so a real Ctrl-Z is detected as a STOP, not left blocking
  the shell until the job fully exits (the exact "faked" job control AP-0052-015 explicitly rules
  out — "Background execution MUST NOT be faked merely by spawning detached child processes");
* `RunForegroundJob` (superseding WP-004/WP-011's `ExecuteAndRecord`/`ExecutePipelineAndRecord`)
  now handles suspend (Ctrl-Z stops ANY foreground command, not only something explicitly
  backgrounded) alongside the ordinary run-to-completion case; `RunBackgroundJob` (`command &`)
  starts a real, job-table-tracked background job without ever waiting on it;
  `PollBackgroundJobs` (polled once per prompt line) discovers a background job finishing or
  stopping on its own schedule without ever blocking the prompt to find out; `jobs`/`fg [%N]`/
  `bg [%N]` are real shell built-ins operating on that same job table;
  `Process.SetupShellJobControl` claims the controlling terminal once at startup and leaves
  `SIGINT`/`SIGQUIT`/`SIGTSTP`/`SIGTTIN`/`SIGTTOU` ignored for the shell's own entire interactive
  lifetime (never saved-and-restored per command);
  as a side effect of unifying execution into one function, `oops` retrying a failed pipeline
  command now correctly preserves and re-runs the WHOLE pipeline (it previously silently dropped
  every stage but the first) — a pre-existing WP-005/WP-011-era gap this pass also closed properly:
  since a pipeline's own result always reflects its LAST stage (matching real shell `$?`
  semantics), `COMMAND_NOT_FOUND` can only ever be reached when the LAST stage is the one that
  actually failed to exec, so `oops` now targets THAT stage specifically — replacing just its
  executable, keeping its own arguments, leaving every earlier stage (a different command
  entirely) untouched — rather than always assuming the first stage was the failure, which is all
  RFC section 15's own "version 1 rules" ever needed to say before pipelines existed;
* verified two ways, matching AP-0052-014's own standard: `--selftest-jobcontrol` (real background
  jobs polled to completion, an external `kill -STOP`/`SIGCONT` exercising the exact same
  `WUNTRACED` stop-detection code path a real Ctrl-Z uses — ctest has no controlling terminal, so
  the signal itself is simulated externally rather than via a real keypress) plus manual
  verification against the real compiled native binary under a real pty: typing `sleep 5`, Ctrl-Z
  (`[1]+  Stopped                 sleep 5`, shell stays fully responsive), `jobs`, `fg` (blocks
  until the job actually finishes), and separately `bg` (resumes into the background, `jobs` shows
  `Running` until it completes on its own).

and AP-0052-011 (WP-008 Display and Themes, implemented out of numeric order alongside WP-012):

* RFC section 17's own literal API (`Display.Style`/`Text`/`Line`/`Reset`) — "Shell components
  SHOULD NOT emit arbitrary ANSI sequences directly", so every raw escape byte anywhere in this
  file comes from exactly these four functions;
* the 8 required semantic roles (`normal`/`prompt`/`path`/`success`/`warning`/`error`/`muted`/
  `selection`), each with `TrueColor`/`Color256`/`Color16` fallbacks in the one bundled default
  theme (`DefaultTheme`), detected once at startup (`DetectColorTier`, using `COLORTERM`/`TERM`/
  `NO_COLOR`/`--no-color`/tty-ness) and collapsing to zero styling bytes at all for tier `None` —
  AP-0052-011's own "all errors must remain understandable in monochrome output" requirement,
  satisfied by every message's own text already carrying its full meaning with color stripped out
  entirely;
* the prompt (`prompt`/`path` roles) and every `arcosh: ...`/job-control notice throughout this
  file now go through `Display.Style` or the small `DisplayError`/`Warning`/`Success`/`Muted`
  convenience wrappers instead of a bare `PRINT`;
* verified structurally and by real output: `--selftest-display` (tier detection for all four
  signal combinations, the theme's own role/tier completeness, the exact ANSI bytes `StyleCode`
  produces, and zero bytes at tier `None`), plus manual verification against the real compiled
  binary under a real pty with `TERM=xterm-256color`/`COLORTERM=truecolor` set — the styled prompt,
  banner, and a real `command not found` error all confirmed byte-for-byte correct.

A new general-purpose `Chr(code)` host function (classic BASIC `CHR$`) was added to `arco_runtime`
for this — the lexer's own string-literal escapes have no way to embed a raw ESC (0x1B) byte to
build an ANSI SGR sequence. Building this also required fixing a fourth real native-backend
miscompilation, found the same way as the others this project: a `BOOL`-typed parameter's argument
could be genuinely Boxed at the call site (any host-function-call result, e.g. `Console.IsTTY()`)
but was never actually unboxed, just bit-loaded and masked to its low byte — a real heap pointer's
low byte, not its truthiness. See `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entry 32, and Entry 31
for a separate, real 4-minute-plus native build this same pass found and fixed (a missing
memoization in the compiler's own static type-inference analysis, disclosed but left undone back
in Entry 24 — now fixed, cutting the affected build down to well under a minute, though still
slower than this project's single-digit-second norm and only partially root-caused).

and a built-in `HELP` command (not one of RFC-0052's own numbered work packages — added directly
on request):

* `help` lists every topic with a one-line summary; `help <topic>` (case-insensitive, some topics
  have short aliases — e.g. `help ?`/`help fg` both resolve to real topics) shows that topic's
  full text. Topics: `help`, `overview`, `paths`, `commands`, `pipelines`, `jobs`, `oops`,
  `history`, `program`, `immediate`, `run`, `themes`, `arcobasic` — covering every feature this
  project has actually built so far, real and accurate, never describing planned functionality as
  implemented (the same discipline RFC-0052 AP-0052-017 states for this project's external
  documentation, applied here too);
* the topic table (`HelpTopics` in `src/arcosh.abas`) is plain data — `{Name, Aliases, Summary,
  Body}` — specifically so it can grow toward a fuller ArcoBASIC language reference, and
  eventually interactive step-by-step tutorials, without `RunHelpCommand`/`FindHelpTopic` ever
  needing to change shape; both of those are real, disclosed *future* work, not attempted in this
  pass — `help arcobasic` is deliberately a short introduction, not the full language reference,
  and says so;
* styled through the same `Display.*` functions WP-008 established (topic names in the `prompt`
  role, an unknown topic in `error`) rather than a raw `PRINT`;
* verified via `--selftest-help` (topic-table integrity, lookup by name and by alias, a clean
  "not found" for an unknown topic) plus manual verification of the real printed output.

and profile/theme/prompt customization (RFC-0052 sections 20 "Prompt Architecture" and 22
"Configuration" — not one of RFC-0052's own numbered work packages either):

* the prompt is composed from named segments (`classic` — the original `arcosh:<path>` look, kept
  as its own segment so the DEFAULT prompt is byte-for-byte unchanged — `path`, `hostname`,
  `exitcode`, `jobs`) instead of one hardcoded format; `prompt` shows the active segments and a
  live preview, `prompt set <seg1,seg2,...>` changes them for the session;
* `theme` previews the active theme by printing every required role's own NAME in that role's own
  live color (a real swatch, not a description); `theme list` lists built-in and saved themes;
  `theme set <role> <tier> <code>` edits one role/tier of the active theme in memory; `theme save
  <name>` / `theme use <name>` persist/switch themes, stored under `~/.arcosh/themes/<name>` as
  plain `RoleName: TrueColor=..., Color256=..., Color16=...` text — a partial custom theme falls
  back to `DefaultTheme`'s own values for anything it doesn't override;
* `profile` shows the settings that would be saved right now; `profile save [name]` persists the
  active theme name, prompt segments, and color policy to `~/.arcosh/config` (always) and,
  if named, a separate snapshot under `~/.arcosh/profiles/<name>` `profile load <name>` can later
  restore; `profile list` lists saved snapshots;
* the on-disk format for both config and themes is deliberately a tiny, non-executable "Key:
  value" line format (`ParseDeclarativeLines`), never ArcoBASIC source run through
  `Runtime.RunString`/`EvalImmediate` — RFC section 22's own explicit rule ("Configuration and
  arbitrary startup code MUST NOT be indistinguishable concepts") ruled that out; an executable
  ArcoBASIC startup file for actual automation is real, disclosed, unattempted future work;
* `~/.arcosh` (or `$ARCOSH_HOME` if set) is only ever created lazily, the first time something is
  actually saved — a plain session that never touches these commands never creates it;
* verified via `--selftest-profile` (config/theme round-tripping, a theme actually written to and
  read back from disk, and prompt-segment rendering, including the two disclosed no-crash
  guarantees: an unrecognized segment/role renders as nothing rather than failing) plus manual
  verification against the real compiled binary under a real pty with `TERM=xterm-256color`/
  `COLORTERM=truecolor` — every theme role previewed in its own distinct, correct color, and a
  saved profile/theme correctly auto-loaded by a brand new session.

Two new general-purpose `arco_runtime` primitives back this: `Directory.Create` (recursive `mkdir
-p`-style creation) and `Directory.List` (enumerate a directory's entries by name), plus
`Host.Hostname()` for the `hostname` prompt segment.

and interactive `theme edit` / `prompt edit` editors on top of the commands above, so designing a
look and feel never requires knowing a raw ANSI SGR code:

* `theme edit` — a menu loop: pick a role by number or name, then pick a color by NAME (red,
  green, yellow, blue, magenta, cyan, white, gray, orange, purple, pink, teal) with an optional
  bold toggle, or `default` to reset that role to `DefaultTheme`'s own value, or `custom` to fall
  back to entering raw per-tier codes directly for full control; every change applies to the
  active theme immediately (a live swatch reprints after each edit) and `s` saves it under a name
  exactly like `theme save` does, refusing to overwrite the built-in `"default"` name;
* `prompt edit` — a menu loop: `add <segment>`, `remove <number>`, `move <number> up|down`, with
  the segment list and a live rendered preview reprinted after every change; `s` persists the
  result as the default for new sessions (the same file `profile save` writes to);
* every mutation the two loops perform (`ApplyNamedColorToRole`, `ResolveRoleSelection`,
  `AddPromptSegment`, `RemovePromptSegmentAt`, `MovePromptSegment`) is a small, pure function with
  no globals or I/O, specifically so `--selftest-editor` can exercise all of them directly; the
  `Console.ReadLine`-driven menu loops themselves are verified manually against the compiled
  binary under a real pty instead, the same split `RunJobControlSelfTest`'s own comment explains
  for a real Ctrl-Z — a full `theme edit` session (pick "error" by name, pick "teal", toggle bold,
  go back, save as a new name) and a full `prompt edit` session (add/remove/reorder segments,
  save) were both run end-to-end against the real binary, confirmed correct in the raw captured
  output AND by reading back the resulting `~/.arcosh/themes/<name>` and `~/.arcosh/config` files.

Building the editors and their self-test also surfaced, and fixed, a second real native-backend
compiler performance regression of the same shape Entry 31 (above) first found and only partly
fixed — see Entry 33 in `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md`.

It does not yet implement plugins or completion — later work packages (WP-009, WP-010).

See `.agents/reports/ARCO_SH_RFC0052_WP000_REPOSITORY_AUDIT.md` (in the repo root's `.agents/`
directory) for the mandatory pre-implementation audit this work was built against.

### New runtime primitives added across these work packages

`arco_runtime` had no way for a hosted ArcoBASIC program to read interactive stdin, query terminal
capability, read/change the current working directory before this RFC. Per the audit's finding that
process/terminal primitives already existed but only inside the retired `arco_shell` library, and
following the existing extraction pattern for `Path.*`/`Process.*` in `src/runtime/runtime.cpp`,
small primitives were added there (colon-path translation itself is pure ArcoBASIC, per RFC section
2.5 — only the host syscalls below needed C++):

* `Console.ReadLine()` -> `{Ok, Text}`, `Ok = FALSE` at EOF
* `Console.Write(text)` -> writes without a trailing newline (PRINT always appends one)
* `Console.IsTTY()` -> bool
* `Path.Cwd()` -> string
* `Directory.Change(path)` -> `{Ok, Error}`
* `Process.Execute(executable, args)` -> `{Ok, Found, ExitCode, Signaled, TermSignal, Error}`
  (WP-004; distinct from the pre-existing `Process.Run`, which shells out via `popen` for a one-shot
  capture with no terminal control — see that function's own comment for why an interactive shell
  needs the real thing instead)
* `Runtime.RunString(code)` -> `{Ok, Error, Exited, ExitCode}` (WP-006; constructs a fresh, isolated
  `arco::Runtime` and calls its own `run_string()` — the same interpreter `tests/unit/
  runtime_tests.cpp` exercises directly — so a hosted/native ArcoBASIC program can dynamically
  compile and execute new ArcoBASIC source, including numbered-line `GOTO` control flow, at
  runtime)
* `Runtime.EvalImmediate(code)` -> `{Ok, Error, Exited, ExitCode}` (WP-006; the opposite lifetime
  from `Runtime.RunString` above — ONE persistent `arco::Runtime` reused for the whole process,
  since `run_string()` never resets its own global variables between calls, so a variable set by
  one call is still visible on the next — backs ArcoSH's own ArcoBASIC immediate-input mode)
* `Process.ExecutePipeline(stages, stdinPath, stdoutPath, appendStdout)` -> an ARRAY of one
  `{Ok, Found, ExitCode, Signaled, TermSignal, Error}` per stage (WP-011; real fork/pipe/`dup2`
  plumbing, one process group for the whole pipeline — see the function's own much larger comment
  in `src/runtime/runtime.cpp` for the job-control design and why it returns one result per stage
  rather than a single combined one; superseded inside arcosh itself by the WP-012 primitives
  below, kept as a simpler one-shot-blocking-run primitive for other ArcoBASIC programs)
* `Process.SetupShellJobControl()` -> `{Ok, Error}` (WP-012; one-time interactive setup — claims
  the controlling terminal and leaves `SIGINT`/`SIGQUIT`/`SIGTSTP`/`SIGTTIN`/`SIGTTOU` ignored for
  the shell's own entire lifetime; a no-op, not an error, without a controlling terminal)
* `Process.StartJob(stages, stdinPath, stdoutPath, appendStdout, foreground)` ->
  `{Ok, Error, Pgid, NotFound}` (WP-012; starts a job's processes — one shared process group,
  identical fd-plumbing to `Process.ExecutePipeline` — WITHOUT waiting for them; `NotFound` is an
  array of `{Executable, Error}` for any stage that failed to exec)
* `Process.WaitJob(pgid, foreground)` -> `{Ok, Error, Stopped, Done, ExitCode, Signaled,
  TermSignal}` (WP-012; blocking wait with `WUNTRACED` stop detection — a real Ctrl-Z during this
  call returns `Stopped: TRUE` without reaping the job, instead of blocking until it fully exits)
* `Process.PollJob(pgid)` -> `{Ok, Changed, Stopped, Done, ExitCode, Signaled, TermSignal}`
  (WP-012; non-blocking `WNOHANG` equivalent, for a background job the shell isn't actively
  waiting on)
* `Process.ContinueJob(pgid, foreground)` -> `{Ok, Error}` (WP-012; sends `SIGCONT` to a job's
  process group — harmless no-op if it wasn't actually stopped — and, if `foreground`, also
  reclaims the terminal for it; backs both `fg` and `bg`)
* `Chr(code)` -> a one-character string (WP-008; classic BASIC `CHR$` — general-purpose, not
  shell-specific, added because the lexer's own string-literal escapes have no way to embed a raw
  ESC byte to build an ANSI SGR sequence)

## Relationship to the previous implementation

The original ArcoSH implementation (`../src/shell/arcosh.cpp`, ~5,900 lines, plus
`../apps/arcosh/main.cpp`) is retired as a product but left in place in the umbrella repo:

* The `arcosh` executable target and its install/packaging/smoke-test wiring have been removed from
  the root build.
* The `arco_shell` static library still compiles internally, purely because
  `../tests/unit/runtime_tests.cpp` exercises `arco::shell::*` directly. It is not installed and no
  executable uses it.
* Nothing here was ported forward from that file. Its C++ logic (fork/exec/job control, ANSI/color
  handling, mods) is a *behavioral* reference for later work packages, not code this project builds
  on directly — RFC-0052 requires those capabilities to become small `arco_runtime` bindings a
  Fission-compiled ArcoBASIC program can call, the same way `Console.*`/`Path.Cwd` were added above.

See `../docs/arcosh.md` for the earlier design vision and `../docs/alpha-known-limitations.md` for
the gaps that earlier implementation actually had. RFC-0052 supersedes both where they conflict.

## Building

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

This locates an already-built `ArcoFission` executable from the umbrella project (via
`../build/ArcoFission`, or `-DARCOFISSION_EXECUTABLE=/path/to/ArcoFission`), builds that project's
opt-in `ArcoNativeRuntimeCoreProbe` target on demand (needed for the host-function bridge below),
and compiles `src/arcosh.abas` straight to **native x86-64 machine code** — no bytecode VM. Build
the umbrella project first if you haven't:

```sh
cmake -S .. -B ../build && cmake --build ../build --target ArcoFission
```

### Native codegen, per RFC-0052 section 6

`arcosh` compiles via `ArcoFission build ... --target linux-x86_64` (ported onto master from
`origin/agent/arcfs-admin-suite` — see `arcology-os/rfcs/RFC-0053_Native_Hosted_ArcoBASIC_
Compilation_and_System_Runtime.md`), not the bytecode-VM capsule format. Getting there required
fixing one real, confirmed miscompilation this project's own interactive loop shape exposed: `!x`
on a dynamically-typed (Boxed) value — e.g. `!line.Ok`, straight out of `Console.ReadLine()` — read
the raw heap-pointer bits of the boxed value directly instead of unboxing it first, an answer that
happened to stay constant across a loop's steady-state allocation pattern and so looked correct
right up until a second host call anywhere in the same loop body shifted that pattern and flipped
it to the other wrong-but-stable answer (EOF silently stopped being detected, spinning forever).
Full root-cause and fix: `.agents/ARCO_NATIVE_RUNTIME_PROGRESS.md` Entries 25 (found) and 26
(fixed). Two narrower, separate gaps in the same backend remain open and are avoided directly in
`src/arcosh.abas`'s own ArcoBASIC style rather than worked around at the codegen level: booleans
can't be compared against `TRUE`/`FALSE` literals (use `!x` instead), and a host-returned bool
crashes if fed directly into `ANDALSO`/`ORELSE` (use nested `IF` instead).

Implementing WP-003's tokenizer found a second, unrelated real miscompilation: `LEN(text)` on a
hosted `AS STRING` parameter took a freestanding-only (UEFI) fast path that treats the argument as
a raw UTF-16 buffer pointer instead of the real Boxed string it is under System V, silently
producing a small wrong length with no crash -- any `WHILE i < LEN(text)`-shaped loop with a host
call in its body would stop after one iteration. Fixed by gating that fast path on the freestanding
convention, which it should always have been. Full write-up: Entry 27.
