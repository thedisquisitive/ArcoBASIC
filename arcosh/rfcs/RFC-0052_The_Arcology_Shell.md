# RFC-0052: The Arcology Shell

**RFC Number:** RFC-0052
**Title:** The Arcology Shell
**Status:** Draft
**Category:** User Environment / Shell / Linux Interoperability
**Authors:** Arcology Project

---

# 1. Executive Summary

This RFC defines **The Arcology Shell**, normally invoked as `arcosh`.

ArcoSH is a general-purpose command shell written primarily in ArcoBASIC. Its first production target is Linux, where it MUST operate correctly inside ordinary terminal emulators without requiring any Arcology-specific terminal application.

ArcoSH SHALL provide:

* ordinary Linux command execution;
* Arcology-style filesystem paths such as `:home:user:Desktop:`;
* normal Linux path compatibility;
* pipelines and redirection;
* environment variables;
* history;
* job control;
* command completion;
* color and semantic theming;
* plugins;
* numbered and unnumbered ArcoBASIC scripting;
* an interactive numbered ArcoBASIC resident program;
* the `oops` command-correction facility.

ArcoSH SHALL use **ArcoBASIC as its programming and automation language** rather than inventing a separate shell scripting language.

The future **Arcology Terminal** is a separate project. It will behave like a conventional terminal emulator while optionally supporting the future enhanced `arco:` protocol for graphics, sound, richer display primitives, and related facilities.

ArcoSH MUST NOT require The Arcology Terminal.

---

# 2. Design Principles

ArcoSH follows five primary design rules.

## 2.1 The Shell Is Not the Terminal

ArcoSH owns:

* commands;
* parsing;
* execution;
* jobs;
* paths;
* scripting;
* history;
* completion;
* plugins;
* shell-facing presentation semantics.

The terminal emulator owns:

* terminal windows;
* text rendering;
* fonts;
* PTYs;
* tabs and panes;
* graphical terminal capabilities;
* sound;
* pointer handling;
* future `arco:` protocol rendering.

Therefore:

```text
Konsole / xterm / Kitty / foot / Linux VT / SSH / tmux
                         |
                      PTY/TTY
                         |
                         v
                      ArcoSH
```

And eventually:

```text
The Arcology Terminal
  + VT / ANSI compatibility
  + optional arco: extensions
             |
          PTY/TTY
             |
             v
          ArcoSH
```

The ordinary ArcoSH implementation MUST remain useful with no Arcology Terminal installed.

## 2.2 ArcoBASIC Is the Shell Programming Language

ArcoSH SHALL NOT create a second programming language for shell automation.

Interactive shell syntax may remain concise:

```text
copy :home:user:file.txt :home:user:Backup:
```

but programmable automation uses ordinary ArcoBASIC:

```basic
source = ":home:user:file.txt"
destination = ":home:user:Backup:"

IF FILE.Exists(source) THEN
    FILE.Copy(source, destination)
END IF
```

Classic numbered source remains equally valid:

```basic
10 source = ":home:user:file.txt"
20 IF FILE.Exists(source) THEN FILE.Copy(source, ":home:user:Backup:")
30 PRINT "DONE"
```

This builds directly on ArcoBASIC's existing dual modern/classic source model rather than defining another dialect.

## 2.3 Commands Are Structured Objects

ArcoSH SHALL retain a parsed representation of interactive commands.

History is not merely:

```text
"git status"
```

It is conceptually:

```text
CommandRecord
    sourceText
    executable
    arguments[]
    redirections[]
    pipelineStages[]
    workingDirectory
    result
    exitStatus
```

This architecture enables `oops`, richer history, better completion, alternate front ends, and future structured command interfaces.

## 2.4 Extensions Register Capabilities

Plugins SHOULD register explicit extension capabilities rather than obtaining arbitrary access to internal shell state.

Examples:

```text
command
completion
prompt.segment
theme
path.provider
history.provider
script.library
event.handler
```

## 2.5 Host-Specific Mechanics Stay Below Shell Policy

Linux-specific process and terminal mechanics MAY require hosted runtime bindings.

Those bindings SHOULD provide small primitives.

Shell behavior, policy, parsing, plugin management, path handling, history and theming SHOULD remain in ArcoBASIC.

---

# 3. Goals

ArcoSH SHALL:

