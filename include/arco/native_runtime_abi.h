#pragma once

// The native ArcoSH system runtime's C ABI -- what generated machine code (src/compiler/fission.cpp's
// generate_x86_64_function, System V convention) calls into for anything beyond raw scalar-double
// arithmetic, the same way a compiled C++ program calls into libstdc++ for std::string/std::vector
// instead of hand-rolling them in every translation unit. See
// .agents/reports/ARCO_NATIVE_COMPILER_BACKEND_PLAN.md for the two-piece architecture this is
// half of (the compiler backend turns control flow into real native code; this is the library that
// code calls into for value operations) and .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md for why this exists as a
// deliberately-designed ABI now rather than growing indefinitely as one narrow function per feature
// the way its predecessor (the same file, formerly named shim.cpp) started out.
//
// ArcoValue is a thin, explicitly reference-counted wrapper around the EXISTING, already-correct
// `arco::Value` (include/arco/value.hpp) -- not a reimplementation of its semantics. Generated code
// has no C++ destructors to rely on, so every ArcoValue* this API hands out is a real, owned
// reference the caller must eventually pass to arco_value_release (or transfer via
// arco_value_retain if it needs to outlive a single use).
//
// Design rule this ABI follows throughout: the compiler backend uses ArcoValue only where a value's
// type genuinely can't be proven statically (a function parameter/return whose type isn't known at
// the call site, a string, eventually arrays/objects) -- provably-numeric locals/temps stay raw
// IEEE-754 doubles in XMM registers/stack slots, exactly as generate_x86_64_function's existing
// hosted-number fast path already does, with zero ArcoValue overhead. Boxing is the fallback for the
// general case, not the default for everything.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArcoValue ArcoValue; // opaque

// --- Construction: each returns a new owned reference (refcount 1). ---
ArcoValue* arco_value_new_number(double value);
// `text` is UTF-16, null-terminated -- the same encoding generate_x86_64_function's Const case
// already produces for every string literal (systems::encode_utf16_null_terminated), so an existing
// string-literal pointer can be boxed with no re-encoding.
ArcoValue* arco_value_new_string_utf16(const uint16_t* text);
ArcoValue* arco_value_new_bool(int value);

// String concatenation via `+` (Phase 2 classes' own real-world motivation -- SELF.Name-style
// string-building is common enough that leaving this unimplemented made classes far less useful in
// practice than the field/method support alone would suggest). Matches arco::Value's own `+`
// semantics exactly: BOTH operands are converted via to_string() and concatenated, regardless of
// their own type -- `left.is_string() || right.is_string()` in eval_binary is the trigger for
// choosing this over numeric addition, but once triggered, a non-string operand (a number, a bool)
// is stringified too (`"x=" + 5` -> "x=5"), not rejected. Never null: a null `left`/`right` renders
// as "NULL" (arco_value_print's own convention) the same way PRINT already does.
ArcoValue* arco_value_concat(const ArcoValue* left, const ArcoValue* right);

// General value equality (RFC-0049, "full Linux support" pass -- found genuinely blocking Arconaut,
// a real program, from compiling: `==`/`!=` between two operands where at least one is Boxed, e.g.
// comparing an object field or array element against a string literal, had no codegen at all
// before this). Mirrors arco::values_equal() exactly (include/arco/value.hpp) -- numbers compare
// numerically, a bool operand truthy-coerces the other side, handles/bit-vectors/tuples/ranges each
// compare their own way, and everything else (including two strings) falls back to comparing
// to_string() output, matching `==`'s own existing ArcoBASIC semantics on every other backend. A
// null `left`/`right` is ArcoBASIC's own "nothing" (arco::Value{}, the default-constructed
// monostate) here too, never a special case needing its own branch -- values_equal already handles
// it via its own to_string()-based fallback. Never panics: unlike most of this ABI, there is no
// "wrong type" a comparison can receive.
int arco_value_equals(const ArcoValue* left, const ArcoValue* right);

// --- Reference counting. ---
void arco_value_retain(ArcoValue* value);
void arco_value_release(ArcoValue* value);

// --- Introspection. ---
int arco_value_is_number(const ArcoValue* value);
int arco_value_is_string(const ArcoValue* value);
int arco_value_is_bool(const ArcoValue* value);
// Panics (see arco_value_panic below) if the value isn't actually a number/bool -- used by
// generated Binary/Unary arithmetic to unbox a Boxed array element/object field (Phase 2, RFC-0049
// Section 4) before doing real SSE2 math on it.
double arco_value_as_number(const ArcoValue* value);

