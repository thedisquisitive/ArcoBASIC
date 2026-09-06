#!/usr/bin/env bash
set -euo pipefail

# Regression coverage for the hot-numeric-loop JIT (include/arco/jit_x86_64.hpp's encoder is
# covered separately, byte-for-byte against nasm, in tests/unit/jit_x86_64_tests.cpp -- this test
# is the higher-level "does the whole thing actually work" check: structural loop detection in
# fission.cpp's prepare_bytecode_module/try_detect_jit_loop, lazy compilation, and the runtime
# type-guarded dispatch in execute_function).
#
# Every shape here was found necessary by a real gap during development, not picked in the
# abstract -- see each FUNCTION's comment below.

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/jit-loop-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

cat > "$TMP_ROOT/loops.abas" <<'SCRIPT'
FUNCTION SumAscending(n)
    total = 0
    FOR i = 1 TO n
        total = total + i
    NEXT
    RETURN total
END FUNCTION

' STEP -1 lowers to a computed STORE (CONST 1; UNARY -; STORE step) rather than a direct
' STORE_CONST -- the detector must resolve that chain, not just recognize a literal.
FUNCTION SumDescending(n)
    total = 0
    FOR i = n TO 1 STEP -1
        total = total + i
    NEXT
    RETURN total
END FUNCTION

' A body with more than one instruction, mixing BINARY_LOCAL_LOCAL (total + i) with
' BINARY_LOCAL_CONST (product * 2, total - 1) -- both opcode shapes must be recognized.
FUNCTION MultiOpBody(n)
    total = 0
    product = 1
    FOR i = 1 TO n
        total = total + i
        product = product * 2
        total = total - 1
    NEXT
    RETURN total + product
END FUNCTION

FUNCTION StepByTwo(n)
    total = 0
    FOR i = 0 TO n STEP 2
        total = total + i
    NEXT
    RETURN total
END FUNCTION

' Start already past the end -- the loop body must run zero times, exercising the
' loop-never-taken path through the native condition check.
FUNCTION EmptyRange(n)
    total = 0
    FOR i = 10 TO n
        total = total + i
    NEXT
    RETURN total
END FUNCTION

FUNCTION SingleIteration()
    total = 0
    FOR i = 5 TO 5
        total = total + i
    NEXT
    RETURN total
END FUNCTION

' A NaN loop bound: ucomisd sets PF=1 ("unordered"), which generated code must check before
' trusting the above/below flags, matching the interpreter's own "a NaN comparison is always
' false" semantics -- otherwise this either loops forever or reads garbage instead of exiting
' immediately with the loop body never run.
FUNCTION NanBound(n)
    total = 0
    FOR i = 1 TO n
        total = total + i
    NEXT
    RETURN total
END FUNCTION

PRINT SumAscending(10)
PRINT SumAscending(1000)
PRINT SumDescending(10)
PRINT MultiOpBody(5)
PRINT StepByTwo(10)
PRINT EmptyRange(5)
PRINT SingleIteration()
PRINT NanBound(0 / 0)
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/loops.abas" > "$TMP_ROOT/jit-run.txt"
grep -q "^55$" "$TMP_ROOT/jit-run.txt"
grep -q "^500500$" "$TMP_ROOT/jit-run.txt"
grep -q "^42$" "$TMP_ROOT/jit-run.txt"
grep -q "^30$" "$TMP_ROOT/jit-run.txt"
grep -q "^0$" "$TMP_ROOT/jit-run.txt"
grep -q "^5$" "$TMP_ROOT/jit-run.txt"
# Exactly 8 PRINTs, two of which ("55" for SumAscending(10)==SumDescending(10) and "0" for
# EmptyRange(5)==NanBound) share a value with another line -- so instead of grepping every
# expected number individually (undercounting real duplicates), just check the total line count
# and the byte-identical cross-checks below carry the actual correctness weight.
test "$(wc -l < "$TMP_ROOT/jit-run.txt")" = "8"

# ARCOFISSION_NO_JIT=1 forces every one of these loops through plain interpretation instead --
# byte-identical output here is the actual correctness proof (interpreter and JIT must agree),
# not just "each individually looks plausible".
ARCOFISSION_NO_JIT=1 "$ARCOFISSION" compile-run "$TMP_ROOT/loops.abas" > "$TMP_ROOT/no-jit-run.txt"
diff -u "$TMP_ROOT/jit-run.txt" "$TMP_ROOT/no-jit-run.txt"

