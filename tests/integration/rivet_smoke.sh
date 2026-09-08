#!/usr/bin/env bash
set -euo pipefail

# End-to-end smoke test implementing rivet-rfc.md section 57/61's own literal acceptance test
# against the real built `rivet` binary and a real tiny C++ project -- not a mock.

RIVET="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/rivet-smoke-$$"
PROJECT_DIR="$TMP_ROOT/project"
mkdir -p "$PROJECT_DIR"
trap 'rm -rf "$TMP_ROOT"' EXIT

cp -r "$SOURCE_DIR/rivet/examples/tiny-project/." "$PROJECT_DIR/"
cd "$PROJECT_DIR"

echo "=== build 1 (cold) ==="
"$RIVET" build | tee build1.log
grep -q "\[COMPILE\] src/main.cpp" build1.log
grep -q "\[COMPILE\] src/math.cpp" build1.log
grep -q "\[LINK\]" build1.log
test -x build/tinyapp

echo "=== running the produced binary ==="
./build/tinyapp
run_exit=$?
if [ "$run_exit" -ne 0 ]; then
    echo "FAIL: build/tinyapp exited $run_exit, expected 0" >&2
    exit 1
fi

echo "=== build 2 (no changes -- the literal 'second run does nothing' bar) ==="
"$RIVET" build | tee build2.log
grep -q "\[CACHE\]" build2.log
if grep -q "\[COMPILE\]" build2.log; then
    echo "FAIL: build 2 recompiled something despite no changes" >&2
    exit 1
fi
if grep -q "\[LINK\]" build2.log; then
    echo "FAIL: build 2 relinked despite no changes" >&2
    exit 1
fi

echo "=== change src/math.cpp (real code, not a comment), build 3 ==="
# A comment-only change would (correctly!) produce a byte-identical .o file, and Rivet's real
# content-based fingerprinting would then (correctly) skip relinking -- confirmed directly: `cmp`
# on two clang++ outputs of math.cpp with only a trailing `// comment` added showed IDENTICAL
# object files. That's real, working, more-precise-than-naive caching, not a bug -- but it means
# this test needs an actual semantic change to guarantee the compiled bytes differ and a relink is
# genuinely required, matching the RFC's own section 57 acceptance test intent.
sed -i 's/return a + b;/return a + b + 0;/' src/math.cpp
"$RIVET" build | tee build3.log
grep -q "\[COMPILE\] src/math.cpp" build3.log
if grep -q "\[COMPILE\] src/main.cpp" build3.log; then
    echo "FAIL: build 3 recompiled main.cpp, which did not change" >&2
    exit 1
fi
grep -q "\[LINK\]" build3.log

echo "=== rivet why rebuild src/math.cpp ==="
"$RIVET" why rebuild src/math.cpp | tee why.log
grep -q "src/math.cpp" why.log
grep -q "content changed" why.log

echo "=== rivet clean ==="
"$RIVET" clean --all
if [ -f build/tinyapp ]; then
    echo "FAIL: rivet clean --all left build/tinyapp in place" >&2
    exit 1
fi

echo "=== static library target feeds executable target ==="
STATIC_PROJECT="$TMP_ROOT/static-project"
mkdir -p "$STATIC_PROJECT/include" "$STATIC_PROJECT/src"
cat > "$STATIC_PROJECT/include/mathcore.hpp" <<'EOF'
#pragma once
int triple(int value);
EOF
cat > "$STATIC_PROJECT/src/mathcore.cpp" <<'EOF'
#include "mathcore.hpp"
int triple(int value) { return value * 3; }
EOF
cat > "$STATIC_PROJECT/src/main.cpp" <<'EOF'
#include "mathcore.hpp"
int main() { return triple(7) == 21 ? 0 : 1; }
EOF
cat > "$STATIC_PROJECT/build.abas" <<'EOF'
Core = BUILD.StaticLibrary("libmathcore.a")
ignored = Core.Includes.Append("include")
ignored = Core.Sources.Append("src/mathcore.cpp")
ignored = Core.Build()

App = BUILD.Executable("uses-static")
ignored = App.Includes.Append("include")
ignored = App.Sources.Append("src/main.cpp")
ignored = App.AddDependency(Core)
ignored = App.Build()
EOF
(
    cd "$STATIC_PROJECT"
    "$RIVET" build --jobs 1 | tee static1.log
    grep -q "\[ARCHIVE\] build/libmathcore.a" static1.log
    grep -q "\[LINK\]    build/uses-static" static1.log
    test -f build/libmathcore.a
    test -x build/uses-static
    ./build/uses-static

    "$RIVET" build --jobs 1 | tee static2.log
    grep -q "\[CACHE\]" static2.log
    if grep -q "\[ARCHIVE\]" static2.log || grep -q "\[LINK\]" static2.log || grep -q "\[COMPILE\]" static2.log; then
        echo "FAIL: static project rebuilt despite no changes" >&2
        exit 1
    fi
)

if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sqlite3; then
    echo "=== pkg-config adapter applies package link flags ==="
    PKG_PROJECT="$TMP_ROOT/pkg-project"
    mkdir -p "$PKG_PROJECT/src"
    cat > "$PKG_PROJECT/src/main.cpp" <<'EOF'
#include <sqlite3.h>
int main() {
    sqlite3* db = nullptr;
    return sqlite3_open(":memory:", &db) == SQLITE_OK && sqlite3_close(db) == SQLITE_OK ? 0 : 1;
}
EOF
    cat > "$PKG_PROJECT/build.abas" <<'EOF'
Sqlite3 = PkgConfigAdapter_Find("sqlite3")
App = BUILD.Executable("uses-sqlite")
ignored = App.Sources.Append("src/main.cpp")
IF Sqlite3.Found THEN ignored = PkgConfigAdapter_Apply(App, Sqlite3)
ignored = App.Build()
EOF
    (
        cd "$PKG_PROJECT"
        "$RIVET" build --jobs 1 | tee pkg.log
        grep -q "\[LINK\]    build/uses-sqlite" pkg.log
        ./build/uses-sqlite
    )
fi

echo "rivet_smoke: all checks passed"
