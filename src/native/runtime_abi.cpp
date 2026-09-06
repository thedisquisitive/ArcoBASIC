// The native ArcoSH system runtime -- implementation. See include/arco/native_runtime_abi.h for the
// ABI contract and design rationale. This file (formerly shim.cpp, three narrow print-only
// functions) is now a real, if still small and deliberately minimal, runtime library: a thin,
// explicitly reference-counted box around the existing, already-correct arco::Value
// (include/arco/value.hpp), which needs no linking beyond this header -- Value is entirely
// inline-defined except for its RuntimeHandle constructor (src/runtime/runtime_handles.cpp), which
// this file never touches. Deliberately kept this small and dependency-free: arco_call_host (the
// generic bridge into a full arco::Runtime -- see src/native/host_bridge.cpp) lives in a SEPARATE
// translation unit specifically so an ordinary native build that never needs a host function keeps
// working with zero dependency on the (much heavier, and only conditionally available) lean
// runtime core -- see host_bridge.cpp's own header comment and build_linux_native_image's.
//
// No C++ exceptions cross this ABI boundary: generated machine code has no unwind tables of its own
// to catch one, so every function here that could otherwise throw (arco::Value::as_number() on a
// non-number, for instance) is expected to be called only after the corresponding is_* check, the
// same discipline the interpreter's own callers already follow. A violation is a real bug in the
// generated code, not something this layer tries to recover from gracefully.

#include "arco/native_runtime_abi.h"
#include "arco/value.hpp"

#include <atomic>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct ArcoValueBox {
    arco::Value value;
    std::atomic<int> refcount{1};
};

// Backs arco_runtime_capture_args/arco_runtime_args (RFC-0049, "full Linux support" pass). Raw
// argc/argv, not a copied std::vector<std::string> -- the C runtime's own argv strings are valid
// for the whole process lifetime (they live in the process's own initial stack region, never
// freed), so there is nothing to own or copy here; arco_runtime_args() itself does the copying,
// once, whenever a real ArcoBASIC-level array is actually needed.
int& captured_argc() {
    static int argc = 0;
    return argc;
}
char**& captured_argv() {
    static char** argv = nullptr;
    return argv;
}

// Backs arco_global_set/arco_global_get (script-scope globals and SHARED class fields). Function-
// local static rather than a namespace-scope global purely to sidestep static-initialization-order
// concerns; this native backend's own generated programs are single-threaded (no concurrency
// primitives exist anywhere in this ABI), so no locking is needed around it.
std::unordered_map<std::string, arco::Value>& global_store() {
    static std::unordered_map<std::string, arco::Value> store;
    return store;
}

// Backs TRY/CATCH (see native_runtime_abi.h's own much larger comment on arco_try_push): a process-
// wide, LIFO stack of jmp_buf pointers, each owned by the generated code that pushed it (a stack
// slot in that function's own frame, still live for as long as its TRY block is). Function-local
// static for the same reason global_store() above is.
std::vector<void*>& handler_stack() {
    static std::vector<void*> stack;
    return stack;
}

// The {Message, Type} object most recently raised, read back by arco_try_error() immediately after
// a longjmp lands. Function-local static for the same reason.
arco::Value& current_error() {
    static arco::Value error;
    return error;
}

ArcoValueBox* box(ArcoValue* handle) { return reinterpret_cast<ArcoValueBox*>(handle); }
const ArcoValueBox* box(const ArcoValue* handle) { return reinterpret_cast<const ArcoValueBox*>(handle); }

std::string utf16_to_utf8(const uint16_t* text) {
    std::string utf8;
    if (text == nullptr) return utf8;
    // No surrogate-pair handling yet -- matches this backend's other disclosed Phase 1
    // simplifications (see native_runtime_abi.h); every string literal generate_x86_64_function's
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

extern "C" {

ArcoValue* arco_value_new_number(double value) {
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(value)});
}

ArcoValue* arco_value_new_string_utf16(const uint16_t* text) {
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(utf16_to_utf8(text))});
}

