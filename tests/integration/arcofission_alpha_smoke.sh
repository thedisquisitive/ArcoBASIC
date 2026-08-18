#!/usr/bin/env bash
set -euo pipefail

ARCOFISSION="$1"
SOURCE_DIR="$2"

TMP_ROOT="${TMPDIR:-/tmp}/arcofission-alpha-smoke-$$"
mkdir -p "$TMP_ROOT"
trap 'rm -rf "$TMP_ROOT"' EXIT

cat > "$TMP_ROOT/hello.abas" <<'SCRIPT'
PRINT "hello native future"
LET count = 2 + 3
score = count * 10
PRINT LEN("abc")
LET items = [1, count, LEN("xy")]
PRINT items[1]
items[0] = score
LET obj = {name: "Ada", score: items[0]}
score += LEN("z")
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/hello.abas" at A-MIR > "$TMP_ROOT/amir.txt"
grep -q "SOURCE ACCEPTED" "$TMP_ROOT/amir.txt"
grep -q "STRUCTURE ASSEMBLED" "$TMP_ROOT/amir.txt"
grep -q "A-MIR GENERATED" "$TMP_ROOT/amir.txt"
grep -q "FUNCTION Main RETURNS I32" "$TMP_ROOT/amir.txt"
grep -q "%t0 := CONST \"hello native future\"" "$TMP_ROOT/amir.txt"
grep -q "CALL Runtime.Print" "$TMP_ROOT/amir.txt"
grep -q "%t3 := + %t1, %t2" "$TMP_ROOT/amir.txt"
grep -q "STORE count" "$TMP_ROOT/amir.txt"
grep -q "%t4 := LOAD count" "$TMP_ROOT/amir.txt"
grep -q "%t6 := \\* %t4, %t5" "$TMP_ROOT/amir.txt"
grep -q "STORE score" "$TMP_ROOT/amir.txt"
grep -q "%t7 := CONST \"abc\"" "$TMP_ROOT/amir.txt"
grep -q "%t8 := CALL LEN %t7" "$TMP_ROOT/amir.txt"
grep -q "CALL Runtime.Print %t8" "$TMP_ROOT/amir.txt"
grep -q " := ARRAY" "$TMP_ROOT/amir.txt"
grep -q " := INDEX" "$TMP_ROOT/amir.txt"
grep -q "STORE_INDEX items" "$TMP_ROOT/amir.txt"
grep -q " := OBJECT name:" "$TMP_ROOT/amir.txt"
grep -q "STORE score" "$TMP_ROOT/amir.txt"

cat > "$TMP_ROOT/numbered.abas" <<'SCRIPT'
10 PRINT "start"
20 GOTO 40
30 PRINT "skip"
40 STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/numbered.abas" at A-MIR > "$TMP_ROOT/numbered-amir.txt"
grep -q "L10:" "$TMP_ROOT/numbered-amir.txt"
grep -q "L20:" "$TMP_ROOT/numbered-amir.txt"
grep -q "JUMP L40" "$TMP_ROOT/numbered-amir.txt"
grep -q "L40:" "$TMP_ROOT/numbered-amir.txt"

cat > "$TMP_ROOT/structured.abas" <<'SCRIPT'
LET x = 1
IF x > 0 THEN
PRINT "yes"
ELSE
PRINT "no"
END IF
WHILE x < 3
x += 1
WEND
DO
x += 1
LOOP UNTIL x > 4
FUNCTION Sum(a, b) AS Number
RETURN a + b
END FUNCTION
TRY
PRINT LEN("abc")
CATCH err
PRINT err
END TRY
CLASS Person
Name = ""
END CLASS
INTERFACE Thing
FUNCTION Name() AS String
END INTERFACE
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/structured.abas" at A-MIR > "$TMP_ROOT/structured-amir.txt"
grep -q "BRANCH" "$TMP_ROOT/structured-amir.txt"
grep -q "BLOCK IfThen" "$TMP_ROOT/structured-amir.txt"
grep -q "BLOCK WhileCond" "$TMP_ROOT/structured-amir.txt"
grep -q "BLOCK DoBody" "$TMP_ROOT/structured-amir.txt"
grep -q "DECLARE_FUNCTION Sum" "$TMP_ROOT/structured-amir.txt"
grep -q "FUNCTION Sum(a, b) RETURNS Number" "$TMP_ROOT/structured-amir.txt"
grep -q "TRY_BEGIN" "$TMP_ROOT/structured-amir.txt"
grep -q "DECLARE_CLASS Person" "$TMP_ROOT/structured-amir.txt"
grep -q "DECLARE_INTERFACE Thing" "$TMP_ROOT/structured-amir.txt"
if grep -q "unterminated block" "$TMP_ROOT/structured-amir.txt"; then
    echo "ArcoFission emitted an unterminated block diagnostic for structured smoke" >&2
    exit 1
fi

