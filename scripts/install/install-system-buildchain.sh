#!/usr/bin/env bash
set -euo pipefail

# Installs the Arcology buildchain tools system-wide:
#
#   ArcoFission  compiler/capsule builder
#   fissure      regression-impact runner
#   rivet        build orchestrator
#
# Defaults to /usr/local. Override with PREFIX=/usr, BUILD_DIR=..., or SUDO=doas.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
RIVET_BUILD_DIR="${RIVET_BUILD_DIR:-$REPO_ROOT/build-rivet}"
PREFIX="${PREFIX:-/usr/local}"
if [[ -z "${SUDO+x}" ]]; then
    if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
        SUDO=()
    else
        SUDO=(sudo)
    fi
else
    # shellcheck disable=SC2206
    SUDO=($SUDO)
fi
NPROC="$(nproc 2>/dev/null || echo 4)"

bindir="$PREFIX/bin"
sharedir="$PREFIX/share"
docdir="$PREFIX/share/doc"
private_dir="$PREFIX/lib/arcobasic/buildchain"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "install-system-buildchain: required command not found: $1" >&2
        exit 1
    fi
}

install_file() {
    local mode="$1"
    local source="$2"
    local dest="$3"
    "${SUDO[@]}" install -D -m "$mode" "$source" "$dest"
}

install_tree_files() {
    local source_dir="$1"
    local dest_dir="$2"
    local pattern="$3"
    if [[ ! -d "$source_dir" ]]; then
        echo "install-system-buildchain: missing directory: $source_dir" >&2
        exit 1
    fi
    while IFS= read -r -d '' file; do
        rel="${file#$source_dir/}"
        install_file 0644 "$file" "$dest_dir/$rel"
    done < <(find "$source_dir" -type f -name "$pattern" -print0)
}

install_tree_all_files() {
    local source_dir="$1"
    local dest_dir="$2"
    if [[ ! -d "$source_dir" ]]; then
        echo "install-system-buildchain: missing directory: $source_dir" >&2
        exit 1
    fi
    while IFS= read -r -d '' file; do
        rel="${file#$source_dir/}"
        install_file 0644 "$file" "$dest_dir/$rel"
    done < <(find "$source_dir" -type f -print0)
}

require_command install

bootstrap_rivet="${RIVET:-}"
if [[ -z "$bootstrap_rivet" ]]; then
    for candidate in "$RIVET_BUILD_DIR/rivet" "$BUILD_DIR/rivet/rivet" "$(command -v rivet 2>/dev/null || true)"; do
        if [[ -n "$candidate" && -x "$candidate" ]]; then
            bootstrap_rivet="$candidate"
            break
        fi
    done
fi

if [[ -n "$bootstrap_rivet" ]]; then
    echo "==> building buildchain with Rivet ($bootstrap_rivet)"
    ( cd "$REPO_ROOT" && ARCO_SOURCE_ROOT_OVERRIDE="$private_dir/source" "$bootstrap_rivet" build --jobs "$NPROC" )
else
    echo "==> no Rivet bootstrap binary found; using CMake only to bootstrap Rivet replacement tools"
    require_command cmake
    cmake -S "$REPO_ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target rivet -j"$NPROC"
    ( cd "$REPO_ROOT" && ARCO_SOURCE_ROOT_OVERRIDE="$private_dir/source" "$BUILD_DIR/rivet/rivet" build --jobs "$NPROC" )
fi

for tool in ArcoFission fissure rivet arco_cli; do
    if [[ ! -x "$RIVET_BUILD_DIR/$tool" ]]; then
        echo "install-system-buildchain: expected Rivet-built tool missing: $RIVET_BUILD_DIR/$tool" >&2
        exit 1
    fi
done

for archive in \
    libarco_compiler.a \
    libarco_runtime.a \
    libarco_compiler_core.a \
    libarco_runtime_core.a \
    libarcology_os.a; do
    if [[ ! -f "$RIVET_BUILD_DIR/$archive" ]]; then
        echo "install-system-buildchain: expected Rivet-built archive missing: $RIVET_BUILD_DIR/$archive" >&2
        exit 1
    fi
done

echo "==> installing tools into $bindir"
install_file 0755 "$RIVET_BUILD_DIR/ArcoFission" "$private_dir/ArcoFission"
install_file 0755 "$RIVET_BUILD_DIR/fissure" "$bindir/fissure"
install_file 0755 "$RIVET_BUILD_DIR/rivet" "$bindir/rivet"
install_file 0755 "$RIVET_BUILD_DIR/arco_cli" "$private_dir/arco_cli"

"${SUDO[@]}" mkdir -p "$bindir"
"${SUDO[@]}" ln -sfn "$private_dir/ArcoFission" "$bindir/ArcoFission"

echo "==> installing ArcoFission native-capsule support into $private_dir"
CXX_COMPILER="${CXX:-$(command -v c++ 2>/dev/null || echo c++)}"
metadata_dir="$(mktemp -d)"
trap 'rm -rf "$metadata_dir"' EXIT
mkdir -p \
    "$metadata_dir/CMakeFiles/ArcoFission.dir" \
    "$metadata_dir/CMakeFiles/arco_cli.dir" \
    "$metadata_dir/CMakeFiles/ArcoFissionCapsuleCoreProbe.dir" \
    "$metadata_dir/CMakeFiles/ArcoNativeRuntimeCoreProbe.dir"
