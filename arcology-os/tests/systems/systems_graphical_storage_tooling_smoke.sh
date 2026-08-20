#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; this fixture only proves anything when executed."
    exit 0
fi

# The real proof (RFC-0039 Phase G, "graphical storage tooling"): renders a real, on-screen GOP
# framebuffer visual health indicator for two REAL ArcFS volume states -- healthy and degraded --
# built with ordinary ArcFS calls and read back via ArcFS.GetHealthState(). RFC-0039 Section 59
# requires health state not be conveyed by color alone, so each state renders with BOTH a
# distinct field color AND a distinct positional black mark (a center bar for Healthy, a
# top-left corner block for Degraded) -- verified by reading the fixture's own written pixels
# back and confirming each region shows exactly its own state's mark and none of the others',
# proving the shapes are genuinely distinct, not merely differently colored copies of one glyph.
#
# Needs a real `-vga std` display device for GOP to exist at all (run-uefi-hello-with-gpu.sh,
# distinct from every other ArcFS fixture's run-uefi-hello.sh, which deliberately disables the
# display so OVMF's console lands on serial instead) -- the fixture itself still reports its
# result over serial, unaffected by which console ConOut happens to default to.
FIXTURE="$ROOT/tests/fixtures/graphical-storage-tooling/graphical-storage-tooling.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/graphical-storage-tooling.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-gpu.sh" "$TMP_ROOT/graphical-storage-tooling.efi" "APS GRAPHICAL OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS GRAPHICAL OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: a real GOP framebuffer renders distinct color+shape health indicators for a real healthy and a real degraded ArcFS volume, verified by reading the rendered pixels back -- under a real CPU (QEMU/OVMF with a real display device)"