"$ARCOFISSION" reveal "$TMP_ROOT/structured.abas" at BYTECODE > "$TMP_ROOT/structured-bytecode.txt"
grep -q "BYTECODE PREPARED" "$TMP_ROOT/structured-bytecode.txt"
grep -q "ARCOFISSION BYTECODE" "$TMP_ROOT/structured-bytecode.txt"
grep -q "FORMAT .arcof-text" "$TMP_ROOT/structured-bytecode.txt"
grep -q "OPCODES" "$TMP_ROOT/structured-bytecode.txt"
grep -q "0 LABEL" "$TMP_ROOT/structured-bytecode.txt"
grep -q "14 BRANCH" "$TMP_ROOT/structured-bytecode.txt"
grep -q "CONSTANTS" "$TMP_ROOT/structured-bytecode.txt"
grep -q "LOCALS" "$TMP_ROOT/structured-bytecode.txt"
grep -q "CALL_VALUE" "$TMP_ROOT/structured-bytecode.txt"
grep -q "CALL_RUNTIME" "$TMP_ROOT/structured-bytecode.txt"

"$ARCOFISSION" build "$TMP_ROOT/structured.abas" -o "$TMP_ROOT/structured.arcof" > "$TMP_ROOT/build.txt"
grep -q "BYTECODE WRITTEN" "$TMP_ROOT/build.txt"
grep -q "ARCOFISSION BYTECODE" "$TMP_ROOT/structured.arcof"
grep -q "END FUNCTION" "$TMP_ROOT/structured.arcof"
"$ARCOFISSION" bytecode "$TMP_ROOT/structured.abas" -o "$TMP_ROOT/structured-explicit.arcof" > "$TMP_ROOT/bytecode-build.txt"
grep -q "BYTECODE WRITTEN" "$TMP_ROOT/bytecode-build.txt"
grep -q "ARCOFISSION BYTECODE" "$TMP_ROOT/structured-explicit.arcof"

cat > "$TMP_ROOT/vm.abas" <<'SCRIPT'
#INSTRUCTION_LIMIT 1000
PRINT "vm hello"
LET x = 2 + 3 * 4
PRINT x
PRINT LEN("abc")
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/vm.abas" > "$TMP_ROOT/vm-compile-run.txt"
grep -q "vm hello" "$TMP_ROOT/vm-compile-run.txt"
grep -q "^14$" "$TMP_ROOT/vm-compile-run.txt"
grep -q "^3$" "$TMP_ROOT/vm-compile-run.txt"

"$ARCOFISSION" build "$TMP_ROOT/vm.abas" -o "$TMP_ROOT/vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/vm.arcof" > "$TMP_ROOT/vm-run.txt"
diff -u "$TMP_ROOT/vm-compile-run.txt" "$TMP_ROOT/vm-run.txt"

"$ARCOFISSION" build "$TMP_ROOT/vm.abas" -o "$TMP_ROOT/vm-native" > "$TMP_ROOT/vm-native-build.txt"
grep -q "ELF64 WRITTEN" "$TMP_ROOT/vm-native-build.txt"
test -x "$TMP_ROOT/vm-native"
test "$(head -c 5 "$TMP_ROOT/vm-native" | od -An -tx1 | tr -d ' \n')" = "7f454c4602"
"$TMP_ROOT/vm-native" > "$TMP_ROOT/vm-native-run.txt"
diff -u "$TMP_ROOT/vm-compile-run.txt" "$TMP_ROOT/vm-native-run.txt"
"$ARCOFISSION" native "$TMP_ROOT/vm.abas" -o "$TMP_ROOT/vm-native-explicit" > "$TMP_ROOT/vm-native-explicit-build.txt"
grep -q "ELF64 WRITTEN" "$TMP_ROOT/vm-native-explicit-build.txt"

cat > "$TMP_ROOT/flow.abas" <<'SCRIPT'
LET x = 0
IF x == 0 THEN
PRINT "then"
ELSE
PRINT "else"
END IF
WHILE x < 3
x += 1
PRINT x
WEND
DO
x += 1
LOOP UNTIL x > 4
PRINT x
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/flow.abas" > "$TMP_ROOT/flow-compile-run.txt"
grep -q "^then$" "$TMP_ROOT/flow-compile-run.txt"
grep -q "^1$" "$TMP_ROOT/flow-compile-run.txt"
grep -q "^2$" "$TMP_ROOT/flow-compile-run.txt"
grep -q "^3$" "$TMP_ROOT/flow-compile-run.txt"
grep -q "^5$" "$TMP_ROOT/flow-compile-run.txt"
if grep -q "^else$" "$TMP_ROOT/flow-compile-run.txt"; then
    echo "ArcoFission VM executed the wrong IF branch" >&2
    exit 1
fi