// --- Presentation: the one formatting authority (arco::Value::to_string()), used for every
// PRINT this backend compiles regardless of the printed value's type -- replaces separately
// hand-rolled per-type formatting logic that would otherwise have to duplicate to_string()'s own
// rules (whole numbers print with no decimal point, bools print TRUE/FALSE, etc.) and could drift
// out of sync with it over time. ---
void arco_value_print(const ArcoValue* value);

// --- Arrays and objects (Phase 2 -- RFC-0049 Section 4): the actual reason ArcoValue exists
// beyond PRINT. An array/object element is stored by VALUE (a copy of the source ArcoValue's
// underlying arco::Value, taken at the moment it's inserted -- exactly arco::Value's own existing
// assignment semantics: a scalar copies outright, a nested array/object copies its shared_ptr and
// so shares storage with the source, matching how `x = y` already behaves for the interpreter/
// bytecode VM today), never a stored pointer to the source ArcoValue -- so `value` below is never
// retained and the caller keeps full, unaffected ownership of its own reference to it.
//
// Lifetime note (superseded -- see fission.cpp's own Kind::Store/Kind::Load/store_result/
// Kind::Return codegen, RFC-0049 Phase 5, .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md Entry 14): this
// backend now DOES emit arco_value_release when a local variable holding an array/object is
// reassigned or goes out of scope, and arco_value_retain when a plain slot-to-slot copy would
// otherwise alias two locals to the same box with only one owner. What remains true, and is the
// reason `value` below is never retained by THIS function specifically: an array/object element is
// stored by VALUE (a copy of the source ArcoValue's underlying arco::Value, taken at the moment
// it's inserted -- exactly arco::Value's own existing assignment semantics: a scalar copies
// outright, a nested array/object copies its shared_ptr and so shares storage with the source,
// matching how `x = y` already behaves for the interpreter/bytecode VM today), never a stored
// pointer to the source ArcoValue -- so the CALLER keeps full, unaffected ownership of its own
// reference to `value`, and generated code must release any temporary it boxed only to satisfy
// this call (see fission.cpp's own box_operand_freshly_boxed/release_scratch_temp).
ArcoValue* arco_value_new_array_empty(void);
// Copies `value`'s current content and appends it -- see the by-value note above.
void arco_value_array_push(ArcoValue* array, ArcoValue* value);
// Bounds-checked exactly like the bytecode VM/interpreter's own index_value/assign_indexed
// (0-based, throws "array index out of range" there -- panics here, see arco_value_panic below,
// since no C++ exception may cross this ABI boundary). `index` is truncated toward zero the same
// way index_value's own `static_cast<int>` already does. Returns a NEW owned reference (a fresh
// copy, per the by-value note above -- mutating it does not affect the array it came from, again
// exactly matching arco::Value's own existing copy semantics).
ArcoValue* arco_value_array_get(const ArcoValue* array, double index);
// Copies `value`'s current content into the slot. Bounds-checked the same way as
// arco_value_array_get.
void arco_value_array_set(ArcoValue* array, double index, ArcoValue* value);

// Tuples (RFC-0049, "full Linux support" pass): unlike an array (built incrementally via
// arco_value_new_array_empty + repeated arco_value_array_push, since AMIR's own Kind::Array
// construction is a variable-length push loop), AMIR's Kind::Tuple hands every element as a fixed
// operand list on ONE instruction -- so this constructs the whole tuple in one call instead.
// `elements`/`element_count` describe a plain array of ArcoValue* (a null entry means ArcoBASIC's
// own "nothing", matching arco_call_host's identical convention); each element's CURRENT content is
// copied in (the same by-value note as every other constructor here), so the caller keeps full,
// unaffected ownership of its own reference to each one. Tuples are immutable (matching the
// interpreter/bytecode VM's own "value is not index-assignable" rejection of `t[0] = x`) -- there
// is deliberately no arco_value_tuple_set to pair with this.
ArcoValue* arco_value_new_tuple(ArcoValue* const* elements, int64_t element_count);