ArcoValue* arco_value_new_bool(int value) {
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(value != 0)});
}

ArcoValue* arco_value_concat(const ArcoValue* left, const ArcoValue* right) {
    const std::string left_text = left == nullptr ? "NULL" : box(left)->value.to_string();
    const std::string right_text = right == nullptr ? "NULL" : box(right)->value.to_string();
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(left_text + right_text)});
}

int arco_value_equals(const ArcoValue* left, const ArcoValue* right) {
    const arco::Value left_value = left == nullptr ? arco::Value() : box(left)->value;
    const arco::Value right_value = right == nullptr ? arco::Value() : box(right)->value;
    return arco::values_equal(left_value, right_value) ? 1 : 0;
}

void arco_value_retain(ArcoValue* value) {
    if (value == nullptr) return;
    box(value)->refcount.fetch_add(1, std::memory_order_relaxed);
}

void arco_value_release(ArcoValue* value) {
    if (value == nullptr) return;
    if (box(value)->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        delete box(value);
    }
}

int arco_value_is_number(const ArcoValue* value) { return value != nullptr && box(value)->value.is_number(); }
int arco_value_is_string(const ArcoValue* value) { return value != nullptr && box(value)->value.is_string(); }
int arco_value_is_bool(const ArcoValue* value) { return value != nullptr && box(value)->value.is_bool(); }

double arco_value_as_number(const ArcoValue* value) {
    // arco::Value::as_number() throws on a non-number/non-bool value -- caught and converted to a
    // panic here (see native_runtime_abi.h's own "no C++ exceptions cross this ABI boundary" rule)
    // now that generated code actually calls this directly (Binary/Unary arithmetic unboxing a
    // Boxed array element/object field -- see fission.cpp's own load_double_operand), not just
    // after an is_number check the way this ABI's header comment originally assumed every caller
    // would.
    if (value == nullptr) arco_value_panic("value is not a number");
    try {
        return box(value)->value.as_number();
    } catch (const std::exception& error) {
        arco_value_panic(error.what());
    }
    return 0.0; // unreachable -- arco_value_panic never returns (std::exit)
}

void arco_value_print(const ArcoValue* value) {
    const std::string text = (value == nullptr ? std::string("NULL") : box(value)->value.to_string()) + "\n";
    std::fwrite(text.data(), 1, text.size(), stdout);
    // Flushed explicitly (RFC-0049, "full Linux support" pass): stdout is fully buffered whenever
    // it isn't a TTY (any redirect to a file/pipe, the common case for a GUI app's own log
    // capture), and neither an uncaught C++ exception's std::terminate/abort() nor a raw SIGSEGV
    // runs atexit flush handlers -- PRINT output already written into that buffer is silently lost
    // the moment either happens. Found the hard way, mid-investigation of a real crash in Arconaut
    // (RFC-0049 Phase 11's own follow-up pass): every PRINT trace this file's own generated code
    // emitted appeared to vanish, making the crash look like it happened far earlier than it
    // actually did, until this fix (initially added just to un-block that debugging session)
    // revealed the true, much narrower failure point. The same class of gotcha the ArcoFission web
    // target already documents ("PRINT doesn't flush in a running GUI loop, use GUI.SetTitle
    // instead" -- see arcoflow/README.md) for a different reason (Emscripten's own stdout
    // buffering) -- kept here permanently since a real GUI program is exactly where this matters
    // most and the cost (one syscall per PRINT) is negligible next to everything else a PRINT call
    // already does (UTF-16 decode, a heap allocation for the resulting std::string).
    std::fflush(stdout);
}