"$ARCOFISSION" build "$TMP_ROOT/flow.abas" -o "$TMP_ROOT/flow.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/flow.arcof" > "$TMP_ROOT/flow-run.txt"
diff -u "$TMP_ROOT/flow-compile-run.txt" "$TMP_ROOT/flow-run.txt"

cat > "$TMP_ROOT/goto-vm.abas" <<'SCRIPT'
10 PRINT "a"
20 GOTO 40
30 PRINT "bad"
40 PRINT "b"
50 STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/goto-vm.abas" > "$TMP_ROOT/goto-vm.txt"
grep -q "^a$" "$TMP_ROOT/goto-vm.txt"
grep -q "^b$" "$TMP_ROOT/goto-vm.txt"
if grep -q "^bad$" "$TMP_ROOT/goto-vm.txt"; then
    echo "ArcoFission VM ignored GOTO" >&2
    exit 1
fi

cat > "$TMP_ROOT/composites-vm.abas" <<'SCRIPT'
LET items = [1, 2 + 3, LEN("abc")]
PRINT items[1]
items[0] = 99
PRINT items[0]
LET obj = {name: "Ada", score: items[2]}
PRINT obj
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/composites-vm.abas" > "$TMP_ROOT/composites-vm.txt"
grep -q "^5$" "$TMP_ROOT/composites-vm.txt"
grep -q "^99$" "$TMP_ROOT/composites-vm.txt"
grep -q "{name: Ada, score: 3}" "$TMP_ROOT/composites-vm.txt"

cat > "$TMP_ROOT/functions-vm.abas" <<'SCRIPT'
FUNCTION Sum(a, b) AS Number
RETURN a + b
END FUNCTION
FUNCTION Twice(n) AS Number
RETURN Sum(n, n)
END FUNCTION
PRINT Sum(2, 5)
PRINT Twice(9)
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/functions-vm.abas" > "$TMP_ROOT/functions-compile-run.txt"
grep -q "^7$" "$TMP_ROOT/functions-compile-run.txt"
grep -q "^18$" "$TMP_ROOT/functions-compile-run.txt"
"$ARCOFISSION" build "$TMP_ROOT/functions-vm.abas" -o "$TMP_ROOT/functions-vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/functions-vm.arcof" > "$TMP_ROOT/functions-run.txt"
diff -u "$TMP_ROOT/functions-compile-run.txt" "$TMP_ROOT/functions-run.txt"

cat > "$TMP_ROOT/instruction-limit.abas" <<'SCRIPT'
#INSTRUCTION_LIMIT 1000
LET total = 0
FOR i = 1 TO 20
total += i
NEXT
PRINT total
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/instruction-limit.abas" at A-MIR > "$TMP_ROOT/instruction-limit-amir.txt"
grep -q "METADATA INSTRUCTION_LIMIT 1000" "$TMP_ROOT/instruction-limit-amir.txt"
"$ARCOFISSION" reveal "$TMP_ROOT/instruction-limit.abas" at BYTECODE > "$TMP_ROOT/instruction-limit-bytecode.txt"
grep -q "METADATA INSTRUCTION_LIMIT 1000" "$TMP_ROOT/instruction-limit-bytecode.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/instruction-limit.abas" > "$TMP_ROOT/instruction-limit-run.txt"
grep -q "^210$" "$TMP_ROOT/instruction-limit-run.txt"
if "$ARCOFISSION" compile-run "$TMP_ROOT/instruction-limit.abas" --instruction-limit 1 > /dev/null 2>&1; then
    echo "ArcoFission ignored the operator instruction-limit override" >&2
    exit 1
fi
"$ARCOFISSION" build "$TMP_ROOT/instruction-limit.abas" -o "$TMP_ROOT/instruction-limit.arcof" > /dev/null
grep -q "METADATA INSTRUCTION_LIMIT 1000" "$TMP_ROOT/instruction-limit.arcof"
"$ARCOFISSION" run "$TMP_ROOT/instruction-limit.arcof" > "$TMP_ROOT/instruction-limit-bytecode-run.txt"
diff -u "$TMP_ROOT/instruction-limit-run.txt" "$TMP_ROOT/instruction-limit-bytecode-run.txt"

