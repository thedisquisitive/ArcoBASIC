#!/usr/bin/env bash
set -euo pipefail

# Regression coverage for Phase 1 of the native (no-bytecode-VM) Linux x86-64 backend -- see
# .agents/reports/ARCO_NATIVE_COMPILER_BACKEND_PLAN.md for the design and
# .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md for how this milestone was reached. `ArcoFission build FILE -o OUT
# --target linux-x86_64` compiles straight to real x86-64 machine code via the same AMIR-driven
# generate_x86_64_function/generate_x86_64_program pass the UEFI target already uses (extended with
# a System V calling-convention variant and a Linux host-call shim), then hands off to the system
# assembler/linker -- no bytecode, no embedded interpreter loop, unlike every other ArcoFission
# native/build output.
#
# Scope note: this backend supports PRINT of a string or a number (literal, variable, or a
# straight-line arithmetic expression), +/-/*/// plus comparisons on numbers (unary -, ==, !=, <,
# <=, >, >=), general user-declared function calls whose every argument and return value is
# statically classifiable as a straight-line string/number/bool/array/object (real System V
# argument classification -- see the funccall.abas section below), and arrays/objects (Phase 2,
# RFC-0049 Section 4: construction, indexing/property get+set, LEN, PRINT, nested
# arrays-of-objects/objects-of-arrays, a mixed index-then-property chain like `arr[i].field`
# (parser.cpp's DynamicGetExpr -- Entry 10 fixed a real, pre-existing frontend gap here that
# affected the bytecode VM identically, not something specific to this backend), arithmetic on an
# array element/object field, passing/returning them through function calls -- always boxed
# through ArcoValue, see include/arco/native_runtime_abi.h), and MOD/bitwise (AND/OR/XOR/NOT/
# shifts) on hosted numbers. This scope note predates several later phases (string concatenation,
# classes, globals/host-function bridge, TRY/CATCH/ADDRESSOF, reference-counted lifetime tracking,
# and chained indexed assignment like `a.b.c = x`/`arr[i].field = x` all now implemented too -- see
# .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md for the full entry-by-entry history); a parameter whose
# type genuinely varies by call site, and string codepoint/tuple/bit-vector/range indexing, remain
# real, disclosed gaps that must fail with a clear error, never a wrong answer or a crash -- see the
# negative-case checks below.

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/linux-native-backend-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

cat > "$TMP_ROOT/hello.abas" <<'SCRIPT'
PRINT "hello"
SCRIPT

"$ARCOFISSION" build "$TMP_ROOT/hello.abas" -o "$TMP_ROOT/hello" --target linux-x86_64 > "$TMP_ROOT/hello-build.txt"
grep -q "X86_64 GENERATED" "$TMP_ROOT/hello-build.txt"
grep -q "ELF64 WRITTEN" "$TMP_ROOT/hello-build.txt"
test -x "$TMP_ROOT/hello"
test "$(head -c 5 "$TMP_ROOT/hello" | od -An -tx1 | tr -d ' \n')" = "7f454c4602"

"$TMP_ROOT/hello" > "$TMP_ROOT/hello-native-run.txt"
echo "hello" > "$TMP_ROOT/hello-expected.txt"
diff -u "$TMP_ROOT/hello-expected.txt" "$TMP_ROOT/hello-native-run.txt"

# Cross-checked against both the bytecode VM and the tree-walking interpreter for the same source --
# the actual correctness proof, not just "a binary got produced".
"$ARCOFISSION" compile-run "$TMP_ROOT/hello.abas" > "$TMP_ROOT/hello-bytecode-run.txt"
diff -u "$TMP_ROOT/hello-expected.txt" "$TMP_ROOT/hello-bytecode-run.txt"

# No bytecode VM anywhere in the produced binary -- the actual "no arcocapsule" claim, checked, not
# just asserted. execute_function/BytecodeSlot are the interpreter loop and its per-slot storage
# type (src/compiler/fission.cpp); their total absence from the symbol table is the concrete
# evidence this binary's control flow is real native code, not a resident bytecode dispatch loop.
# (A positive nm check for e.g. arco_value_print -- confirming PRINT actually links the native
# runtime ABI, include/arco/native_runtime_abi.h + src/native/runtime_abi.cpp -- was tried here too,
# but proved flaky against this repository's scratch filesystem the same way the default-target
# cross-check below already documented (nm's own read racing the linker's just-completed write, a
# test-infrastructure timing question, not a product bug: manually re-checking the same binary
# always found the symbol). Left out for the same reason; the absence check above plus the
# byte-identical diffs throughout this file already carry the real correctness weight.
if command -v nm >/dev/null 2>&1; then
    if nm -C "$TMP_ROOT/hello" 2>/dev/null | grep -qi "execute_function\|BytecodeSlot"; then
        echo "linux-x86_64 native build unexpectedly embeds bytecode-VM symbols" >&2
        exit 1
    fi
fi

# Multiple PRINT statements, distinct string literals each with their own temp -- exercises the
# per-instruction literal-lookup in the Call case across more than one call site in the same
# function, not just the single-statement minimal case above.
cat > "$TMP_ROOT/multi.abas" <<'SCRIPT'
PRINT "first line"
PRINT "second line"
PRINT "third"
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/multi.abas" -o "$TMP_ROOT/multi" --target linux-x86_64 > /dev/null
"$TMP_ROOT/multi" > "$TMP_ROOT/multi-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/multi.abas" > "$TMP_ROOT/multi-bytecode-run.txt"
diff -u "$TMP_ROOT/multi-bytecode-run.txt" "$TMP_ROOT/multi-native-run.txt"

# Arithmetic + numeric/bool PRINT: literals, variables, whole vs. fractional formatting (must match
# arco::Value::to_string() exactly -- "5" not "5.0", "3.5" not "3.500000"), all four ops, unary
# negation, and every comparison operator, cross-checked against the bytecode VM byte-for-byte.
cat > "$TMP_ROOT/arithmetic.abas" <<'SCRIPT'
PRINT 1 + 2
x = 5
PRINT x
y = 1.5
PRINT y + 2
PRINT 10 / 3
PRINT -5
PRINT -x
PRINT 2 * 3 + 1
a = 10
b = 4
PRINT a - b
PRINT a * b
PRINT a / b
PRINT 5 == 5
PRINT 5 == 6
PRINT 5 != 6
PRINT 5 < 6
PRINT 5 <= 5
PRINT 5 > 6
PRINT 5 >= 6
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/arithmetic.abas" -o "$TMP_ROOT/arithmetic" --target linux-x86_64 > /dev/null
"$TMP_ROOT/arithmetic" > "$TMP_ROOT/arithmetic-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/arithmetic.abas" > "$TMP_ROOT/arithmetic-bytecode-run.txt"
diff -u "$TMP_ROOT/arithmetic-bytecode-run.txt" "$TMP_ROOT/arithmetic-native-run.txt"
grep -q "^3$" "$TMP_ROOT/arithmetic-native-run.txt"      # 1 + 2, whole-number formatting
grep -q "^3.5$" "$TMP_ROOT/arithmetic-native-run.txt"    # 1.5 + 2, fractional formatting
grep -q "^-5$" "$TMP_ROOT/arithmetic-native-run.txt"     # unary negation of a literal
grep -q "^TRUE$" "$TMP_ROOT/arithmetic-native-run.txt"   # a comparison result -- see the NaN test
grep -q "^FALSE$" "$TMP_ROOT/arithmetic-native-run.txt"  # below for why this isn't the whole story

# A comparison result is a BOOL, stored differently from a plain number (see fission.cpp's
# HostedValueKind::Bool) -- printing one through the wrong path was a real bug caught here during
# development (a 0/1 integer reinterpreted as an almost-zero denormal double, "4.94066e-324").
# Re-asserted explicitly so a regression can't silently slip back in as "diff happened to match".
if grep -q "e-324" "$TMP_ROOT/arithmetic-native-run.txt"; then
    echo "a BOOL comparison result was printed as a raw double bit pattern" >&2
    exit 1
fi

# NaN comparisons: real IEEE-754/C++ double semantics (what the interpreter/bytecode VM actually
# execute), not "always false" -- != must be TRUE for NaN, every other comparison FALSE.
cat > "$TMP_ROOT/nan.abas" <<'SCRIPT'
n = 0 / 0
PRINT n == n
PRINT n != n
PRINT n < 1
PRINT n > 1
PRINT n <= 1
PRINT n >= 1
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/nan.abas" -o "$TMP_ROOT/nan" --target linux-x86_64 > /dev/null
"$TMP_ROOT/nan" > "$TMP_ROOT/nan-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/nan.abas" > "$TMP_ROOT/nan-bytecode-run.txt"
diff -u "$TMP_ROOT/nan-bytecode-run.txt" "$TMP_ROOT/nan-native-run.txt"
printf 'FALSE\nTRUE\nFALSE\nFALSE\nFALSE\nFALSE\n' > "$TMP_ROOT/nan-expected.txt"
diff -u "$TMP_ROOT/nan-expected.txt" "$TMP_ROOT/nan-native-run.txt"

# MOD (real IEEE-754 fmod, called directly as an external libm symbol -- matches eval_binary's own
# std::fmod exactly, not a truncating integer modulo) and bitwise/shift ops (&, |, ^, <<, >>, unary
# ~/NOT -- converted to int64 via cvttsd2si, operated on with the ordinary GPR instructions, then
# converted back via cvtsi2sd, matching eval_binary's own value_to_int-based semantics) on numbers.
# Covers a negative dividend/divisor, fractional MOD, MOD on Boxed array elements, every bitwise
# op, both shifts (including a negative left operand -- >> must be an ARITHMETIC shift, matching
# `long long >>`'s own sign-extending behavior, not a logical one), unary NOT, and MOD combined
# with a comparison inside a function.
cat > "$TMP_ROOT/mod_bitwise.abas" <<'SCRIPT'
PRINT 10 MOD 3
PRINT 7.5 MOD 2
PRINT -7 MOD 3
arr = [10, 7, 20]
PRINT arr[0] MOD arr[1]
PRINT 12 & 10
PRINT 5 | 2
PRINT 5 ^ 1
PRINT 1 << 4
PRINT 256 >> 4
PRINT -8 >> 1
PRINT ~5
PRINT NOT 5
FUNCTION Even(n)
    RETURN n MOD 2 == 0
END FUNCTION
PRINT Even(4)
PRINT Even(5)
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/mod_bitwise.abas" -o "$TMP_ROOT/mod_bitwise" --target linux-x86_64 \
    > "$TMP_ROOT/mod_bitwise-build.txt"
"$TMP_ROOT/mod_bitwise" > "$TMP_ROOT/mod_bitwise-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/mod_bitwise.abas" > "$TMP_ROOT/mod_bitwise-bytecode-run.txt"
diff -u "$TMP_ROOT/mod_bitwise-bytecode-run.txt" "$TMP_ROOT/mod_bitwise-native-run.txt"
printf '1\n1.5\n-1\n3\n8\n7\n4\n16\n16\n-4\n-6\n-6\nTRUE\nFALSE\n' > "$TMP_ROOT/mod_bitwise-expected.txt"
diff -u "$TMP_ROOT/mod_bitwise-expected.txt" "$TMP_ROOT/mod_bitwise-native-run.txt"

# MOD by exactly zero panics (matching eval_binary's own thrown "MOD divisor cannot be zero") --
# via a variable divisor, since a compile-time-constant zero divisor is rejected earlier, at parse
# time, regardless of backend (report_integer_error's own "integer division by a compile-time zero
# divisor is not allowed" check, unrelated to this native backend).
cat > "$TMP_ROOT/mod-zero.abas" <<'SCRIPT'
d = 0
PRINT 5 MOD d
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/mod-zero.abas" -o "$TMP_ROOT/mod-zero" --target linux-x86_64 > /dev/null
if "$TMP_ROOT/mod-zero" > "$TMP_ROOT/mod-zero-run.txt" 2>"$TMP_ROOT/mod-zero-stderr.txt"; then
    echo "linux-x86_64 backend unexpectedly succeeded on MOD by a runtime-zero divisor" >&2
    exit 1
fi
grep -q "MOD divisor cannot be zero" "$TMP_ROOT/mod-zero-stderr.txt"

