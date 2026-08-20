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

# The real proof (RFC-0039 Phase G, "recovery environment support"): a DISTINCT boot artifact
# from ordinary boot's ArcFS.ActivateSystemVolume -- instead of silently self-healing, it stages
# RFC-0039 Section 39's own Inspect -> Apply -> Verify as three independently observable steps,
# reusing Phase F's existing primitives (ArcFS.ScrubVolume, ArcFS.RepairReattachOrphans,
# ArcFS.RepairCommit) completely unchanged. Proves both the already-healthy case (Inspect finds
# nothing, Apply is proven to be genuinely SKIPPED, not merely harmless) and the real-defect case
# (Inspect finds the same orphan scenario Phase F's own fixture builds, Apply fixes it, Verify
# confirms the fix, and the repaired object is still the SAME object -- same OID, same content) in
# one continuous boot session, with every stage's own result reported over serial.
FIXTURE="$ROOT/tests/fixtures/recovery-environment/recovery-environment.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/recovery-environment.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/recovery-environment.efi" "APS RECOVERY OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS RECOVERY OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: the recovery environment boot artifact stages Inspect/Apply/Verify as independently observable steps, correctly skips repair on a healthy volume, and correctly fixes and re-verifies a real structural defect -- under a real CPU (QEMU/OVMF)"