cat > "$TMP_ROOT/slices-vm.abas" <<'SCRIPT'
values = [0, 1, 2, 3, 4]
PRINT values[1:4]
PRINT values[::-1]
PRINT "Aé猫Z"[1:3]
copied = COPY values
copied[0] = 9
PRINT values[0]
PRINT copied[0]
values[1:4] = [8, 9]
PRINT values
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/slices-vm.abas" at A-MIR > "$TMP_ROOT/slices-amir.txt"
grep -q " := SLICE " "$TMP_ROOT/slices-amir.txt"
grep -q " := COPY " "$TMP_ROOT/slices-amir.txt"
grep -q "STORE_SLICE values" "$TMP_ROOT/slices-amir.txt"
"$ARCOFISSION" reveal "$TMP_ROOT/slices-vm.abas" at BYTECODE > "$TMP_ROOT/slices-bytecode.txt"
grep -q "24 SLICE" "$TMP_ROOT/slices-bytecode.txt"
grep -q "25 COPY" "$TMP_ROOT/slices-bytecode.txt"
grep -q "26 STORE_SLICE" "$TMP_ROOT/slices-bytecode.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/slices-vm.abas" > "$TMP_ROOT/slices-compile-run.txt"
grep -q '^\[1, 2, 3\]$' "$TMP_ROOT/slices-compile-run.txt"
grep -q '^\[4, 3, 2, 1, 0\]$' "$TMP_ROOT/slices-compile-run.txt"
grep -q '^é猫$' "$TMP_ROOT/slices-compile-run.txt"
grep -q '^\[0, 8, 9, 4\]$' "$TMP_ROOT/slices-compile-run.txt"
"$ARCOFISSION" build "$TMP_ROOT/slices-vm.abas" -o "$TMP_ROOT/slices-vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/slices-vm.arcof" > "$TMP_ROOT/slices-bytecode-run.txt"
diff -u "$TMP_ROOT/slices-compile-run.txt" "$TMP_ROOT/slices-bytecode-run.txt"
"$ARCOFISSION" native "$TMP_ROOT/slices-vm.abas" -o "$TMP_ROOT/slices-native" > /dev/null
"$TMP_ROOT/slices-native" > "$TMP_ROOT/slices-native-run.txt"
diff -u "$TMP_ROOT/slices-compile-run.txt" "$TMP_ROOT/slices-native-run.txt"

cat > "$TMP_ROOT/tuples-vm.abas" <<'SCRIPT'
FUNCTION Pair(a, b) AS TUPLE
RETURN (a, b)
END FUNCTION
LET (left, right) = Pair(3, 4)
PRINT (left, right)
(left, right) = (right, left)
PRINT left
PRINT right
PRINT Pair(1, 2)[::-1]
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/tuples-vm.abas" at A-MIR > "$TMP_ROOT/tuples-amir.txt"
grep -q " := TUPLE" "$TMP_ROOT/tuples-amir.txt"
grep -q "DESTRUCTURE" "$TMP_ROOT/tuples-amir.txt"
"$ARCOFISSION" reveal "$TMP_ROOT/tuples-vm.abas" at BYTECODE > "$TMP_ROOT/tuples-bytecode.txt"
grep -q "27 TUPLE" "$TMP_ROOT/tuples-bytecode.txt"
grep -q "28 DESTRUCTURE" "$TMP_ROOT/tuples-bytecode.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/tuples-vm.abas" > "$TMP_ROOT/tuples-compile-run.txt"
grep -q '^(3, 4)$' "$TMP_ROOT/tuples-compile-run.txt"
grep -q '^(2, 1)$' "$TMP_ROOT/tuples-compile-run.txt"
"$ARCOFISSION" build "$TMP_ROOT/tuples-vm.abas" -o "$TMP_ROOT/tuples-vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/tuples-vm.arcof" > "$TMP_ROOT/tuples-bytecode-run.txt"
diff -u "$TMP_ROOT/tuples-compile-run.txt" "$TMP_ROOT/tuples-bytecode-run.txt"
"$ARCOFISSION" native "$TMP_ROOT/tuples-vm.abas" -o "$TMP_ROOT/tuples-native" > /dev/null
"$TMP_ROOT/tuples-native" > "$TMP_ROOT/tuples-native-run.txt"
diff -u "$TMP_ROOT/tuples-compile-run.txt" "$TMP_ROOT/tuples-native-run.txt"