void arco_value_panic(const char* message) {
    // If a TRY block is active, this raises a catchable error there instead of terminating --
    // matching how the bytecode VM's own per-frame try/catch already treats an out-of-range index,
    // an unknown host function, etc. as an ordinary catchable exception, not something special.
    // Pops the handler HERE (before longjmp), not on the receiving end: the receiving end (the
    // TryBegin landing pad in generated code) has no way to know whether IT is the one that should
    // pop, since arbitrarily many nested calls may have happened between the push and this raise.
    if (!handler_stack().empty()) {
        void* buf = handler_stack().back();
        handler_stack().pop_back();
        arco::Value::Object object;
        object["Message"] = message == nullptr ? std::string() : std::string(message);
        object["Type"] = std::string("RuntimeError");
        current_error() = arco::Value(std::move(object));
        std::longjmp(*reinterpret_cast<jmp_buf*>(buf), 1);
    }
    // Matches `ArcoFission compile-run`'s own uncaught-error wrapper text exactly ("BYTECODE RUN
    // FAILED\n\n<message>\n", apps/arcofission/main.cpp) -- a real, disclosed gap this pass closes
    // (RFC-0049, "full Linux support"): the two previously differed ("runtime error: <message>"
    // here vs. this), so a smoke test could diff stdout (already byte-for-byte matched everywhere
    // else in this suite) but never stderr for the uncaught-error case. stdout itself is
    // DELIBERATELY NOT made to match compile-run's own here: that command buffers the ENTIRE
    // bytecode VM's output and discards it on failure (apps/arcofission/main.cpp's own `result.ok`
    // branch never prints `result.output`), a dev-CLI-tool characteristic, not a language
    // semantic -- the tree-walking interpreter (arco_cli) flushes PRINT output before an uncaught
    // error exactly like this backend already does, and matching THAT (not compile-run's own
    // output-buffering quirk) is the correct behavior to keep.
    const std::string text = std::string("BYTECODE RUN FAILED\n\n") + (message == nullptr ? "" : message) + "\n";
    std::fflush(stdout);
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
    std::exit(1);
}

ArcoValue* arco_value_new_array_empty() {
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(arco::Value::Array{})});
}

void arco_value_array_push(ArcoValue* array, ArcoValue* value) {
    if (array == nullptr || !box(array)->value.is_array()) arco_value_panic("value is not an array");
    // A null `value` is ArcoBASIC's own "nothing" sentinel (see generate_x86_64_function's Const
    // case), a legitimate array element (arco::Value's own default-constructed monostate), not an
    // error -- e.g. `[1, nothing, 3]` or an uninitialized-field default flowing through here.
    box(array)->value.as_array().push_back(value == nullptr ? arco::Value() : box(value)->value);
}

ArcoValue* arco_value_array_get(const ArcoValue* array, double index) {
    if (array == nullptr || !box(array)->value.is_array()) arco_value_panic("value is not an array");
    const auto& elements = box(array)->value.as_array();
    const long long signed_index = static_cast<long long>(index);
    if (signed_index < 0 || static_cast<std::size_t>(signed_index) >= elements.size()) {
        arco_value_panic("array index out of range");
    }
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{elements[static_cast<std::size_t>(signed_index)]});
}

void arco_value_array_set(ArcoValue* array, double index, ArcoValue* value) {
    if (array == nullptr || !box(array)->value.is_array()) arco_value_panic("value is not an array");
    auto& elements = box(array)->value.as_array();
    const long long signed_index = static_cast<long long>(index);
    if (signed_index < 0 || static_cast<std::size_t>(signed_index) >= elements.size()) {
        arco_value_panic("array index out of range");
    }
    // See arco_value_array_push's own comment: a null `value` is ArcoBASIC's "nothing", a
    // legitimate value to store, not an error.
    elements[static_cast<std::size_t>(signed_index)] = value == nullptr ? arco::Value() : box(value)->value;
}

ArcoValue* arco_value_new_tuple(ArcoValue* const* elements, int64_t element_count) {
    arco::Value::Array items;
    items.reserve(element_count < 0 ? 0 : static_cast<std::size_t>(element_count));
    for (int64_t i = 0; i < element_count; ++i) {
        // See arco_value_array_push's own comment: a null entry is ArcoBASIC's "nothing", a
        // legitimate tuple element, not an error.
        items.push_back(elements[i] == nullptr ? arco::Value() : box(elements[i])->value);
    }
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value::tuple(std::move(items))});
}

