# RFC-0052 AP-0052-003: Repository Audit

Audit performed before any WP-001 (Shell Skeleton) coding, per RFC-0052 section 30. No prior
ArcoSH-specific audit report exists in `.agents/reports/` to build on or contradict — see "Prior
audit / arco: coupling" below.

## ArcoBASIC lexer (`src/frontend/lexer.cpp`, 441 lines)

**Existing and reusable.** Standard tokenizer; recognizes numeric line-leading tokens as ordinary
`Number` tokens, nothing line-number-specific baked in here. No changes needed for ArcoSH.

## Parser (`src/frontend/parser.cpp`, 3568 lines)

**Existing and reusable, with a gap.** `Parser::skip_line_number()` strips a leading line-number
token before parsing each statement (used at 4 call sites) and `GOTO` consumes a target line
number (line ~3136). This is enough for classic numbered *script* execution (a `.abas` file with
`10 PRINT ...` / `20 GOTO 10` runs correctly today — confirmed by
`tests/unit/runtime_tests.cpp:173`, `10 PRINT "before"\n20 STOP\n30 PRINT "after"`). It does **not**
provide an interactive resident-program editor (insert/replace/delete by line number, `LIST`,
`RUN`, `NEW`) — that lives entirely in the old shell (see below).

## Line-number handling / resident program (LIST/RUN/NEW)

**Existing but unsuitable.** The only implementation is inline in `src/shell/arcosh.cpp`'s
`repl()` function (`LIST`/`NEW`/`RUN` handling around lines 5744-5790, plus a `numbered_program`
container and `stored_program_source()` helper local to that function). It is real and it works,
but it is not a class, not a library boundary, and not reusable outside that one 5900-line REPL
loop — matching RFC-0052 section 9's claim exactly. **No regression test exercises interactive
line insert/replace/delete at all** (only `runtime_tests.cpp:173` touches numbered-line *script*
execution, not the resident editor). Whoever picks up WP-006 needs to design a standalone
`ResidentProgram` abstraction from scratch, using this code only as a behavioral reference, not a
source to port.

## RFC-0007 (`arcology-os/rfcs/RFC-0007_ArcoBASIC_Interactive_Program_Model.md`)

