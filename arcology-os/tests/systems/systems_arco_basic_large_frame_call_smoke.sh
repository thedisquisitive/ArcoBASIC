#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
FIXTURE="$ROOT/tests/fixtures/integer-core/large-frame-call.abas"
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

"$ARCOFISSION" reveal "$FIXTURE" at X86_64 --entry LargeFrameLoop > "$TMP_ROOT/x86.txt"

# frame_size for LargeFrameLoop lands in [128,255] -- the exact range where `sub/add rsp, imm8`
# (opcode 0x83, a sign-extended one-byte immediate) previously encoded a negative displacement,
# silently corrupting the stack instead of allocating a frame. Confirm the prologue uses the
# 7-byte `sub rsp, imm32` form (opcode 0x81), not the 4-byte imm8 form.
grep -qF '48 81 ec 98 00 00 00' "$TMP_ROOT/x86.txt"

# The buggy imm8 form would still appear here as "48 83 ec 98" if it ever regressed.
if grep -qF '48 83 ec 98' "$TMP_ROOT/x86.txt"; then
    echo "FAIL: prologue used the sign-extension-unsafe sub rsp, imm8 form" >&2
    exit 1
fi

"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/large-frame-call.efi" --target uefi-x86_64 > /dev/null

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF not installed; static encoding check above still ran."
    exit 0
fi

# The real proof: booted under QEMU/OVMF, a corrupted frame doesn't hang cleanly -- it produces a
# wild jump to a bogus return address and a CPU exception (#UD) well before "OK" ever prints.
OUTPUT="$("$ROOT/scripts/run/run-uefi-hello.sh" "$TMP_ROOT/large-frame-call.efi" "OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

echo "PASS: a [128,255]-byte stack frame calling another function does not corrupt the stack"