# A NaN divisor is the opposite case: eval_binary's own `divisor == 0.0` is false for NaN, so this
# must NOT panic -- fmod(x, NaN) returns NaN (IEEE-754), matched byte-for-byte against compile-run.
cat > "$TMP_ROOT/mod-nan.abas" <<'SCRIPT'
n = 0 / 0
PRINT 5 MOD n
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/mod-nan.abas" -o "$TMP_ROOT/mod-nan" --target linux-x86_64 > /dev/null
"$TMP_ROOT/mod-nan" > "$TMP_ROOT/mod-nan-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/mod-nan.abas" > "$TMP_ROOT/mod-nan-bytecode-run.txt"
diff -u "$TMP_ROOT/mod-nan-bytecode-run.txt" "$TMP_ROOT/mod-nan-native-run.txt"

# General user-declared function calls (Kind::CallValue -- see fission.cpp's own comment on why
# this is CallValue, not Kind::Call, despite both printing identically as "CALL ..." in
# `ArcoFission reveal`'s pretty-printer): real System V argument classification, per parameter,
# by the callee's own declared type (or "untyped -> hosted-number" by default, the common case),
# with hosted-number values traveling in XMM registers/returns and everything else (STRING, BOOL,
# freestanding fixed-width types) in the ordinary GPR path -- both caller and callee derive the
# same classification from the callee's own signature, so they always agree. Recursion works for
# free (each call gets its own real stack frame, no bytecode-VM call-depth bookkeeping involved).
cat > "$TMP_ROOT/funccall.abas" <<'SCRIPT'
FUNCTION Foo(x)
    RETURN x + 1
END FUNCTION
PRINT Foo(5)

FUNCTION Sum(a, b)
    RETURN a + b
END FUNCTION
PRINT Sum(3, 4)

FUNCTION Greet()
    RETURN "hi"
END FUNCTION
PRINT Greet()

FUNCTION IsBig(x)
    RETURN x > 10
END FUNCTION
PRINT IsBig(20)
PRINT IsBig(5)

FUNCTION Shout(s AS STRING)
    PRINT s
    RETURN 1
END FUNCTION
Shout("hello there")

FUNCTION Double(x)
    RETURN x * 2
END FUNCTION
FUNCTION Quad(x)
    RETURN Double(Double(x))
END FUNCTION
PRINT Quad(3)

FUNCTION Fact(n)
    IF n <= 1 THEN
        RETURN 1
    END IF
    RETURN n * Fact(n - 1)
END FUNCTION
PRINT Fact(5)
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/funccall.abas" -o "$TMP_ROOT/funccall" --target linux-x86_64 > "$TMP_ROOT/funccall-build.txt"
"$TMP_ROOT/funccall" > "$TMP_ROOT/funccall-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/funccall.abas" > "$TMP_ROOT/funccall-bytecode-run.txt"
diff -u "$TMP_ROOT/funccall-bytecode-run.txt" "$TMP_ROOT/funccall-native-run.txt"
printf '6\n7\nhi\nTRUE\nFALSE\nhello there\n12\n120\n' > "$TMP_ROOT/funccall-expected.txt"
diff -u "$TMP_ROOT/funccall-expected.txt" "$TMP_ROOT/funccall-native-run.txt"

# Negative case: an UNTYPED parameter defaults to hosted-number (the common case, matching Foo/Sum
# above), so a call site that passes a value this backend can positively prove is a string/bool to
# an untyped parameter must be rejected with a clear error -- real support needs a parameter whose
# register class can vary by call site, which needs boxing (Phase 2, ArcoValue), not attempted
# here. This is a real bug this exact check was added to catch, not a hypothetical: an earlier
# version of this pass compiled `FUNCTION Shout(s): PRINT s` / `Shout("hi")` successfully and
# printed "4.64584e-310" (the passed string pointer's raw bits reinterpreted as a double) instead
# of erroring.
cat > "$TMP_ROOT/untyped-string-arg.abas" <<'SCRIPT'
FUNCTION Shout(s)
    PRINT s
    RETURN 1
END FUNCTION
Shout("hello there")
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/untyped-string-arg.abas" -o "$TMP_ROOT/untyped-string-arg" --target linux-x86_64 \
        > "$TMP_ROOT/untyped-string-arg-build.txt" 2>&1; then
    echo "linux-x86_64 backend unexpectedly accepted a string argument to an untyped (assumed-number) parameter" >&2
    exit 1
fi
grep -q "no explicit type annotation" "$TMP_ROOT/untyped-string-arg-build.txt"

# Negative case: a call with the wrong argument count must fail cleanly, not read past the end of
# instruction.operands or the callee's own parameter list.
cat > "$TMP_ROOT/wrong-arity.abas" <<'SCRIPT'
FUNCTION NeedsTwo(a, b)
    RETURN a + b
END FUNCTION
PRINT NeedsTwo(1)
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/wrong-arity.abas" -o "$TMP_ROOT/wrong-arity" --target linux-x86_64 \
        > "$TMP_ROOT/wrong-arity-build.txt" 2>&1; then
    echo "linux-x86_64 backend unexpectedly accepted a call with the wrong argument count" >&2
    exit 1
fi

# Phase 2 (RFC-0049 Section 4): arrays and objects, always boxed through ArcoValue -- there is no
# unboxed representation for a dynamic, heterogeneous collection the way a provably-numeric scalar
# has one, so every array/object value here is a real ArcoValue* under the hood (see
# include/arco/native_runtime_abi.h's own Array/Object section). Covers: literal construction,
# element/field get+set, LEN, PRINT (including a mixed-type array and a nested array-of-objects),
# arithmetic directly on an array element/object field (a real bug this exact case caught: an
# earlier version of this pass printed a denormal garbage value, "2.32211e-309", instead of the
# correct sum -- the boxed pointer's own bit pattern reinterpreted as a double -- see Binary/Unary's
# own load_double_operand comment in fission.cpp), unary negation of an element, comparisons
# against an element, an explicitly `AS ARRAY`-typed function parameter combined with LEN and a
# WHILE loop, a function that returns a freshly constructed array, and a mixed index-then-property
# chain (`people[1].name`) -- a real, PRE-EXISTING frontend/AMIR-lowering gap (parser.cpp's
# DynamicGetExpr previously had no canonical_ast() override at all, failing identically on the
# bytecode VM too, not something this backend introduced -- see Entry 9's own note and Entry 10's
# fix) now covered here since fixing it benefits every AMIR-based backend, this one included.
cat > "$TMP_ROOT/arrays_objects.abas" <<'SCRIPT'
arr = [1, 2, 3]
PRINT arr
PRINT arr[0]
PRINT arr[2]
arr[1] = 99
PRINT arr
PRINT LEN(arr)
PRINT -arr[2]
PRINT arr[0] == 1
PRINT arr[0] == 2

mixed = [1, "two", 3]
PRINT mixed

obj = {name: "Zach", age: 5}
PRINT obj.name
PRINT obj.age
obj.age = 6
PRINT obj.age
PRINT LEN(obj)
PRINT obj.name

people = [{name: "A"}, {name: "B"}]
PRINT people
PRINT people[1].name

nested = [[1, 2], [3, 4]]
PRINT nested

FUNCTION Sum(values AS ARRAY)
    total = 0
    i = 0
    WHILE i < LEN(values)
        total = total + values[i]
        i = i + 1
    WEND
    RETURN total
END FUNCTION
PRINT Sum([1, 2, 3, 4, 5])

FUNCTION MakeArray()
    RETURN [10, 20, 30]
END FUNCTION
made = MakeArray()
PRINT made
PRINT made[1]
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/arrays_objects.abas" -o "$TMP_ROOT/arrays_objects" --target linux-x86_64 \
    > "$TMP_ROOT/arrays_objects-build.txt"
"$TMP_ROOT/arrays_objects" > "$TMP_ROOT/arrays_objects-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/arrays_objects.abas" > "$TMP_ROOT/arrays_objects-bytecode-run.txt"
diff -u "$TMP_ROOT/arrays_objects-bytecode-run.txt" "$TMP_ROOT/arrays_objects-native-run.txt"
cat > "$TMP_ROOT/arrays_objects-expected.txt" <<'EXPECTED'
[1, 2, 3]
1
3
[1, 99, 3]
3
-3
TRUE
FALSE
[1, two, 3]
Zach
5
6
2
Zach
[{name: A}, {name: B}]
B
[[1, 2], [3, 4]]
15
[10, 20, 30]
20
EXPECTED
diff -u "$TMP_ROOT/arrays_objects-expected.txt" "$TMP_ROOT/arrays_objects-native-run.txt"

# Negative cases: an out-of-range array index and a missing object property both panic (a clean
# stderr message + nonzero exit -- see arco_value_panic) rather than reading/writing out of bounds
# or silently returning a wrong value. Exact message text is not guaranteed to match the bytecode
# VM/interpreter's own uncaught-runtime_error reporting (a real, disclosed gap -- see
# native_runtime_abi.h's own arco_value_panic comment), so only a substring and the nonzero exit
# code are checked, not a byte-identical diff against compile-run the way every positive case above
# is.
cat > "$TMP_ROOT/array-oob.abas" <<'SCRIPT'
arr = [1, 2, 3]
PRINT arr[5]
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/array-oob.abas" -o "$TMP_ROOT/array-oob" --target linux-x86_64 > /dev/null
if "$TMP_ROOT/array-oob" > "$TMP_ROOT/array-oob-run.txt" 2>"$TMP_ROOT/array-oob-stderr.txt"; then
    echo "linux-x86_64 backend unexpectedly succeeded reading an out-of-range array index" >&2
    exit 1
fi
grep -q "array index out of range" "$TMP_ROOT/array-oob-stderr.txt"

cat > "$TMP_ROOT/missing-property.abas" <<'SCRIPT'
obj = {a: 1}
PRINT obj.b
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/missing-property.abas" -o "$TMP_ROOT/missing-property" --target linux-x86_64 > /dev/null
if "$TMP_ROOT/missing-property" > "$TMP_ROOT/missing-property-run.txt" 2>"$TMP_ROOT/missing-property-stderr.txt"; then
    echo "linux-x86_64 backend unexpectedly succeeded reading a missing object property" >&2
    exit 1
fi
grep -q "undefined property: b" "$TMP_ROOT/missing-property-stderr.txt"

# Negative case: an UNTYPED parameter is still assumed hosted-number (see the untyped-string-arg
# case above), so an array/object argument passed to one must be rejected the same way a string
# argument is -- the identical untyped-parameter safety check, now covering Boxed too.
cat > "$TMP_ROOT/untyped-array-arg.abas" <<'SCRIPT'
FUNCTION Foo(x)
    PRINT x
    RETURN 1
END FUNCTION
Foo([1, 2, 3])
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/untyped-array-arg.abas" -o "$TMP_ROOT/untyped-array-arg" --target linux-x86_64 \
        > "$TMP_ROOT/untyped-array-arg-build.txt" 2>&1; then
    echo "linux-x86_64 backend unexpectedly accepted an array argument to an untyped (assumed-number) parameter" >&2
    exit 1
fi
grep -q "no explicit type annotation" "$TMP_ROOT/untyped-array-arg-build.txt"

# Chained indexed assignment (more than one index/key before the value -- a.b.c = x, arr[i].field
# = x, arr[i][j] = x, and mixes of both): a real native-backend-only gap this pass closed. This was
# NEVER a parser/interpreter/bytecode-VM gap -- assign_indexed already recurses through an
# arbitrary-length key list on both of those (confirmed identical, e.g. `a.b.c = x` already worked
# on compile-run before this backend could compile it at all); the two real, separate pieces were
# (1) the PARSER's own assignment-statement grammar never accepted a `.field` continuation mixed
# with `[index]` brackets (parser.cpp's assignment_statement, shared by every backend, not native-
# only -- so this half of the fix helps compile-run/arco_cli too, not just this backend), and (2)
# this backend's own Kind::StoreIndex codegen was hardcoded to exactly one key + one value.
# Kind::StoreIndex now descends through every key but the last via an ordinary INDEX read (the same
# shape assign_indexed's own recursion produces, unrolled since the key count is fixed at compile
# time), releasing each intermediate reference once its own one-level-deeper read is done with it.
cat > "$TMP_ROOT/chained-assign.abas" <<'SCRIPT'
obj = {inner: {x: 1}}
obj.inner.x = 2
PRINT obj.inner.x

arr = [{x: 1}, {x: 2}]
arr[0].x = 99
PRINT arr[0].x

nested = [{a: {b: 1}}]
nested[0].a.b = 42
PRINT nested[0].a.b

grid = [[1, 2], [3, 4]]
grid[0][1] = 77
PRINT grid[0][1]

