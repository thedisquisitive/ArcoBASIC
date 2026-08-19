# ArcoFission

ArcoFission is the ArcoBASIC compiler pipeline tool. In the current alpha it can inspect compiler stages, emit hosted bytecode, run hosted bytecode, build Linux ELF64 runtime capsules, cross-compile Windows PE32+ runtime capsules from Linux, and build UEFI x86-64 PE32+ images for the freestanding systems target.

## Build

Build the tool from the repository root:

```sh
cmake -S . -B build
cmake --build build --target ArcoFission
```

The native capsule builder expects the `ArcoFission` executable to come from a CMake build tree so it can reuse the ArcoBASIC static libraries beside that executable. It does not require your shell's current directory to be the build directory.

## Inspect Compiler Stages

Reveal the parsed AST:

```sh
build/ArcoFission reveal examples/hello.bas at AST
```

Reveal A-MIR:

```sh
build/ArcoFission reveal examples/hello.bas at A-MIR
```

Reveal hosted bytecode:

```sh
build/ArcoFission reveal examples/hello.bas at BYTECODE
```

The `--stage` spelling is also accepted:

```sh
build/ArcoFission reveal examples/hello.bas --stage BYTECODE
```

## Bytecode

Write `.arcof-text` bytecode:

```sh
build/ArcoFission bytecode examples/hello.bas -o hello.arcof
```

`build` also writes bytecode when the output path ends in `.arcof`:

```sh
build/ArcoFission build examples/hello.bas -o hello.arcof
```

Run bytecode:

```sh
build/ArcoFission run hello.arcof
```

Compile and run in one step:

```sh
build/ArcoFission compile-run examples/hello.bas
```

## Linux Native Capsules

Build a Linux ELF64 runtime capsule:

```sh
build/ArcoFission build examples/hello.bas -o hello
./hello
```

The explicit native command is equivalent:

```sh
build/ArcoFission native examples/hello.bas -o hello
./hello
```

In the alpha compiler model, the executable is native on the outside and runs the ArcoFission bytecode VM on the inside. Native capsules embed a compact binary bytecode payload and link it with the ArcoFission runtime from the active CMake build tree. The hosted VM prepares typed slots and fused numeric bytecode for common loop arithmetic before execution.

### Lean capsules (no GUI/network dependency footprint)

By default, `ArcoFission` links a capsule against whatever it was itself built against -- and on a
desktop dev machine with GTK/GLFW/X11/libcurl installed, that means every capsule needs those
(and their own transitive dependencies -- TLS, Kerberos, LDAP, systemd, ~90 shared libraries in
total) present on the machine it runs on, even a capsule that never calls a `GUI.*` or `Network.*`
function. That's a real problem for distributing a capsule to a machine you don't control (a
different distro, a minimal container, a headless server).

Opt in to a second, lean build of the runtime with no GUI backend and no libcurl:

```sh
cmake --build build --target ArcoFissionCapsuleCoreProbe
```

(`EXCLUDE_FROM_ALL`, so this doesn't add build time to a normal build; build it once and it stays
built.) Once that target exists, `native`/`build` automatically link a capsule against it instead
of the full runtime whenever the program doesn't call a real GUI function -- `GUI.Available()` and
`GUI.Backend()` still work either way, since the lean build's stub backend answers those directly
rather than needing a real backend; any other `GUI.*` call still gets the full runtime
automatically, so nothing breaks, it just isn't lean. `Network.*` functions don't need this
carve-out at all: every one of them already degrades gracefully without libcurl (`Network.Get`
etc. return `{Ok: false, Error: "networking was not enabled in this build"}`; `Network.TcpConnect`/
`Network.ResolveDNS` use plain POSIX sockets and work in both builds).

The command's output says which one it picked:

```text
LEAN RUNTIME LINKED (no GUI backend, no libcurl)
```

or, if `ArcoFissionCapsuleCoreProbe` hasn't been built yet:

```text
LEAN RUNTIME UNAVAILABLE (run `cmake --build . --target ArcoFissionCapsuleCoreProbe` in the build tree to enable it) -- linked the full runtime instead
```

A lean capsule for a `PRINT`/arithmetic/array-and-object program links against 6 shared libraries
(libc, libstdc++, libm, libgcc_s, the dynamic linker, and vdso) instead of ~96.

Native capsules built with `native`/`build` and no `--target` are Linux ELF64 outputs.

## Windows Native Capsules

`ArcoFission` can also cross-compile a capsule to a genuine Windows PE32+ x86-64 executable
from a Linux host, using a mingw-w64 cross-compiler:

```sh
sudo apt-get install g++-mingw-w64-x86-64
```

Configure and build a mingw-w64-targeted tree of the runtime and compiler libraries (a normal
build tree, just pointed at the cross toolchain file; this only needs `arco_compiler`, not the
whole project):

```sh
cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows --target arco_compiler
```

Then point `ARCOFISSION_WINDOWS_TOOLCHAIN_DIR` at that tree and build with `--target
windows-x86_64`:

```sh
export ARCOFISSION_WINDOWS_TOOLCHAIN_DIR="$PWD/build-windows"
build/ArcoFission native examples/hello.bas -o hello.exe --target windows-x86_64
```

The resulting `.exe` is statically linked (`-static -static-libgcc -static-libstdc++`) and
imports only `KERNEL32.dll` and `msvcrt.dll` -- the two DLLs present on every Windows install --
so it doesn't require anything else to be installed on the target machine. Run it directly on
Windows, or under Wine (`wine hello.exe`) on Linux.

