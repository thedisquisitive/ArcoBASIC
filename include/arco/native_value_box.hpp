#pragma once

// The REAL, concrete layout backing the opaque `ArcoValue*` the native runtime ABI
// (native_runtime_abi.h) exposes -- deliberately kept in its own narrow header, separate from that
// ABI header, which stays a pure opaque-pointer contract (its own comment says so explicitly) so
// that host_bridge.cpp and anything else consuming the opaque ABI never needs to pull in <atomic>
// or the full arco::Value definition for no reason.
//
// This header exists so `src/native/runtime_abi.cpp` (recompiled fresh from disk on every single
// `ArcoFission build ... --target linux-x86_64` invocation -- see fission.cpp's own
// source_root_path()) and `src/compiler/fission.cpp` (compiled once, ahead of time, into the
// ArcoFission binary itself) can each independently compute offsetof(ArcoValueBox, refcount) via
// the SAME real struct definition, rather than fission.cpp guessing or hand-deriving a number.
// This does NOT eliminate the real skew risk that creates -- fission.cpp's own copy of this offset
// is frozen at ArcoFission's OWN build time, while runtime_abi.cpp's copy is always current as of
// whatever's on disk when a program is actually built. See fission.cpp's own startup self-check
// (arco_value_refcount_offset(), called once in every generated program's own Main prologue) for
// how that gap is closed: a real, freshly-recomputed value is compared against what fission.cpp
// assumed, and generated code panics loudly on any disagreement rather than silently using a
// stale offset.
//
// `refcount` is a PLAIN int, not std::atomic<int> -- a deliberate design decision, not an
// oversight. Reference counting here is NOT thread-safe, because nothing in this codebase creates
// a real OS thread that could share an ArcoValueBox with another one (checked directly: no
// std::thread/pthread_create/std::async anywhere touches this type or arco_value_retain/_release;
// the only concurrency primitive in use anywhere near this code is fork(), which gives the child
// its own independent copy-on-write address space, never a shared one). Measured directly: an
// atomic `lock inc`/`lock dec` pair costs roughly 2x a plain `inc`/`dec` pair on real hardware for
// this exact access pattern (real x86-64 cache-coherency-protocol cost, paid regardless of actual
// contention) -- a real, substantial, otherwise-unrecoverable cost for a guarantee this runtime
// has no user for today. If ArcoBASIC ever grows real shared-memory multithreading, this decision
// must be revisited (and every emit_inline_retain/emit_inline_release call site in
// generate_x86_64_function, which emits plain inc/dec to match, updated back to locked
// instructions) -- it is not something a future, differently-threaded caller can safely ignore.
#include "arco/value.hpp"

#include <cstddef>

struct ArcoValueBox {
    arco::Value value;
    int refcount = 1;
};