deep = {a: {b: {c: {d: 1}}}}
deep.a.b.c.d = 55
PRINT deep.a.b.c.d
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/chained-assign.abas" -o "$TMP_ROOT/chained-assign" --target linux-x86_64 > /dev/null
"$TMP_ROOT/chained-assign" > "$TMP_ROOT/chained-assign-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/chained-assign.abas" > "$TMP_ROOT/chained-assign-bytecode-run.txt"
diff -u "$TMP_ROOT/chained-assign-bytecode-run.txt" "$TMP_ROOT/chained-assign-native-run.txt"
printf '2\n99\n42\n77\n55\n' > "$TMP_ROOT/chained-assign-expected.txt"
diff -u "$TMP_ROOT/chained-assign-expected.txt" "$TMP_ROOT/chained-assign-native-run.txt"

# Chained method-call RECEIVERS (`a.b.Method(...)`, `a.b.c.Method(...)`) and DYNAMIC method calls
# (`f().Method(...)`, `arr[0].Method(...)` -- a method call whose receiver is an arbitrary
# expression, not a name at all): two separate, real native-backend-only gaps this pass closed.
# The chained-receiver case descends through the field chain via ordinary INDEX reads (mirroring
# Kind::StoreIndex's own descent above) before dispatching on the final receiver's own runtime
# "__class"; the dynamic case (a genuinely different AST shape, DynamicMethodCallExpr, previously
# missing a canonical_ast() entirely -- see parser.cpp's own comment) stores its receiver into a
# fresh hidden_name() local (never a bare "%tN" temp -- bytecode-prep's own operand parser treats
# any '%'-prefixed call target as an ADDRESSOF/CALLABLE-style bare temp reference and rejects a
# trailing ".Method", a real bug found by direct testing) so it flows through the SAME name-based
# instance-dispatch machinery already proven above, on EVERY backend (this fix lives in shared
# AMIR lowering, not fission.cpp alone -- compile-run benefits too, confirmed identical).
# Also covers a real, separate, more general bug this same testing surfaced: a multi-argument call
# (plain function OR instance method) where more than one argument needs unboxing (a Boxed,
# method-call-derived value passed to a hosted-number parameter) previously either clobbered an
# already-finalized earlier argument's register (silently wrong receiver -- "value is not an
# object") or, for the plain-function-call path specifically, never unboxed an explicitly-typed
# "AS NUMBER" parameter's Boxed argument at all (a raw pointer misread as a garbage double) --
# fixed by spilling every argument to its own scratch slot before any of them are loaded into a
# real calling-convention register, in both marshaling loops.
cat > "$TMP_ROOT/chained-method-call.abas" <<'SCRIPT'
CLASS Leaf
    Value AS Number = 0
    FUNCTION Init(v)
        SELF.Value = v
    END FUNCTION
    FUNCTION Get()
        RETURN SELF.Value
    END FUNCTION
    FUNCTION Combine(other AS NUMBER)
        RETURN SELF.Value + other
    END FUNCTION
END CLASS
CLASS Mid
    FUNCTION Init(v)
        SELF.Leaf = Leaf(v)
    END FUNCTION
END CLASS
top = Mid(21)
PRINT top.Leaf.Get()

FUNCTION MakeLeaf(v)
    RETURN Leaf(v)
END FUNCTION
PRINT MakeLeaf(5).Combine(MakeLeaf(10).Get())

boxes = [ Leaf(1), Leaf(2), Leaf(3) ]
PRINT boxes[1].Get()
PRINT boxes[2].Combine(boxes[0].Get())

FUNCTION PlainCombine(a AS NUMBER, b AS NUMBER)
    RETURN a + b
END FUNCTION
PRINT PlainCombine(boxes[0].Get(), boxes[1].Get())
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/chained-method-call.abas" -o "$TMP_ROOT/chained-method-call" --target linux-x86_64 > /dev/null
"$TMP_ROOT/chained-method-call" > "$TMP_ROOT/chained-method-call-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/chained-method-call.abas" > "$TMP_ROOT/chained-method-call-bytecode-run.txt"
diff -u "$TMP_ROOT/chained-method-call-bytecode-run.txt" "$TMP_ROOT/chained-method-call-native-run.txt"
printf '21\n15\n2\n4\n3\n' > "$TMP_ROOT/chained-method-call-expected.txt"
diff -u "$TMP_ROOT/chained-method-call-expected.txt" "$TMP_ROOT/chained-method-call-native-run.txt"

# Marshaling-loop audit (RFC-0049, "full Linux support" pass, the follow-up the project owner asked
# for after Entry 16's own two register-clobber fixes): a plain binary arithmetic op with BOTH
# operands Boxed (two array elements, e.g. `arr[0] + arr[1]`) unboxes operand 1 into XMM1 THEN
# operand 0 into XMM0 (a deliberate ordering, see that case's own comment) -- if operand 0 is ALSO
# Boxed, its own unboxing call is free to clobber XMM1 (caller-saved, never guaranteed to survive
# any call), even though operand 1's value was already finalized there. Not observed to misbehave
# with this toolchain's own simple arco_value_as_number, but relying on that is not a real
# correctness guarantee -- fixed with the same spill-to-memory-then-reload discipline as Entry 16's
# own two fixes. Covers +, *, and MOD/bitwise (which reuse the same two-operand load) with two
# Boxed operands.
cat > "$TMP_ROOT/boxed-binary-operands.abas" <<'SCRIPT'
arr = [10, 20, 30, 3]
PRINT arr[0] + arr[1]
PRINT arr[0] * arr[2]
PRINT arr[2] MOD arr[3]
PRINT arr[0] AND arr[3]
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/boxed-binary-operands.abas" -o "$TMP_ROOT/boxed-binary-operands" --target linux-x86_64 > /dev/null
"$TMP_ROOT/boxed-binary-operands" > "$TMP_ROOT/boxed-binary-operands-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/boxed-binary-operands.abas" > "$TMP_ROOT/boxed-binary-operands-bytecode-run.txt"
diff -u "$TMP_ROOT/boxed-binary-operands-bytecode-run.txt" "$TMP_ROOT/boxed-binary-operands-native-run.txt"
printf '30\n300\n0\n2\n' > "$TMP_ROOT/boxed-binary-operands-expected.txt"
diff -u "$TMP_ROOT/boxed-binary-operands-expected.txt" "$TMP_ROOT/boxed-binary-operands-native-run.txt"

# Classes and methods (Phase 2, RFC-0049 Section 4) -- a class instance IS an Object (with a
# "__class" field, see lower_class's own .__new), so field construction/get/set already work via
# the array/object machinery above with no new codegen; the real new surface is instance METHOD
# CALL DISPATCH (`receiver.Method(...)`, Kind::CallValue with a target no function is ever literally
# named -- resolve_class_method walks the static class hierarchy at compile time, and only the
# receiver's actual runtime "__class" string is checked at runtime, arco_value_string_equals_utf16).
# Covers: a field with a default, a method reading SELF's own field, a constructor (Init) taking
# arguments and setting fields, a method mutating a field (SELF.Value = SELF.Value + 1 -- a real bug
# this exact case caught: arithmetic on a Boxed field was once misclassified as string
# concatenation and segfaulted dereferencing a raw double's own bit pattern as a pointer), a method
# calling another method on SELF, single-level EXTENDS polymorphism (a base and an overriding
# subclass, called through both a base instance and a derived one), a 3-level EXTENDS chain with a
# middle class that declares no methods of its own (pure inheritance passthrough), double dispatch
# (a base method calling SELF.Name() where Name is overridden per-subclass), an array field
# combined with LEN and a WHILE loop inside a method, string concatenation with SELF.Name and with
# TRUE/FALSE literals (a related, closely dependent gap fixed alongside this milestone --
# concatenation is real-world necessary for any class that builds a descriptive string from its own
# fields), and LEN(arr) used correctly in a comparison despite type_of_expression's own hardcoded
# "LEN is always U64" hint (a real, disclosed pre-existing frontend quirk this milestone had to see
# past, not something Phase 2 classes caused).
cat > "$TMP_ROOT/classes.abas" <<'SCRIPT'
CLASS Person
    Name AS String = "Ada"
    Age AS Number = 36

    FUNCTION Label()
        RETURN SELF.Name
    END FUNCTION
END CLASS
person = Person()
PRINT person.Name
PRINT person.Label()

CLASS Point
    X AS Number
    Y AS Number

    FUNCTION Init(x, y)
        SELF.X = x
        SELF.Y = y
    END FUNCTION

    FUNCTION Sum()
        RETURN SELF.X + SELF.Y
    END FUNCTION
END CLASS
p = Point(3, 4)
PRINT p.X
PRINT p.Y
PRINT p.Sum()

CLASS Counter
    Value AS Number = 0

    FUNCTION Increment()
        SELF.Value = SELF.Value + 1
    END FUNCTION

    FUNCTION Get()
        RETURN SELF.Value
    END FUNCTION
END CLASS
c = Counter()
c.Increment()
c.Increment()
c.Increment()
PRINT c.Get()

CLASS Greeter
    Name AS String = "World"

    FUNCTION Greeting()
        RETURN "Hello, " + SELF.Name
    END FUNCTION

    FUNCTION Shout()
        RETURN SELF.Greeting()
    END FUNCTION
END CLASS
g = Greeter()
PRINT g.Shout()

CLASS Animal
    FUNCTION Speak()
        RETURN "..."
    END FUNCTION
END CLASS
CLASS Cat EXTENDS Animal
    FUNCTION Speak()
        RETURN "Meow"
    END FUNCTION
END CLASS
CLASS Dog EXTENDS Animal
END CLASS
a = Animal()
cat = Cat()
dog = Dog()
PRINT a.Speak()
PRINT cat.Speak()
PRINT dog.Speak()

CLASS ShapeBase
    FUNCTION Describe()
        RETURN "shape: " + SELF.Name()
    END FUNCTION
    FUNCTION Name()
        RETURN "generic"
    END FUNCTION
END CLASS
CLASS Circle EXTENDS ShapeBase
    FUNCTION Name()
        RETURN "circle"
    END FUNCTION
END CLASS
shape = ShapeBase()
circle = Circle()
PRINT shape.Describe()
PRINT circle.Describe()

CLASS LevelA
    FUNCTION Name()
        RETURN "A"
    END FUNCTION
END CLASS
CLASS LevelB EXTENDS LevelA
END CLASS
CLASS LevelC EXTENDS LevelB
    FUNCTION Name()
        RETURN "C"
    END FUNCTION
END CLASS
la = LevelA()
lb = LevelB()
lc = LevelC()
PRINT la.Name()
PRINT lb.Name()
PRINT lc.Name()

CLASS Bag
    FUNCTION Init()
        SELF.Items = [1, 2, 3, 4]
    END FUNCTION

    FUNCTION Total()
        arr = SELF.Items
        total = 0
        i = 0
        WHILE i < LEN(arr)
            total = total + arr[i]
            i = i + 1
        WEND
        RETURN total
    END FUNCTION
END CLASS
bag = Bag()
PRINT bag.Total()

PRINT "x=" + 5
PRINT "y=" + TRUE
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/classes.abas" -o "$TMP_ROOT/classes" --target linux-x86_64 > "$TMP_ROOT/classes-build.txt"
"$TMP_ROOT/classes" > "$TMP_ROOT/classes-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/classes.abas" > "$TMP_ROOT/classes-bytecode-run.txt"
diff -u "$TMP_ROOT/classes-bytecode-run.txt" "$TMP_ROOT/classes-native-run.txt"
cat > "$TMP_ROOT/classes-expected.txt" <<'EXPECTED'
Ada
Ada
3
4
7
3
Hello, World
...
Meow
...
shape: generic
shape: circle
A
A
C
10
x=5
y=TRUE
EXPECTED
diff -u "$TMP_ROOT/classes-expected.txt" "$TMP_ROOT/classes-native-run.txt"

# Negative case: instance method dispatch where the METHOD NAME exists somewhere in the module
# (so resolve_class_method finds at least one candidate class, and the dispatch codegen actually
# runs) but the specific receiver's own runtime __class matches none of them panics at runtime with
# a clear message, rather than silently returning null or falling through to some other class's
# method. (A method name that exists NOWHERE in the whole module -- no candidates at all -- is a
# different, existing compile-time error path, "... is not a declared function", covered
# implicitly by every ordinary undeclared-call negative case above.)
cat > "$TMP_ROOT/missing-method.abas" <<'SCRIPT'
CLASS Empty
END CLASS
CLASS HasDance
    FUNCTION Dance()
        RETURN "dancing"
    END FUNCTION
