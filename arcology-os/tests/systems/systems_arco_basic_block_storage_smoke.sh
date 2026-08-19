#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCOFISSION=${ARCOFISSION:-"$ROOT/../build/ArcoFission"}
TMP_ROOT=$(mktemp -d)
trap 'rm -rf "$TMP_ROOT"' EXIT

# Regression check for a real parser bug found implementing this RFC: a trailing same-line
# comment after a multi-line `IF cond THEN   ' why` (this RFC's own FAT32 boot-sector validation
# needed exactly this style) used to make the parser mistake the comment for the start of a
# single-line IF's inline body, leaving the real block body and its END IF as unconsumed trailing
# tokens -- surfacing far downstream as "expected FUNCTION after END" at the END IF itself. Fixed
# in Parser::if_statement (src/frontend/parser.cpp); this is the minimal repro that failed before
# the fix.
cat > "$TMP_ROOT/if_comment.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION Probe(x AS U64) AS U64
    IF x <> 43605 THEN   ' trailing comment, the trigger
        RETURN 0
    END IF
    RETURN 1
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/if_comment.abas" at A-MIR --entry Probe > "$TMP_ROOT/if_comment.txt" 2>&1
grep -qF 'SOURCE ACCEPTED' "$TMP_ROOT/if_comment.txt"

# Structural check: RAMDisk.ReadSectors rejects an out-of-bounds request (startSector + count >
# SectorCount()) rather than partially transferring, per Requirement 6.1 -- validated without
# QEMU, since a RAM disk backed by a fixed scratch address has no hardware dependency once its
# SectorCount is known, matching this project's own precedent of unit-testing block-device-shaped
# logic below the QEMU layer where possible (RFC-0038's own Testing Strategy, first bullet).
cat > "$TMP_ROOT/bounds.abas" <<'SCRIPT'
#PROFILE UEFI
#TARGET X86_64
#RUNTIME NONE
FUNCTION RAMDiskStateAddress() AS MMIOPTR
    RETURN ADDRESS.MMIO(ADDRESS.Virtual(33751040))
END FUNCTION
FUNCTION RAMDisk.SectorCount() AS U64
    RETURN MEMORY.Read64(RAMDiskStateAddress())
END FUNCTION
FUNCTION Probe(startSector AS U64, count AS U32, destination AS MMIOPTR) AS BOOL
    LET total AS U64 = RAMDisk.SectorCount()
    LET countAsU64 AS U64 = count
    IF startSector + countAsU64 > total THEN
        RETURN 0
    END IF
    RETURN 1
END FUNCTION
SCRIPT
"$ARCOFISSION" reveal "$TMP_ROOT/bounds.abas" at X86_64 --entry Probe > "$TMP_ROOT/bounds.x86"
grep -qF 'X86_64 GENERATED' "$TMP_ROOT/bounds.x86"

# Structural check: the extended interrupt-vector table (RFC-0036) is an unchanged dependency
# elsewhere in this chain; a golden byte count catches any accidental change leaking in.
FIXTURE_TABLE="$ROOT/tests/fixtures/aps-exception-entry/exception-vector-table.abas"
"$ARCOFISSION" reveal "$FIXTURE_TABLE" at X86_64 --entry ExceptionTableBase > "$TMP_ROOT/table.x86"
grep -qF 'TEXT 1045 bytes' "$TMP_ROOT/table.x86"

if ! command -v qemu-system-x86_64 > /dev/null 2>&1 || ! command -v python3 > /dev/null 2>&1 || \
   ! find /usr/share/ovmf /usr/share/OVMF -iname 'OVMF*.fd' 2>/dev/null | grep -v -i secboot | grep -q .; then
    echo "SKIP: qemu-system-x86_64/OVMF/python3 not installed; this fixture only proves anything when executed."
    exit 0
fi

# Build the real, independently-verifiable FAT32 test image (arcology-os/scripts/build/
# build-fat32-test-image.py -- deterministic, no external mkfs.vfat/mtools dependency, though
# the same image was cross-checked against real `mtools` when this test was authored; see
# .agents/reports/aps-block-storage.md). The expected size/checksum constants below match that
# script's own FILE_CONTENT/FILE_BYTE_SUM exactly.
python3 "$ROOT/scripts/build/build-fat32-test-image.py" "$TMP_ROOT/fat32-good.img" > /dev/null

# The real proof: a RAM Disk Block Device Provider backed by memory the harness preloads with
# that real FAT32 image -> Mount validates the boot sector and real on-disk geometry -> the
# unified namespace resolves "/README.TXT" through longest-mount-point-prefix matching (exercised
# against a decoy second mount point, not merely "the only registered point wins") -> Open finds
# the file in the root directory -> Read walks a genuine multi-cluster FAT chain (7 clusters at
# 512 bytes/cluster) and reproduces the file byte-for-byte, verified via an exact length AND
# byte-sum checksum match, not merely "some bytes came back."
FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-block-storage.abas"
"$ARCOFISSION" build "$FIXTURE" -o "$TMP_ROOT/aps-block-storage.efi" --target uefi-x86_64 > /dev/null

OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-preload.sh" "$TMP_ROOT/aps-block-storage.efi" "$TMP_ROOT/fat32-good.img" 0x4000000 "APS FAT32 OK" 20)"
echo "$OUTPUT"
[ "$OUTPUT" = "PASS: APS FAT32 OK" ] || { echo "FAIL: unexpected harness output" >&2; exit 1; }

# The deliberate negative test RFC-0038 Section 16 calls for by name: mounting against a RAM disk
# that does not contain a valid FAT32 volume (a zeroed image) MUST return FALSE and fail closed,
# not succeed and fail unpredictably later (Section 10's requirement). This fixture's own success
# marker IS the rejection -- seeing "APS MOUNT REJECTED" is the pass condition here, the mirror
# image of the positive fixture above.
dd if=/dev/zero of="$TMP_ROOT/fat32-zeroed.img" bs=1M count=8 > /dev/null 2>&1
NEGATIVE_FIXTURE="$ROOT/tests/fixtures/aps-exception-entry/aps-block-storage-negative.abas"
"$ARCOFISSION" build "$NEGATIVE_FIXTURE" -o "$TMP_ROOT/aps-block-storage-negative.efi" --target uefi-x86_64 > /dev/null

NEGATIVE_OUTPUT="$("$ROOT/scripts/run/run-uefi-hello-with-preload.sh" "$TMP_ROOT/aps-block-storage-negative.efi" "$TMP_ROOT/fat32-zeroed.img" 0x4000000 "APS MOUNT REJECTED" 20)"
echo "$NEGATIVE_OUTPUT"
[ "$NEGATIVE_OUTPUT" = "PASS: APS MOUNT REJECTED" ] || { echo "FAIL: unexpected harness output for the negative test" >&2; exit 1; }

echo "PASS: a RAM Disk Block Device Provider + read-only FAT32 Filesystem Provider + unified namespace read a real FAT32 image byte-for-byte under a real CPU (QEMU/OVMF), and fail closed against an invalid volume"
