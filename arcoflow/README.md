# ArcoFlow

ArcoFlow is the IDE for ArcoBASIC/ArcoFission: an intent-based editor that will eventually offer a
graph/flowchart canvas (nodes, ports, wires) with a bidirectional Intent-view <-> ArcoBASIC-source
toggle, alongside plain text editing. It's itself an ArcoBASIC program, built on `stdlib/gui.abas`.

Current state is a working text-editing prototype: edit an ArcoBASIC file, save it, run it via a
real `Process.Run` -> `ArcoFission compile-run` round trip, see the output. It understands the
[project format](#project-format) below, including a collapsible project explorer sidebar. No
Intent/graph view yet, no syntax highlighting yet -- both deliberately deferred.

## Running it

All builds land inside this folder, under `arcoflow/build/` (gitignored, same as the top-level
`build/`) -- nothing gets written outside `arcoflow/`. Build both the native and (if you have an
Emscripten toolchain tree set up -- see below) web capsules with:

```sh
ARCOFISSION_WEB_TOOLCHAIN_DIR=/path/to/build-wasm arcoflow/build.sh   # web capsule needs this env var
arcoflow/build.sh                                                     # native only, if you don't
```

Then launch the native capsule, optionally pointing at a file or a `.arcoproj` project file:

```sh
arcoflow/build/arcoflow [path/to/file.abas | path/to/project.arcoproj]
```

Requires a live Wayland or X11 desktop session. `ARCOFISSION_PATH` can point Run at a specific
`ArcoFission` binary; it otherwise falls back to whatever's on `PATH`, or a project's own
`ArcoFissionPath` field if set (see below).

Launched with no argument, or pointed straight at a bare `.abas` file, there's no project around it
to show an explorer sidebar for -- that's expected, not a bug (see below). To actually see the
explorer, point it at `example-project/`, which has a `project.arcoproj` with a couple of folders:

```sh
arcoflow/build/arcoflow arcoflow/example-project/project.arcoproj
```

### In a browser

ArcoFlow also runs as a WebAssembly capsule (see "Web Capsules" in `docs/arcofission.md` for full
Emscripten toolchain setup). Once `arcoflow/build.sh` has produced `arcoflow/build/web/arcoflow.*`,
serve it over plain HTTP and open the printed URL -- opening `arcoflow.html` directly via a
`file://` URL doesn't work, browsers refuse to fetch a `.wasm` file across the `file://` origin
(CORS) and the page aborts before it runs:

```sh
arcoflow/serve.sh          # serves arcoflow/build/web/ at http://127.0.0.1:8000/arcoflow.html
arcoflow/serve.sh 8080     # or a specific port
```

Editing, saving, and the project explorer all work the same as on desktop. `Run` fails gracefully
with no subprocess to shell out to (there's no `ArcoFission` reachable from inside a browser
sandbox) -- everything else (`GUI.Image`, real file/save dialogs, clipboard) has the same
deliberate first-pass gaps documented in `docs/arcofission.md`.

## Project format

A project's manifest is a file named `project.arcoproj`, either opened directly or discovered as a
sibling of whatever file was opened directly -- entirely optional, a bare `.abas` file with no
project around it behaves exactly as if none of this existed. The file's entire content is a single
ArcoBASIC object-literal expression (`Project.Load` in `runtime.cpp` evaluates it through the
language's own parser, not a bespoke format), for example:

```
{
    Name: "My Project",
    Entry: "main.abas",
    Files: ["main.abas", "src/app.abas", "src/utils/helper.abas"],
    Window: {Width: 1000, Height: 700},
    ArcoFissionPath: ""
}
```

All fields are optional. `Files` entries may contain `/` to place a file under one or more folders,
which the explorer sidebar renders as a collapsible tree (folders sorted before files, alphabetical
within each level).

## Layout

- `arcoflow.abas` — the editor itself.
- `build.sh` / `serve.sh` — build both capsules into `build/`; serve the web one over HTTP.
- `build/` — build output (gitignored): `build/arcoflow` (native), `build/web/arcoflow.*` (web).
- `example-project/` — a small sample project (a `project.arcoproj`, a couple of folders) for
  trying the project explorer without having to build one first.
- `concept_render.png` — early concept art for the eventual Intent graph view.