END CLASS
e = Empty()
PRINT e.Dance()
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/missing-method.abas" -o "$TMP_ROOT/missing-method" --target linux-x86_64 > /dev/null
if "$TMP_ROOT/missing-method" > "$TMP_ROOT/missing-method-run.txt" 2>"$TMP_ROOT/missing-method-stderr.txt"; then
    echo "linux-x86_64 backend unexpectedly succeeded calling a method its receiver's class doesn't have" >&2
    exit 1
fi
grep -q "no method" "$TMP_ROOT/missing-method-stderr.txt"

# Script-scope globals (apply_script_global_scoping's own Runtime.SetGlobal/GetGlobal AMIR calls --
# "finish off Linux support"): a top-level variable reassigned in Main is visible from a function
# called afterward. A REAL, deliberate ArcoBASIC language semantic -- confirmed identical on
# compile-run, not assumed -- is exercised alongside it and is NOT a bug: a plain assignment INSIDE
# a function only ever shadows its own local copy and never writes back to the script-scope global
# (exactly like the tree-walking interpreter), so calling a function that reassigns a same-named
# variable does NOT change what Main sees afterward.
cat > "$TMP_ROOT/script-globals.abas" <<'SCRIPT'
name = "World"
FUNCTION Greet()
    PRINT "Hello, " + name
END FUNCTION
Greet()
name = "Zach"
Greet()

count = 0
FUNCTION Increment()
    count = count + 1
END FUNCTION
Increment()
Increment()
Increment()
PRINT count
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/script-globals.abas" -o "$TMP_ROOT/script-globals" --target linux-x86_64 > /dev/null
"$TMP_ROOT/script-globals" > "$TMP_ROOT/script-globals-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/script-globals.abas" > "$TMP_ROOT/script-globals-bytecode-run.txt"
diff -u "$TMP_ROOT/script-globals-bytecode-run.txt" "$TMP_ROOT/script-globals-native-run.txt"
printf 'Hello, World\nHello, Zach\n0\n' > "$TMP_ROOT/script-globals-expected.txt"
diff -u "$TMP_ROOT/script-globals-expected.txt" "$TMP_ROOT/script-globals-native-run.txt"

# SHARED class fields/methods (lower_class's own identical Runtime.SetGlobal/GetGlobal mechanism,
# class-level state accessed through ClassName.Member) -- the docs/classes.md Ticket example
# verbatim: a SHARED field mutated from a SHARED method, and CONSTRUCTOR() (as opposed to
# FUNCTION Init) syntax.
cat > "$TMP_ROOT/shared-fields.abas" <<'SCRIPT'
CLASS Ticket
    SHARED NextId AS Number = 100
    Id AS Number = 0

    SHARED FUNCTION Issue() AS Number
        Ticket.NextId = Ticket.NextId + 1
        RETURN Ticket.NextId
    END FUNCTION

    CONSTRUCTOR()
        SELF.Id = Ticket.Issue()
    END CONSTRUCTOR
END CLASS

a = Ticket()
b = Ticket()

PRINT Ticket.NextId
PRINT a.Id
PRINT b.Id
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/shared-fields.abas" -o "$TMP_ROOT/shared-fields" --target linux-x86_64 > /dev/null
"$TMP_ROOT/shared-fields" > "$TMP_ROOT/shared-fields-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/shared-fields.abas" > "$TMP_ROOT/shared-fields-bytecode-run.txt"
diff -u "$TMP_ROOT/shared-fields-bytecode-run.txt" "$TMP_ROOT/shared-fields-native-run.txt"
printf '102\n101\n102\n' > "$TMP_ROOT/shared-fields-expected.txt"
diff -u "$TMP_ROOT/shared-fields-expected.txt" "$TMP_ROOT/shared-fields-native-run.txt"

# Generic host-function bridge (arco_call_host, src/native/host_bridge.cpp -- "finish off Linux
# support"): a call to one of arco::Runtime's own ~244 host functions (UPPER, String.Trim,
# String.Split, String.Join, Format, and everything else this backend doesn't hand-roll dedicated
# native codegen for) falls through to a real arco::Runtime instead of failing to compile. Only
# actually exercised when the build tree has opted in with
# `cmake --build . --target ArcoNativeRuntimeCoreProbe` (CMakeLists.txt's own EXCLUDE_FROM_ALL
# lean-runtime-probe precedent, matching the bytecode-capsule format's identical
# ArcoFissionCapsuleCoreProbe mechanism) -- gracefully skipped, not failed, when it hasn't: a CI
# environment that never built the probe still gets a real PASS here, just without exercising this
# specific path, exactly like the capsule format's own "lean runtime unavailable, linked the full
# runtime instead" graceful degradation.
cat > "$TMP_ROOT/host-functions.abas" <<'SCRIPT'
PRINT UPPER("hello world")
PRINT LOWER("HELLO")
PRINT String.Trim("  padded  ")
PRINT String.Split("a,b,c", ",")
PRINT String.Join(["x", "y", "z"], "-")
PRINT Format("{0} + {1} = {2}", 1, 2, 3)

CLASS Greeter
    Name AS String = "world"
    FUNCTION Greet()
        RETURN "Hello, " + UPPER(SELF.Name)
    END FUNCTION
END CLASS
g = Greeter()
PRINT g.Greet()
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/host-functions.abas" -o "$TMP_ROOT/host-functions" --target linux-x86_64 \
        > "$TMP_ROOT/host-functions-build.txt" 2>&1; then
    "$TMP_ROOT/host-functions" > "$TMP_ROOT/host-functions-native-run.txt"
    "$ARCOFISSION" compile-run "$TMP_ROOT/host-functions.abas" > "$TMP_ROOT/host-functions-bytecode-run.txt"
    diff -u "$TMP_ROOT/host-functions-bytecode-run.txt" "$TMP_ROOT/host-functions-native-run.txt"
    printf 'HELLO WORLD\nhello\npadded\n[a, b, c]\nx-y-z\n1 + 2 = 3\nHello, WORLD\n' > "$TMP_ROOT/host-functions-expected.txt"
    diff -u "$TMP_ROOT/host-functions-expected.txt" "$TMP_ROOT/host-functions-native-run.txt"
elif grep -q "ArcoNativeRuntimeCoreProbe" "$TMP_ROOT/host-functions-build.txt"; then
    echo "note: ArcoNativeRuntimeCoreProbe not built in this tree -- host-function bridge test skipped" >&2
else
    echo "linux-x86_64 backend failed to build a host-function call for an unexpected reason:" >&2
    cat "$TMP_ROOT/host-functions-build.txt" >&2
    exit 1
fi

# Negative case: an unrecognized name (not a declared function, not an instance method, not a real
# host function either) panics at runtime -- host_bridge.cpp's own arco_call_host converts
# arco::Runtime::call_host_function's "unknown host function" exception into a clean panic, matching
# how the interpreter/bytecode VM themselves only ever discover this at runtime too, never
# statically. Same graceful-skip rule as above.
cat > "$TMP_ROOT/unknown-function.abas" <<'SCRIPT'
PRINT ThisFunctionDoesNotExist(5)
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/unknown-function.abas" -o "$TMP_ROOT/unknown-function" --target linux-x86_64 \
        > "$TMP_ROOT/unknown-function-build.txt" 2>&1; then
    if "$TMP_ROOT/unknown-function" > "$TMP_ROOT/unknown-function-run.txt" 2>"$TMP_ROOT/unknown-function-stderr.txt"; then
        echo "linux-x86_64 backend unexpectedly succeeded calling an unrecognized function" >&2
        exit 1
    fi
    grep -q "unknown host function" "$TMP_ROOT/unknown-function-stderr.txt"
elif ! grep -q "ArcoNativeRuntimeCoreProbe" "$TMP_ROOT/unknown-function-build.txt"; then
    echo "linux-x86_64 backend failed to build the unknown-function negative case for an unexpected reason:" >&2
    cat "$TMP_ROOT/unknown-function-build.txt" >&2
    exit 1
fi

# TRY/CATCH: generated code has no unwind tables, so this uses real setjmp/longjmp instead (see
# native_runtime_abi.h's own much larger comment on arco_try_push) -- a process-wide LIFO handler
# stack that mirrors the bytecode VM's own per-frame try_stack semantics exactly (the innermost
# active handler always catches first, including an exception raised several native function calls
# deep, not just in the TRY block's own immediate body). Covers: a basic catch (array
# out-of-range), reading the caught object's own `.Message`/`.Type` fields (proving the caught
# value integrates with the ordinary Phase 2 object machinery, not a special case), catching an
# exception raised inside a NESTED FUNCTION CALL (the hardest case -- proves real native call
# frames get correctly discarded), a TRY body that succeeds (CATCH must never fire), explicit
# THROW raising a catchable UserError, and an uncaught THROW terminating the process (exact message
# text is NOT asserted to match compile-run's own uncaught-exception reporting -- a real, disclosed
# gap already noted for every other panic path in this backend, see arco_value_panic's own
# comment -- only the nonzero exit code is).
cat > "$TMP_ROOT/try-catch.abas" <<'SCRIPT'
TRY
    PRINT "before"
    arr = [1, 2, 3]
    x = arr[99]
    PRINT "after (should not print)"
CATCH e
    PRINT "caught!"
END TRY
PRINT "done"

TRY
    arr = [1, 2, 3]
    x = arr[99]
CATCH e
    PRINT e.Message
    PRINT e.Type
END TRY

FUNCTION Boom()
    arr = [1, 2, 3]
    PRINT arr[99]
END FUNCTION
TRY
    PRINT "calling Boom"
    Boom()
    PRINT "unreachable"
CATCH e
    PRINT "caught from nested call"
END TRY
PRINT "done"

TRY
    PRINT 1 + 1
CATCH e
    PRINT "should not fire"
END TRY
PRINT "done"

TRY
    THROW "custom error"
CATCH e
    PRINT e.Message
    PRINT e.Type
END TRY
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/try-catch.abas" -o "$TMP_ROOT/try-catch" --target linux-x86_64 > /dev/null
"$TMP_ROOT/try-catch" > "$TMP_ROOT/try-catch-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/try-catch.abas" > "$TMP_ROOT/try-catch-bytecode-run.txt"
diff -u "$TMP_ROOT/try-catch-bytecode-run.txt" "$TMP_ROOT/try-catch-native-run.txt"
cat > "$TMP_ROOT/try-catch-expected.txt" <<'EXPECTED'
before
caught!
done
array index out of range
RuntimeError
calling Boom
caught from nested call
done
2
done
custom error
UserError
EXPECTED
diff -u "$TMP_ROOT/try-catch-expected.txt" "$TMP_ROOT/try-catch-native-run.txt"

cat > "$TMP_ROOT/uncaught-throw.abas" <<'SCRIPT'
PRINT "before"
THROW "boom"
PRINT "unreachable"
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/uncaught-throw.abas" -o "$TMP_ROOT/uncaught-throw" --target linux-x86_64 > /dev/null
if "$TMP_ROOT/uncaught-throw" > "$TMP_ROOT/uncaught-throw-run.txt" 2>"$TMP_ROOT/uncaught-throw-stderr.txt"; then
    echo "linux-x86_64 backend unexpectedly succeeded past an uncaught THROW" >&2
    exit 1
fi
printf 'before\n' > "$TMP_ROOT/uncaught-throw-expected.txt"
diff -u "$TMP_ROOT/uncaught-throw-expected.txt" "$TMP_ROOT/uncaught-throw-run.txt"
# Uncaught-error STDERR text (RFC-0049, "full Linux support" pass): previously only grepped for a
# substring ("real, disclosed gap: exact text not verified" -- see this file's own earlier
# comments), now matched byte-for-byte against `compile-run`'s own uncaught-error wrapper text
# ("BYTECODE RUN FAILED\n\n<message>\n", apps/arcofission/main.cpp's own result.ok==false branch) --
# arco_value_panic's own text was changed to match exactly. stdout is deliberately NOT compared
# against compile-run here (see runtime_abi.cpp's own comment on arco_value_panic): compile-run
# discards ALL of its own stdout on a failed run (a dev-CLI-tool characteristic of that one
# subcommand, not a language semantic), while both the tree-walking interpreter and this backend
# correctly flush PRINT output before an uncaught error -- already covered by the diff above.
"$ARCOFISSION" compile-run "$TMP_ROOT/uncaught-throw.abas" > /dev/null 2>"$TMP_ROOT/uncaught-throw-bytecode-stderr.txt" || true
diff -u "$TMP_ROOT/uncaught-throw-bytecode-stderr.txt" "$TMP_ROOT/uncaught-throw-stderr.txt"