printf 'CMAKE_CXX_COMPILER:FILEPATH=%s\n' "$CXX_COMPILER" > "$metadata_dir/CMakeCache.txt"
printf '%s apps/arcofission/main.cpp -o ArcoFission libarco_compiler.a libarco_runtime.a libarcology_os.a\n' \
    "$CXX_COMPILER" > "$metadata_dir/CMakeFiles/ArcoFission.dir/link.txt"
printf '%s apps/arco/main.cpp -o arco_cli libarco_runtime.a libarcology_os.a\n' \
    "$CXX_COMPILER" > "$metadata_dir/CMakeFiles/arco_cli.dir/link.txt"
printf '%s apps/arcofission/capsule_core_probe.cpp -o ArcoFissionCapsuleCoreProbe libarco_compiler_core.a libarco_runtime_core.a libarcology_os.a\n' \
    "$CXX_COMPILER" > "$metadata_dir/CMakeFiles/ArcoFissionCapsuleCoreProbe.dir/link.txt"
printf '%s apps/arcofission/runtime_core_probe.cpp -o ArcoNativeRuntimeCoreProbe libarco_runtime_core.a libarcology_os.a\n' \
    "$CXX_COMPILER" > "$metadata_dir/CMakeFiles/ArcoNativeRuntimeCoreProbe.dir/link.txt"

install_file 0644 "$metadata_dir/CMakeCache.txt" "$private_dir/CMakeCache.txt"
install_file 0644 "$metadata_dir/CMakeFiles/ArcoFission.dir/link.txt" "$private_dir/CMakeFiles/ArcoFission.dir/link.txt"
install_file 0644 "$metadata_dir/CMakeFiles/arco_cli.dir/link.txt" "$private_dir/CMakeFiles/arco_cli.dir/link.txt"
install_file 0644 "$metadata_dir/CMakeFiles/ArcoFissionCapsuleCoreProbe.dir/link.txt" "$private_dir/CMakeFiles/ArcoFissionCapsuleCoreProbe.dir/link.txt"
install_file 0644 "$metadata_dir/CMakeFiles/ArcoNativeRuntimeCoreProbe.dir/link.txt" "$private_dir/CMakeFiles/ArcoNativeRuntimeCoreProbe.dir/link.txt"

for archive in \
    libarco_compiler.a \
    libarco_runtime.a \
    libarco_compiler_core.a \
    libarco_runtime_core.a; do
    install_file 0644 "$RIVET_BUILD_DIR/$archive" "$private_dir/$archive"
done
install_file 0644 "$RIVET_BUILD_DIR/libarcology_os.a" "$private_dir/libarcology_os.a"

for cross_dir in \
    "build-rivet-windows:windows-x86_64" \
    "build-rivet-web:web-wasm32"; do
    source_name="${cross_dir%%:*}"
    dest_name="${cross_dir##*:}"
    source_path="$REPO_ROOT/$source_name"
    if [[ -f "$source_path/libarco_compiler.a" && -f "$source_path/libarco_runtime.a" && -f "$source_path/libarcology_os.a" ]]; then
        echo "==> installing optional cross buildchain support for $dest_name"
        install_file 0644 "$source_path/libarco_compiler.a" "$private_dir/$dest_name/libarco_compiler.a"
        install_file 0644 "$source_path/libarco_runtime.a" "$private_dir/$dest_name/libarco_runtime.a"
        install_file 0644 "$source_path/libarcology_os.a" "$private_dir/$dest_name/libarcology_os.a"
    fi
done

echo "==> installing shared runtime resources"
install_tree_files "$REPO_ROOT/stdlib" "$sharedir/arcobasic/stdlib" "*.abas"
install_tree_files "$REPO_ROOT/rivet/stdlib" "$sharedir/rivet/stdlib" "*.abas"
install_tree_files "$REPO_ROOT/rivet/adapters" "$sharedir/rivet/adapters" "*.ab"
install_tree_files "$REPO_ROOT/fissure/adapters" "$sharedir/fissure/adapters" "*.ab"
install_tree_all_files "$REPO_ROOT/include" "$private_dir/source/include"
install_tree_all_files "$REPO_ROOT/arcology-os/include" "$private_dir/source/arcology-os/include"
install_tree_all_files "$REPO_ROOT/src/native" "$private_dir/source/src/native"
install_file 0644 "$REPO_ROOT/src/runtime/runtime_handles.cpp" "$private_dir/source/src/runtime/runtime_handles.cpp"
install_file 0644 "$REPO_ROOT/src/gui/web_shell.html" "$private_dir/source/src/gui/web_shell.html"

install_file 0644 "$REPO_ROOT/README.md" "$docdir/arcobasic/README.md"
install_file 0644 "$REPO_ROOT/docs/arcofission.md" "$docdir/arcobasic/arcofission.md"
install_file 0644 "$REPO_ROOT/fissure/AGENT_PROGRESS.md" "$docdir/fissure/AGENT_PROGRESS.md"
install_file 0644 "$REPO_ROOT/fissure/docs/fissure-rfc.md" "$docdir/fissure/fissure-rfc.md"
install_file 0644 "$REPO_ROOT/rivet/RIVET_PROGRESS.md" "$docdir/rivet/RIVET_PROGRESS.md"
install_file 0644 "$REPO_ROOT/rivet/docs/rivet-rfc.md" "$docdir/rivet/rivet-rfc.md"

echo "==> installed"
echo "    $bindir/ArcoFission"
echo "    $bindir/fissure"
echo "    $bindir/rivet"
echo
echo "Smoke checks:"
echo "    ArcoFission --version"
echo "    fissure status"
echo "    rivet clean --all    # from a directory with build.abas"
