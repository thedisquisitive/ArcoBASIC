#!/usr/bin/env bash
set -euo pipefail

RIVET="$(cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")"

TMP_ROOT="$(mktemp -d)"
trap 'rm -rf "$TMP_ROOT"' EXIT

if command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1 && command -v x86_64-w64-mingw32-ar >/dev/null 2>&1; then
    WIN_PROJECT="$TMP_ROOT/windows"
    mkdir -p "$WIN_PROJECT/src"
    cat > "$WIN_PROJECT/src/winprobe.cpp" <<'EOF'
int winprobe() { return 42; }
EOF
    cat > "$WIN_PROJECT/build.abas" <<'EOF'
Mingw = WindowsAdapter_DetectMingwX86_64()
IF !Mingw.Found THEN
    ignored = RIVET.Log.Warn("mingw-w64 unavailable")
ELSE
    Probe = BUILD.StaticLibrary("libwinprobe.a")
    Probe.OutputDirectory = "build-windows"
    ignored = WindowsAdapter_ConfigureTarget(Probe, Mingw)
    ignored = Probe.Sources.Append("src/winprobe.cpp")
    ignored = Probe.Build()
END IF
EOF
    (
        cd "$WIN_PROJECT"
        "$RIVET" build --jobs 1 | tee win.log
        grep -q "\[ARCHIVE\] build-windows/libwinprobe.a" win.log
        test -f build-windows/libwinprobe.a
    )
else
    echo "NOTE: mingw-w64 unavailable; Windows adapter compile coverage skipped."
fi

EM_PROJECT="$TMP_ROOT/emscripten"
FAKE_BIN="$TMP_ROOT/fake-bin"
mkdir -p "$EM_PROJECT/src" "$FAKE_BIN"
cat > "$EM_PROJECT/src/webprobe.cpp" <<'EOF'
int webprobe() { return 7; }
EOF
cat > "$FAKE_BIN/em++" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == "--version" ]]; then
    echo "fake em++ 1.0"
    exit 0
fi
out=""
dep=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -o) out="$2"; shift 2 ;;
        -MF) dep="$2"; shift 2 ;;
        *) shift ;;
    esac
done
mkdir -p "$(dirname "$out")"
printf 'fake object\n' > "$out"
if [[ -n "$dep" ]]; then
    mkdir -p "$(dirname "$dep")"
    printf '%s: src/webprobe.cpp\n' "$out" > "$dep"
fi
EOF
cat > "$FAKE_BIN/emar" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == "--version" ]]; then
    echo "fake emar 1.0"
    exit 0
fi
out="${2:-}"
mkdir -p "$(dirname "$out")"
printf 'fake archive\n' > "$out"
EOF
chmod +x "$FAKE_BIN/em++" "$FAKE_BIN/emar"
cat > "$EM_PROJECT/build.abas" <<'EOF'
Emscripten = EmscriptenAdapter_Detect()
IF !Emscripten.Found THEN
    ignored = RIVET.Log.Error("fake Emscripten toolchain was not detected")
ELSE
    Probe = BUILD.StaticLibrary("libwebprobe.a")
    Probe.OutputDirectory = "build-web"
    ignored = EmscriptenAdapter_ConfigureTarget(Probe, Emscripten)
    ignored = Probe.Sources.Append("src/webprobe.cpp")
    ignored = Probe.Build()
END IF
EOF
(
    cd "$EM_PROJECT"
    PATH="$FAKE_BIN:$PATH" "$RIVET" build --jobs 1 | tee web.log
    grep -q "\[ARCHIVE\] build-web/libwebprobe.a" web.log
    test -f build-web/libwebprobe.a
)

echo "rivet_cross_adapters_smoke: all checks passed"
