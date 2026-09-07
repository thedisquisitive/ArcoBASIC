#!/usr/bin/env bash
set -euo pipefail

# End-to-end smoke test for `fissure run`/`explain` against the synthetic tiny-project example
# (fissure/examples/tiny-project/), covering RFC section 22.6's own M1 definition of done: given a
# repository, registered probes, a baseline, and a changed file, Fissure must classify every probe
# and run exactly AFFECTED + UNKNOWN, skipping only a proven UNAFFECTED one -- and (acceptance
# criterion 11) a real Fracture must show useful diagnostics.

FISSURE="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/fissure-smoke-$$"
PROJECT_DIR="$TMP_ROOT/project"
LOG_DIR="$TMP_ROOT/logs"
mkdir -p "$PROJECT_DIR" "$LOG_DIR"
trap 'rm -rf "$TMP_ROOT"' EXIT

cp -r "$SOURCE_DIR/fissure/examples/tiny-project/." "$PROJECT_DIR/"
cd "$PROJECT_DIR"

# Log files MUST live outside the project directory being analyzed -- `tee` creates its output
# file (empty) before `fissure run` finishes, and Fissure's own git adapter correctly (this is
# real, working behavior, not a bug that needed working around) detects that as a genuine new
# untracked file if it lands inside the same git working tree it's diffing. Found the hard way: an
# early version of this script wrote logs into $TMP_ROOT itself (== the project root) and every
# "baseline, no changes" assertion below saw a phantom 1-file disturbance from its own previous
# log file.

git init -q
git config user.email "smoke-test@example.invalid"
git config user.name "Fissure Smoke Test"
git add -A
git commit -q -m "baseline"

# Baseline run: no changes since HEAD, and no untracked files (git.ab reports both). The
# disturbance is empty, so probe.core/probe.unrelated/probe.broken (each with a real, non-
# intersecting-with-nothing declared dependency) are all trivially UNAFFECTED -- an empty changed
# set can never intersect anything, that's correct, not a bug. Only probe.nothing_declared, which
# has no declared evidence at all, is UNKNOWN and runs -- RFC 6.1's own invariant, exercised here
# with zero real disturbance rather than a nonempty one.
echo "=== baseline run (no changes) ==="
"$FISSURE" run | tee "$LOG_DIR"/baseline.log
grep -q "\[UNAFFECTED\] probe.core -- SKIP" "$LOG_DIR"/baseline.log
grep -q "\[UNAFFECTED\] probe.unrelated -- SKIP" "$LOG_DIR"/baseline.log
grep -q "\[UNAFFECTED\] probe.broken -- SKIP" "$LOG_DIR"/baseline.log
grep -q "\[UNKNOWN\] probe.nothing_declared -- RUN" "$LOG_DIR"/baseline.log
grep -q "PASS  probe.nothing_declared" "$LOG_DIR"/baseline.log

# Now change src/core.txt only.
echo "changed" >> src/core.txt

echo "=== run after changing src/core.txt ==="
# probe.broken always exits 1, so `fissure run` itself must exit nonzero here (failed > 0) --
# capture that explicitly rather than letting `set -e` abort the whole test script on it.
set +e
"$FISSURE" run > "$LOG_DIR"/changed.log 2>&1
run_exit=$?
set -e
cat "$LOG_DIR"/changed.log
if [ "$run_exit" -eq 0 ]; then
    echo "FAIL: fissure run exited 0 despite probe.broken failing" >&2
    exit 1
fi

# probe.core: AFFECTED (declares src/core.txt), must run and pass.
grep -q "\[AFFECTED\] probe.core -- RUN" "$LOG_DIR"/changed.log
grep -q "PASS  probe.core" "$LOG_DIR"/changed.log

# probe.broken: also AFFECTED (declares src/core.txt too), must run, fail, and surface its
# captured diagnostic output (RFC section 21 acceptance criterion 11) -- not just a bare FAIL line.
grep -q "\[AFFECTED\] probe.broken -- RUN" "$LOG_DIR"/changed.log
grep -q "FAIL  probe.broken" "$LOG_DIR"/changed.log
grep -q "diagnostic-detail-line" "$LOG_DIR"/changed.log

# probe.unrelated: declares only src/docs.txt, which did not change -- must be classified
# UNAFFECTED and skipped, never run (no PASS/FAIL line for it at all).
grep -q "\[UNAFFECTED\] probe.unrelated -- SKIP" "$LOG_DIR"/changed.log
if grep -qE "(PASS|FAIL)  probe.unrelated" "$LOG_DIR"/changed.log; then
    echo "FAIL: probe.unrelated ran despite being classified UNAFFECTED" >&2
    exit 1
fi

# probe.nothing_declared: no declared dependencies at all -- must be UNKNOWN, and UNKNOWN always
# runs (RFC 6.1's own invariant, the one this whole test exists to prove end-to-end).
grep -q "\[UNKNOWN\] probe.nothing_declared -- RUN" "$LOG_DIR"/changed.log
grep -q "PASS  probe.nothing_declared" "$LOG_DIR"/changed.log

# `fissure explain` must give the same verdicts with real, non-empty reasoning for both an
# affected and a skipped probe (RFC section 21 acceptance criterion 7: "Every skipped probe is
# explainable with recorded evidence").
echo "=== explain probe.unrelated ==="
"$FISSURE" explain probe.unrelated | tee "$LOG_DIR"/explain_unrelated.log
grep -q "classification: UNAFFECTED" "$LOG_DIR"/explain_unrelated.log
grep -q "declared dependenc" "$LOG_DIR"/explain_unrelated.log

echo "=== explain probe.core ==="
"$FISSURE" explain probe.core | tee "$LOG_DIR"/explain_core.log
grep -q "classification: AFFECTED" "$LOG_DIR"/explain_core.log

# --full must run every probe regardless of classification (RFC section 11's "full" mode) --
# probe.unrelated should now get a PASS/FAIL line despite being UNAFFECTED.
echo "=== --full run ==="
"$FISSURE" run --full > "$LOG_DIR"/full.log 2>&1 || true
cat "$LOG_DIR"/full.log
grep -qE "(PASS|FAIL)  probe.unrelated" "$LOG_DIR"/full.log

# `fissure status` must reflect the persisted graph from the runs above.
echo "=== status ==="
"$FISSURE" status | tee "$LOG_DIR"/status.log
grep -q "probe(s)" "$LOG_DIR"/status.log

echo "fissure_smoke: all checks passed"
