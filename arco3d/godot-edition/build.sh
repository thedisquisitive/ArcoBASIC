#!/usr/bin/env bash
# Exports Arco3D (Godot Edition) into build/Arco3D -- a real standalone ELF64 binary with the
# project's .pck data embedded, needing no Godot installation to run afterward. Run from anywhere;
# it locates itself. Requires the matching Godot export templates to already be installed
# (~/.local/share/godot/export_templates/<version>/) -- see README.md for how to fetch them.
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
GODOT="${GODOT:-$HOME/godot/godot}"

if [ ! -x "$GODOT" ]; then
    echo "build.sh: $GODOT not found -- set GODOT=/path/to/godot" >&2
    exit 1
fi

mkdir -p "$SCRIPT_DIR/build"
"$GODOT" --headless --path "$SCRIPT_DIR" --export-release "Linux" build/Arco3D
echo "==> godot-edition/build/Arco3D"

# The ArcoBASIC automation feature (scripting/arco3d_api.abas, see Main.gd's
# _find_arcosh_path/_on_run_script_chosen) needs two things as REAL files on disk next to the
# exported binary, not just bundled into the .pck's own virtual filesystem: the arcosh interpreter
# itself (a real subprocess, can't run from inside a .pck), and the .abas scripts it #IMPORTs and
# runs (an external subprocess can't read Godot's packed resource filesystem either). Copying both
# here, alongside the export, is what makes automation actually work from a portable exported
# build and not just from source/the editor.
cp -f "$SCRIPT_DIR/../build/arcosh" "$SCRIPT_DIR/build/arcosh"
rm -rf "$SCRIPT_DIR/build/scripting"
cp -r "$SCRIPT_DIR/scripting" "$SCRIPT_DIR/build/scripting"
echo "==> godot-edition/build/arcosh + build/scripting/ (for the ArcoBASIC automation feature)"

# Same real reason as scripting/ above, confirmed directly after a real "the button does nothing"
# bug report: Godot's headless --export-release does NOT pull content/*.a3d into the .pck the way
# export_filter="all_resources" would suggest (confirmed with `strings build/Arco3D | grep
# ArcoArcher` -- zero matches), since it's a plain-text file with an extension Godot's own resource
# pipeline doesn't recognize. _find_arco_archer_path() in Main.gd looks here first.
rm -rf "$SCRIPT_DIR/build/content"
cp -r "$SCRIPT_DIR/content" "$SCRIPT_DIR/build/content"
echo "==> godot-edition/build/content/ (the Arco Archer signature model)"