ArcoValue* arco_value_new_object(void);
// `key_utf16` is UTF-16, null-terminated -- the same raw pointer a string constant/dotted-property
// access already carries, so no re-encoding is needed at the call site. Panics
// ("undefined property: NAME") if the key is absent, matching arco::Value::get_property exactly.
// Returns a NEW owned reference (a fresh copy, per the by-value note above).
ArcoValue* arco_value_object_get(const ArcoValue* object, const uint16_t* key_utf16);
// Auto-vivifies the key (matching assign_indexed's own object branch, std::map::operator[]) and
// copies `value`'s current content into it.
void arco_value_object_set(ArcoValue* object, const uint16_t* key_utf16, ArcoValue* value);

int arco_value_is_array(const ArcoValue* value);
int arco_value_is_object(const ArcoValue* value);

// Compares a Boxed string value's CONTENT against a UTF-16, null-terminated text buffer (the same
// raw pointer a string literal/object field-name already carries) -- returns 0 if `value` isn't
// actually a string, never a pointer comparison. Used for instance-method dispatch (Phase 2 classes
// -- `receiver.Method(...)` resolving against the receiver's actual runtime `"__class"` field,
// generate_x86_64_function's Kind::CallValue case): the class hierarchy itself is static
// (module.class_parents never changes at runtime), so which candidate class the receiver's __class
// string matches is the only thing that has to be checked at runtime at all.
int arco_value_string_equals_utf16(const ArcoValue* value, const uint16_t* text);

// LEN(value): array/tuple element count, object field count, bit vector/range length, or string
// codepoint count -- matching runtime.cpp's own LEN host function precedence exactly (array, tuple,
// object, bit vector, range, string, then a to_string()-length fallback this backend's own Boxed
// values never actually reach, since anything reaching this ABI function is already one of the
// preceding cases). Panics on a null value rather than guessing.
double arco_value_length(const ArcoValue* value);

// Index-by-number (RFC-0049, "full Linux support" pass): a general-purpose sibling to
// arco_value_array_get, mirroring the bytecode VM/interpreter's own index_value() dispatch exactly
// -- array (identical to arco_value_array_get), tuple, bit vector (returns a new Boxed NUMBER, 0.0
// or 1.0), range (returns a new Boxed NUMBER, the Nth value in the sequence), and string (returns a
// new Boxed single-codepoint STRING, matching utf8_codepoints' own UTF-8-codepoint-aware slicing --
// never a raw UTF-16 unit or byte). Bounds-checked the same way as arco_value_array_get; panics
// (never crosses this ABI boundary as a C++ exception) on an out-of-range index or a value that
// isn't one of these five types. Returns a NEW owned reference in every case. `arco_value_array_get`
// itself is left untouched (narrower, array-only) rather than folded into this, since
// Kind::StoreIndex's own chained-assignment descent (fission.cpp) only ever needs to descend
// through arrays/objects, never the four read-only container types this adds.
ArcoValue* arco_value_index_get(const ArcoValue* target, double index);

// Runtime.Args() (RFC-0049, "full Linux support" pass): the process's own command-line arguments.
// arco_runtime_capture_args must be called exactly once, at generated code's own Main entry point,
// before anything else can clobber the argc/argv registers the C runtime's own startup convention
// hands to `main` -- see generate_x86_64_function's own prologue comment for why that moment is
// safe. arco_runtime_args() then builds a real, never-null Boxed array from argv[1..] (skipping
// argv[0], the program's own path -- the closest equivalent to arco_cli's own "Args" convention,
// adapted since a standalone native binary has no separate script-file argument to also skip).
// Returns a NEW owned reference (a fresh array) on every call, matching every other ArcoValue
// constructor here; safe to call zero, one, or many times after the one required capture call.
void arco_runtime_capture_args(int argc, char** argv);
ArcoValue* arco_runtime_args(void);

// Script-scope globals (RFC-0049 Section 4, the mechanism apply_script_global_scoping's own
// Runtime.SetGlobal/GetGlobal AMIR calls target -- fission.cpp's compile-time pass that lets a
// top-level ArcoBASIC variable referenced from inside a FUNCTION see the current script-scope
// value, the same way arco::Runtime's own `globals_` map already does for the interpreter/bytecode
// VM). A process-lifetime key/value store, keyed by name, holding a COPY of whatever value was set
// -- see the by-value note on arco_value_array_push above for why a copy, not a stored pointer.
// Also backs SHARED class fields (lower_class's own field.flag branch uses the identical
// mechanism).
void arco_global_set(const uint16_t* name_utf16, const ArcoValue* value);
// A name that was never set reads back as null (Runtime.GetGlobal's own "has_global(name) ?
// get_global(name) : Value()" convention), never a panic. Returns a NEW owned reference.
ArcoValue* arco_global_get(const uint16_t* name_utf16);