ArcoValue* arco_value_index_get(const ArcoValue* target, double index) {
    if (target == nullptr) arco_value_panic("value is not indexable");
    const arco::Value& value = box(target)->value;
    const long long signed_index = static_cast<long long>(index);
    if (value.is_array()) {
        const auto& elements = value.as_array();
        if (signed_index < 0 || static_cast<std::size_t>(signed_index) >= elements.size()) {
            arco_value_panic("array index out of range");
        }
        return reinterpret_cast<ArcoValue*>(new ArcoValueBox{elements[static_cast<std::size_t>(signed_index)]});
    }
    if (value.is_tuple()) {
        const auto& elements = value.as_tuple();
        if (signed_index < 0 || static_cast<std::size_t>(signed_index) >= elements.size()) {
            arco_value_panic("tuple index out of range");
        }
        return reinterpret_cast<ArcoValue*>(new ArcoValueBox{elements[static_cast<std::size_t>(signed_index)]});
    }
    if (value.is_bit_vector()) {
        const auto& bits = value.as_bit_vector();
        if (signed_index < 0 || static_cast<std::size_t>(signed_index) >= bits.length) {
            arco_value_panic("bit vector index out of range");
        }
        return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(bits.get(static_cast<std::size_t>(signed_index)) ? 1.0 : 0.0)});
    }
    if (value.is_range()) {
        const auto& range = value.as_range();
        if (signed_index < 0 || static_cast<std::size_t>(signed_index) >= range.length) {
            arco_value_panic("range index out of range");
        }
        return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(static_cast<double>(range.at(static_cast<std::size_t>(signed_index))))});
    }
    if (value.is_string()) {
        // utf8_codepoints (include/arco/value.hpp) mirrors index_value()'s own string branch
        // exactly -- a real Unicode codepoint boundary, never a raw UTF-16 unit or byte, matching
        // this backend's own "STRING is always encoded/decoded as real text, not code units"
        // convention everywhere else in this ABI.
        const auto points = arco::utf8_codepoints(value.to_string());
        if (signed_index < 0 || static_cast<std::size_t>(signed_index) >= points.size()) {
            arco_value_panic("string index out of range");
        }
        return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(points[static_cast<std::size_t>(signed_index)])});
    }
    arco_value_panic("value is not indexable");
    return nullptr; // unreachable -- arco_value_panic never returns (std::exit), silences -Wreturn-type
}

ArcoValue* arco_value_new_object() {
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(arco::Value::Object{})});
}

ArcoValue* arco_value_object_get(const ArcoValue* object, const uint16_t* key_utf16) {
    if (object == nullptr || !box(object)->value.is_object()) arco_value_panic("value is not an object");
    const auto& fields = box(object)->value.as_object();
    const std::string key = utf16_to_utf8(key_utf16);
    const auto found = fields.find(key);
    if (found == fields.end()) {
        arco_value_panic(("undefined property: " + key).c_str());
    }
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{found->second});
}

void arco_value_object_set(ArcoValue* object, const uint16_t* key_utf16, ArcoValue* value) {
    if (object == nullptr || !box(object)->value.is_object()) arco_value_panic("value is not an object");
    // See arco_value_array_push's own comment: a null `value` is ArcoBASIC's "nothing" sentinel, a
    // legitimate field value (e.g. a typed class field declared with no default -- lower_class's
    // own field-default handling), not an error.
    box(object)->value.as_object()[utf16_to_utf8(key_utf16)] = value == nullptr ? arco::Value() : box(value)->value;
}

int arco_value_is_array(const ArcoValue* value) { return value != nullptr && box(value)->value.is_array(); }
int arco_value_is_object(const ArcoValue* value) { return value != nullptr && box(value)->value.is_object(); }

