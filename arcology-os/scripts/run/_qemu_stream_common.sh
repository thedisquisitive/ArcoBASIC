#!/usr/bin/env bash
# Shared streaming-capture helper for every run-uefi-hello*.sh / run-arcology-hardware-image.sh /
# run-uefi-image-with-usb-gpu.sh harness in this directory. Every one of those scripts used to
# capture QEMU's serial output via `timeout N qemu-system-x86_64 ... || true` -- meaning even a
# PASSING run had to wait out the FULL fixed TIMEOUT_SECONDS before the captured output was ever
# inspected, since `timeout` doesn't return early just because the child already printed what the
# harness wanted. A real ArcoBASIC UEFI test application typically reaches its own DONE/PASS
# marker and CPU.HaltForever within a couple of seconds of boot -- the fixed 20-30s timeouts this
# directory's scripts use are a FAILURE-CASE ceiling (a hung/crashed VM that never prints anything
# must still not block the suite past it), not a realistic PASSING-case duration. Polling the
# growing output for the expected string and killing QEMU the moment it appears turns most
# passing runs from "wait the full timeout" into "however long boot plus the test itself actually
# takes" -- the single biggest lever available for the regression suite's own wall-clock time,
# since every QEMU-based smoke test in this project routes through one of these six scripts.
#
# qemu_run_and_check TIMEOUT_SECONDS EXPECTED_OUTPUT QEMU_BIN [QEMU_ARGS...]
#
# Prints "PASS: EXPECTED_OUTPUT" and returns 0 if EXPECTED_OUTPUT appears in QEMU's captured
# stdout before TIMEOUT_SECONDS elapses; prints "FAIL: ..." plus the last 4000 bytes of captured
# output to stderr and returns 1 otherwise. Never exits the calling shell itself -- callers still
# do their own `... ; exit $?`, matching every other function convention in this project.
qemu_run_and_check() {
    local timeout_seconds="$1"; shift
    local expected="$1"; shift

    local outfile
    outfile="$(mktemp)"

    # QEMU launched directly (no `timeout` wrapper) so $qemu_pid is the real qemu process --
    # killing it directly, either on match below or on our own deadline, needs no signal-forwarding
    # assumptions about an intermediary `timeout` process.
    "$@" > "$outfile" 2>/dev/null &
    local qemu_pid=$!

    local start_ts
    start_ts=$(date +%s)
    local matched=0
    while kill -0 "$qemu_pid" 2>/dev/null; do
        if grep -aqF "$expected" "$outfile" 2>/dev/null; then
            matched=1
            break
        fi
        local now
        now=$(date +%s)
        if [ $(( now - start_ts )) -ge "$timeout_seconds" ]; then
            break
        fi
        sleep 0.1
    done

    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true

    local captured
    captured="$(cat "$outfile" 2>/dev/null || true)"
    rm -f "$outfile"

    # Final full-text check even in the "loop exited via timeout" path: the expected string could
    # have landed in the last flush right as the deadline hit, between a poll and the kill above.
    if [ "$matched" = "0" ] && printf '%s' "$captured" | grep -aqF "$expected"; then
        matched=1
    fi

    if [ "$matched" = "1" ]; then
        echo "PASS: $expected"
        return 0
    fi

    echo "FAIL: expected output not found: $expected" >&2
    echo "--- captured console output (last 4000 bytes) ---" >&2
    printf '%s' "$captured" | tail -c 4000 >&2
    return 1
}
