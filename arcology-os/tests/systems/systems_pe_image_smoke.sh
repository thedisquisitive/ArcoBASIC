#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-pe-image-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

# Packet WP-009 objective: produce a valid UEFI-loadable executable.
"$ARCOFISSION" build "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-hello/hello.abas" -o "$TMP_ROOT/hello.efi" --target uefi-x86_64 > "$TMP_ROOT/build.out"
grep -qF "PE32+ WRITTEN" "$TMP_ROOT/build.out"

ACTUAL_SIZE=$(wc -c < "$TMP_ROOT/hello.efi")
if [ "$ACTUAL_SIZE" -ne 12288 ]; then
    echo "FAIL: expected a 12288-byte image (headers+text+rdata, each one 4096-byte-aligned page), got $ACTUAL_SIZE" >&2
    exit 1
fi

# Packet WP-009 "Required validation": read the exact fields at their known, deterministic file
# offsets (computed the same way tests/unit/runtime_tests.cpp's PE writer unit test verifies them) using
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
# must NOT be set here -- see arcology-os/docs/systems/pe32-image.md: a real boot under QEMU/OVMF proved this
# flag causes the firmware to refuse to load the image ("Not Found" with no other diagnostic),
# even though the PE32+ structure was otherwise byte-perfect per pefile and objdump.
[ "$(field 0x56 2)" = "22 00" ] || { echo "FAIL: Characteristics is not 0x0022 (RELOCS_STRIPPED must not be set)" >&2; exit 1; }
[ "$(field 0x58 2)" = "0b 02" ] || { echo "FAIL: Optional header Magic is not PE32+" >&2; exit 1; }
[ "$(field 0x68 4)" = "00 10 00 00" ] || { echo "FAIL: AddressOfEntryPoint is not 0x1000" >&2; exit 1; }
[ "$(field 0x9c 2)" = "0a 00" ] || { echo "FAIL: Subsystem is not IMAGE_SUBSYSTEM_EFI_APPLICATION" >&2; exit 1; }
# Import Directory (DataDirectory[1] at 0x58 + 0x70 + 1*8 = 0xD0): "absence of host runtime imports".
[ "$(field 0xd0 8)" = "00 00 00 00 00 00 00 00" ] || { echo "FAIL: Import Directory is not empty" >&2; exit 1; }

echo "PASS: PE32+ image structure (file/objdump/pefile-independent field checks)"

# The real "does OVMF actually load and run this" acceptance test lives in
# arcology-os/tests/systems/systems_qemu_ovmf_harness_smoke.sh (Packet WP-010), which uses the reusable
# arcology-os/scripts/run/run-uefi-hello.sh harness. This test stays structural-only and dependency-free so it
# always runs, independent of whether QEMU/OVMF are installed.
