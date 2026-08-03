#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-pe-image-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Packet WP-009 objective: produce a valid UEFI-loadable executable.
"$ARCOFISSION" build "$SOURCE_DIR/tests/systems/uefi-hello/hello.abas" -o "$TMP_ROOT/hello.efi" --target uefi-x86_64 > "$TMP_ROOT/build.out"
grep -qF "PE32+ WRITTEN" "$TMP_ROOT/build.out"

ACTUAL_SIZE=$(wc -c < "$TMP_ROOT/hello.efi")
if [ "$ACTUAL_SIZE" -ne 12288 ]; then
    echo "FAIL: expected a 12288-byte image (headers+text+rdata, each one 4096-byte-aligned page), got $ACTUAL_SIZE" >&2
    exit 1
fi

# Packet WP-009 "Required validation": read the exact fields at their known, deterministic file
# offsets (computed the same way tests/runtime_tests.cpp's PE writer unit test verifies them) using
# only `od`, so this test has no dependency beyond what the rest of the suite already needs.
field() {
    # field OFFSET LENGTH -- prints LENGTH bytes at OFFSET as space-separated lowercase hex
    od -An -tx1 -j "$1" -N "$2" "$TMP_ROOT/hello.efi" | tr -s ' \n' ' ' | sed -e 's/^ //' -e 's/ $//'
}

[ "$(field 0 2)" = "4d 5a" ] || { echo "FAIL: missing MZ DOS signature" >&2; exit 1; }
[ "$(field 0x40 4)" = "50 45 00 00" ] || { echo "FAIL: missing PE signature at e_lfanew" >&2; exit 1; }
[ "$(field 0x44 2)" = "64 86" ] || { echo "FAIL: Machine is not IMAGE_FILE_MACHINE_AMD64" >&2; exit 1; }
[ "$(field 0x46 2)" = "02 00" ] || { echo "FAIL: NumberOfSections is not 2" >&2; exit 1; }
# Characteristics must be EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE only. IMAGE_FILE_RELOCS_STRIPPED
# must NOT be set here -- see docs/systems/pe32-image.md: a real boot under QEMU/OVMF proved this
# flag causes the firmware to refuse to load the image ("Not Found" with no other diagnostic),
# even though the PE32+ structure was otherwise byte-perfect per pefile and objdump.
[ "$(field 0x56 2)" = "22 00" ] || { echo "FAIL: Characteristics is not 0x0022 (RELOCS_STRIPPED must not be set)" >&2; exit 1; }
[ "$(field 0x58 2)" = "0b 02" ] || { echo "FAIL: Optional header Magic is not PE32+" >&2; exit 1; }
[ "$(field 0x68 4)" = "00 10 00 00" ] || { echo "FAIL: AddressOfEntryPoint is not 0x1000" >&2; exit 1; }
[ "$(field 0x9c 2)" = "0a 00" ] || { echo "FAIL: Subsystem is not IMAGE_SUBSYSTEM_EFI_APPLICATION" >&2; exit 1; }
# Import Directory (DataDirectory[1] at 0x58 + 0x70 + 1*8 = 0xD0): "absence of host runtime imports".
[ "$(field 0xd0 8)" = "00 00 00 00 00 00 00 00" ] || { echo "FAIL: Import Directory is not empty" >&2; exit 1; }

echo "PASS: PE32+ image structure (file/objdump/pefile-independent field checks)"

# Real acceptance test (Packet WP-009: "OVMF loads the image without an invalid-image error"),
# skipped gracefully rather than failed if the tools this needs are not installed in this
# environment (matching the environment policy WP-010 documents for its own QEMU/OVMF harness).
QEMU_BIN="$(command -v qemu-system-x86_64 || true)"
OVMF_FD="$(find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | head -1 || true)"
if [ -z "$QEMU_BIN" ] || [ -z "$OVMF_FD" ]; then
    echo "SKIP: qemu-system-x86_64 and/or an OVMF firmware image were not found; install qemu-system-x86 "
    echo "      and the ovmf package to exercise the real boot check this test would otherwise run."
    exit 0
fi

BOOT_DIR="$TMP_ROOT/efi_boot"
mkdir -p "$BOOT_DIR/EFI/BOOT"
cp "$TMP_ROOT/hello.efi" "$BOOT_DIR/EFI/BOOT/BOOTX64.EFI"

OUTPUT="$(timeout 15 "$QEMU_BIN" \
    -bios "$OVMF_FD" \
    -drive file="fat:rw:$BOOT_DIR",format=raw \
    -net none -vga none -display none -serial stdio -monitor none -no-reboot 2>/dev/null || true)"

if ! printf '%s' "$OUTPUT" | grep -aq "Hello from ArcoBASIC"; then
    echo "FAIL: OVMF did not print \"Hello from ArcoBASIC\" when booting the built image" >&2
    printf '%s' "$OUTPUT" | tail -c 2000 >&2
    exit 1
fi

echo "PASS: OVMF boots the image and it prints the expected UEFI console output"
