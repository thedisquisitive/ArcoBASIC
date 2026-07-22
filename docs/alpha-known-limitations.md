# ArcoSH Alpha Known Limitations

ArcoSH is usable for alpha testing, scripting, and profile experimentation, but it is not a complete POSIX shell.

Known alpha gaps:

* Job control tracks background jobs and supports `jobs`, `fg`, `bg`, `kill`, and `disown`; foreground terminal handoff exists for interactive commands, but Ctrl-Z suspension is not complete.
* Bare host commands use the host shell for pipes, redirection, `&&`, `||`, and `;`; ArcoSH does not yet own a full shell grammar.
* The object pipeline model is not implemented yet.
* Permission prompts and capability enforcement are not implemented beyond directive metadata.
* `#IMPORT "module" AS Alias` provides namespaced function aliases, but imports still execute through the compatibility global import model rather than hard isolated module scopes.
* Classes are implemented for alpha use, but type checks are runtime checks and the object model may still gain stricter diagnostics.
* The bytecode VM is still an alpha subset, and the default native build model currently emits a runtime capsule around embedded bytecode rather than direct machine code for every ArcoBASIC construct. The repository includes alpha Linux builders for `.deb`, portable `.tar.gz`, `.rpm` when `rpmbuild` is installed, Void Linux `xbps-src` template archives, and a native `xbps-src` release builder for installable Void Linux `.xbps` packages.
* Windows process/job control is limited in this alpha.

Recommended recovery commands:

```sh
arcosh --safe
arcosh --norc
arcosh --rc ~/.arcosh/minimal.abas
arcosh --doctor
```