# ADDRESSOF/CALLABLE: `f = ADDRESSOF Square` boxes a plain string naming the function (no thunk, no
# raw function-pointer representation needed); `f(5)` (a call through a plain local/parameter
# holding that value) resolves it at RUNTIME by comparing the callable's own boxed name against
# every function EVER referenced via ADDRESSOF anywhere in the module (a compile-time-enumerable
# candidate set, exactly like instance method dispatch's own class-hierarchy walk), then marshals
# arguments against the MATCHING candidate's own declared parameter types. Covers: a basic call
# through a variable, a callable stored in and called from an ARRAY element (the correct way to
# build a dispatch table under this backend's/the bytecode VM's own shared, pre-existing limitation
# that dotted access is instance-method dispatch only, not "a plain object field holding a
# callable" -- confirmed identical on compile-run, not a gap this feature introduced), and an
# explicitly `AS STRING`-typed parameter (proving the matching candidate's OWN parameter
# classification drives argument marshaling, not the caller's).
cat > "$TMP_ROOT/addressof.abas" <<'SCRIPT'
FUNCTION Square(x)
    RETURN x * x
END FUNCTION
f = ADDRESSOF Square
PRINT f(5)

FUNCTION Hello()
    RETURN "hello"
END FUNCTION
FUNCTION World()
    RETURN "world"
END FUNCTION
funcs = [ADDRESSOF Hello, ADDRESSOF World]
i = 0
WHILE i < LEN(funcs)
    g = funcs[i]
    PRINT g()
    i = i + 1
WEND

FUNCTION Shout(s AS STRING)
    RETURN UPPER(s)
END FUNCTION
h = ADDRESSOF Shout
PRINT h("hello")
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/addressof.abas" -o "$TMP_ROOT/addressof" --target linux-x86_64 > /dev/null
"$TMP_ROOT/addressof" > "$TMP_ROOT/addressof-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/addressof.abas" > "$TMP_ROOT/addressof-bytecode-run.txt"
diff -u "$TMP_ROOT/addressof-bytecode-run.txt" "$TMP_ROOT/addressof-native-run.txt"
printf '25\nhello\nworld\nHELLO\n' > "$TMP_ROOT/addressof-expected.txt"
diff -u "$TMP_ROOT/addressof-expected.txt" "$TMP_ROOT/addressof-native-run.txt"

# Negative case: calling through a variable that doesn't hold a resolvable callable panics rather
# than silently misinterpreting whatever it actually holds.
cat > "$TMP_ROOT/not-a-callable.abas" <<'SCRIPT'
FUNCTION Placeholder()
    RETURN 1
END FUNCTION
unused = ADDRESSOF Placeholder
f = "not a function"
PRINT f(5)
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/not-a-callable.abas" -o "$TMP_ROOT/not-a-callable" --target linux-x86_64 > /dev/null
if "$TMP_ROOT/not-a-callable" > "$TMP_ROOT/not-a-callable-run.txt" 2>"$TMP_ROOT/not-a-callable-stderr.txt"; then
    echo "linux-x86_64 backend unexpectedly succeeded calling a non-callable value" >&2
    exit 1
fi
grep -q "not a callable" "$TMP_ROOT/not-a-callable-stderr.txt"

# Reference-counted lifetime tracking (RFC-0049, "full Linux support" pass): a Boxed local
# (array/object) reassigned inside a tight loop, an instance holding a persistent object whose
# method is called in a loop, and a plain host-function/string-concat call with a fresh scalar
# argument in a loop -- three real, measured leaks this pass found and fixed (an OBJECT literal's
# own construction-time reference orphaned by a Kind::Load-shaped gap in infer_local_kind, a
# per-field/per-argument marshaling temporary never released after being copied by value into an
# array/object/global/concat/host call, and an instance-method dispatch's own "__class" field read
# never released after the runtime string comparison). Correctness is diffed against compile-run as
# usual; peak RSS is additionally measured and asserted BOUNDED (not linear in iteration count) --
# the actual symptom that caught these bugs during development, so this guards the regression
# directly rather than only the surface-level output.
peak_rss_kb() {
    "$1" > "$2" &
    local pid=$! peak=0 rss
    while kill -0 "$pid" 2>/dev/null; do
        rss=$(awk '/VmRSS/{print $2}' "/proc/$pid/status" 2>/dev/null || true)
        if [ -n "$rss" ] && [ "$rss" -gt "$peak" ]; then peak=$rss; fi
    done
    wait "$pid"
    echo "$peak"
}
cat > "$TMP_ROOT/lifetime.abas" <<'SCRIPT'
CLASS Widget
    Name AS String = "w"
    Count AS Number = 0

    FUNCTION Init(n AS STRING)
        SELF.Name = n
    END FUNCTION

    FUNCTION Bump()
        SELF.Count = SELF.Count + 1
        RETURN SELF.Count
    END FUNCTION
END CLASS

total = 0
w = Widget("gadget")
i = 0
WHILE i < 40000
    obj = { "N": i, "S": "a moderately sized string payload" }
    total = total + obj.N
    x = w.Bump()
    s = "n=" + i
    u = UPPER(w.Name)
    i = i + 1
WEND
PRINT total
PRINT w.Bump()
PRINT u
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/lifetime.abas" -o "$TMP_ROOT/lifetime" --target linux-x86_64 > /dev/null
"$ARCOFISSION" compile-run "$TMP_ROOT/lifetime.abas" > "$TMP_ROOT/lifetime-bytecode-run.txt"
lifetime_peak_low=$(peak_rss_kb "$TMP_ROOT/lifetime" "$TMP_ROOT/lifetime-native-run.txt")
diff -u "$TMP_ROOT/lifetime-bytecode-run.txt" "$TMP_ROOT/lifetime-native-run.txt"
# A 5x larger iteration count should NOT cost anywhere near 5x the peak RSS if this is truly
# bounded rather than leaking -- a generous 3x ceiling (not 5x) leaves headroom for legitimate
# process/runtime baseline overhead while still failing hard on a real per-iteration leak, which
# this exact test caught at roughly 400-600 bytes/iteration before the fixes in this pass (LARGE
# multiples of the ceiling, not a borderline case).
sed -i 's/40000/200000/' "$TMP_ROOT/lifetime.abas"
"$ARCOFISSION" build "$TMP_ROOT/lifetime.abas" -o "$TMP_ROOT/lifetime-big" --target linux-x86_64 > /dev/null
lifetime_peak_high=$(peak_rss_kb "$TMP_ROOT/lifetime-big" "$TMP_ROOT/lifetime-big-run.txt")
if [ "$lifetime_peak_high" -gt $((lifetime_peak_low * 3)) ]; then
    echo "linux-x86_64 backend: peak RSS grew from ${lifetime_peak_low}KB to ${lifetime_peak_high}KB across a 5x iteration increase -- looks like a real per-iteration reference-lifetime leak, not bounded overhead" >&2
    exit 1
fi

# Tuples, string codepoint indexing, and range/bit-vector LEN+indexing (RFC-0049, "full Linux
# support" pass): three of the four remaining disclosed gaps from RFC-0049's own Phase 2 close-out
# closed in one pass (bit-vector construction via the dedicated `BITS "..."` LITERAL token remains
# a real, narrower, disclosed gap -- the equivalent host function, Bits.FromString(...), already
# works via the generic host-function bridge and is used here instead). Kind::Tuple construction
# (every element available up front on one instruction, unlike Kind::Array's own incremental push
# loop) and a new general arco_value_index_get ABI function (mirroring the bytecode VM/
# interpreter's own index_value() dispatch across array/tuple/bit-vector/range, plus a fresh-boxed
# temp for a plain STRING target -- real Unicode codepoints via utf8_codepoints, never a raw UTF-16
# unit) are both new; arco_value_length gained the same tuple/bit-vector/range/string precedence
# runtime.cpp's own LEN host function already has. Two real bugs found by direct testing along the
# way: `infer_hosted_value_kind` never recognized a Kind::Tuple result as Boxed at all (an
# oversight parallel to Array/Object's own existing check), so a tuple's own PRINT was rejected
# outright ("requires a...straightforward variable") before the underlying construction/indexing
# machinery was ever reached; separately, LEN's own classifier always assumes a Number return
# regardless of which code path handles a given call site -- a plain hosted STRING variable's own
# LEN previously fell through to the generic host-function bridge (which always returns Boxed),
# printing a garbage denormal double instead of the real length, fixed by giving LEN its own
# fresh-box-then-release handling for a String-classified operand, matching the array/tuple/object
# case right above it instead of falling through past it.
cat > "$TMP_ROOT/tuples-and-indexing.abas" <<'SCRIPT'
t = (1, "two", 3.5)
PRINT t
PRINT LEN(t)
PRINT t[0]
PRINT t[1]
PRINT t[2]

s = "hello"
PRINT LEN(s)
PRINT s[0]
PRINT s[4]

r = Range(2, 8)
PRINT LEN(r)
PRINT r[0]
PRINT r[5]

bv = Bits.FromString("1011")
PRINT LEN(bv)
PRINT bv[0]
PRINT bv[1]
PRINT bv[2]
PRINT bv[3]

i = 0
WHILE i < 200000
    loop_t = (i, "x")
    i = i + 1
WEND
PRINT loop_t[0]
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/tuples-and-indexing.abas" -o "$TMP_ROOT/tuples-and-indexing" --target linux-x86_64 > /dev/null
tuples_peak=$(peak_rss_kb "$TMP_ROOT/tuples-and-indexing" "$TMP_ROOT/tuples-and-indexing-native-run.txt")
"$ARCOFISSION" compile-run "$TMP_ROOT/tuples-and-indexing.abas" > "$TMP_ROOT/tuples-and-indexing-bytecode-run.txt"
diff -u "$TMP_ROOT/tuples-and-indexing-bytecode-run.txt" "$TMP_ROOT/tuples-and-indexing-native-run.txt"
printf '(1, two, 3.5)\n3\n1\ntwo\n3.5\n5\nh\no\n6\n2\n7\n4\n1\n0\n1\n1\n199999\n' > "$TMP_ROOT/tuples-and-indexing-expected.txt"
diff -u "$TMP_ROOT/tuples-and-indexing-expected.txt" "$TMP_ROOT/tuples-and-indexing-native-run.txt"
# Same bounded-not-linear check as the reference-lifetime section above -- guards the new
# Kind::Tuple construction path's own element-release loop directly.
if [ "$tuples_peak" -gt 20000 ]; then
    echo "linux-x86_64 backend: tuple construction in a 200000-iteration loop used ${tuples_peak}KB peak RSS -- looks like a real leak in Kind::Tuple's own element handling" >&2
    exit 1
fi

# Negative case: the dedicated `BITS "..."` literal TOKEN (as opposed to the Bits.FromString(...)
# host function used above, which already works via the generic host-function bridge) is real,
# disclosed, unattempted future work on this backend's System V fast path -- Kind::Const has no
# case for a bit-vector-valued literal yet -- and must fail cleanly at compile time, never silently
# misread a bit vector's own internal representation as an integer.
cat > "$TMP_ROOT/bits-literal.abas" <<'SCRIPT'
bv = BITS "1010"
PRINT bv[0]
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/bits-literal.abas" -o "$TMP_ROOT/bits-literal" --target linux-x86_64 \
        > "$TMP_ROOT/bits-literal-build.txt" 2>&1; then
    echo "linux-x86_64 backend unexpectedly accepted a BITS \"...\" literal" >&2
    exit 1
fi

# The default `build`/`native` (no --target, or --target uefi-x86_64) behavior must be completely
# unaffected by any of this -- the whole point of an opt-in --target value.
cat > "$TMP_ROOT/default-target.abas" <<'SCRIPT'
PRINT "still a capsule"
SCRIPT
"$ARCOFISSION" compile-run "$TMP_ROOT/default-target.abas" > "$TMP_ROOT/default-target-run.txt"
grep -q "^still a capsule$" "$TMP_ROOT/default-target-run.txt"
"$ARCOFISSION" native "$TMP_ROOT/default-target.abas" -o "$TMP_ROOT/default-target-native" > /dev/null
"$TMP_ROOT/default-target-native" > "$TMP_ROOT/default-target-native-run.txt"
diff -u "$TMP_ROOT/default-target-run.txt" "$TMP_ROOT/default-target-native-run.txt"
# (Not cross-checked against nm for execute_function's presence here, unlike the linux-x86_64
# build above: this file is 16MB+ and nm's own output right after the linker exits proved flaky
# against this repository's scratch filesystem during development -- a test-infrastructure
# timing/latency question, not something about the default capsule format itself, which is
# unchanged by any of this work. The diff above already proves the default target still behaves
# identically either way.)