cat > "$TMP_ROOT/callables-vm.abas" <<'SCRIPT'
FUNCTION Score(item)
RETURN item.Score
END FUNCTION
FUNCTION Invoke(fn AS CALLABLE, value)
RETURN fn(value)
END FUNCTION
FUNCTION Track(value)
RETURN value
END FUNCTION
items = [{Name: "b", Score: 2}, {Name: "a", Score: 1}, {Name: "c", Score: 2}]
key = ADDRESSOF Score
PRINT TYPEOF(key)
PRINT key({Score: 5})
PRINT Invoke(key, {Score: 7})
ranked = Array.SortBy(items, key)
first = ranked[0]
second = ranked[1]
third = ranked[2]
PRINT first.Name
PRINT second.Name
PRINT third.Name
descending = Array.SortBy(items, key, TRUE)
descFirst = descending[0]
PRINT descFirst.Name
minItem = Array.MinBy(items, key)
maxItem = Array.MaxBy(items, key)
PRINT minItem.Name
PRINT maxItem.Name
Array.SortBy([3, 1, 2], ADDRESSOF Track)
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/callables-vm.abas" at A-MIR > "$TMP_ROOT/callables-amir.txt"
grep -q " := ADDRESSOF Score" "$TMP_ROOT/callables-amir.txt"
grep -q " := ADDRESSOF Track" "$TMP_ROOT/callables-amir.txt"
"$ARCOFISSION" reveal "$TMP_ROOT/callables-vm.abas" at BYTECODE > "$TMP_ROOT/callables-bytecode.txt"
grep -q "29 ADDRESSOF" "$TMP_ROOT/callables-bytecode.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/callables-vm.abas" > "$TMP_ROOT/callables-compile-run.txt"
grep -q '^Callable$' "$TMP_ROOT/callables-compile-run.txt"
grep -q '^5$' "$TMP_ROOT/callables-compile-run.txt"
grep -q '^7$' "$TMP_ROOT/callables-compile-run.txt"
grep -q '^a$' "$TMP_ROOT/callables-compile-run.txt"
grep -q '^b$' "$TMP_ROOT/callables-compile-run.txt"
"$ARCOFISSION" build "$TMP_ROOT/callables-vm.abas" -o "$TMP_ROOT/callables-vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/callables-vm.arcof" > "$TMP_ROOT/callables-bytecode-run.txt"
diff -u "$TMP_ROOT/callables-compile-run.txt" "$TMP_ROOT/callables-bytecode-run.txt"
"$ARCOFISSION" native "$TMP_ROOT/callables-vm.abas" -o "$TMP_ROOT/callables-native" > /dev/null
"$TMP_ROOT/callables-native" > "$TMP_ROOT/callables-native-run.txt"
diff -u "$TMP_ROOT/callables-compile-run.txt" "$TMP_ROOT/callables-native-run.txt"

cat > "$TMP_ROOT/ranges-vm.abas" <<'SCRIPT'
squares = [i * i FOR i IN Range(1, 5)]
PRINT squares
evens = [j FOR j IN Range(7) IF j % 2 == 0]
PRINT evens
TRY
PRINT j
CATCH err
PRINT err.Type
END TRY
total = 0
FOR i IN Range(5)
total += i
NEXT
PRINT total
PRINT TYPEOF(Range(3))
PRINT LEN(Range(5, 0, -2))
PRINT Range(5, 0, -2)[1]
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/ranges-vm.abas" at AST > "$TMP_ROOT/ranges-ast.txt"
grep -q "ArrayComprehension i" "$TMP_ROOT/ranges-ast.txt"
"$ARCOFISSION" reveal "$TMP_ROOT/ranges-vm.abas" at A-MIR > "$TMP_ROOT/ranges-amir.txt"
grep -q "BLOCK ComprehensionCond" "$TMP_ROOT/ranges-amir.txt"
grep -q "CALL Array.Add" "$TMP_ROOT/ranges-amir.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/ranges-vm.abas" > "$TMP_ROOT/ranges-compile-run.txt"
grep -q '^\[1, 4, 9, 16\]$' "$TMP_ROOT/ranges-compile-run.txt"
grep -q '^\[0, 2, 4, 6\]$' "$TMP_ROOT/ranges-compile-run.txt"
grep -q '^10$' "$TMP_ROOT/ranges-compile-run.txt"
grep -q '^Range$' "$TMP_ROOT/ranges-compile-run.txt"
grep -q '^RuntimeError$' "$TMP_ROOT/ranges-compile-run.txt"
"$ARCOFISSION" build "$TMP_ROOT/ranges-vm.abas" -o "$TMP_ROOT/ranges-vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/ranges-vm.arcof" > "$TMP_ROOT/ranges-bytecode-run.txt"
diff -u "$TMP_ROOT/ranges-compile-run.txt" "$TMP_ROOT/ranges-bytecode-run.txt"
"$ARCOFISSION" native "$TMP_ROOT/ranges-vm.abas" -o "$TMP_ROOT/ranges-native" > /dev/null
"$TMP_ROOT/ranges-native" > "$TMP_ROOT/ranges-native-run.txt"
diff -u "$TMP_ROOT/ranges-compile-run.txt" "$TMP_ROOT/ranges-native-run.txt"

cat > "$TMP_ROOT/bits-vm.abas" <<'SCRIPT'
LET genome AS BITVECTOR = BITS "0001_1011"
PRINT LEN(genome)
PRINT genome[3]
PRINT Bits.Count(genome)
PRINT Bits.ToString(Bits.Flip(genome, 0))
PRINT Bits.ToString(genome + BITS "01")
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/bits-vm.abas" at A-MIR > "$TMP_ROOT/bits-amir.txt"
grep -q 'CONST BITS "00011011"' "$TMP_ROOT/bits-amir.txt"
"$ARCOFISSION" reveal "$TMP_ROOT/bits-vm.abas" at BYTECODE > "$TMP_ROOT/bits-bytecode.txt"
grep -q 'BITS "00011011"' "$TMP_ROOT/bits-bytecode.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/bits-vm.abas" > "$TMP_ROOT/bits-compile-run.txt"
grep -q '^8$' "$TMP_ROOT/bits-compile-run.txt"
grep -q '^10011011$' "$TMP_ROOT/bits-compile-run.txt"
grep -q '^0001101101$' "$TMP_ROOT/bits-compile-run.txt"
"$ARCOFISSION" build "$TMP_ROOT/bits-vm.abas" -o "$TMP_ROOT/bits-vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/bits-vm.arcof" > "$TMP_ROOT/bits-bytecode-run.txt"
diff -u "$TMP_ROOT/bits-compile-run.txt" "$TMP_ROOT/bits-bytecode-run.txt"
"$ARCOFISSION" native "$TMP_ROOT/bits-vm.abas" -o "$TMP_ROOT/bits-native" > /dev/null
"$TMP_ROOT/bits-native" > "$TMP_ROOT/bits-native-run.txt"
diff -u "$TMP_ROOT/bits-compile-run.txt" "$TMP_ROOT/bits-native-run.txt"

