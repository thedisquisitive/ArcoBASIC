#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcology-hardware-artifact-smoke-$$"
mkdir -p "$TMP_ROOT/one" "$TMP_ROOT/two"
trap 'rm -rf "$TMP_ROOT"' EXIT

"$SOURCE_DIR/scripts/build-arcology-hardware-artifact.sh" "$ARCOFISSION" "$TMP_ROOT/one" >/dev/null
"$SOURCE_DIR/scripts/build-arcology-hardware-artifact.sh" "$ARCOFISSION" "$TMP_ROOT/two" >/dev/null

cmp "$TMP_ROOT/one/BOOTX64.EFI" "$TMP_ROOT/two/BOOTX64.EFI"
cmp "$TMP_ROOT/one/arcology-seed-0.1-x86_64.img" "$TMP_ROOT/two/arcology-seed-0.1-x86_64.img"
cmp "$TMP_ROOT/one/SHA256SUMS" "$TMP_ROOT/two/SHA256SUMS"
[ "$(wc -c < "$TMP_ROOT/one/arcology-seed-0.1-x86_64.img")" -eq 67108864 ]

# Fixed FAT32 BPB and standard removable-media pathname. mtools is an optional independent reader,
# not an artifact-build dependency.
BOOT_SIGNATURE=$(od -An -tx1 -j 510 -N 2 "$TMP_ROOT/one/arcology-seed-0.1-x86_64.img" | tr -d ' \n')
[ "$BOOT_SIGNATURE" = "55aa" ]
if command -v mcopy >/dev/null 2>&1; then
    mcopy -i "$TMP_ROOT/one/arcology-seed-0.1-x86_64.img" ::EFI/BOOT/BOOTX64.EFI "$TMP_ROOT/extracted.efi"
    cmp "$TMP_ROOT/one/BOOTX64.EFI" "$TMP_ROOT/extracted.efi"
fi

if ! command -v qemu-system-x86_64 >/dev/null 2>&1 || \
   ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo 'SKIP: deterministic artifact passed; QEMU/OVMF is unavailable.'
    exit 0
fi

OUTPUT=$("$SOURCE_DIR/scripts/run-arcology-hardware-image.sh" \
    "$TMP_ROOT/one/arcology-seed-0.1-x86_64.img" 'System halted intentionally.' 20)
[ "$OUTPUT" = 'PASS: System halted intentionally.' ]

echo 'PASS: byte-identical FAT32 artifact and QEMU/OVMF hardware-test boot'
