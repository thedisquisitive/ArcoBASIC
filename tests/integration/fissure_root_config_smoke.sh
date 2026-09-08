#!/usr/bin/env bash
set -euo pipefail

# Validates that the repository-root fissure.ab loads and registers the coarse project probes.
# Uses `explain` rather than `run` so this test does not recursively launch the whole CTest suite.

FISSURE="$1"
SOURCE_DIR="$2"

cd "$SOURCE_DIR"

"$FISSURE" explain project.fissure | tee fissure-root-config.log
grep -q "probe: project.fissure" fissure-root-config.log
grep -Eq "classification: (AFFECTED|UNAFFECTED|UNKNOWN)" fissure-root-config.log
rm -f fissure-root-config.log

echo "fissure_root_config_smoke: all checks passed"
