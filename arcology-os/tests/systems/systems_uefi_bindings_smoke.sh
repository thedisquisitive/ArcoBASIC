#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-uefi-bindings-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

expect_accept() {
    local name="$1"
    local file="$2"
    if ! "$ARCOFISSION" reveal "$file" at AST > "$TMP_ROOT/$name.out" 2>&1; then
        echo "FAIL: expected $name to be accepted, but it was rejected:" >&2
        cat "$TMP_ROOT/$name.out" >&2
        exit 1
    fi
}

expect_reject() {
    local name="$1"
    local source="$2"
    local expected_substring="$3"
    local file="$TMP_ROOT/$name.abas"
    printf '%s\n' "$source" > "$file"
    if "$ARCOFISSION" reveal "$file" at AST > "$TMP_ROOT/$name.out" 2>&1; then
        echo "FAIL: expected $name to be rejected, but it was accepted:" >&2
        cat "$TMP_ROOT/$name.out" >&2
        exit 1
    fi
    if ! grep -qF "$expected_substring" "$TMP_ROOT/$name.out"; then
        echo "FAIL: $name diagnostic did not contain expected text: $expected_substring" >&2
        cat "$TMP_ROOT/$name.out" >&2
        exit 1
    fi
}

# Packet WP-006 acceptance: the compiler can type-check the hello-world source and resolve the
# required UEFI fields.
expect_accept "hello_world_fixture" "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-hello/hello.abas"

# Packet 002's hardware test disables the firmware watchdog before intentionally halting. Boot
# Services is a service table (not a protocol), so SetWatchdogTimer has four explicit arguments
# and no implicit This argument.
cat > "$TMP_ROOT/watchdog.abas" <<'SCRIPT'
FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64
    systemTable.BootServices.SetWatchdogTimer(0, 0, 0, 0)
    RETURN 0
END FUNCTION
SCRIPT
expect_accept "watchdog_binding" "$TMP_ROOT/watchdog.abas"

# GOP discovery entry point is bound for semantic validation. The raw GUID/output-pointer
# argument wrapper is intentionally deferred, so this checks only field-chain resolution.
cat > "$TMP_ROOT/gop_locate.abas" <<'SCRIPT'
FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64
    systemTable.BootServices.LocateProtocol(0, 0, 0)
    RETURN 0
END FUNCTION
SCRIPT
expect_accept "gop_locate_binding" "$TMP_ROOT/gop_locate.abas"

# ExitBootServices is exposed as a typed service-table call. The fixture deliberately passes an
# invalid key (0); runtime handoff requires a prior GetMemoryMap implementation and is tested only
# at the binding/lowering layer here.
expect_accept "exit_boot_services_binding" "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-exit-boot-services/exit-boot-services.abas"
expect_accept "memory_map_binding" "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-memory-map/memory-map-bindings.abas"
expect_accept "pointer_to_pointer" "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-memory-map/pointer-to-pointer.abas"
"$ARCOFISSION" reveal "$SOURCE_DIR/arcology-os/tests/fixtures/uefi-memory-map/pointer-to-pointer.abas" at X86_64 > "$TMP_ROOT/pointer_to_pointer.x86"
grep -qF 'lea rsp' "$TMP_ROOT/pointer_to_pointer.x86" || grep -qF '48 8d 44 24' "$TMP_ROOT/pointer_to_pointer.x86"
MAP_FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/uefi-memory-map/acquire-memory-map.abas"
"$ARCOFISSION" reveal "$MAP_FIXTURE" at A-MIR > "$TMP_ROOT/memory-map.amir"
grep -qF 'CALL_EXTERNAL systemTable.BootServices.GetMemoryMap' "$TMP_ROOT/memory-map.amir"
grep -qF 'CALL AcquireMemoryMap' "$TMP_ROOT/memory-map.amir"
"$ARCOFISSION" build "$MAP_FIXTURE" -o "$TMP_ROOT/memory-map.efi" --target uefi-x86_64 --entry Main >/dev/null
test -s "$TMP_ROOT/memory-map.efi"
HANDOFF_FIXTURE="$SOURCE_DIR/arcology-os/tests/fixtures/uefi-memory-map/exit-boot-services-safe.abas"
"$ARCOFISSION" reveal "$HANDOFF_FIXTURE" at X86_64 --entry Main > "$TMP_ROOT/handoff.x86"
grep -qF 'INTERNAL_CALLS 2' "$TMP_ROOT/handoff.x86"
"$ARCOFISSION" build "$HANDOFF_FIXTURE" -o "$TMP_ROOT/handoff.efi" --target uefi-x86_64 --entry Main >/dev/null
test -s "$TMP_ROOT/handoff.efi"

# An unknown field on a known UEFI type is rejected with a diagnostic naming what is bound.
expect_reject "unknown_field" '
FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64
    systemTable.Foo.Bar(1)
    RETURN 0
END FUNCTION' \
    'UEFI.SystemTable has no bound field or method "Foo" in this milestone. Bound fields: ConsoleOut, BootServices.'

# A real UEFI field that this milestone deliberately did not bind is rejected honestly (not
# silently accepted, and not confused with an invented/nonexistent field).
expect_reject "real_but_unbound_field" '
FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64
    systemTable.ConIn.Reset(1)
    RETURN 0
END FUNCTION' \
    'UEFI.SystemTable has no bound field or method "ConIn" in this milestone.'

# An unknown method on a correctly-resolved protocol is rejected, and the diagnostic names the
# protocol type the chain actually resolved to (not the root type or an empty string).
expect_reject "unknown_method_on_protocol" '
FUNCTION Main(systemTable AS UEFI.SystemTable) AS U64
    systemTable.ConsoleOut.Clear()
    RETURN 0
END FUNCTION' \
    'UEFI.SimpleTextOutputProtocol has no bound field or method "Clear" in this milestone. Bound fields: Write.'

# A parameter typed as a UEFI.Handle (opaque, no fields) rejects any field access on it.
expect_reject "handle_has_no_fields" '
FUNCTION Main(imageHandle AS UEFI.Handle) AS U64
    imageHandle.Anything()
    RETURN 0
END FUNCTION' \
    'UEFI.Handle has no bound field or method "Anything" in this milestone. No fields are bound for this type yet.'

# Regression: non-UEFI parameter types are never validated against the UEFI registry.
cat > "$TMP_ROOT/hosted.abas" <<'SCRIPT'
FUNCTION Check(path AS String) AS BOOL
    path.AnythingAtAll()
    RETURN TRUE
END FUNCTION
SCRIPT
expect_accept "hosted_type_unaffected" "$TMP_ROOT/hosted.abas"

echo "PASS: UEFI bindings smoke test"
