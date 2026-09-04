#!/usr/bin/env bash
set -euo pipefail

ARCFSCTL=$1
TMP_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/arcfsctl-smoke.XXXXXX")
trap 'rm -rf "$TMP_ROOT"' EXIT
export PATH="$(dirname "$ARCFSCTL"):$PATH"

status=$("$ARCFSCTL" status)
grep -q '^platform: linux$' <<<"$status"
grep -q '^arcfs-linux: ' <<<"$status"

image=$TMP_ROOT/volume.arcfs
"$ARCFSCTL" create-image "$image" 4194304 --format >/dev/null
"$ARCFSCTL" inspect "$image" | grep -q '^ArcFS FMV2 volume$'
"$ARCFSCTL" ls "$image" / >/dev/null