This target builds the interpreter/stdlib core only: because the GUI backend is gated to Unix in
`cmake/Dependencies.cmake` and the mingw cross build won't have libcurl available either, GUI and
networking builtins compile out automatically for this target (`GUI.*` calls will report the
backend as unavailable; `Network.*` calls that need DNS/sockets return a not-implemented error --
see the `#ifdef _WIN32` branches in `src/runtime/runtime.cpp`). A capsule that only uses core
language features, `PRINT`, arrays/objects, and non-network stdlib works exactly as it does on
Linux.

## Web Capsules

`ArcoFission` can also cross-compile a capsule to WebAssembly, runnable in any browser, using the
[Emscripten](https://emscripten.org) toolchain:

```sh
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest
~/emsdk/emsdk activate latest
```

Configure and build an Emscripten-targeted tree of the runtime and compiler libraries (this only
needs `arco_compiler`, not the whole project; `-DARCO_ENABLE_GUI=ON` is the default and is what
selects `src/gui/canvas_backend.cpp`, described below, over the stub backend):

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --target arco_compiler
```

Then point `ARCOFISSION_WEB_TOOLCHAIN_DIR` at that tree and build with `--target web`:

```sh
export ARCOFISSION_WEB_TOOLCHAIN_DIR="$PWD/build-wasm"
export ARCOFISSION_WEB_CXX="$HOME/emsdk/upstream/emscripten/em++"  # only if em++ isn't on PATH
build/ArcoFission native examples/hello.bas -o hello.html --target web
```

This writes a single self-contained `hello.html` -- just open it directly (`file://` included, no
server needed: `-sSINGLE_FILE=1` embeds the wasm module as base64 inside the generated JS, itself
inlined into the page, rather than the default shape of a bare `fetch()` of a separate `.wasm`
file, which browsers refuse to do across a `file://` origin -- the same "both async and sync
fetching of the wasm failed" error you'd get trying to open a plain Emscripten export directly).
Console output (`PRINT`, uncaught errors) appears both on the page and in the browser's own JS
console. `--target web -o hello.js` or `-o hello.wasm` instead produces the traditional separate
`.js`/`.wasm` files (no `-sSINGLE_FILE`, no custom shell) for callers who want that shape instead,
e.g. a real HTTP-served deployment where the fetch/caching tradeoff runs the other way.

**GUI capsules** (anything using `stdlib/gui.abas`/`GUI.*`, including `arcoflow/arcoflow.abas`
itself) work too: `src/gui/canvas_backend.cpp` implements the same `arco::gui` interface
`src/gui/glfw_backend.cpp` does for desktop, backed by an HTML5 `<canvas>` instead of
GLFW+Cairo+Pango+GTK. The window fills the browser viewport rather than being a fixed size, and
the blocking-style `WHILE ... GUI.WaitEvent(...) ... WEND` loop every GUI capsule is written
against works unchanged in a browser because the capsule links with `-sASYNCIFY` -- see the
file-level comment in `canvas_backend.cpp` for how that works and its current limitations
(`GUI.Image` isn't implemented yet).

`GUI.OpenFileDialog`/`SaveFileDialog` use the real File System Access API
(`showOpenFilePicker`/`showSaveFilePicker`) when the browser supports it -- a genuine native OS
file dialog reading from and writing to the user's actual disk, bridged through Asyncify via
`EM_ASYNC_JS` the same way `emscripten_sleep` is used elsewhere. Browsers without the API (Firefox,
Safari as of this writing) fall back to a `window.prompt()`-based path into Emscripten's ephemeral
`MEMFS`, and the same fallback also kicks in if a real picker call fails for any reason other than
the user's own cancellation (e.g. missing "transient activation" in an unusual call context) so
Open/Save never just silently do nothing. See the file-level comment in `canvas_backend.cpp` for
the full mechanics, including how Save's real disk handle gets synced after `File.WriteText`
completes even though ArcoBASIC's own path-then-write contract has no idea it exists.

`Process.Run` has no real subprocess to shell out to in a browser sandbox and fails immediately
there -- but every capsule that links `arco_compiler` (which every native/web capsule does, to
embed and run its own bytecode) also has the full compiler available in-process, so
`ArcoFission.CompileRunSource(source)` compiles and runs ArcoBASIC source text directly, no
subprocess involved, returning the same `{Ok, Output, ExitCode, Error}` shape `Process.Run` does
(see `register_self_compile_run` in `fission.cpp`). This is what ArcoFlow's own Run button falls
back to when `GUI.Backend() == "canvas"` instead of shelling out to a separate `ArcoFission`
invocation the way it does on desktop.

## Instruction Limits

Hosted run/build commands accept an operator instruction limit:

```sh
build/ArcoFission compile-run program.abas --instruction-limit 50000000
build/ArcoFission run program.arcof --instruction-limit unlimited
build/ArcoFission native program.abas -o program --instruction-limit 50000000
```

`unlimited` disables instruction-count termination for that invocation.

## UEFI Target

Build a freestanding UEFI x86-64 PE32+ image:

```sh
build/ArcoFission build arcology-os/tests/fixtures/uefi-hello/hello.abas -o hello.efi --target uefi-x86_64
```

Use `--entry NAME` to select a different entry function:

```sh
build/ArcoFission build kernel.abas -o kernel.efi --target uefi-x86_64 --entry Main
```

## Exit Codes

ArcoFission exits with:

```text
0  success
1  source intake, build, or runtime failure
2  command-line usage or option error
```