1. function as a practical Linux interactive shell;
2. work inside ordinary terminal emulators;
3. work through SSH and compatible terminal multiplexers;
4. be implemented primarily in ArcoBASIC;
5. compile using the Fission Compiler Substrate or Fission G0 during bootstrap;
6. support Arcology colon-delimited paths;
7. continue supporting normal Linux paths;
8. execute ordinary Linux software;
9. support pipelines and redirection;
10. support job control;
11. use ArcoBASIC for scripting;
12. execute both numbered and unnumbered ArcoBASIC files;
13. support interactive numbered ArcoBASIC program entry;
14. provide color with graceful degradation;
15. provide themes;
16. support plugins;
17. maintain structured history;
18. implement `oops`;
19. remain independent of The Arcology Terminal;
20. provide a clean path toward future Arcology OS hosting.

---

# 4. Non-Goals

This RFC does not define:

* The Arcology Terminal;
* the `arco:` protocol;
* sprite rendering;
* graphics surfaces;
* sound playback;
* graphical terminal widgets;
* terminal tabs or panes;
* Bash compatibility;
* complete Bash/Zsh syntax emulation;
* ArcFS internals;
* an extension marketplace.

The future Arcology Terminal and `arco:` protocol SHALL receive their own RFC.

---

# 5. Three Modern Shell Problems ArcoSH Intends to Solve

## 5.1 Shell Scripts Become Their Own Programming Ecosystem

Traditional shell languages contain specialized rules for:

* word splitting;
* expansion;
* quoting;
* substitution;
* globbing;
* conditionals;
* arrays;
* subshells;
* exit-status semantics.

Eventually many shell programs become complex enough that users rewrite them in another language.

ArcoSH removes this language cliff.

Interactive commands remain concise, while nontrivial automation is simply ArcoBASIC.

There is no point where an ArcoSH automation project has to stop being a "shell script" and become a "real program."

It was already a real program.

---

## 5.2 Shell Configuration Becomes an Uninspectable Startup Program

Modern Unix environments often accumulate combinations of:

```text
.profile
.bashrc
.bash_profile
.zshrc
.zprofile
/etc/profile
framework initialization
prompt scripts
environment hooks
plugin loaders
```

ArcoSH SHOULD separate:

```text
configuration
themes
plugins
startup scripts
```

Suggested layout:

```text
:home:user:.arcosh:
    config.abconf
    plugins:
    themes:
    scripts:
```

Configuration SHOULD be declarative.

Executable startup automation SHALL be explicitly identifiable as executable ArcoBASIC.

A user SHOULD be able to determine exactly which plugins are loaded and what each contributes.

---

## 5.3 History Commonly Stores Text Instead of Intent

Traditional shell history generally records command strings.

ArcoSH SHALL preserve parsed command structure.

This enables safer and more useful command correction.

Example:

```text
> gti commit -m "Fix path handling"
ArcoSH: command not found: gti

> oops git
```

ArcoSH executes the structured equivalent of:

```text
git commit -m "Fix path handling"
```

without reconstructing or globally replacing text.

---

# 6. Implementation Language

ArcoSH SHALL be written primarily in ArcoBASIC.

Preferred build path:

```text
ArcoBASIC source
      |
      v
Fission Compiler Substrate
      |
      v
native hosted executable
```

Fission G0 MAY be used during bootstrap.

The repository already contains a hosted Fission path capable of building Linux executables from ArcoBASIC source, though the implementation agent MUST audit the current native/hosted state rather than assume older documentation is current.

Where required hosted primitives do not yet exist, the preferred solution is a small reusable runtime binding.

Potential primitives include:

```text
Process.Spawn
Process.Wait
Process.ExitStatus
Process.Signal
Pipe.Create
FD.Duplicate
Environment.Get
Environment.Set
Directory.Change
Terminal.QueryCapabilities
Terminal.GetMode
Terminal.SetMode
```

Shell policy MUST NOT migrate into C++ merely because a binding is required.

---

# 7. Arcology Path Model

## 7.1 Absolute Arcology Paths

A leading colon denotes an absolute Arcology path.

```text
:
:home:
:home:user:
:home:user:Desktop:
:home:user:Desktop:file.txt
```

On Linux:

```text
:                         -> /
:home:                    -> /home/
:home:user:               -> /home/user/
:home:user:Desktop:       -> /home/user/Desktop/
```

## 7.2 Linux Compatibility

Ordinary Linux paths remain valid:

```text
/usr/bin
./build
../source
/home/user/file.txt
```

Arcology path support extends Linux shell behavior; it does not replace host-native paths.

## 7.3 Centralized Resolution

Path conversion MUST be implemented through one path service.

Conceptual API:

```basic
Path.IsArcology(path)
Path.ToHost(path)
Path.ToArcology(path)
Path.Normalize(path)
Path.Join(base, child)
```

Built-ins MUST NOT each implement colon-path parsing independently.

## 7.4 Logical and Host Current Directories

ArcoSH SHOULD retain:

```text
Logical path: :home:daedalus:Projects:Arcology:
Host path:    /home/daedalus/Projects/Arcology/
```

The prompt may display the logical Arcology path while Linux processes receive the host path.

---

# 8. Input Classification

Interactive input SHALL resolve deterministically into one of these categories:

```text
Shell command
ArcoBASIC immediate input
Numbered ArcoBASIC program line
Shell built-in/control command
```

Example numbered program input:

```basic
10 PRINT "HELLO"
20 GOTO 10
```

The existing ArcoBASIC interactive model already establishes numbered Program Mode and unnumbered Immediate Mode concepts.

ArcoSH SHOULD reuse that behavior rather than invent incompatible semantics.

---

# 9. Resident ArcoBASIC Program

ArcoSH SHALL maintain an interactive resident program.

Required behavior:

```text
10 PRINT "HELLO"
20 GOTO 10
LIST
RUN
NEW
```

Rules:

* new numbered line -> insert;
* existing numbered line -> replace;
* bare line number -> delete;
* lines remain numerically ordered.

Example:

```text
10 PRINT "ONE"
20 PRINT "TWO"

20 PRINT "THREE"
```

results in:

```text
10 PRINT "ONE"
20 PRINT "THREE"
```

Then:

```text
20
```

deletes line 20.

A reusable resident-program abstraction SHOULD be implemented rather than copying the old monolithic Seed implementation. The prior repository audit specifically found that the existing Seed implementation is real but not reusable as a general `ResidentProgram` component.

---

# 10. Script Execution

ArcoSH SHALL execute both:

```text
modern free-form ArcoBASIC
classic numbered ArcoBASIC
```

Examples:

```basic
PRINT "HELLO"
FOR i = 1 TO 10
    PRINT i
NEXT
```

and:

```basic
10 PRINT "HELLO"
20 GOTO 10
```

ArcoSH MUST NOT require numbered programs to be converted into modern format.

ArcoSH MUST NOT require modern programs to acquire line numbers.

The canonical ArcoBASIC parser/compiler SHALL be reused.

---

# 11. Shell Command Representation

After parsing, shell commands SHOULD be represented structurally.

Conceptually:

```text
Command
    executable
    arguments[]
    redirections[]
    environmentAssignments[]
```

Pipelines contain commands:

```text
Pipeline
    commands[]
    background
```

Example:

```text
cat :home:user:file.txt | grep ERROR > :home:user:errors.txt
```

becomes approximately:

```text
Pipeline
    Command
        executable = cat
        argument = :home:user:file.txt

    Command
        executable = grep
        argument = ERROR
        stdout -> :home:user:errors.txt
```

Execution SHOULD use this parsed representation rather than reparsing the input string.

---

# 12. Command Resolution

Recommended resolution order:

1. protected ArcoSH built-ins;
2. registered plugin commands;
3. registered Arcology commands;
4. executable ArcoBASIC scripts;
5. explicit executable paths;
6. executables discovered through `$PATH`.

Exact precedence SHALL be documented and tested.

Plugins MUST NOT silently replace protected shell commands.

---

# 13. Linux Process Execution

ArcoSH SHALL eventually support normal Unix shell behavior including:

```text
stdin
stdout
stderr
exit status
pipes
input redirection
output redirection
append redirection
foreground jobs
background jobs
signals
environment inheritance
working directories
```

Examples that MUST ultimately work:

```text
git status
gcc main.c -o program
cat file.txt | grep ERROR
program > output.txt
program >> output.txt
program < input.txt
```

---

# 14. Structured History

History SHOULD retain both original input and parsed information.

Minimum record:

```text
HistoryEntry
    sourceText
    executable
    arguments[]
    workingDirectory
    timestamp
    resultKind
    exitStatus
```

Potential result kinds include:

```text
SUCCESS
COMMAND_NOT_FOUND
PERMISSION_DENIED
PATH_NOT_FOUND
INVALID_SYNTAX
PROCESS_FAILED
PLUGIN_FAILED
SCRIPT_FAILED
```

Persisted history MUST NOT require serializing transient process handles.

---

# 15. `oops`

## 15.1 Basic Behavior

If a command fails because its executable cannot be resolved:

```text
> gti status
ArcoSH: command not found: gti
```

then:

```text
> oops git
```

executes:

```text
git status
```

## 15.2 Structural Replacement

`oops` replaces only the executable field.

It MUST NOT perform blind text replacement.

Example:

```text
> gti commit -m "documentation says gti here intentionally"
> oops git
```

must preserve:

```text
"documentation says gti here intentionally"
```

unchanged.

## 15.3 Version 1 Rules

`oops`:

* applies only to the immediately preceding command;
* requires that command to have failed with `COMMAND_NOT_FOUND`;
* preserves arguments;
* preserves quoting semantics;
* preserves redirections;
* preserves pipeline structure;
* clearly shows what command is being retried.

Future extensions may allow:

```text
oops 3 git
oops arg 2 something
```

but those are outside version 1.

---

# 16. Terminal Compatibility

ArcoSH MUST work under ordinary terminals.

Target environments include:

```text
Linux VT
xterm
Konsole
GNOME Terminal
Kitty
Alacritty
foot
tmux
screen
SSH sessions
```

The shell SHOULD support, when available:

```text
UTF-8
16 colors
256 colors
24-bit truecolor
bold
dim
underline
reverse video
cursor positioning
screen/line clearing
terminal title setting
```

Every feature MUST degrade gracefully.

A no-color environment MUST remain completely usable.

Information MUST NOT be conveyed by color alone.

---

# 17. Display Abstraction

Shell components SHOULD NOT emit arbitrary ANSI sequences directly.

Instead:

```basic
Display.Text("Build complete")
Display.Style("success")
Display.Line("Finished")
Display.Reset()
```

Semantic style roles SHOULD include:

```text
normal
prompt
path
success
warning
error
muted
selection
```

The active terminal renderer maps those roles onto:

```text
truecolor
256-color
16-color
plain text
```

as appropriate.

This abstraction is intentionally text-terminal focused.

It is not the future `arco:` graphics protocol.

---

# 18. Themes

Themes SHALL customize semantic presentation.

A theme MAY specify:

* foregrounds;
* backgrounds;
* emphasis;
* prompt appearance;
* error appearance;
* warning appearance;
* selection appearance.

Themes SHOULD provide fallback mappings for:

```text
24-bit
256-color
16-color
no-color
```

Themes MUST NOT automatically receive general process execution capability.

---

# 19. Plugin System

## 19.1 Capability Registration

Plugins extend explicit interfaces.

Initial capability classes SHOULD include:

```text
command
completion
prompt.segment
theme
path.provider
event.handler
script.library
```

Conceptual API:

```basic
Shell.RegisterCommand(...)
Shell.RegisterCompleter(...)
Shell.RegisterPromptSegment(...)
Shell.RegisterTheme(...)
Shell.RegisterPathProvider(...)
Shell.RegisterEventHandler(...)
```

The precise API SHALL be proven by implementation before being frozen.

## 19.2 Inspectability

Users SHALL be able to inspect plugins.

Example:

```text
> plugins

GitTools
  command.gitstatus
  completion.git
  prompt.segment.git

Neon
  theme.neon
```

## 19.3 Failure Recovery

A broken optional plugin SHOULD NOT make the shell unrecoverable.

ArcoSH SHALL provide a startup method that disables optional/user plugins.

Plugin load failures SHALL produce clear diagnostics.

## 19.4 Trust

Linux-hosted ArcoSH MUST NOT claim capabilities are security enforcement if the operating system is not actually enforcing them.

Capability metadata still matters for:

* inspection;
* API boundaries;
* future Arcology enforcement;
* preventing data-only components such as themes from automatically becoming arbitrary code.

---

# 20. Prompt Architecture

The prompt SHOULD be composed from segments.

Potential built-ins:

```text
hostname
current Arcology path
previous exit status
background job count
```

Plugins may provide additional segments such as Git state.

Example:

```text
atrium :home:daedalus:Projects:Arcology:>
```

---

# 21. Completion

Completion SHOULD support:

```text
built-ins
$PATH executables
plugin commands
Arcology paths
Linux paths
variables
command-specific completion providers
```

Completion providers SHOULD return structured candidates.

---

# 22. Configuration

Configuration SHOULD primarily be declarative.

Potential settings:

```text
theme
history size
color policy
path display mode
enabled plugins
prompt segments
completion behavior
```

Executable startup automation SHOULD live in an explicitly executable ArcoBASIC startup file.

Configuration and arbitrary startup code MUST NOT be indistinguishable concepts.

---

# 23. Job Control

Production Linux ArcoSH SHALL eventually support proper POSIX job control:

```text
foreground process groups
background jobs
jobs
fg
bg
suspend/resume
signal forwarding
terminal foreground ownership
```

Background execution MUST NOT be faked merely by spawning detached child processes.

---

# 24. Arcology Terminal Boundary

The Arcology Terminal is a separate terminal emulator application.

Its responsibilities may eventually include:

```text
VT/ANSI compatibility
arco: protocol
graphics
tile/sprite surfaces
images
sound
rich pointer input
semantic screen regions
advanced terminal UI facilities
```

ArcoSH SHALL NOT implement those responsibilities.

A future ArcoSH integration MAY detect enhanced capabilities and expose them to applications or plugins.

However:

* ordinary terminal operation remains mandatory;
* enhanced capabilities remain optional;
* SSH MUST remain useful;
* scripts MUST NOT require the enhanced terminal unless explicitly written for it.

---

# 25. Suggested Internal Architecture

```text
+---------------------------------------------------+
|                     ArcoSH                        |
|                                                   |
| Input                                             |
|   |                                               |
|   v                                               |
| Tokenizer / Parser                                |
|   |                                               |
|   +------> Numbered ArcoBASIC ----------------+   |
|   |                                          |   |
|   +------> Immediate ArcoBASIC               |   |
|   |                                          |   |
|   +------> Shell Command AST                  |   |
|                    |                         |   |
|                    v                         |   |
|              Path Resolver                   |   |
|                    |                         |   |
|                    v                         |   |
|             Command Resolver                 |   |
|                    |                         |   |
|                    v                         |   |
|             Executor / Jobs                  |   |
|                                              |   |
| History   Plugins   Themes   Completion      |   |
| Prompt    Display   Configuration            |   |
+----------------------+----------------------------+
                       |
                       v
              Hosted runtime bindings
                       |
                       v
                 Linux / POSIX
```

---

# 26. Initial Production Scope

The first useful Linux release SHOULD include:

1. ArcoBASIC-authored `arcosh`;
2. interactive shell loop;
3. prompt;
4. Linux command execution;
5. Arcology path translation;
6. Linux path compatibility;
7. `cd`;
8. environment variables;
9. structured history;
10. `oops`;
11. pipelines;
12. basic redirection;
13. color capability detection;
14. semantic display styles;
15. themes;
16. numbered ArcoBASIC resident program;
17. numbered ArcoBASIC file execution;
18. modern ArcoBASIC file execution;
19. plugin foundation;
20. completion;
21. safe startup without user plugins.

Advanced job control may land incrementally but remains required before calling ArcoSH a mature Linux login-shell replacement.

---

# 27. Acceptance Criteria

The Linux implementation is accepted when it demonstrates:

* launch under at least two unrelated standard terminal emulators;
* readable operation with color disabled;
* execution of normal `$PATH` binaries;
* `cd :home:<user>:` resolving correctly;
* `/home/<user>` remaining valid;
* working pipelines;
* working input/output redirection;
* recorded child exit status;
* `oops` correctly replacing only a missing executable;
* quoted argument contents remaining untouched by `oops`;
* interactive numbered ArcoBASIC insertion;
* numbered-line replacement;
* numbered-line deletion;
* `LIST`;
* `RUN`;
* `NEW`;
* unnumbered script execution;
* numbered script execution;
* at least one theme;
* reduced-color theme fallback;
* at least one plugin;
* plugin capability enumeration;
* recovery from a deliberately broken plugin;
* no dependency on The Arcology Terminal.

---

# 28. Relationship to Earlier ArcoSH Work

Earlier ArcoSH work is historical reference only where it conflicts with this RFC.

A September repository audit previously framed the mission as both an ArcoSH reimplementation **and** an `arco:` semantic terminal protocol.

That coupling is superseded.

This RFC establishes:

```text
ArcoSH          = shell
Arcology Terminal = terminal emulator
arco:           = future enhanced terminal protocol
```

The same earlier audit also identified that the existing numbered ArcoBASIC Seed implementation is real but monolithic and not available as a reusable resident-program abstraction.

The implementation SHOULD extract the reusable concept without copying that implementation wholesale.

---

# 29. Future RFCs

Expected follow-up work includes:

* The Arcology Terminal;
* the `arco:` semantic terminal protocol;
* persistent structured command history;
* richer command-object dataflow;
* Arcology OS ArcoSH hosting;
* enforced plugin capabilities;
* Arcology remote-shell/session contracts.

---

# 30. Agent Packet

This section is the implementation packet for coding agents.

It SHALL remain part of this RFC.

It SHALL NOT be moved into a separate agent-packet document.

## AP-0052-001 — Mission

Implement the first production-capable Linux version of The Arcology Shell defined by RFC-0052.

ArcoSH MUST remain a shell.

Do not implement The Arcology Terminal.

Do not implement the enhanced `arco:` protocol.

---

## AP-0052-002 — Mandatory Constraints

The implementation agent SHALL:

1. implement shell policy primarily in ArcoBASIC;
2. use Fission Substrate when possible;
3. use Fission G0 where necessary for bootstrap;
4. reuse the canonical ArcoBASIC parser/compiler;
5. preserve numbered and unnumbered ArcoBASIC;
6. centralize path translation;
7. retain structured commands after parsing;
8. centralize terminal styling;
9. make plugins register declared extension capabilities;
10. keep Linux host bindings small and policy-free.

The implementation agent SHALL NOT:

* turn ArcoSH into a terminal emulator;
* depend on The Arcology Terminal;
* implement `arco:` as part of this packet;
* introduce a shell-specific ArcoBASIC parser;
* require all ArcoBASIC to have line numbers;
* prohibit classic numbered source;
* remove Linux path support;
* implement `oops` as string replacement;
* give themes arbitrary execution privileges by default;
* claim Linux sandbox enforcement that does not exist.

---

## AP-0052-003 — Repository Audit

Before coding, inspect the current repository state for:

* ArcoBASIC lexer;
* parser;
* line-number handling;
* RFC-0007 behavior;
* Fission Linux hosted build support;
* hosted filesystem APIs;
* process APIs;
* environment APIs;
* terminal APIs;
* module/plugin facilities;
* structured error support;
* path utilities;
* numbered-source tests.

Write:

```text
.agents/reports/ARCO_SH_RFC0052_WP000_REPOSITORY_AUDIT.md
```

The audit must distinguish:

```text
existing and reusable
existing but unsuitable
missing
blocked
```

Do not assume earlier ArcoSH audits remain current.

---

## AP-0052-004 — WP-001 Shell Skeleton

Implement:

* `arcosh` executable;
* interactive loop;
* basic prompt;
* clean exit;
* current directory;
* diagnostic output;
* terminal capability query;
* explicit no-color mode.

Proof:

* launch in two unrelated standard terminal emulators;
* launch without Arcology Terminal;
* operate safely in reduced terminal capability mode.

---

## AP-0052-005 — WP-002 Path Layer

Implement centralized translation.

Required:

```text
:                       -> /
:home:                  -> /home/
:home:user:             -> /home/user/
:home:user:Desktop:     -> /home/user/Desktop/
```

Test:

* root;
* trailing colon;
* spaces;
* quoted paths;
* file paths;
* dot components;
* Linux path passthrough;
* malformed paths.

No built-in may contain its own independent Arcology path parser.

---

## AP-0052-006 — WP-003 Structured Command Parser

Implement structures for at least:

```text
executable
arguments
redirections
pipeline stages
background flag
original source
```

Execution MUST operate from parsed structures.

Keep source text only for display/history.

---

## AP-0052-007 — WP-004 Linux Process Layer

Implement or expose the minimum host primitives necessary for:

```text
spawn
exec
wait
exit status
pipes
fd duplication
stdin/stdout/stderr routing
environment inheritance
working directory
signals
```

POSIX implementation code may use C/C++ where unavoidable.

Shell policy remains ArcoBASIC.

---

## AP-0052-008 — WP-005 History and `oops`

Implement structured history.

Minimum stored fields:

```text
source text
command
arguments
working directory
result classification
exit status
```

Implement:

```text
oops <correct-command>
```

Required regression test:

```text
gti commit -m "gti must remain inside this message"
oops git
```

The resulting command must semantically equal:

```text
git commit -m "gti must remain inside this message"
```

No global textual replacement is permitted.

---

## AP-0052-009 — WP-006 Interactive ArcoBASIC

Implement reusable resident-program support.

Required:

```text
10 PRINT "HELLO"
20 GOTO 10
LIST
RUN
NEW
```

Also prove:

* replacement of numbered line;
* deletion by bare line number;
* numerical ordering;
* immediate ArcoBASIC input where supported.

Do not copy the old Seed console wholesale.

---

## AP-0052-010 — WP-007 Script Execution

Prove execution of:

```text
numbered .abas source
unnumbered .abas source
mixed source accepted by canonical compiler rules
```

Do not preprocess numbered source into a different language.

Use the canonical compiler/parser path.

---

## AP-0052-011 — WP-008 Display and Themes

Implement semantic shell styling.

Required roles:

```text
normal
prompt
path
success
warning
error
muted
selection
```

Provide graceful handling for:

```text
truecolor
256-color
16-color
no-color
```

Create at least one bundled theme proving the interface.

All errors must remain understandable in monochrome output.

---

## AP-0052-012 — WP-009 Plugin System

Implement first-version plugin loading.

Initial capability types:

```text
command
completion
prompt.segment
theme
```

Required features:

* discovery;
* enable/disable;
* deterministic load order;
* capability enumeration;
* plugin diagnostics;
* recovery launch with optional plugins disabled.

Provide at least one proof plugin.

Do not create a marketplace.

---

## AP-0052-013 — WP-010 Completion

Initial completion providers:

* built-ins;
* `$PATH` executables;
* Arcology paths;
* Linux paths;
* plugin commands.

Providers SHOULD emit structured candidate data.

---

## AP-0052-014 — WP-011 Pipelines and Redirection

Prove real behavior equivalent to:

```text
printf ... | grep ...
program > file
program >> file
program < file
```

Tests MUST inspect actual execution/output behavior.

Parser-only tests are insufficient.

---

## AP-0052-015 — WP-012 Job Control

Research and document POSIX process-group and controlling-terminal semantics before implementation.

Target:

```text
command &
jobs
fg
bg
suspend/resume
foreground process groups
signal forwarding
terminal ownership restoration
```

Do not fake job control using detached child processes.

---

## AP-0052-016 — Testing Rules

Every work package must introduce focused regression coverage.

Minimum areas:

* path translation;
* parsing;
* quoting;
* resolution;
* process execution;
* exit status;
* history;
* `oops`;
* numbered ArcoBASIC;
* modern ArcoBASIC;
* plugin loading;
* plugin failure;
* theme fallback;
* pipelines;
* redirection.

Compilation alone is not proof.

Use real behavior tests wherever practical.

---

## AP-0052-017 — Documentation

Document features as they become real.

User documentation must eventually explain:

* launching ArcoSH;
* Arcology paths;
* Linux paths;
* scripting;
* numbered programming;
* `oops`;
* themes;
* plugins;
* recovery mode;
* known limitations.

Do not describe planned functionality as implemented functionality.

---

## AP-0052-018 — Stop-and-Report Conditions

Stop the affected work package and report rather than silently redesigning the system if:

1. required behavior needs an incompatible ArcoBASIC language change;
2. implementation appears to require Arcology Terminal;
3. Fission cannot compile the required hosted ArcoBASIC subset;
4. a supposedly small runtime primitive actually requires a major compiler backend;
5. current repository architecture materially contradicts this RFC;
6. a proposed Linux security guarantee cannot actually be enforced.

---

## AP-0052-019 — Completion Report

When implementation reaches the RFC acceptance boundary, create:

```text
.agents/reports/ARCO_SH_RFC0052_IMPLEMENTATION_STATUS.md
```

The report must identify:

```text
implemented
partially implemented
deferred
blocked
known bugs
runtime changes
compiler changes
tests executed
terminal emulators tested
```

It MUST explicitly demonstrate that The Arcology Terminal is not required.

---

# 31. Final Architectural Rule

**ArcoSH owns command semantics.**

**ArcoBASIC owns shell programming.**

**The terminal emulator owns terminal presentation.**

**Plugins extend declared shell interfaces.**

None of those layers should need to impersonate the others.