# ARCOFISSION_JIT_DIAG=1 must actually report a dispatch for each shape -- confirming the fixture
# above is really exercising the native path (and not silently, quietly falling back to
# interpretation for some shape a future change accidentally stops detecting).
ARCOFISSION_JIT_DIAG=1 "$ARCOFISSION" compile-run "$TMP_ROOT/loops.abas" > /dev/null 2> "$TMP_ROOT/diag.txt"
for fn in SumAscending SumDescending MultiOpBody StepByTwo EmptyRange SingleIteration NanBound; do
    grep -q "dispatching native loop in $fn " "$TMP_ROOT/diag.txt"
done
# And the inverse: with the JIT off, no dispatch line should appear at all.
ARCOFISSION_NO_JIT=1 ARCOFISSION_JIT_DIAG=1 "$ARCOFISSION" compile-run "$TMP_ROOT/loops.abas" > /dev/null 2> "$TMP_ROOT/diag-off.txt"
if [ -s "$TMP_ROOT/diag-off.txt" ]; then
    echo "ARCOFISSION_NO_JIT did not actually suppress JIT dispatch" >&2
    cat "$TMP_ROOT/diag-off.txt" >&2
    exit 1
fi

# The same bytecode VM (and so the same JIT) is embedded in a .arcof-run capsule and in a fully
# native ELF capsule -- both must agree with the in-process compile-run result too.
"$ARCOFISSION" build "$TMP_ROOT/loops.abas" -o "$TMP_ROOT/loops.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/loops.arcof" > "$TMP_ROOT/bytecode-run.txt"
diff -u "$TMP_ROOT/jit-run.txt" "$TMP_ROOT/bytecode-run.txt"

"$ARCOFISSION" native "$TMP_ROOT/loops.abas" -o "$TMP_ROOT/loops-native" > /dev/null
"$TMP_ROOT/loops-native" > "$TMP_ROOT/native-run.txt"
diff -u "$TMP_ROOT/jit-run.txt" "$TMP_ROOT/native-run.txt"

# Leaf-call inlining: a loop body containing `dest = SimpleFn(arg)` is inlined directly into the
# generated native code (try_resolve_inlinable_leaf/try_inline_leaf_call) rather than making the
# whole loop fall back to interpretation just because it contains a call -- found necessary because
# RETURN's own expression compiles through generic LOAD/CONST/BINARY-on-temps instructions, a
# completely different shape than the BINARY_LOCAL_LOCAL/BINARY_LOCAL_CONST assignment-statement
# opcodes the rest of this file's detector already recognized.
cat > "$TMP_ROOT/inline.abas" <<'SCRIPT'
FUNCTION Add1(x)
    RETURN x + 1
END FUNCTION

' Self-referencing: the compiler emits TWO separate LOAD instructions for `x + x` (not one temp
' reused twice), a distinct shape from the constant-operand case above.
FUNCTION Double(x)
    RETURN x + x
END FUNCTION

FUNCTION SubTwo(x)
    RETURN x - 2
END FUNCTION

' A multi-op body (x / 2 - 1 needs two BINARY instructions with an intermediate temp) is NOT a
' leaf this JIT inlines -- the whole loop must safely fall back to interpretation instead of
' inlining only half the expression.
FUNCTION HalfMinusOne(x)
    RETURN x / 2 - 1
END FUNCTION

' A second parameter makes this ineligible too -- single-parameter leaves only, this pass.
FUNCTION TwoParamCallee(a, b)
    RETURN a + b
END FUNCTION

FUNCTION Recursive(x)
    IF x <= 0 THEN
        RETURN 0
    END IF
    RETURN x + Recursive(x - 1)
END FUNCTION

FUNCTION CallsHost(x)
    RETURN x + ABS(-1)
END FUNCTION

FUNCTION SumAdd1(n)
    total = 0
    FOR i = 1 TO n
        total = Add1(total)
    NEXT
    RETURN total
END FUNCTION

FUNCTION SumDouble(n)
    total = 1
    FOR i = 1 TO n
        total = Double(total)
    NEXT
    RETURN total
END FUNCTION

' Two different inlined calls, plus a plain BINARY_LOCAL_LOCAL statement, in the same body.
FUNCTION MixedInlineAndDirect(n)
    total = 0
    FOR i = 1 TO n
        total = Add1(total)
        total = total + i
        total = SubTwo(total)
    NEXT
    RETURN total
END FUNCTION

