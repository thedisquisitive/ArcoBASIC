# ArcoSH Alpha Known Limitations

ArcoSH is usable for alpha testing, scripting, and profile experimentation, but it is not a complete POSIX shell.

Known alpha gaps:

* Job control tracks background jobs and supports `jobs`, `fg`, `bg`, `kill`, and `disown`, but terminal process-group handoff and Ctrl-Z suspension are not complete.
* Bare host commands use the host shell for pipes, redirection, `&&`, `||`, and `;`; ArcoSH does not yet own a full shell grammar.
* The object pipeline model is not implemented yet.
* Permission prompts and capability enforcement are not implemented beyond directive metadata.
* `#IMPORT` executes `.abas` files, but imports do not yet provide isolated module namespaces.
* Classes, bytecode, debugger, and full package/build systems remain future work. The repository includes an alpha `.deb` builder for packaging the current tools.
* Windows process/job control is limited in this alpha.

Recommended recovery commands:

```sh
arcosh --safe
arcosh --norc
arcosh --rc ~/.arcosh/minimal.abas
arcosh --doctor
```