cat > "$TMP_ROOT/throw-vm.abas" <<'SCRIPT'
FUNCTION Validate(value)
IF value < 0 THEN
THROW "value must be non-negative"
END IF
RETURN value
END FUNCTION
TRY
PRINT Validate(-1)
CATCH err
PRINT err.Type
PRINT err.Message
END TRY
TRY
PRINT missing_runtime_value
CATCH err
PRINT err.Type
END TRY
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/throw-vm.abas" at A-MIR > "$TMP_ROOT/throw-amir.txt"
grep -q "THROW" "$TMP_ROOT/throw-amir.txt"
if grep -q "unsupported lowering" "$TMP_ROOT/throw-amir.txt"; then
    echo "ArcoFission reported THROW as unsupported hosted lowering" >&2
    exit 1
fi
"$ARCOFISSION" reveal "$TMP_ROOT/throw-vm.abas" at BYTECODE > "$TMP_ROOT/throw-bytecode.txt"
grep -q "23 THROW" "$TMP_ROOT/throw-bytecode.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/throw-vm.abas" > "$TMP_ROOT/throw-compile-run.txt"
grep -q "^UserError$" "$TMP_ROOT/throw-compile-run.txt"
grep -q "^value must be non-negative$" "$TMP_ROOT/throw-compile-run.txt"
grep -q "^RuntimeError$" "$TMP_ROOT/throw-compile-run.txt"
"$ARCOFISSION" bytecode "$TMP_ROOT/throw-vm.abas" -o "$TMP_ROOT/throw-vm.arcof" > /dev/null
"$ARCOFISSION" run "$TMP_ROOT/throw-vm.arcof" > "$TMP_ROOT/throw-run.txt"
diff -u "$TMP_ROOT/throw-compile-run.txt" "$TMP_ROOT/throw-run.txt"
"$ARCOFISSION" build "$TMP_ROOT/throw-vm.abas" -o "$TMP_ROOT/throw-native" > /dev/null
"$TMP_ROOT/throw-native" > "$TMP_ROOT/throw-native.txt"
diff -u "$TMP_ROOT/throw-compile-run.txt" "$TMP_ROOT/throw-native.txt"

cat > "$TMP_ROOT/math-vm.abas" <<'SCRIPT'
PRINT ABS(SIN(PI())) < 0.000001
PRINT COS(0)
PRINT TAN(0)
PRINT ATAN2(1, 0) > 1.57
PRINT SQRT(81)
PRINT FLOOR(3.9)
PRINT CEIL(3.1)
PRINT ROUND(3.5)
PRINT ABS(-12)
PRINT MIN(9, 3, 5)
PRINT MAX(9, 3, 5)
PRINT CLAMP(12, 0, 10)
PRINT LERP(10, 20, 0.25)
PRINT Math.Pow(2, 8)
PRINT Object.Get(Math.Constants(), "TAU") > 6.28
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/math-vm.abas" at A-MIR > "$TMP_ROOT/math-amir.txt"
grep -q "CALL SIN" "$TMP_ROOT/math-amir.txt"
grep -q "CALL Math.Pow" "$TMP_ROOT/math-amir.txt"
grep -q "CALL Math.Constants" "$TMP_ROOT/math-amir.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/math-vm.abas" > "$TMP_ROOT/math-vm.txt"
grep -q "^TRUE$" "$TMP_ROOT/math-vm.txt"
grep -q "^256$" "$TMP_ROOT/math-vm.txt"
grep -q "^12.5$" "$TMP_ROOT/math-vm.txt"