' The callee is declared AFTER this loop's own containing function -- prepare_bytecode_module
' processes functions in declaration order, so DeclaredLater's own param_local_indices/
' prepared_numeric_op aren't populated yet when this loop is examined. Must safely NOT inline
' (falls back to interpretation) rather than reading stale/incomplete callee data.
FUNCTION UsesForwardDeclared(n)
    total = 0
    FOR i = 1 TO n
        total = DeclaredLater(total)
    NEXT
    RETURN total
END FUNCTION

FUNCTION DeclaredLater(x)
    RETURN x + 5
END FUNCTION

FUNCTION UsesTwoParamCallee(n)
    total = 0
    FOR i = 1 TO n
        total = TwoParamCallee(total, 1)
    NEXT
    RETURN total
END FUNCTION

FUNCTION UsesHalfMinusOne(n)
    total = 100
    FOR i = 1 TO n
        total = HalfMinusOne(total)
    NEXT
    RETURN total
END FUNCTION

FUNCTION UsesRecursive(n)
    total = 0
    FOR i = 1 TO n
        total = Recursive(3)
    NEXT
    RETURN total
END FUNCTION

FUNCTION UsesCallsHost(n)
    total = 0
    FOR i = 1 TO n
        total = CallsHost(total)
    NEXT
    RETURN total
END FUNCTION

PRINT SumAdd1(10)
PRINT SumDouble(5)
PRINT MixedInlineAndDirect(5)
PRINT UsesForwardDeclared(10)
PRINT UsesTwoParamCallee(10)
PRINT UsesHalfMinusOne(3)
PRINT UsesRecursive(4)
PRINT UsesCallsHost(5)
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/inline.abas" > "$TMP_ROOT/inline-jit-run.txt"
grep -q "^10$" "$TMP_ROOT/inline-jit-run.txt"    # SumAdd1(10):        0 +1 ten times
grep -q "^32$" "$TMP_ROOT/inline-jit-run.txt"    # SumDouble(5):       1 doubled five times
grep -q "^10$" "$TMP_ROOT/inline-jit-run.txt"    # MixedInlineAndDirect(5) and UsesTwoParamCallee(10) both
grep -q "^50$" "$TMP_ROOT/inline-jit-run.txt"    # UsesForwardDeclared(10): 0 +5 ten times
test "$(wc -l < "$TMP_ROOT/inline-jit-run.txt")" = "8"

# Byte-identical against a fully-interpreted run is the real correctness proof here too --
# including for the five loops that must NOT inline (forward-declared/two-param/multi-op-body/
# recursive/host-call callees), where this also proves the safe-fallback path still gets the
# right answer, not just that it declines to inline.
ARCOFISSION_NO_JIT=1 "$ARCOFISSION" compile-run "$TMP_ROOT/inline.abas" > "$TMP_ROOT/inline-no-jit-run.txt"
diff -u "$TMP_ROOT/inline-jit-run.txt" "$TMP_ROOT/inline-no-jit-run.txt"

ARCOFISSION_JIT_DIAG=1 "$ARCOFISSION" compile-run "$TMP_ROOT/inline.abas" > /dev/null 2> "$TMP_ROOT/inline-diag.txt"
for fn in SumAdd1 SumDouble MixedInlineAndDirect; do
    grep -q "dispatching native loop in $fn " "$TMP_ROOT/inline-diag.txt"
done
# The five ineligible-callee loops must NOT dispatch -- confirms this is a real, working negative
# case (falling back to interpretation), not an accidental false positive elsewhere masking it.
for fn in UsesForwardDeclared UsesTwoParamCallee UsesHalfMinusOne UsesRecursive UsesCallsHost; do
    if grep -q "dispatching native loop in $fn " "$TMP_ROOT/inline-diag.txt"; then
        echo "JIT unexpectedly inlined a call it should have rejected: $fn" >&2
        exit 1
    fi
done

"$ARCOFISSION" build "$TMP_ROOT/inline.abas" -o "$TMP_ROOT/inline.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/inline.arcof" > "$TMP_ROOT/inline-bytecode-run.txt"
diff -u "$TMP_ROOT/inline-jit-run.txt" "$TMP_ROOT/inline-bytecode-run.txt"

"$ARCOFISSION" native "$TMP_ROOT/inline.abas" -o "$TMP_ROOT/inline-native" > /dev/null
"$TMP_ROOT/inline-native" > "$TMP_ROOT/inline-native-run.txt"
diff -u "$TMP_ROOT/inline-jit-run.txt" "$TMP_ROOT/inline-native-run.txt"