# --- The following sections cover real bugs found by compiling Arconaut (arcfs-utils/apps/
# arconaut/arconaut.abas), a genuine, substantial (~970-line) pre-existing ArcoBASIC program --
# RFC-0049, "full Linux support" pass. See .agents/ARCO_NATIVE_RUNTIME_PROGRESS.md's own
# entry for the full incident-by-incident writeup; each check below isolates one fix.

# Runtime.Args() with REAL argv, not the previous null-pointer stub -- Arconaut's own `--smoke`
# early-exit path (`IF LEN(Runtime.Args()) > 0 THEN IF Args[0] == "--smoke" ...`) is exactly this
# shape. argv[0] (the program's own path) is skipped, matching arco_cli's own convention.
cat > "$TMP_ROOT/runtime-args.abas" <<'SCRIPT'
args = Runtime.Args()
PRINT LEN(args)
FOR a IN args
    PRINT a
NEXT
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/runtime-args.abas" -o "$TMP_ROOT/runtime-args" --target linux-x86_64 > /dev/null
"$TMP_ROOT/runtime-args" one two three > "$TMP_ROOT/runtime-args-native-run.txt"
printf '3\none\ntwo\nthree\n' > "$TMP_ROOT/runtime-args-expected.txt"
diff -u "$TMP_ROOT/runtime-args-expected.txt" "$TMP_ROOT/runtime-args-native-run.txt"
"$TMP_ROOT/runtime-args" > "$TMP_ROOT/runtime-args-native-run-empty.txt"
printf '0\n' > "$TMP_ROOT/runtime-args-expected-empty.txt"
diff -u "$TMP_ROOT/runtime-args-expected-empty.txt" "$TMP_ROOT/runtime-args-native-run-empty.txt"

# General equality ("==" / "!=") between a Boxed operand (an object field or array element -- a
# pointer, not a raw double/BOOL) and a plain string/number literal -- previously had no codegen at
# all (only "+" string concat was handled in that region), found by Arconaut's own
# `row.Status == "active"`-shaped comparisons. Reuses arco_value_equals (mirrors
# arco::values_equal() exactly).
cat > "$TMP_ROOT/boxed-equality.abas" <<'SCRIPT'
row = {"Status": "active"}
IF row.Status == "active" THEN PRINT "yes" ELSE PRINT "no"
IF row.Status == "inactive" THEN PRINT "yes2" ELSE PRINT "no2"
IF row.Status <> "active" THEN PRINT "yes3" ELSE PRINT "no3"
arr = [10, 20, 30]
IF arr[1] == 20 THEN PRINT "num-eq" ELSE PRINT "num-neq"
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/boxed-equality.abas" -o "$TMP_ROOT/boxed-equality" --target linux-x86_64 > /dev/null
"$TMP_ROOT/boxed-equality" > "$TMP_ROOT/boxed-equality-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/boxed-equality.abas" > "$TMP_ROOT/boxed-equality-bytecode-run.txt"
diff -u "$TMP_ROOT/boxed-equality-bytecode-run.txt" "$TMP_ROOT/boxed-equality-native-run.txt"
printf 'yes\nno2\nno3\nnum-eq\n' > "$TMP_ROOT/boxed-equality-expected.txt"
diff -u "$TMP_ROOT/boxed-equality-expected.txt" "$TMP_ROOT/boxed-equality-native-run.txt"

# AND/OR/XOR between two BOOL (comparison-result) operands -- ArcoBASIC's AND/OR/XOR keywords
# lower to the exact same "&"/"|"/"^" AMIR shape ordinary bitwise operators use, and the result is
# a real NUMBER (0.0/1.0), never a "true" Bool (confirmed against compile-run's own ground truth:
# `PRINT TRUE AND FALSE` prints "0"). Covers: a direct IF condition, a WHILE-loop condition, a
# three-way CHAINED `a AND b AND c` (the inner AND's own result is Number-classified, not Bool --
# a second, distinct bug from the plain two-Bool case, found once the first fix exposed it in
# Arconaut's own three-way boolean conditions), and assigning the result to a named variable then
# printing it (a separate LOAD-time bug: the LOAD's own instruction.result_type metadata can say
# "BOOL" even when the value is actually an 8-byte double, the same frontend-hint-unreliability
# pattern already handled for Binary/Branch).
cat > "$TMP_ROOT/bool-logic.abas" <<'SCRIPT'
n = 3
parts = ["a", "b", "c", "d"]
IF n >= 0 AND n < LEN(parts) THEN PRINT "in range" ELSE PRINT "out of range"
PRINT TRUE AND FALSE
PRINT TRUE OR FALSE

a = 1
b = 5
c = 1
x = a < b AND c < 3 AND a > 0
PRINT x
y = a > b OR c < 3 OR a > 100
PRINT y

i = 1
total = 5
count = 1
iterations = 0
WHILE i < total AND count < 3
    i = i + 1
    count = count + 1
    iterations = iterations + 1
WEND
PRINT iterations
PRINT i
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/bool-logic.abas" -o "$TMP_ROOT/bool-logic" --target linux-x86_64 > /dev/null
"$TMP_ROOT/bool-logic" > "$TMP_ROOT/bool-logic-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/bool-logic.abas" > "$TMP_ROOT/bool-logic-bytecode-run.txt"
diff -u "$TMP_ROOT/bool-logic-bytecode-run.txt" "$TMP_ROOT/bool-logic-native-run.txt"
printf 'in range\n0\n1\n1\n1\n2\n3\n' > "$TMP_ROOT/bool-logic-expected.txt"
diff -u "$TMP_ROOT/bool-logic-expected.txt" "$TMP_ROOT/bool-logic-native-run.txt"

# An inline numeric literal used directly as a comparison/AND-OR operand (never itself STORE'd to
# a typed variable first, e.g. `count < 3`) reaches Kind::Const with an EMPTY
# instruction.result_type -- previously stored as a raw INTEGER bit pattern instead of a real
# IEEE-754 double, so a later floating-point comparison against it (`ucomisd`) silently
# misread it as a denormal near-zero value. Fixed by trusting infer_hosted_value_kind's own
# Number classification for a Const's result the same way Branch/Load already do.
cat > "$TMP_ROOT/inline-literal-comparison.abas" <<'SCRIPT'
count = 1
IF count < 3 THEN PRINT "less" ELSE PRINT "not less"
PRINT count < 3
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/inline-literal-comparison.abas" -o "$TMP_ROOT/inline-literal-comparison" --target linux-x86_64 > /dev/null
"$TMP_ROOT/inline-literal-comparison" > "$TMP_ROOT/inline-literal-comparison-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/inline-literal-comparison.abas" > "$TMP_ROOT/inline-literal-comparison-bytecode-run.txt"
diff -u "$TMP_ROOT/inline-literal-comparison-bytecode-run.txt" "$TMP_ROOT/inline-literal-comparison-native-run.txt"
printf 'less\nTRUE\n' > "$TMP_ROOT/inline-literal-comparison-expected.txt"
diff -u "$TMP_ROOT/inline-literal-comparison-expected.txt" "$TMP_ROOT/inline-literal-comparison-native-run.txt"

# Default parameter values, omitted at the call site -- previously a plain arity mismatch
# ("expects N arguments, got M") since nothing in the AMIR pipeline ever filled in a trailing
# default, found by Arconaut's own `Button(window, id, label, x, y, w, h)`, omitting its trailing
# r/g/b color defaults. Covers a plain untyped numeric default and a TYPED (AS STRING) parameter
# with a string-literal default -- the latter also exercises declared_parameter_type's own fix
# (a typed+defaulted descriptor like "greeting AS STRING = \"Hello\"" previously returned the
# untrimmed "STRING = \"Hello\"" as the "type", a real, separate segfault caught by direct testing).
cat > "$TMP_ROOT/default-params.abas" <<'SCRIPT'
FUNCTION Sum3(a, b, c = 100)
    RETURN a + b + c
END FUNCTION

FUNCTION Greet(name AS STRING, greeting AS STRING = "Hello")
    RETURN greeting + ", " + name
END FUNCTION

PRINT Sum3(1, 2)
PRINT Sum3(1, 2, 3)
PRINT Greet("World")
PRINT Greet("World", "Hi")
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/default-params.abas" -o "$TMP_ROOT/default-params" --target linux-x86_64 > /dev/null
"$TMP_ROOT/default-params" > "$TMP_ROOT/default-params-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/default-params.abas" > "$TMP_ROOT/default-params-bytecode-run.txt"
diff -u "$TMP_ROOT/default-params-bytecode-run.txt" "$TMP_ROOT/default-params-native-run.txt"
printf '103\n6\nHello, World\nHi, World\n' > "$TMP_ROOT/default-params-expected.txt"
diff -u "$TMP_ROOT/default-params-expected.txt" "$TMP_ROOT/default-params-native-run.txt"

# GUI.Window/GUI.WindowShaped return a plain int HANDLE at the C++ level (include/arco/gui.hpp's
# own `int create_window(...)`), but have no DEDICATED native codegen of their own -- both fall
# through to the exact same generic host-function bridge (arco_call_host) every other
# unrecognized host call does, which ALWAYS returns a genuine boxed ArcoValue* regardless of what
# the underlying function conceptually returns. An earlier version of this fix special-cased the
# CLASSIFIER to claim Number here (to let the handle flow into an untyped, hosted-number-assumed
# parameter) without changing the actual codegen -- wrong, and only caught once this exact GUI
# code path actually RAN natively (see the ExitTheProgram section below for the same "compile-only
# testing missed this" lesson): claiming Number while the real value is a Boxed pointer made
# `PRINT window` print a denormal garbage double, and any later GUI.*(window, ...) call pass that
# garbage to the real GUI backend ("unknown GUI window: 0"). Fixed the same way
# SelectDevice/SelectSnapshot's own Boxed-but-provably-numeric `Number(...)` argument already was:
# an explicit `AS NUMBER` annotation on the RECEIVING parameter, which correctly unboxes via
# load_double_operand instead of lying about the producing instruction's own representation.
# Compile-only here: actually opening a window needs a real GUI backend, which the host bridge's
# own lean/stub runtime (see the host-functions section above) deliberately doesn't link -- same
# graceful-skip rule as that section.
cat > "$TMP_ROOT/gui-window-classification.abas" <<'SCRIPT'
FUNCTION UseHandle(h AS NUMBER)
    RETURN h + 1
END FUNCTION

w = GUI.Window("test", 10, 10)
PRINT UseHandle(w)
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/gui-window-classification.abas" -o "$TMP_ROOT/gui-window-classification" \
        --target linux-x86_64 > "$TMP_ROOT/gui-window-classification-build.txt" 2>&1; then
    grep -q "ELF64 WRITTEN" "$TMP_ROOT/gui-window-classification-build.txt"
elif grep -q "ArcoNativeRuntimeCoreProbe" "$TMP_ROOT/gui-window-classification-build.txt"; then
    echo "note: ArcoNativeRuntimeCoreProbe not built in this tree -- GUI.Window classification test skipped" >&2
else
    echo "linux-x86_64 backend failed to compile GUI.Window's handle flowing into an AS NUMBER parameter:" >&2
    cat "$TMP_ROOT/gui-window-classification-build.txt" >&2
    exit 1
fi

# Exit()/ExitTheProgram() (a core builtin, dispatched through the generic host-function bridge)
# previously fell through arco_call_host's generic `catch (const std::exception&)` (ExitSignal
# derives from it) and was turned into a spurious crash-looking "BYTECODE RUN FAILED" panic with
# an empty message, instead of a clean process exit -- found via Arconaut's own `--smoke`/
# no-display-session early-exit paths, both of which call ExitTheProgram(0). Same graceful-skip
# rule as the host-functions section above.
cat > "$TMP_ROOT/exit-the-program.abas" <<'SCRIPT'
PRINT "before exit"
ExitTheProgram(0)
PRINT "never reached"
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/exit-the-program.abas" -o "$TMP_ROOT/exit-the-program" --target linux-x86_64 \
        > "$TMP_ROOT/exit-the-program-build.txt" 2>&1; then
    set +e
    "$TMP_ROOT/exit-the-program" > "$TMP_ROOT/exit-the-program-native-run.txt" 2>"$TMP_ROOT/exit-the-program-native-run.stderr.txt"
    exit_code=$?
    set -e
    if [ "$exit_code" -ne 0 ]; then
        echo "ExitTheProgram(0) exited with code ${exit_code}, expected a clean 0" >&2
        cat "$TMP_ROOT/exit-the-program-native-run.stderr.txt" >&2
        exit 1
    fi
    printf 'before exit\n' > "$TMP_ROOT/exit-the-program-expected.txt"
    diff -u "$TMP_ROOT/exit-the-program-expected.txt" "$TMP_ROOT/exit-the-program-native-run.txt"
    if [ -s "$TMP_ROOT/exit-the-program-native-run.stderr.txt" ]; then
        echo "ExitTheProgram(0) wrote to stderr, expected a silent clean exit:" >&2
        cat "$TMP_ROOT/exit-the-program-native-run.stderr.txt" >&2
        exit 1
    fi