cat > "$TMP_ROOT/network-core-vm.abas" <<'SCRIPT'
PRINT TYPEOF(Network.Available())
PRINT Network.UrlEncode("hello world!")
PRINT Network.UrlDecode("hello%20world%21")
PRINT Network.QueryString({"q": "arco basic", "page": 2})
dns = Net.ResolveDNS("localhost")
PRINT dns.Host
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/network-core-vm.abas" > "$TMP_ROOT/network-core-vm.txt"
grep -q "^Boolean$" "$TMP_ROOT/network-core-vm.txt"
grep -q "^hello%20world%21$" "$TMP_ROOT/network-core-vm.txt"
grep -q "^hello world!$" "$TMP_ROOT/network-core-vm.txt"
grep -q "^page=2&q=arco%20basic$" "$TMP_ROOT/network-core-vm.txt"
grep -q "^localhost$" "$TMP_ROOT/network-core-vm.txt"

cat > "$TMP_ROOT/text-doc-vm.abas" <<SCRIPT
path = "$TMP_ROOT/doc.txt"
File.WriteText(path, "alpha")
File.AppendText(path, " beta")
PRINT File.Exists(path)
PRINT File.ReadText(path)
bytes = Bytes.New(3, 65)
bytes = Bytes.SetU8(bytes, 1, 66)
PRINT Bytes.GetU8(bytes, 1)
PRINT Bytes.ToText(bytes)
File.WriteBytes("$TMP_ROOT/bytes.bin", bytes)
loaded = File.ReadBytes("$TMP_ROOT/bytes.bin")
PRINT Bytes.Length(loaded)
PRINT String.Insert("abcd", 2, "XX")
PRINT String.Delete("abcd", 1, 2)
PRINT String.Join(["a", "b", "c"], "|")
doc = Document.New("hello")
doc = Document.InsertText(doc, 5, " world")
PRINT Document.Text(doc)
doc = Document.DeleteRange(doc, 5, 1)
PRINT Document.LineAt(doc, 0)
doc = Document.ReplaceRange(doc, 5, 5, " there")
PRINT Document.Text(doc)
pos = Document.LineColumnAt(Document.New("a\nbc"), 3)
PRINT pos.Line
PRINT pos.Column
PRINT Document.OffsetAtLineColumn(Document.New("a\nbc"), 1, 1)
doc = Document.ApplyFormat(doc, 0, 5, {"Bold": TRUE, "FontSize": 22})
PRINT LEN(Document.Runs(doc))
packed = Document.Serialize(doc)
round = Document.Parse(packed)
PRINT Document.Text(round)
PRINT Object.Get(Document.Runs(round)[0], "Bold")
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/text-doc-vm.abas" at A-MIR > "$TMP_ROOT/text-doc-amir.txt"
grep -q "CALL File.WriteText" "$TMP_ROOT/text-doc-amir.txt"
grep -q "CALL Bytes.SetU8" "$TMP_ROOT/text-doc-amir.txt"
grep -q "CALL Document.InsertText" "$TMP_ROOT/text-doc-amir.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/text-doc-vm.abas" > "$TMP_ROOT/text-doc-vm.txt"
grep -q "^TRUE$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^alpha beta$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^66$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^ABA$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^3$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^abXXcd$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^ad$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^a|b|c$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^hello world$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^helloworld$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^hello there$" "$TMP_ROOT/text-doc-vm.txt"
grep -q "^1$" "$TMP_ROOT/text-doc-vm.txt"

cat > "$TMP_ROOT/multiline-vm.abas" <<'SCRIPT'
person = {
    "Name": "Ada",
    "Scores": [
        2,
        3,
    ],
}
PRINT person.Name
PRINT person.Scores[1]
PRINT MAX(
    2,
    9,
    4,
)
STOP
SCRIPT

"$ARCOFISSION" compile-run "$TMP_ROOT/multiline-vm.abas" > "$TMP_ROOT/multiline-vm.txt"
grep -q "^Ada$" "$TMP_ROOT/multiline-vm.txt"
grep -q "^3$" "$TMP_ROOT/multiline-vm.txt"
grep -q "^9$" "$TMP_ROOT/multiline-vm.txt"

cat > "$TMP_ROOT/gui-vm.abas" <<'SCRIPT'
PRINT TYPEOF(GUI.Available())
PRINT TYPEOF(GUI.Backend())
IF GUI.Available() THEN
GUI.Application("arcofission-smoke", "ArcoFission Smoke")
window = GUI.Window("ArcoFission Smoke", 160, 100)
size = GUI.Size(window)
PRINT size.Width > 0
GUI.Clear(window, 0.05, 0.06, 0.08)
GUI.Pixel(window, 3, 3, 1.0, 1.0, 1.0)
GUI.FillRect(window, 4, 4, 20, 12, 0.1, 0.2, 0.3)
GUI.Column(window, 30, 2, 80, 0.9, 0.2, 0.1)
GUI.Rectangle(window, 10, 10, 60, 30, 0.3, 0.7, 1.0)
GUI.Text(window, "A", 82, 16, 18, 1.0, 1.0, 1.0)
GUI.Present(window)
event = GUI.PollEvent()
PRINT TYPEOF(event.Type)
PRINT TYPEOF(GUI.KeyDown(window, "escape"))
pointer = GUI.PointerPosition(window)
PRINT TYPEOF(pointer.X)
GUI.Close(window)
ELSE
PRINT "headless"
END IF
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/gui-vm.abas" at A-MIR > "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.Available" "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.Pixel" "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.FillRect" "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.Column" "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.Window" "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.KeyDown" "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.PointerPosition" "$TMP_ROOT/gui-amir.txt"
grep -q "CALL GUI.Present" "$TMP_ROOT/gui-amir.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/gui-vm.abas" > "$TMP_ROOT/gui-vm.txt"
grep -q "^Boolean$" "$TMP_ROOT/gui-vm.txt"
grep -q "^String$" "$TMP_ROOT/gui-vm.txt"
if grep -q "^headless$" "$TMP_ROOT/gui-vm.txt"; then
    true