**Existing but a different target — do not treat as reusable code.** RFC-0007 describes
"Arcology Seed," the **freestanding, bare-metal UEFI boot environment** ("the machine boots
directly into a line-numbered ArcoBASIC environment... READY."), not a hosted Linux program. Per
its own status line, only PRINT and GOTO are implemented there so far (LET/variables, IF,
FOR/NEXT, PEEK/POKE, INSPECT are still unimplemented), and the implementation lives under
`arcology-os/src/` targeting the freestanding x86-64 backend — a completely different code path
from the hosted interpreter arcosh will run on. RFC-0052 section 9 says ArcoSH "should reuse
[RFC-0007's] behavior rather than invent incompatible semantics" — read this as "match the same
*conceptual* Program Mode / Immediate Mode split," not "link against RFC-0007's implementation."
The name "Seed" is used for two different things in this repo (the freestanding RFC-0007
environment, and colloquially for arcosh.cpp's resident-program REPL feature per RFC-0052 section
28) — worth disambiguating explicitly in any WP-006 writeup to avoid confusion.

## Fission Linux hosted build support (`src/compiler/fission.cpp`, `apps/arcofission/`)

**Existing and reusable, with a caveat.** `ArcoFission build x.bas -o out` on Linux today produces
a real, standalone ELF64 executable (confirmed via `apps/arcofission/main.cpp` usage text and
README.md). However, per that same usage text, the generated ELF64 "embeds the prepared bytecode
and links it against the ArcoFission runtime" — i.e. it's a self-contained bytecode-VM capsule
(interpreted at run time), not ahead-of-time compiled native machine code the way the UEFI target
is (lexer -> parser -> A-MIR -> real x86-64 encoding -> PE32+). For WP-001's purposes ("ArcoBASIC
source -> Fission Compiler Substrate -> native hosted executable") this is good enough — it yields
one binary, no external interpreter needed — but don't describe it in docs as "compiled to native
machine code"; it is not, yet.

## Hosted filesystem / path APIs (`src/runtime/runtime.cpp`)

**Existing and reusable.** `File.Exists/ReadText/WriteText/AppendText/ReadBytes/WriteBytes` and
`Path.Join/Home/BaseName/DirName/Extension` are all real, in `arco_runtime` (not shell-specific),
and available to any hosted ArcoBASIC program including a Fission-compiled arcosh. Good foundation
for WP-002.

## Process APIs (RUN, Process.*, System.Launch)

**Existing but unsuitable — wrong library boundary.** The real process primitives — `fork()`,
`waitpid()`, `popen()`, job-control process groups, `RUN`, `System.Launch`, `Process.List/Exists/Kill`
— are all implemented in `src/shell/arcosh.cpp` (`arco_shell`), not `arco_runtime`. `arco_runtime`
itself only exposes a thin `Process.Run` (single string execution, no structured spawn/wait/pipe
control) and `Process.Env` (read-only single-var getenv). This is the single biggest gap for
WP-004: none of `Process.Spawn/Wait/ExitStatus/Signal`, `Pipe.Create`, or `FD.Duplicate` exist as
`arco_runtime` primitives today. The fork/exec/waitpid logic in arcosh.cpp is a working reference
for correctness (signal handling, process-group semantics for job control are already solved
problems there) but needs to be extracted into small `arco_runtime` bindings per RFC-0052 section
6 — "Shell policy MUST NOT migrate into C++ merely because a binding is required" cuts the other
way too: these *are* legitimately low-level primitives that belong in `arco_runtime`, not
re-implemented in ArcoBASIC.

## Environment APIs (ENV, Environment.Get/Set)

**Existing but partial.** Reads exist in `arco_runtime` (`Process.Env` at runtime.cpp:2747, plus
ad hoc `getenv` calls for `HOME`/`USERPROFILE`/`ARCOBASIC_STDLIB`). Mutation (`export`, `unset`,
`setenv()`) exists only in `arco_shell` (arcosh.cpp `env`/`export`/`unset` builtins). No
`Environment.Set` exists at the runtime layer. Missing piece for WP-004/WP-002.

## Terminal APIs (capability detection, color/ANSI, TUI)

**Existing but unsuitable — wrong library boundary.** All of it (77 references to TUI/Color/ANSI
handling) lives in `arco_shell` only; zero equivalent exists in `arco_runtime`. This includes the
`Display`-shaped primitives RFC-0052 section 17 wants (box drawing, themed rules, status/badge/
progress rendering, color enable/disable) — the *behavior* already exists and is tested
(`tests/unit/runtime_tests.cpp`'s `TUI.*`/`Color.*` assertions), but as shell-internal C++
functions, not a reusable `Display.*`/`Terminal.*` runtime binding. Same extraction problem as
Process APIs.

## Module/plugin facilities (ArcoSH mods)

**Existing but a different shape.** The current mods system (`arcosh_mods_directory()`,
`Mod.Install/Activate/Deactivate/List/Load`, persisted via `~/.arcosh/mods/enabled.txt`) is a flat
"install and activate an `.abas` file" model with no typed capability registration. RFC-0052
section 19 wants plugins to register into explicit typed capability classes (`command`,
`completion`, `prompt.segment`, `theme`, etc.) with enumeration and isolated-failure recovery. The
existing mods system is a reasonable *loading/persistence* mechanism to reuse (directory
convention, enabled-list persistence) but the registration/capability model needs to be built new
for WP-009 — don't treat `Mod.*` as already satisfying the plugin RFC.

## Structured error support (TRY/CATCH/THROW)

**Existing and reusable.** Fully implemented per RFC-0022 (already shipped: lexer, parser,
canonical AST, A-MIR, bytecode, hosted VM, native capsule path all handle `THROW`; caught
source-defined errors get `Type = "UserError"`, runtime failures get `Type = "RuntimeError"`). No
gap here — WP-005's structured history `resultKind` classification (COMMAND_NOT_FOUND,
PERMISSION_DENIED, etc.) can build directly on this.

## Path utilities / Arcology colon-path translation

**Missing — confirmed gap, plus one easy-to-confuse near-miss.** There is no `Path.IsArcology`,
`Path.ToHost`, `Path.ToArcology`, or `Path.Normalize` anywhere in the repository. A colon-path
parser *does* exist — `split_colon_path()` / `Volume::resolve_colon_path()` in
`src/arcfs/host.cpp` / `include/arco/arcfs/host.hpp` — but it resolves a colon path to an **ArcFS
on-disk object ID inside a mounted ArcFS volume image**, not to a Linux filesystem path. Same
surface syntax (`:home:user:file.txt`), completely different resolution target. Do not reuse this
code directly; at most mirror its segment-splitting convention for consistency. WP-002 starts from
zero on the actual Linux-path-mapping logic.

## Numbered-source tests

**Missing (for the interactive/resident behavior).** `tests/unit/runtime_tests.cpp:173` is the
only test touching numbered BASIC lines at all, and it only exercises script-level `GOTO`/`STOP`
semantics, not interactive line insert/replace/delete, `LIST`, `RUN`, or `NEW`. WP-006 starts with
zero regression coverage to extend or preserve — write it new against the acceptance criteria in
RFC-0052 section 27, not against arcosh.cpp's current behavior.

## Prior audit / "arco:" protocol coupling (RFC-0052 section 28)

**Not found in this repository.** Searched `.agents/reports/` for any file mentioning "Arcology
Terminal," "terminal protocol," or "arco: protocol" — no matches, and no report file is named for
ArcoSH/shell specifically. The "September repository audit" RFC-0052 references either was not
committed to this repo, was lost in the crash this project is recovering from, or existed outside
version control. Treat RFC-0052 section 28 as the authoritative record of that prior coupling;
there is nothing further to reconcile it against in-repo.

## Summary for whoever picks up WP-001

Fission can produce a real, standalone Linux executable from ArcoBASIC source today (bytecode
capsule, not AOT native codegen — good enough to start). `arco_runtime` already has solid file/path
read primitives and full TRY/CATCH. The real work for WP-001 through WP-004 is extraction, not
invention: process spawning, terminal/color handling, and environment mutation are all *already
solved* in `src/shell/arcosh.cpp`, just trapped in the wrong (retired) library. The path forward is
lifting those primitives into small `arco_runtime` bindings (Process.Spawn/Wait/Signal,
Pipe.Create, Display.*/Terminal.*, Environment.Set) that a Fission-compiled ArcoBASIC `arcosh` can
call — not reimplementing fork/exec or ANSI handling from scratch, and not linking the retired
`arco_shell` library into the new project. Nothing found here rises to an AP-0052-018
stop-and-report condition.
