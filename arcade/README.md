See `docs/arcade/README.md` for the actual ARCADE documentation, and `docs/ARCADE_PROGRESS.md` for
the current engineering state and exact next action.

```text
arcade.abas               the ARCADE shell itself (Phase B)
reference-project/        the packet's own "Customer Lookup" reference application (Section 35)
build.abas                Rivet target: BUILD.ArcoCapsule("arcade")
fissure.ab                Fissure probe that runs the Rivet build
build.sh                  builds standalone arcade/build/arcade via Fissure -> Rivet -> ArcoFission
build/                    build output -- not checked in, see .gitignore's bare `build/`
```

Quick start (see `docs/ARCADE_PROGRESS.md`'s own "Build and run commands" section for the full set,
including the standalone-capsule buildchain via `build.sh`):

```sh
ARCOFISSION_PATH="$(pwd)/build/ArcoFission" \
WAYLAND_DISPLAY= XDG_SESSION_TYPE=x11 DISPLAY=:1 \
  ./build/arco_cli arcade/arcade.abas arcade/reference-project/project.arcoproj

./arcade/build.sh
```