else
    grep -q "^TRUE$" "$TMP_ROOT/gui-vm.txt"
fi

cat > "$TMP_ROOT/fission-more-flow.abas" <<'SCRIPT'
LET total = 0
FOR i = 1 TO 5
IF i == 2 THEN
CONTINUE FOR
END IF
IF i == 5 THEN
EXIT FOR
END IF
total += i
NEXT
PRINT total
FOR down = 3 TO 1 STEP -1
PRINT down
NEXT
LET names = ["Ada", "Grace", "Linus"]
FOR name IN names
PRINT name
NEXT
LET person = {Name: "Ada", Score: 1}
PRINT person.Name
person.Score = person.Score + 6
PRINT person.Score
LET x = 2
SELECT CASE x
CASE 1
PRINT "one"
CASE 2, 3 TO 4
PRINT "twoish"
CASE ELSE
PRINT "other"
END SELECT
TRY
PRINT names[99]
CATCH err
PRINT err.Message CONTAINS "range"
END TRY
CLASS Counter
SHARED FUNCTION Sum(a, b) AS Number
RETURN a + b
END FUNCTION
END CLASS
PRINT Counter.Sum(4, 6)
STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/fission-more-flow.abas" at A-MIR > "$TMP_ROOT/fission-more-flow-amir.txt"
grep -q "BLOCK ForCond" "$TMP_ROOT/fission-more-flow-amir.txt"
grep -q "BLOCK ForEachCond" "$TMP_ROOT/fission-more-flow-amir.txt"
grep -q "BLOCK SelectCase" "$TMP_ROOT/fission-more-flow-amir.txt"
grep -q "FUNCTION Counter.Sum(a, b) RETURNS Number" "$TMP_ROOT/fission-more-flow-amir.txt"
"$ARCOFISSION" compile-run "$TMP_ROOT/fission-more-flow.abas" > "$TMP_ROOT/fission-more-flow.txt"
grep -q "^8$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^3$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^2$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^1$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^Ada$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^Grace$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^Linus$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^7$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^twoish$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^TRUE$" "$TMP_ROOT/fission-more-flow.txt"
grep -q "^10$" "$TMP_ROOT/fission-more-flow.txt"

cat > "$TMP_ROOT/bad-goto.abas" <<'SCRIPT'
10 GOTO 99
20 STOP
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/bad-goto.abas" at A-MIR > "$TMP_ROOT/bad-goto-amir.txt"
grep -q "DIAGNOSTICS 1" "$TMP_ROOT/bad-goto-amir.txt"
grep -q "unresolved A-MIR target L99" "$TMP_ROOT/bad-goto-amir.txt"

cat > "$TMP_ROOT/unsupported-lowering.abas" <<'SCRIPT'
RUN "echo hi"
SCRIPT

"$ARCOFISSION" reveal "$TMP_ROOT/unsupported-lowering.abas" at A-MIR > "$TMP_ROOT/unsupported-amir.txt"
grep -q "unsupported lowering" "$TMP_ROOT/unsupported-amir.txt"

"$ARCOFISSION" reveal "$TMP_ROOT/hello.abas" at AST > "$TMP_ROOT/ast.txt"
grep -q "SOURCE ACCEPTED" "$TMP_ROOT/ast.txt"
grep -q "STRUCTURE ASSEMBLED" "$TMP_ROOT/ast.txt"
grep -q "AST MODULE" "$TMP_ROOT/ast.txt"
grep -q "Program" "$TMP_ROOT/ast.txt"
grep -q "Print" "$TMP_ROOT/ast.txt"
grep -q "Assign count" "$TMP_ROOT/ast.txt"
grep -q "Binary +" "$TMP_ROOT/ast.txt"
grep -q "Stop" "$TMP_ROOT/ast.txt"

if "$ARCOFISSION" reveal "$SOURCE_DIR/README.md" at A-MIR > "$TMP_ROOT/bad.txt" 2>&1; then
    echo "ArcoFission accepted non-ArcoBASIC source unexpectedly" >&2
    exit 1
fi
grep -q "SOURCE INTAKE FAILED" "$TMP_ROOT/bad.txt"