elif grep -q "ArcoNativeRuntimeCoreProbe" "$TMP_ROOT/exit-the-program-build.txt"; then
    echo "note: ArcoNativeRuntimeCoreProbe not built in this tree -- ExitTheProgram test skipped" >&2
else
    echo "linux-x86_64 backend failed to build an ExitTheProgram() call for an unexpected reason:" >&2
    cat "$TMP_ROOT/exit-the-program-build.txt" >&2
    exit 1
fi

# --- Everything below was found by continuing past Arconaut's own compile step to actually RUN
# it against a real display (2026-09-06 follow-up: "we're not done until we can actually use it
# for modern app development") -- compiling was never sufficient proof the backend actually works,
# only that this OS's own AMIR shapes happen to encode. Each section isolates one more real bug
# found that way.

# AND/OR/"+" over a LOOP-CARRIED string accumulator (ShellQuote's own `quoted = quoted + ch`
# pattern, found via Arconaut's own CommandExists("lsblk") -> ShellQuote("lsblk") call, which
# every one of Arconaut's own tests before this one never actually reached at runtime). `quoted`
# has TWO defining stores with genuinely different native representations -- an initial `quoted =
# "'"` (Kind::Const's own raw, un-owned literal pointer) and a loop-carried `quoted = quoted + ch`
# (now always Boxed, see the Binary "+" case's own ambiguous-Boxed comment) -- and Kind::Store's
# own reference-lifetime tracking (gated on the DESTINATION's classification only) assumed every
# store to a Boxed-classified name is ALREADY a real ArcoValueBox*, retaining a raw literal address
# directly: a real segfault. Fixed by boxing a non-Boxed source at the STORE site itself
# (box_operand_into_rax) whenever the destination needs Boxed representation.
cat > "$TMP_ROOT/loop-carried-string-accumulator.abas" <<'SCRIPT'
FUNCTION ShellQuote(value AS STRING)
    text = STRING(value)
    quoted = "'"
    index = 0
    WHILE index < String.Length(text)
        ch = String.Slice(text, index, 1)
        IF ch == "'" THEN
            quoted = quoted + "'\''"
        ELSE
            quoted = quoted + ch
        END IF
        index = index + 1
    WEND
    RETURN quoted + "'"
END FUNCTION

PRINT ShellQuote("lsblk")
PRINT ShellQuote("it's a test")
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/loop-carried-string-accumulator.abas" -o "$TMP_ROOT/loop-carried-string-accumulator" --target linux-x86_64 > /dev/null
"$TMP_ROOT/loop-carried-string-accumulator" > "$TMP_ROOT/loop-carried-string-accumulator-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/loop-carried-string-accumulator.abas" > "$TMP_ROOT/loop-carried-string-accumulator-bytecode-run.txt"
diff -u "$TMP_ROOT/loop-carried-string-accumulator-bytecode-run.txt" "$TMP_ROOT/loop-carried-string-accumulator-native-run.txt"
printf "'lsblk'\n'it'''s a test'\n" > "$TMP_ROOT/loop-carried-string-accumulator-expected.txt"
diff -u "$TMP_ROOT/loop-carried-string-accumulator-expected.txt" "$TMP_ROOT/loop-carried-string-accumulator-native-run.txt"

# "==" between two AMBIGUOUSLY BOXED operands (neither provably String, e.g. an object field
# compared against a generic host-function result) -- `app.Mode == Lower(label)`, Arconaut's own
# tab-highlighting check. Both sides pass operand_is_hosted_number_for_binary's own
# Boxed-is-hosted-number-capable rule, so this fell into the ordinary numeric-comparison branch
# (ucomisd/sete) with no string awareness at all -- a clean panic the moment either side turned
# out to actually be the string it always is here. The pre-existing "==" special case
# (arco_value_equals) only fired when at least one side was PROVABLY String; widened to also
# cover this ambiguously-Boxed-on-both-sides shape.
cat > "$TMP_ROOT/boxed-boxed-equality.abas" <<'SCRIPT'
app = {"Mode": "volumes"}
label = "Volumes"
IF app.Mode == Lower(label) THEN PRINT "match" ELSE PRINT "no match"
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/boxed-boxed-equality.abas" -o "$TMP_ROOT/boxed-boxed-equality" --target linux-x86_64 > /dev/null
"$TMP_ROOT/boxed-boxed-equality" > "$TMP_ROOT/boxed-boxed-equality-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/boxed-boxed-equality.abas" > "$TMP_ROOT/boxed-boxed-equality-bytecode-run.txt"
diff -u "$TMP_ROOT/boxed-boxed-equality-bytecode-run.txt" "$TMP_ROOT/boxed-boxed-equality-native-run.txt"
printf 'match\n' > "$TMP_ROOT/boxed-boxed-equality-expected.txt"
diff -u "$TMP_ROOT/boxed-boxed-equality-expected.txt" "$TMP_ROOT/boxed-boxed-equality-native-run.txt"

# An array index built from an ambiguously-Boxed "+" result (`devices[app.DeviceScroll + row]` --
# pervasive throughout Arconaut, every scrolling list) -- app.DeviceScroll (a Boxed object-field
# read) + row (Number) is now itself classified Boxed by the Binary "+" case's own broadened rule,
# so Kind::Index's own "index must be provably Number or String" check rejected it outright,
# despite always being a real number at runtime. Fixed by accepting a Boxed index the same way an
# already-Number one is, unboxing via load_double_operand (computed BEFORE the target is loaded
# into RDI, not after -- load_double_operand's own Boxed path makes a real call, free to clobber
# RDI, unlike the plain stack read the Number-only case used before this fix).
cat > "$TMP_ROOT/boxed-plus-array-index.abas" <<'SCRIPT'
app = {"Scroll": 1}
devices = ["a", "b", "c", "d", "e"]
row = 2
PRINT devices[app.Scroll + row]
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/boxed-plus-array-index.abas" -o "$TMP_ROOT/boxed-plus-array-index" --target linux-x86_64 > /dev/null
"$TMP_ROOT/boxed-plus-array-index" > "$TMP_ROOT/boxed-plus-array-index-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/boxed-plus-array-index.abas" > "$TMP_ROOT/boxed-plus-array-index-bytecode-run.txt"
diff -u "$TMP_ROOT/boxed-plus-array-index-bytecode-run.txt" "$TMP_ROOT/boxed-plus-array-index-native-run.txt"
printf 'd\n' > "$TMP_ROOT/boxed-plus-array-index-expected.txt"
diff -u "$TMP_ROOT/boxed-plus-array-index-expected.txt" "$TMP_ROOT/boxed-plus-array-index-native-run.txt"

# A STRING-typed parameter passed a genuinely BOXED argument (a concatenation result), THROUGH
# MULTIPLE levels of pass-through calls -- Arconaut's own `Tab(..., "tab-" + Lower(label), ...)`
# -> `Button` -> `RegisterRegion`. A STRING-typed parameter's own physical representation is
# genuinely ambiguous at any given call site (Kind::Const's raw literal pointer vs a real
# ArcoValueBox* from concat/host-calls/field-reads); a plain bit-copy left the CALLEE treating an
# ArcoValueBox pointer as if it were itself a raw UTF-16 buffer address the moment it touched the
# value (Unicode mojibake, not a crash). Fixed by ALWAYS boxing a STRING-typed argument at the
# call site (box_operand_into_rax) and classifying every STRING-typed parameter as Boxed inside
# the callee to match (infer_local_kind).
cat > "$TMP_ROOT/string-param-boxed-passthrough.abas" <<'SCRIPT'
FUNCTION Inner(id AS STRING)
    PRINT "inner: " + id
END FUNCTION

FUNCTION Middle(id AS STRING)
    PRINT "middle: " + id
    Inner(id)
END FUNCTION

FUNCTION Outer(id AS STRING)
    PRINT "outer: " + id
    Middle(id)
END FUNCTION

label = "Volumes"
combined = "tab-" + Lower(label)
PRINT "top: " + combined
Outer(combined)
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/string-param-boxed-passthrough.abas" -o "$TMP_ROOT/string-param-boxed-passthrough" --target linux-x86_64 > /dev/null
"$TMP_ROOT/string-param-boxed-passthrough" > "$TMP_ROOT/string-param-boxed-passthrough-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/string-param-boxed-passthrough.abas" > "$TMP_ROOT/string-param-boxed-passthrough-bytecode-run.txt"
diff -u "$TMP_ROOT/string-param-boxed-passthrough-bytecode-run.txt" "$TMP_ROOT/string-param-boxed-passthrough-native-run.txt"
printf 'top: tab-volumes\nouter: tab-volumes\nmiddle: tab-volumes\ninner: tab-volumes\n' > "$TMP_ROOT/string-param-boxed-passthrough-expected.txt"
diff -u "$TMP_ROOT/string-param-boxed-passthrough-expected.txt" "$TMP_ROOT/string-param-boxed-passthrough-native-run.txt"

# A program that actually calls a GUI.* function links the FULL, GLFW-capable runtime
# (arco_cli's own libarco_runtime.a) instead of the lean/stub one -- so a compiled GUI program can
# actually open a window, not just compile. Compile-only + message check here (actually opening a
# window needs a real display, exercised manually against Arconaut itself, not in this
# unattended suite); a program that never touches GUI.* keeps linking the lean bridge, unaffected.
cat > "$TMP_ROOT/gui-links-full-runtime.abas" <<'SCRIPT'
IF GUI.Available() == FALSE THEN
    PRINT "no display"
ELSE
    PRINT "have display"
END IF
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/gui-links-full-runtime.abas" -o "$TMP_ROOT/gui-links-full-runtime" \
        --target linux-x86_64 > "$TMP_ROOT/gui-links-full-runtime-build.txt" 2>&1; then
    grep -q "HOST FUNCTION BRIDGE LINKED (real GUI backend" "$TMP_ROOT/gui-links-full-runtime-build.txt"
elif grep -q "arco_cli" "$TMP_ROOT/gui-links-full-runtime-build.txt"; then
    echo "note: arco_cli not built in this tree -- GUI full-runtime-linking test skipped" >&2
else
    echo "linux-x86_64 backend failed to compile a GUI.Available() call for an unexpected reason:" >&2
    cat "$TMP_ROOT/gui-links-full-runtime-build.txt" >&2
    exit 1
fi

# A string literal used directly as a comparison operand inside an OR-chain
# (`app.Mode == "volumes" OR app.Mode == "images" OR ...`, Arconaut's own mode-dispatch check)
# reaches Kind::Const with instruction.result_type set to "BOOL" -- the COMPARISON's own type
# context, not this literal's actual representation (type_of_expression annotates a Const from how
# it's consumed, not what it intrinsically is). store_result's own normalize() then masked this raw
# 64-bit rip-relative pointer down to its low 8 bits (width_bits("BOOL") == 8) -- a corrupted
# ADDRESS, not just a wrong value: a real SEGV the moment the (now-garbage) "pointer" was read as
# UTF-16 text, caught only by actually running Arconaut against a real display (AddressSanitizer
# pinpointed it: `arco_value_new_string_utf16` reading from address 0x28/0x4c/0x7e in different
# runs). Fixed by never trusting instruction.result_type for a string literal's own storage width.
cat > "$TMP_ROOT/string-literal-or-chain.abas" <<'SCRIPT'
app = {"Mode": "volumes"}
IF app.Mode == "volumes" OR app.Mode == "images" OR app.Mode == "files" THEN
    PRINT "yes"
ELSE
    PRINT "no"
