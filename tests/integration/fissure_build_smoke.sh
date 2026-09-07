#!/usr/bin/env bash
set -euo pipefail

# End-to-end smoke test for M2's cmake.ab adapter, against the synthetic tiny-cmake-project
# example (fissure/examples/tiny-cmake-project/): a real `cmake -S . -B build` configure with
# CMAKE_EXPORT_COMPILE_COMMANDS=ON, then `fissure run` against the real built CLI, proving a probe
# declared by CMAKE TARGET NAME (RegisterTargetProbe, not a hand-listed file path) is correctly
# classified AFFECTED when a source file belonging to that target changes.

FISSURE="$1"
SOURCE_DIR="$2"
CMAKE="${3:-cmake}"

TMP_ROOT="${TMPDIR:-/tmp}/fissure-build-smoke-$$"
PROJECT_DIR="$TMP_ROOT/project"
LOG_DIR="$TMP_ROOT/logs"
mkdir -p "$PROJECT_DIR" "$LOG_DIR"
trap 'rm -rf "$TMP_ROOT"' EXIT

cp -r "$SOURCE_DIR/fissure/examples/tiny-cmake-project/." "$PROJECT_DIR/"
cd "$PROJECT_DIR"

# A real CMake configure -- this is what proves cmake.ab's compile_commands.json parsing against
# actual CMake output, not a hand-crafted fixture. Kept to a deliberately tiny project (one static
# lib, one executable) so this stays fast and isolated from the real, huge arcobasic build tree
# (see fissure/adapters/build/cmake.ab's own comment on why this adapter never runs cmake itself
# against an unknown project -- this script is the one place that's a reasonable thing to do,
# because it owns the tiny project it's configuring).
"$CMAKE" -S . -B build > "$LOG_DIR"/configure.log 2>&1

git init -q
git config user.email "smoke-test@example.invalid"
git config user.name "Fissure Smoke Test"
git add -A
git commit -q -m "baseline"

# Baseline: no changes -- probe.app (RegisterTargetProbe("probe.app", ..., ["app", "mathlib"]),
# see fissure.ab) has real declared dependencies via both targets, so it's trivially UNAFFECTED
# against an empty disturbance -- same reasoning as fissure_smoke.sh's own baseline case.
echo "=== baseline run ==="
"$FISSURE" run | tee "$LOG_DIR"/baseline.log
grep -q "\[UNAFFECTED\] probe.app -- SKIP" "$LOG_DIR"/baseline.log

# The graph must contain real build-derived edges: one `consumes` edge per translation unit
# (object -> source, RFC section 5.2/5.3 Build evidence) plus the two declared-test-association
# edges RegisterTargetProbe added by resolving "app"/"mathlib" through TargetSources(). Checked via
# `fissure status`'s own node/edge counts rather than reaching into the sqlite file directly, to
# exercise the same CLI surface a real user has.
echo "=== status after baseline ==="
"$FISSURE" status | tee "$LOG_DIR"/status.log
# 2 consumes + 2 declared-test-association = 4 edges exactly -- not 6 or more, which would mean
# the graph got written multiple times (the exact bug this adapter's own rewrite fixed: an
# earlier version's caching relied on a top-level ArcoBASIC variable mutation from inside a
# function that does not persist on the tree-walking interpreter, silently re-running the whole
# discovery pass -- and therefore re-registering every edge -- once per TargetSources() call).
grep -q "edges: 4" "$LOG_DIR"/status.log

# Change mathlib.cpp (a source file of the "mathlib" target, linked into "app" but not itself
# named "app") -- probe.app must become AFFECTED purely through the target-name declaration,
# proving TargetSources("mathlib") really did resolve to src/mathlib.cpp.
echo "// changed" >> src/mathlib.cpp

echo "=== run after changing src/mathlib.cpp ==="
"$FISSURE" run | tee "$LOG_DIR"/changed.log
grep -q "\[AFFECTED\] probe.app -- RUN" "$LOG_DIR"/changed.log
grep -q "PASS  probe.app" "$LOG_DIR"/changed.log

echo "=== explain probe.app ==="
"$FISSURE" explain probe.app | tee "$LOG_DIR"/explain.log
grep -q "classification: AFFECTED" "$LOG_DIR"/explain.log
grep -q "src/mathlib.cpp" "$LOG_DIR"/explain.log

echo "fissure_build_smoke: all checks passed"
