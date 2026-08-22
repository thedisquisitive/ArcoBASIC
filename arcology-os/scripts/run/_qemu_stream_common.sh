#!/usr/bin/env bash
# Shared streaming-capture helper for every run-uefi-hello*.sh / run-arcology-hardware-image.sh /
# run-uefi-image-with-usb-gpu.sh harness in this directory. Every one of those scripts used to
# capture QEMU's serial output via `timeout N qemu-system-x86_64 ... || true` -- meaning even a
# PASSING run had to wait out the FULL fixed TIMEOUT_SECONDS before the captured output was ever
# inspected, since `timeout` doesn't return early just because the child already printed what the
# harness wanted. A real ArcoBASIC UEFI test application typically reaches its own DONE/PASS
# marker and CPU.HaltForever within a couple of seconds of boot -- the fixed 20-30s timeouts this
# directory's scripts use are a FAILURE-CASE ceiling (a hung/crashed VM that never prints anything
# must still not block the suite past it), not a realistic PASSING-case duration.
#
# First attempt at fixing this used a busy-poll loop (grep the growing output file every 0.1s,
# later 1s). That was a REAL regression, not just a theoretical risk: under `ctest -jN` on a
# machine with only N real cores, every concurrently-running poll loop forks a `grep` (and
# originally also a `date`) subprocess on every iteration, competing for the SAME fully-saturated
# CPU the QEMU processes themselves need. This measurably pushed an already CPU-heavy, borderline
# test (systems_arco_basic_arcfs_long_session_durability_smoke: ~93s solo, 150s ceiling) over its
# own timeout under -j4 parallel load -- confirmed by isolating the harness change alone (reverting
# ONLY this file made the same test pass again under identical parallel load).
#
# This version instead waits EVENT-DRIVEN, not time-polled: `tail -f` (GNU coreutils, uses inotify
# on Linux) blocks with near-zero CPU cost until the output file actually grows, `grep -m1` exits
# the instant the expected string appears (closing the pipe, which stops `tail -f` via SIGPIPE on
# its next write), and the whole wait is bounded by a real `timeout` exactly like the original
# approach -- so a run that never produces the expected string costs the SAME near-zero CPU while
# waiting as the pre-streaming version did, while a run that finishes quickly still returns almost
# immediately instead of waiting out the full ceiling.
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
    # killing it directly below needs no signal-forwarding assumptions about an intermediary
    # `timeout` process.
    "$@" > "$outfile" 2>/dev/null &
    local qemu_pid=$!

    local matched=0
    if timeout "$timeout_seconds" bash -c 'tail -n +1 -f "$1" | grep -m1 -qF "$2"' _ "$outfile" "$expected"; then
        matched=1
    fi

    kill "$qemu_pid" 2>/dev/null || true
    wait "$qemu_pid" 2>/dev/null || true

    local captured
    captured="$(cat "$outfile" 2>/dev/null || true)"
    rm -f "$outfile"

    # Final full-text check even on the timeout path: the expected string could have landed in
    # the last flush right as the deadline hit, between grep's own read and the kill above.
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
