# Games

Small self-contained ArcoBASIC games, each built as both a native desktop app and an
ArcoFission web capsule (see "Web Capsules" in `docs/arcofission.md`). Every game is pure
ArcoBASIC using only the core `GUI.*` primitives and, where useful, `stdlib/gui.abas`'s widget
classes (`Button`, `Checkbox`, `Slider`) -- no external assets.

## Building

```sh
games/build.sh                                                     # native only
ARCOFISSION_WEB_TOOLCHAIN_DIR=/path/to/build-wasm games/build.sh   # native + web
```

All output lands under `games/build/` (gitignored, same convention as `arcoflow/build/`):
native binaries directly in `games/build/`, web capsules in `games/build/web/`.

Web capsules are single self-contained `.html` files (`-sSINGLE_FILE=1` -- see
`docs/arcofission.md`) -- just open them directly, no server needed:

```sh
xdg-open games/build/web/tetris.html   # or double-click it in a file manager
```

Native binaries need a live Wayland or X11 desktop session:

```sh
games/build/tetris
```

## Games

- **`tetris.abas`** -- the obligatory Tetris clone. Classic 7 tetrominoes, ghost-piece preview,
  next-piece preview, scoring/levels/line-clearing, pause and restart. Controls: Left/Right move,
  Down soft drop, Up rotate, Space hard drop, P pause, R restart.

- **`sine.abas`** -- SINE, a signal-reconstruction puzzle: a key's cut silhouette *is* a combined
  sine waveform, sampled once per pixel column and rendered as the key's actual solid material
  below that sampled curve (no polygon-fill primitive exists, so this uses many adjacent 1px
  `GUI.Column` fills, the same bar-under-a-curve technique the lock's glowing target contour also
  uses). Dragging Amplitude/Frequency/Phase sliders reshapes the key in real time; a live match
  percentage tracks how close the combined waveform is to the target; TEST KEY plays the key into
  a lock body with a seat-and-flash success animation or a wiggle-and-reject failure one. Ten
  levels build up from a single locked-down parameter to three fully free oscillators. Controls:
  drag or scroll a slider, Space = Test Key, R = Reset level.