// Generic bridge into arco::Runtime's own ~244-entry host-function dispatch table
// (Runtime::call_host_function) -- the interpreter/bytecode VM's shared library of string/array/
// math/etc. utilities (UPPER, String.Split, Array.Join, Format, and everything else this backend
// doesn't hand-roll dedicated native codegen for). `name_utf16` is the function's full dotted name
// (e.g. "String.Trim"); `args`/`arg_count` describe a plain array of ArcoValue* (a null entry
// means ArcoBASIC's own "nothing"). Uses one process-lifetime arco::Runtime for the whole program,
// exactly matching how the interpreter/bytecode VM each construct exactly one for a full program
// run. An unrecognized name, or any exception the underlying host function itself throws, panics
// (see arco_value_panic) rather than crossing this ABI boundary as a C++ exception -- this is a
// REAL Runtime, so a genuine GUI.*/Network.* call the native binary's stub GUI backend or missing
// network stack can't actually perform also panics here, not silently no-ops.
ArcoValue* arco_call_host(const uint16_t* name_utf16, ArcoValue* const* args, int64_t arg_count);

// Prints `message` to stderr and terminates the process with a nonzero exit code UNLESS a TRY
// block is currently active (see arco_try_push below), in which case it instead raises a catchable
// error there instead of terminating -- see that function's own comment for the full TRY/CATCH
// design. Exit code and exact message text (for the UNCAUGHT case) are NOT guaranteed to match the
// bytecode VM/interpreter's own uncaught-runtime_error reporting -- a real, disclosed gap; see
// RFC-0049.
void arco_value_panic(const char* message);

// TRY/CATCH (RFC-0049): generated native code has no unwind tables for a C++-style exception to
// propagate through, so this uses real setjmp/longjmp instead, mirroring the bytecode VM's own
// TryBegin/TryEnd semantics (a per-call, LIFO stack of active handlers -- BytecodeOp::TryBegin/
// TryEnd's own try_stack) with a single PROCESS-WIDE stack instead of one per interpreter call
// frame: an exception raised anywhere -- including several native function calls deep -- always
// unwinds to the MOST RECENTLY pushed still-active handler, which is the same observable behavior
// the bytecode VM's own per-frame re-throw-until-caught chain already produces.
//
// `setjmp` itself must be called DIRECTLY by generated code, immediately before arco_try_push --
// wrapping it in an ABI function would save that wrapper's own stack frame instead of the TRY
// block's, since setjmp captures its IMMEDIATE caller's context. `jmp_buf_ptr` points to a
// caller-owned buffer at least as large as a real `jmp_buf` (200 bytes on this backend's only
// supported target, x86-64 Linux glibc; generated code reserves a rounded-up, generously-sized
// region per TryBegin site in its own stack frame, one per site rather than one per call, so
// nested TRY blocks in the same function never share a buffer).
//
// Called by generated code immediately after `call setjmp` returns 0 (the initial, non-longjmp
// path) to register this TRY block as the current innermost active handler.
void arco_try_push(void* jmp_buf_ptr);
// Called on NORMAL (no exception) exit from a TRY block, to deactivate its handler. A handler that
// actually caught something is already popped by the raising side (arco_value_panic/arco_throw)
// before it longjmps back -- this is only for the "nothing went wrong" path.
void arco_try_pop(void);
// The {Message, Type} object most recently raised and caught -- read this immediately after
// `setjmp`'s own return value is nonzero (a longjmp landed here) to bind it to the declared CATCH
// variable. `Type` is "UserError" for an explicit THROW (see arco_throw) or "RuntimeError" for
// anything else (an out-of-range index, an unknown host function, MOD by zero, and so on) --
// matching BytecodeOp::TryBegin's own object shape exactly. Returns a NEW owned reference.
ArcoValue* arco_try_error(void);
// THROW <value> (matching BytecodeOp::Throw): raises `value`'s own string content as a catchable
// UserError if a TRY handler is active, longjmping back to it exactly like arco_value_panic;
// panics (uncatchably terminating, via arco_value_panic) if no handler is active OR if `value`
// isn't actually a string, matching the bytecode VM's own "THROW message must be String" check.
void arco_throw(const ArcoValue* value);

#ifdef __cplusplus
}
#endif
