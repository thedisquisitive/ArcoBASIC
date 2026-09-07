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

echo "rivet_smoke: all checks passed"