int arco_value_string_equals_utf16(const ArcoValue* value, const uint16_t* text) {
    if (value == nullptr || !box(value)->value.is_string()) return 0;
    return box(value)->value.to_string() == utf16_to_utf8(text) ? 1 : 0;
}

double arco_value_length(const ArcoValue* value) {
    if (value == nullptr) arco_value_panic("value is not an array or object");
    // Precedence matches runtime.cpp's own LEN host function exactly (array, tuple, object, bit
    // vector, range, string) -- see that function's own comment for why this order matters (a
    // string ALSO has an is_array()-shaped internal representation in some Value variants, so
    // checking the more specific kinds first is deliberate, not arbitrary).
    const arco::Value& v = box(value)->value;
    if (v.is_array()) return static_cast<double>(v.as_array().size());
    if (v.is_tuple()) return static_cast<double>(v.as_tuple().size());
    if (v.is_object()) return static_cast<double>(v.as_object().size());
    if (v.is_bit_vector()) return static_cast<double>(v.as_bit_vector().length);
    if (v.is_range()) return static_cast<double>(v.as_range().length);
    if (v.is_string()) return static_cast<double>(arco::utf8_codepoints(v.to_string()).size());
    arco_value_panic("LEN on this backend currently supports only arrays, tuples, objects, bit vectors, ranges, and strings");
    return 0.0; // unreachable -- arco_value_panic never returns (std::exit), silences -Wreturn-type
}

void arco_runtime_capture_args(int argc, char** argv) {
    captured_argc() = argc;
    captured_argv() = argv;
}

ArcoValue* arco_runtime_args() {
    arco::Value::Array items;
    // Skip argv[0] -- the program's own path, not a real user-supplied argument. A capture that
    // never happened (argc == 0, e.g. this were ever called before arco_runtime_capture_args, which
    // generated code's own Main prologue always calls first) safely produces an empty array rather
    // than reading a null argv.
    for (int i = 1; i < captured_argc(); ++i) {
        items.emplace_back(std::string(captured_argv()[i]));
    }
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{arco::Value(std::move(items))});
}

void arco_global_set(const uint16_t* name_utf16, const ArcoValue* value) {
    // See arco_value_array_push's own by-value note: a null `value` is ArcoBASIC's "nothing", a
    // legitimate value to store, not an error.
    global_store()[utf16_to_utf8(name_utf16)] = value == nullptr ? arco::Value() : box(value)->value;
}

ArcoValue* arco_global_get(const uint16_t* name_utf16) {
    const std::string key = utf16_to_utf8(name_utf16);
    const auto& store = global_store();
    const auto found = store.find(key);
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{found == store.end() ? arco::Value() : found->second});
}

void arco_try_push(void* jmp_buf_ptr) { handler_stack().push_back(jmp_buf_ptr); }

void arco_try_pop() {
    if (!handler_stack().empty()) handler_stack().pop_back();
}

ArcoValue* arco_try_error() {
    return reinterpret_cast<ArcoValue*>(new ArcoValueBox{current_error()});
}

void arco_throw(const ArcoValue* value) {
    if (value == nullptr || !box(value)->value.is_string()) {
        arco_value_panic("THROW message must be String");
        return; // unreachable -- arco_value_panic never returns when it terminates the process
    }
    const std::string message = box(value)->value.to_string();
    // Mirrors arco_value_panic's own handler-pop-then-longjmp shape exactly, except the raised
    // object is tagged "UserError" (matching BytecodeOp::Throw's own UserError) instead of
    // "RuntimeError", and an UNCAUGHT throw is reported through arco_value_panic itself (the
    // message text is the only thing that differs from an ordinary panic once uncaught).
    if (!handler_stack().empty()) {
        void* buf = handler_stack().back();
        handler_stack().pop_back();
        arco::Value::Object object;
        object["Message"] = message;
        object["Type"] = std::string("UserError");
        current_error() = arco::Value(std::move(object));
        std::longjmp(*reinterpret_cast<jmp_buf*>(buf), 1);
    }
    arco_value_panic(message.c_str());
}

} // extern "C"