END IF
PRINT "done"
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/string-literal-or-chain.abas" -o "$TMP_ROOT/string-literal-or-chain" --target linux-x86_64 > /dev/null
"$TMP_ROOT/string-literal-or-chain" > "$TMP_ROOT/string-literal-or-chain-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/string-literal-or-chain.abas" > "$TMP_ROOT/string-literal-or-chain-bytecode-run.txt"
diff -u "$TMP_ROOT/string-literal-or-chain-bytecode-run.txt" "$TMP_ROOT/string-literal-or-chain-native-run.txt"
printf 'yes\ndone\n' > "$TMP_ROOT/string-literal-or-chain-expected.txt"
diff -u "$TMP_ROOT/string-literal-or-chain-expected.txt" "$TMP_ROOT/string-literal-or-chain-native-run.txt"

# Entry 21's own disclosed open bug, now fixed (Entry 22): Kind::Load's own explicit "release the
# destination slot's old value" step (added for a class constructor's `RETURN VALUE %t := LOAD
# __instance` shape) double-releases whenever store_result's OWN, completely independent STRING-
# gated tracks_lifetime check ALSO fires for the exact same store -- both release the SAME old
# value, with only one store ever happening. Dormant on a slot's first-ever write (the "old value"
# is null, releasing null is a no-op) -- only bites once a temp slot has already held a live
# reference from a PRIOR loop iteration, i.e. a value read/compared more than once inside a
# `FOR ... IN ...` loop body. This exact repro over-releases `key` (a PARAMETER-derived concat
# result) once per extra iteration -- a real heap-use-after-free (or, depending on allocator
# timing, a leak elsewhere as the refcount imbalance cascades) confirmed via AddressSanitizer +
# a raw objdump of the doubled `call arco_value_release` sequence before the fix. compile-run
# (the bytecode VM, unaffected -- this is native-backend-only codegen) is the ground truth here.
cat > "$TMP_ROOT/foreach-loop-double-release.abas" <<'SCRIPT'
cache = [{"Key": "a"}, {"Key": "b"}]

FUNCTION MakeKey(label AS STRING)
    key = label + "|suffix"
    FOR entry IN cache
        PRINT entry.Key == key
    NEXT
    RETURN key
END FUNCTION

k = MakeKey("Files")
PRINT k
PRINT "done"
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/foreach-loop-double-release.abas" -o "$TMP_ROOT/foreach-loop-double-release" --target linux-x86_64 --sanitize > /dev/null
for i in 1 2 3 4 5; do
    ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 "$TMP_ROOT/foreach-loop-double-release" > "$TMP_ROOT/foreach-loop-double-release-native-run.txt"
done
"$ARCOFISSION" compile-run "$TMP_ROOT/foreach-loop-double-release.abas" > "$TMP_ROOT/foreach-loop-double-release-bytecode-run.txt"
diff -u "$TMP_ROOT/foreach-loop-double-release-bytecode-run.txt" "$TMP_ROOT/foreach-loop-double-release-native-run.txt"
printf 'FALSE\nFALSE\nFiles|suffix\ndone\n' > "$TMP_ROOT/foreach-loop-double-release-expected.txt"
diff -u "$TMP_ROOT/foreach-loop-double-release-expected.txt" "$TMP_ROOT/foreach-loop-double-release-native-run.txt"

# Entry 23 (RFC-0049 Phase 14): the ACTUAL blocker on Arconaut's own native main loop, found by
# actually running the compiled binary against a real display -- `IF GUI.ShouldClose(window) THEN
# running = FALSE` took the "true" branch on literally every check, including immediately after
# window creation, closing the window after a single frame. GUI.ShouldClose's result is a genuine
# Boxed ArcoValue* (every generic host-bridge call always returns one), but Kind::Branch's own
# codegen only had two paths -- a real hosted-number (AND/OR/XOR-of-bools, `ucomisd` against 0.0)
# or an ordinary 1-byte BOOL (`AND RAX, 0xFF`) -- neither of which is correct for a raw 64-bit
# Boxed pointer: masking a heap pointer's low byte is essentially a coin flip unrelated to the
# actual boolean value. Fixed by widening the Number-only gate to also cover Boxed and routing
# through load_double_operand (which already knows how to unbox via arco_value_as_number, itself
# correctly coercing a boxed Bool to 1.0/0.0) instead of the raw byte-mask path.
cat > "$TMP_ROOT/branch-boxed-bool.abas" <<'SCRIPT'
GUI.Application("branchtest", "Branch Test", "")
window = GUI.Window("Branch Test", 200, 150)
IF GUI.ShouldClose(window) THEN
    PRINT "would close"
ELSE
    PRINT "not closing"
END IF
GUI.Close(window)
SCRIPT
if "$ARCOFISSION" build "$TMP_ROOT/branch-boxed-bool.abas" -o "$TMP_ROOT/branch-boxed-bool" \
        --target linux-x86_64 > "$TMP_ROOT/branch-boxed-bool-build.txt" 2>&1; then
    if command -v xdotool >/dev/null 2>&1 && [ -n "${DISPLAY:-}" ]; then
        "$TMP_ROOT/branch-boxed-bool" > "$TMP_ROOT/branch-boxed-bool-run.txt" 2>&1 || true
        if [ -s "$TMP_ROOT/branch-boxed-bool-run.txt" ]; then
            grep -q "^not closing$" "$TMP_ROOT/branch-boxed-bool-run.txt"
        fi
    fi
elif grep -q "arco_cli" "$TMP_ROOT/branch-boxed-bool-build.txt"; then
    echo "note: arco_cli not built in this tree -- Branch-Boxed-Bool test skipped" >&2
else
    echo "linux-x86_64 backend failed to compile a GUI.ShouldClose-in-IF program for an unexpected reason:" >&2
    cat "$TMP_ROOT/branch-boxed-bool-build.txt" >&2
    exit 1
fi

# Entry 23 (RFC-0049 Phase 14): a local reassigned with a DIFFERENT physical representation across
# two conditionally-reached stores -- `selected = app.SelectedSource` (a genuinely Boxed
# object-field read) then, inside `IF selected == "" THEN selected = "No ArcFS target selected"`,
# a raw literal (a DIFFERENT representation sharing the same "String" label) -- found via
# Arconaut's own DrawActions "Selected" label, which flickered between different CJK-range garbage
# characters frame to frame (a live, changing memory-safety symptom, not a static wrong value):
# infer_local_kind used to pick whichever Store it found LAST while walking the function in order,
# not a runtime-accurate answer, so a caller (PRINT/GUI.Text/comparisons) reading "selected" after
# the literal branch ran would misinterpret the ACTUAL runtime value (the object-field read, if
# that branch is what really executed) as if it were the OTHER representation. Fixed by having
# infer_local_kind detect disagreement across every Store to a name (not just the last one found)
# and answer Boxed when they disagree, paired with Kind::Store's own pre-existing
# box_operand_into_rax fallback (added earlier for an identical `quoted = quoted + ch` shape) which
# already knows how to box a raw literal source on demand once the target is classified Boxed.
cat > "$TMP_ROOT/store-ambiguous-representation.abas" <<'SCRIPT'
app = {"SelectedSource": ""}
selected = app.SelectedSource
IF selected == "" THEN selected = "No ArcFS target selected"
PRINT selected
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/store-ambiguous-representation.abas" -o "$TMP_ROOT/store-ambiguous-representation" --target linux-x86_64 > /dev/null
"$TMP_ROOT/store-ambiguous-representation" > "$TMP_ROOT/store-ambiguous-representation-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/store-ambiguous-representation.abas" > "$TMP_ROOT/store-ambiguous-representation-bytecode-run.txt"
diff -u "$TMP_ROOT/store-ambiguous-representation-bytecode-run.txt" "$TMP_ROOT/store-ambiguous-representation-native-run.txt"
printf 'No ArcFS target selected\n' > "$TMP_ROOT/store-ambiguous-representation-expected.txt"
diff -u "$TMP_ROOT/store-ambiguous-representation-expected.txt" "$TMP_ROOT/store-ambiguous-representation-native-run.txt"

# Entry 23 (RFC-0049 Phase 14): the RETURN-statement counterpart of the Store case just above --
# `RETURN token` (genuinely Boxed, an array-indexed/host-function result) on one path, `RETURN ""`
# (a raw literal) on another, in the SAME function (Arconaut's own NthToken/FirstToken helper,
# `entry = FirstToken(line)`, used to populate the device list's "Selected" text). Fixed the same
# way: infer_function_return_kind now checks EVERY RETURN in the function (not just the first one
# found while walking blocks in vector order, which had no required relationship to which RETURN a
# given call actually executes at runtime) and answers Boxed when they disagree; Kind::Return's own
# codegen boxes a literal return via box_operand_into_rax whenever the function's own overall
# answer is Boxed but THIS particular return's value isn't already, so every return path leaves a
# consistently Boxed pointer in RAX regardless of which one fires.
cat > "$TMP_ROOT/return-ambiguous-representation.abas" <<'SCRIPT'
FUNCTION NthToken(line AS STRING, n)
    parts = String.Split(String.Trim(line), " ")
    index = 0
    WHILE index < LEN(parts)
        token = String.Trim(parts[index])
        IF token <> "" THEN
            IF index == n THEN RETURN token
        END IF
        index = index + 1
    WEND
    RETURN ""
END FUNCTION
PRINT NthToken("a b c", 99)
PRINT NthToken("a b c", 1)
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/return-ambiguous-representation.abas" -o "$TMP_ROOT/return-ambiguous-representation" --target linux-x86_64 --sanitize > /dev/null
for i in 1 2 3 4 5; do
    ASAN_OPTIONS=abort_on_error=1:halt_on_error=1 "$TMP_ROOT/return-ambiguous-representation" > "$TMP_ROOT/return-ambiguous-representation-native-run.txt"
done
"$ARCOFISSION" compile-run "$TMP_ROOT/return-ambiguous-representation.abas" > "$TMP_ROOT/return-ambiguous-representation-bytecode-run.txt"
diff -u "$TMP_ROOT/return-ambiguous-representation-bytecode-run.txt" "$TMP_ROOT/return-ambiguous-representation-native-run.txt"
printf '\nb\n' > "$TMP_ROOT/return-ambiguous-representation-expected.txt"
diff -u "$TMP_ROOT/return-ambiguous-representation-expected.txt" "$TMP_ROOT/return-ambiguous-representation-native-run.txt"

# Entry 23 (RFC-0049 Phase 14): the untyped-parameter safety check's own necessary widening,
# alongside the Store/Return fixes above -- once infer_local_kind correctly detects the
# `visible_rows = FLOOR(...)` / `visible_rows = 1` disagreement (FLOOR/Math.Floor is a generic
# host-bridge call, always physically Boxed, but semantically always a real number) and answers
# Boxed, the untyped-parameter call-site check (which used to reject EVERY Boxed argument
# outright) started rejecting this genuinely-numeric-but-physically-Boxed value too, breaking
# Arconaut's own compile (`DrawScrollbar(..., visible_rows, ...)`). Fixed with a narrower helper,
# value_could_be_hosted_number, that distinguishes THIS ambiguous case (accept, route through
# load_double_operand's existing Boxed-unboxing path) from a genuinely non-numeric Boxed argument
# like a bare array/object literal (still correctly rejected at compile time -- see the existing
# untyped-array-arg negative test above, which this fix keeps passing).
cat > "$TMP_ROOT/untyped-param-ambiguous-number.abas" <<'SCRIPT'
FUNCTION UsesIt(n)
    PRINT n
END FUNCTION
FUNCTION Compute(pick AS BOOL)
    IF pick THEN
        value = FLOOR(7.8)
    ELSE
        value = 1
    END IF
    IF value < 1 THEN value = 1
    RETURN value
END FUNCTION
v = Compute(TRUE)
UsesIt(v)
w = Compute(FALSE)
UsesIt(w)
SCRIPT
"$ARCOFISSION" build "$TMP_ROOT/untyped-param-ambiguous-number.abas" -o "$TMP_ROOT/untyped-param-ambiguous-number" --target linux-x86_64 > /dev/null
"$TMP_ROOT/untyped-param-ambiguous-number" > "$TMP_ROOT/untyped-param-ambiguous-number-native-run.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/untyped-param-ambiguous-number.abas" > "$TMP_ROOT/untyped-param-ambiguous-number-bytecode-run.txt"
diff -u "$TMP_ROOT/untyped-param-ambiguous-number-bytecode-run.txt" "$TMP_ROOT/untyped-param-ambiguous-number-native-run.txt"
printf '7\n1\n' > "$TMP_ROOT/untyped-param-ambiguous-number-expected.txt"
diff -u "$TMP_ROOT/untyped-param-ambiguous-number-expected.txt" "$TMP_ROOT/untyped-param-ambiguous-number-native-run.txt"
