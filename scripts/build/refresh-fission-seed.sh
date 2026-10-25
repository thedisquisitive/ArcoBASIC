#!/usr/bin/env bash
set -euo pipefail

# Refreshes the committed Fission seed compiler (packaging/fission/seed/).
#
# Starting from the current seed (or FISSION_SEED=...), compiles fission/ twice
# and requires the two generations to be byte-identical -- a true fixpoint of
# the checked-in sources -- before it replaces the seed and its checksum.
# Run it whenever fission/ changes in a way that alters the compiler, then
# commit the new seed together with the change.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
SEED_DIR="$REPO_ROOT/packaging/fission/seed"
SEED_BINARY="$SEED_DIR/fission-native-linux-x86_64"
CURRENT="${FISSION_SEED:-$SEED_BINARY}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

[[ -x "$CURRENT" ]] || { echo "refresh-fission-seed: no compiler to start from: $CURRENT" >&2; exit 1; }
cd "$REPO_ROOT"
"$CURRENT" build fission/cli/fission_build.abas -o "$WORK/gen1"
"$WORK/gen1" build fission/cli/fission_build.abas -o "$WORK/gen2"
cmp -s "$WORK/gen1" "$WORK/gen2" || { echo "refresh-fission-seed: generations differ; not a fixpoint" >&2; exit 1; }

mkdir -p "$SEED_DIR"
install -m 0755 "$WORK/gen2" "$SEED_BINARY"
(cd "$SEED_DIR" && sha256sum "$(basename "$SEED_BINARY")" > "$(basename "$SEED_BINARY").sha256")
echo "refresh-fission-seed: seed updated ($(wc -c < "$SEED_BINARY") bytes, fixpoint verified)"
cat "$SEED_BINARY.sha256"
