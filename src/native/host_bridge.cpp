// The generic host-function bridge for the native Linux backend (RFC-0049, "finish off Linux
// support"): gives generated native code access to arco::Runtime's own ~244-entry host-function
// dispatch table (Runtime::call_host_function) -- the SAME string/array/math/etc. utility library
// (UPPER, String.Split, Array.Join, Format, and everything else generate_x86_64_function doesn't
// hand-roll dedicated native codegen for) the interpreter and bytecode VM already share -- instead
// of failing to compile outright the moment a program calls one.
//
// Deliberately a SEPARATE translation unit from runtime_abi.cpp: constructing a real arco::Runtime
// needs the full frontend/runtime source set (Runtime's own constructor references the parser, the
// GUI backend, resource_registry, etc. -- see build_linux_native_image's own comment on exactly
// which files and why), which is both heavier and only conditionally available (it needs
// `cmake --build . --target ArcoNativeRuntimeCoreProbe` to have been run at least once in this
// build tree -- the identical opt-in, EXCLUDE_FROM_ALL shape ArcoFissionCapsuleCoreProbe already
// established for the bytecode-capsule format's own "lean runtime" precedent, see CMakeLists.txt).
// Keeping this in its own file means an ordinary native program that never calls a host function
// (everything Phase 1/2/classes already cover with dedicated codegen) never pays for or depends on
// any of this.
//
// ArcoValueBox is intentionally duplicated here (byte-for-byte identical to runtime_abi.cpp's own,
// private, anonymous-namespace definition) rather than shared through a header: neither
// definition's NAME is ever visible outside its own translation unit (both files only ever hand
// out/receive the fully opaque ArcoValue*, reinterpret_cast'd back to whichever TU's own
// ArcoValueBox at the point of use), so there is no linker-visible symbol clash and no actual ODR
// violation -- just two structurally identical private types. If ArcoValueBox's own shape in
// runtime_abi.cpp ever changes, this copy must change with it.
#include "arco/native_runtime_abi.h"
#include "arco/runtime.hpp"
#include "arco/value.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace {

struct ArcoValueBox {
    arco::Value value;
    std::atomic<int> refcount{1};
};

const ArcoValueBox* box(const ArcoValue* handle) { return reinterpret_cast<const ArcoValueBox*>(handle); }

std::string utf16_to_utf8(const uint16_t* text) {
    std::string utf8;
    if (text == nullptr) return utf8;
    // No surrogate-pair handling yet -- matches runtime_abi.cpp's own identical, disclosed Phase 1
    // simplification (see native_runtime_abi.h); every string literal generate_x86_64_function's
    // Const case produces today is plain BMP text.
    for (const uint16_t* cursor = text; *cursor != 0; ++cursor) {
        const unsigned int unit = *cursor;
        if (unit < 0x80) {
            utf8.push_back(static_cast<char>(unit));
        } else if (unit < 0x800) {
            utf8.push_back(static_cast<char>(0xC0 | (unit >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        } else {
            utf8.push_back(static_cast<char>(0xE0 | (unit >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((unit >> 6) & 0x3F)));
            utf8.push_back(static_cast<char>(0x80 | (unit & 0x3F)));
        }
    }
    return utf8;
}

} // namespace

extern "C" ArcoValue* arco_call_host(const uint16_t* name_utf16, ArcoValue* const* args, int64_t arg_count) {
    // One process-lifetime Runtime for the whole program -- matching exactly how the interpreter
    // (arco_cli) and the bytecode VM (execute_bytecode) each construct exactly one Runtime for a
    // full program run, so a host function that keeps its own state across calls behaves
    // identically here. Deliberately leaked (never destructed) rather than an ordinary function-
    // local static Runtime: Runtime's own destructor tears down GUI/resource state that has no
    // well-defined order relative to other static destructors also running at exit, the same
    // "construct once, never destruct" idiom used elsewhere in this ABI (see global_store's
    // sibling comment in runtime_abi.cpp for the same static-initialization-order motivation, one
    // level earlier in the object's lifetime).
    static arco::Runtime& runtime = [] () -> arco::Runtime& {
        auto* instance = new arco::Runtime();
        // RuntimeLimits::instruction_limit defaults to 100000 -- a safety cap meant to bound a
        // runaway BYTECODE interpreter loop (arco_cli/execute_bytecode), which does not apply here
        // at all: the calling program is already compiled to real native machine code with no
        // interpreter loop to run away, and Runtime::call_host_function itself calls tick() on
        // every single host-function call regardless of caller. A real, surprising failure found by
        // direct testing (RFC-0049, "full Linux support" pass): a native program calling an
        // ordinary host function like UPPER more than 100,000 times over its own lifetime (e.g.
        // inside a long-running loop) hit "instruction limit exceeded" for a reason no native
        // caller would expect or intend. Disabled via the same override+prepare_execution path a
        // bytecode caller would use to raise its own limit -- 0 means unlimited (tick()'s own `> 0`
        // guard skips the check entirely); instruction_limit_hard_maximum_ is never set for this
        // Runtime, so prepare_execution accepts the override without complaint.
        instance->set_instruction_limit_override(0);
        instance->prepare_execution(std::nullopt);
        return *instance;
    }();
    const std::string name = utf16_to_utf8(name_utf16);
    std::vector<arco::Value> values;
    values.reserve(arg_count < 0 ? 0 : static_cast<std::size_t>(arg_count));
    for (int64_t i = 0; i < arg_count; ++i) {
        values.push_back(args[i] == nullptr ? arco::Value() : box(args[i])->value);
    }
    try {
        arco::Value result = runtime.call_host_function(name, values);
        return reinterpret_cast<ArcoValue*>(new ArcoValueBox{std::move(result)});
    } catch (const arco::ExitSignal& signal) {
        // Exit()/ExitTheProgram() (a core builtin, see Runtime's constructor in runtime.cpp) is a
        // real, immediate, CLEAN process exit, not an error -- run_bytecode/run_bytecode_binary
        // (fission.cpp) both already special-case this exact exception for the bytecode-VM/capsule
        // paths; this backend's own generic host bridge had no matching catch at all, so
        // ExitSignal fell through to the generic `catch (const std::exception&)` just below
        // (ExitSignal derives from std::exception) and was turned into a spurious crash-looking
        // panic instead -- a real bug caught by direct testing: Arconaut, a real program, calls
        // ExitTheProgram(0) on its very first no-display-session/`--smoke` early-exit path, so
        // EVERY invocation of the compiled binary in a smoke-test/headless context printed
        // "BYTECODE RUN FAILED" with an empty message (ExitSignal::what() carries no text) and
        // exited 1, instead of the clean, silent, exit-0 every other backend already gives this
        // exact call. No output to flush here first (unlike those two catches): arco_value_print
        // writes directly to real stdout on every PRINT already, nothing is buffered in an
        // ostringstream the way the bytecode-VM paths need to drain before exiting.
        std::exit(signal.code());
    } catch (const std::exception& error) {
        // No C++ exception may cross this ABI boundary (generated machine code has no unwind
        // tables of its own) -- an unrecognized host function name, or any exception the
        // underlying function itself throws (e.g. a real runtime_error from a malformed argument),
        // is turned into a clean panic instead, the same discipline runtime_abi.cpp's own
        // arco_value_panic exists for.
        arco_value_panic(error.what());
    }
    return nullptr; // unreachable -- arco_value_panic never returns (std::exit)
}
