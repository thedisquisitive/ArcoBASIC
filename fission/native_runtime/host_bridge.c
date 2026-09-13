// The Arcology Fusion Linker's own host-function bridge -- real, native
// machine code for a real subset of the ~244-entry host-function dispatch
// table legacy ArcoFission's own bytecode VM offers via
// `arco_call_host`/host_bridge.cpp, ported here NOT by reimplementing
// everything from scratch in hand-written ArcoBASIC-generated x86-64 (an
// enormous undertaking for ~244 functions), but by compiling REAL, tiny,
// freestanding native functions with the system's own C compiler and
// linking them into a program's own final ELF64 via Fusion itself -- the
// SAME real linker `fission/amir/fusion_linker.abas`, extended
// (`fission/amir/elf_object.abas`) to read a genuine ELF64 relocatable
// object file, not just this backend's own "fragment" format.
//
// This is a ONE-TIME, DEVELOPMENT-TIME build step (see
// `fission/build/rivet_native_capsules.abas`'s own
// `Fission_RivetBuildHostBridge`) -- gcc compiles this file into a real
// `.o` once, checked-in-spirit the same way Rivet already compiles the
// REST of this C++ project's own sources; an end user's own ArcoBASIC
// program compile NEVER shells out to gcc, only to this project's own
// Fusion, preserving the "no external toolchain dependency" value at the
// level that actually matters (compiling ArcoBASIC).
//
// Every function here is DELIBERATELY self-contained: no static/global
// data, no calls between functions in this file (each own helper is
// forced `static inline __attribute__((always_inline))`) -- confirmed via
// a real, direct `objdump -r` probe that this produces a `.o` with ZERO
// relocation entries at all, keeping fission/amir/elf_object.abas's own
// FIRST real ELF-parsing scope small and fully verifiable (a symbol whose
// target is a SECTION symbol with an addend -- GCC's own convention for
// referencing static/local data -- remains a real, disclosed,
// unimplemented case for now, to be added once a real host function
// actually needs static data or a genuine cross-function call).
//
// -ffreestanding -fno-pic -fno-pie -nostdlib: no libc, no PIC/PLT/GOT, no
// C runtime startup -- these functions are called directly, exactly like
// any other label this backend's own native codegen already calls
// (`call str_concat`, `call array_alloc`, ...), via the ordinary SysV
// calling convention (args in rdi/rsi/rdx/rcx/r8/r9, result in rax) that
// convention this whole backend's own Fission_X86_64EmitCall already
// uses -- no adapter/marshaling layer needed at all, since a native
// ArcoBASIC String value IS already a plain NUL-terminated byte pointer
// (confirmed by Fission_X86_64StrPrintSubroutine's own NUL-scan) and a
// a Bool/Number value IS already a plain 64-bit integer.
//
// Real, disclosed scope for this first slice: plain BYTE semantics, not
// Unicode-aware (ASCII works correctly; this differs from any UTF-16-based
// LEN/MID semantics elsewhere in the project, e.g. the freestanding
// Arcology OS shell's own hand-assembled builtins -- a real, deliberate
// simplification matching this whole native backend's own current string
// representation, which has no Unicode awareness anywhere yet either).
// A returned NEW string (ArcoTrim/ArcoSlice) is allocated via a raw mmap
// syscall, never freed -- the same "simple, real, deliberately minimal"
// memory management this backend's OWN str_concat/array_alloc already
// use, not a new limitation introduced here.

typedef unsigned long arco_u64;
typedef long arco_i64;

static inline __attribute__((always_inline)) void* arco_raw_mmap(arco_u64 length) {
    long ret;
    register long r10 __asm__("r10") = 0x22; // MAP_PRIVATE | MAP_ANONYMOUS
    register long r8 __asm__("r8") = -1;     // fd
    register long r9 __asm__("r9") = 0;      // offset
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "a"(9), "D"(0), "S"(length), "d"(3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory"
    );
    return (void*)ret;
}

static inline __attribute__((always_inline)) arco_i64 arco_raw_strlen(const char* s) {
    arco_i64 n = 0;
    while (s[n] != 0) n = n + 1;
    return n;
}

static inline __attribute__((always_inline)) int arco_raw_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static inline __attribute__((always_inline)) int arco_raw_strings_equal(const char* a, const char* b) {
    arco_i64 i = 0;
    while (a[i] != 0 && b[i] != 0) {
        if (a[i] != b[i]) return 0;
        i = i + 1;
    }
    return a[i] == b[i];
}

// Ordinary byte-wise ordering (unsigned-char comparison, matching
// `std::string::operator<`'s own real semantics -- the comparator
// Array.Sort/String comparisons in the oracle actually use).
static inline __attribute__((always_inline)) int arco_raw_strings_less_than(const char* a, const char* b) {
    arco_i64 i = 0;
    while (a[i] != 0 && b[i] != 0) {
        if ((unsigned char)a[i] != (unsigned char)b[i]) return (unsigned char)a[i] < (unsigned char)b[i];
        i = i + 1;
    }
    return (unsigned char)a[i] < (unsigned char)b[i];
}

// A real byte-for-byte substring search (Rabin-Karp would be overkill for
// this backend's own real workloads; a plain O(n*m) scan is what legacy's
// own `String.Contains`/`String.IndexOf` semantics were confirmed against
// directly via the oracle anyway, matching content, not a smarter
// algorithm's own different edge-case behavior). Returns the 0-based
// index of the first match, or -1.
static inline __attribute__((always_inline)) arco_i64 arco_raw_find(const char* haystack, arco_i64 haystackLen, const char* needle, arco_i64 needleLen) {
    if (needleLen == 0) return 0;
    if (needleLen > haystackLen) return -1;
    arco_i64 i = 0;
    while (i + needleLen <= haystackLen) {
        arco_i64 j = 0;
        while (j < needleLen && haystack[i + j] == needle[j]) j = j + 1;
        if (j == needleLen) return i;
        i = i + 1;
    }
    return -1;
}

// String.Length(s) -- a plain byte count.
arco_i64 arco_host_string_length(const char* s) {
    return arco_raw_strlen(s);
}

// String.Contains(haystack, needle) -- 0/1, this backend's own native
// Bool representation.
arco_i64 arco_host_string_contains(const char* haystack, const char* needle) {
    arco_i64 hLen = arco_raw_strlen(haystack);
    arco_i64 nLen = arco_raw_strlen(needle);
    return arco_raw_find(haystack, hLen, needle, nLen) >= 0 ? 1 : 0;
}

// String.IndexOf(haystack, needle) -- 0-based, -1 if not found.
arco_i64 arco_host_string_index_of(const char* haystack, const char* needle) {
    arco_i64 hLen = arco_raw_strlen(haystack);
    arco_i64 nLen = arco_raw_strlen(needle);
    return arco_raw_find(haystack, hLen, needle, nLen);
}

// String.StartsWith(s, prefix).
arco_i64 arco_host_string_starts_with(const char* s, const char* prefix) {
    arco_i64 sLen = arco_raw_strlen(s);
    arco_i64 pLen = arco_raw_strlen(prefix);
    if (pLen > sLen) return 0;
    arco_i64 i = 0;
    while (i < pLen) {
        if (s[i] != prefix[i]) return 0;
        i = i + 1;
    }
    return 1;
}

// String.EndsWith(s, suffix).
arco_i64 arco_host_string_ends_with(const char* s, const char* suffix) {
    arco_i64 sLen = arco_raw_strlen(s);
    arco_i64 fLen = arco_raw_strlen(suffix);
    if (fLen > sLen) return 0;
    arco_i64 base = sLen - fLen;
    arco_i64 i = 0;
    while (i < fLen) {
        if (s[base + i] != suffix[i]) return 0;
        i = i + 1;
    }
    return 1;
}

// String.Trim(s) -- strips leading/trailing whitespace, returns a REAL
// new NUL-terminated string (mmap-allocated).
const char* arco_host_string_trim(const char* s) {
    arco_i64 sLen = arco_raw_strlen(s);
    arco_i64 start = 0;
    while (start < sLen && arco_raw_is_space(s[start])) start = start + 1;
    arco_i64 end = sLen;
    while (end > start && arco_raw_is_space(s[end - 1])) end = end - 1;
    arco_i64 resultLen = end - start;
    char* result = (char*)arco_raw_mmap((arco_u64)(resultLen + 1));
    arco_i64 i = 0;
    while (i < resultLen) {
        result[i] = s[start + i];
        i = i + 1;
    }
    result[resultLen] = 0;
    return result;
}

// String.Slice(s, start, length) -- a REAL new NUL-terminated string
// (mmap-allocated), clamped to `s`'s own real bounds the same way the
// oracle's own String.Slice already is (confirmed via the oracle, the
// same real bound-safety discipline this project's own freestanding
// Arcology OS LEN/MID builtins already use elsewhere).
const char* arco_host_string_slice(const char* s, arco_i64 start, arco_i64 length) {
    arco_i64 sLen = arco_raw_strlen(s);
    arco_i64 clampedStart = start;
    if (clampedStart < 0) clampedStart = 0;
    if (clampedStart > sLen) clampedStart = sLen;
    arco_i64 clampedLength = length;
    if (clampedLength < 0) clampedLength = 0;
    if (clampedStart + clampedLength > sLen) clampedLength = sLen - clampedStart;
    char* result = (char*)arco_raw_mmap((arco_u64)(clampedLength + 1));
    arco_i64 i = 0;
    while (i < clampedLength) {
        result[i] = s[clampedStart + i];
        i = i + 1;
    }
    result[clampedLength] = 0;
    return result;
}

// --- Batch 2: the real Number<->String conversion this whole language
// uses pervasively (`String(n)`, a genuine, real gap in native codegen
// found only by trying to use it -- confirmed via a direct probe:
// `s = String(42)` failed to compile at all before this), a batch of
// pure String.*/Bit.*/Path.* functions, and BytesToHex. Every real
// semantic here (including which edge cases the oracle itself gets
// "wrong" -- `NUMBER("abc")` genuinely crashes the oracle with a raw
// `stod` C++ exception, `StringToHex`/`HexToString` crash too and are
// simply not implemented here at all -- a broken host function is not
// something a correct native implementation should try to replicate)
// confirmed via direct, real oracle probes first, not guessed at.

// String(number) -- a real itoa, matching this backend's own existing
// itoa_write subroutine's algorithm exactly (see
// fission/amir/lower_x86_64.abas's own comment), just returning a real
// mmap-allocated string instead of writing via syscall directly. This
// backend's own Number representation is integer-only throughout (no
// floating point anywhere yet), so this is real, complete coverage for
// every Number value this backend can actually produce -- not a
// simplification of a "real" float-capable String() the oracle has that
// this backend lacks; the oracle's own float support is a real,
// disclosed, SEPARATE, pre-existing gap of this whole native backend,
// not something String() itself needs to solve.
const char* arco_host_number_to_string(arco_i64 value) {
    char buf[24];
    arco_i64 pos = 24;
    int negative = value < 0;
    arco_u64 v = negative ? (arco_u64)(-(value + 1)) + 1 : (arco_u64)value;
    if (v == 0) {
        buf[--pos] = '0';
    } else {
        while (v != 0) {
            buf[--pos] = (char)('0' + (v % 10));
            v = v / 10;
        }
    }
    if (negative) buf[--pos] = '-';
    arco_i64 len = 24 - pos;
    char* result = (char*)arco_raw_mmap((arco_u64)(len + 1));
    arco_i64 i = 0;
    while (i < len) {
        result[i] = buf[pos + i];
        i = i + 1;
    }
    result[len] = 0;
    return result;
}

// NUMBER(s) -- real integer parsing (this backend's own Number
// representation is integer-only). A leading `-` is a real sign; any
// other non-digit stops parsing there (matching a real, sane "parse as
// much as looks like a number" contract) -- confirmed the oracle itself
// has no sane defined behavior for a non-numeric string at all (a raw
// C++ `stod` exception), so there is no real oracle behavior to match
// for that case; this just never crashes, real disclosed divergence.
arco_i64 arco_host_string_to_number(const char* s) {
    arco_i64 i = 0;
    int negative = 0;
    if (s[0] == '-') { negative = 1; i = 1; }
    arco_i64 value = 0;
    while (s[i] >= '0' && s[i] <= '9') {
        value = value * 10 + (s[i] - '0');
        i = i + 1;
    }
    return negative ? -value : value;
}

static inline __attribute__((always_inline)) char arco_raw_to_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 32);
    return c;
}
static inline __attribute__((always_inline)) char arco_raw_to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

// Upper(s)/Lower(s) -- real new NUL-terminated strings (mmap-allocated),
// plain ASCII case folding (matching this backend's own current
// byte-oriented, non-Unicode-aware string representation throughout).
const char* arco_host_upper(const char* s) {
    arco_i64 len = arco_raw_strlen(s);
    char* result = (char*)arco_raw_mmap((arco_u64)(len + 1));
    arco_i64 i = 0;
    while (i < len) {
        result[i] = arco_raw_to_upper(s[i]);
        i = i + 1;
    }
    result[len] = 0;
    return result;
}
const char* arco_host_lower(const char* s) {
    arco_i64 len = arco_raw_strlen(s);
    char* result = (char*)arco_raw_mmap((arco_u64)(len + 1));
    arco_i64 i = 0;
    while (i < len) {
        result[i] = arco_raw_to_lower(s[i]);
        i = i + 1;
    }
    result[len] = 0;
    return result;
}

// String.Delete(s, start, count) -- removes `count` bytes starting at
// `start`, real bound-clamping the same way String.Slice already does.
const char* arco_host_string_delete(const char* s, arco_i64 start, arco_i64 count) {
    arco_i64 sLen = arco_raw_strlen(s);
    arco_i64 clampedStart = start;
    if (clampedStart < 0) clampedStart = 0;
    if (clampedStart > sLen) clampedStart = sLen;
    arco_i64 clampedCount = count;
    if (clampedCount < 0) clampedCount = 0;
    if (clampedStart + clampedCount > sLen) clampedCount = sLen - clampedStart;
    arco_i64 resultLen = sLen - clampedCount;
    char* result = (char*)arco_raw_mmap((arco_u64)(resultLen + 1));
    arco_i64 i = 0;
    while (i < clampedStart) { result[i] = s[i]; i = i + 1; }
    arco_i64 j = clampedStart + clampedCount;
    while (j < sLen) { result[i] = s[j]; i = i + 1; j = j + 1; }
    result[resultLen] = 0;
    return result;
}

// String.Insert(s, index, text) -- inserts `text` at byte offset
// `index`, real bound-clamping the same way every other real function
// here already does.
const char* arco_host_string_insert(const char* s, arco_i64 index, const char* text) {
    arco_i64 sLen = arco_raw_strlen(s);
    arco_i64 tLen = arco_raw_strlen(text);
    arco_i64 clampedIndex = index;
    if (clampedIndex < 0) clampedIndex = 0;
    if (clampedIndex > sLen) clampedIndex = sLen;
    arco_i64 resultLen = sLen + tLen;
    char* result = (char*)arco_raw_mmap((arco_u64)(resultLen + 1));
    arco_i64 i = 0;
    while (i < clampedIndex) { result[i] = s[i]; i = i + 1; }
    arco_i64 k = 0;
    while (k < tLen) { result[i] = text[k]; i = i + 1; k = k + 1; }
    arco_i64 j = clampedIndex;
    while (j < sLen) { result[i] = s[j]; i = i + 1; j = j + 1; }
    result[resultLen] = 0;
    return result;
}

// String.Replace(s, oldText, newText) -- ALL occurrences (confirmed via
// a direct oracle probe: `String.Replace("aXbXcX", "X", "-")` ->
// "a-b-c-", not just the first), real content matching (the same
// technique arco_raw_find already proved correct for Contains/IndexOf).
// A two-pass approach (measure the real result length first, then
// build it) since this backend's own mmap allocator has no realloc --
// the same "know the exact size before allocating once" discipline
// Trim/Slice/Delete/Insert above already use.
const char* arco_host_string_replace(const char* s, const char* oldText, const char* newText) {
    arco_i64 sLen = arco_raw_strlen(s);
    arco_i64 oldLen = arco_raw_strlen(oldText);
    arco_i64 newLen = arco_raw_strlen(newText);
    if (oldLen == 0) {
        char* copy = (char*)arco_raw_mmap((arco_u64)(sLen + 1));
        arco_i64 i = 0;
        while (i < sLen) { copy[i] = s[i]; i = i + 1; }
        copy[sLen] = 0;
        return copy;
    }
    arco_i64 resultLen = 0;
    arco_i64 i = 0;
    while (i < sLen) {
        if (i + oldLen <= sLen) {
            arco_i64 j = 0;
            while (j < oldLen && s[i + j] == oldText[j]) j = j + 1;
            if (j == oldLen) { resultLen = resultLen + newLen; i = i + oldLen; continue; }
        }
        resultLen = resultLen + 1;
        i = i + 1;
    }
    char* result = (char*)arco_raw_mmap((arco_u64)(resultLen + 1));
    arco_i64 outPos = 0;
    i = 0;
    while (i < sLen) {
        if (i + oldLen <= sLen) {
            arco_i64 j = 0;
            while (j < oldLen && s[i + j] == oldText[j]) j = j + 1;
            if (j == oldLen) {
                arco_i64 k = 0;
                while (k < newLen) { result[outPos] = newText[k]; outPos = outPos + 1; k = k + 1; }
                i = i + oldLen;
                continue;
            }
        }
        result[outPos] = s[i];
        outPos = outPos + 1;
        i = i + 1;
    }
    result[resultLen] = 0;
    return result;
}

// Bit.And/Or/Xor/Not/ShiftLeft/ShiftRight, BITCOUNT, SETBIT, CLEARBIT,
// TOGGLEBIT, BIT, ROTATELEFT, ROTATERIGHT, SHIFT -- plain 64-bit integer
// bit manipulation, every real semantic confirmed against the oracle
// directly first (`Bit.ShiftRight`/`SHIFT` are real ARITHMETIC
// (sign-preserving) shifts, confirmed via `Bit.ShiftRight(-8, 1)` ->
// `-4`, matching C's own signed right-shift on `arco_i64` exactly, no
// special-casing needed; `ROTATELEFT`/`ROTATERIGHT` operate over the
// FULL 64-bit width, confirmed via a real wraparound probe:
// `ROTATELEFT(2^60, 4)` -> `1`).
arco_i64 arco_host_bit_and(arco_i64 a, arco_i64 b) { return a & b; }
arco_i64 arco_host_bit_or(arco_i64 a, arco_i64 b) { return a | b; }
arco_i64 arco_host_bit_xor(arco_i64 a, arco_i64 b) { return a ^ b; }
arco_i64 arco_host_bit_not(arco_i64 a) { return ~a; }
arco_i64 arco_host_bit_shift_left(arco_i64 a, arco_i64 n) { return a << n; }
arco_i64 arco_host_bit_shift_right(arco_i64 a, arco_i64 n) { return a >> n; }
arco_i64 arco_host_bit_count(arco_i64 a) {
    arco_u64 v = (arco_u64)a;
    arco_i64 count = 0;
    while (v != 0) { count = count + (arco_i64)(v & 1); v = v >> 1; }
    return count;
}
arco_i64 arco_host_set_bit(arco_i64 value, arco_i64 index) { return value | (((arco_i64)1) << index); }
arco_i64 arco_host_clear_bit(arco_i64 value, arco_i64 index) { return value & ~(((arco_i64)1) << index); }
arco_i64 arco_host_toggle_bit(arco_i64 value, arco_i64 index) { return value ^ (((arco_i64)1) << index); }
arco_i64 arco_host_test_bit(arco_i64 value, arco_i64 index) { return ((value >> index) & 1) != 0 ? 1 : 0; }
arco_i64 arco_host_rotate_left(arco_i64 value, arco_i64 n) {
    arco_u64 v = (arco_u64)value;
    arco_u64 shift = (arco_u64)n & 63;
    if (shift == 0) return (arco_i64)v;
    return (arco_i64)((v << shift) | (v >> (64 - shift)));
}
arco_i64 arco_host_rotate_right(arco_i64 value, arco_i64 n) {
    arco_u64 v = (arco_u64)value;
    arco_u64 shift = (arco_u64)n & 63;
    if (shift == 0) return (arco_i64)v;
    return (arco_i64)((v >> shift) | (v << (64 - shift)));
}
arco_i64 arco_host_shift(arco_i64 value, arco_i64 n) {
    if (n >= 0) return value << n;
    return value >> (-n);
}

// BytesToHex(s) -- treats `s` as a raw byte sequence (real, matching the
// oracle exactly: confirmed `BytesToHex("Hi")` -> `"4869"`, the ASCII
// bytes 0x48/0x69 hex-encoded, lowercase confirmed too) and returns a
// real new hex-digit string. `StringToHex`/`HexToString` are
// deliberately NOT implemented here -- confirmed via a direct oracle
// probe that BOTH genuinely crash the oracle itself (a raw `stoll` C++
// exception / "value is not a number"), so there is no real, working
// oracle behavior to port at all.
static inline __attribute__((always_inline)) char arco_raw_hex_digit(int nibble) {
    if (nibble < 10) return (char)('0' + nibble);
    return (char)('a' + (nibble - 10));
}
const char* arco_host_bytes_to_hex(const char* s) {
    arco_i64 len = arco_raw_strlen(s);
    char* result = (char*)arco_raw_mmap((arco_u64)(len * 2 + 1));
    arco_i64 i = 0;
    while (i < len) {
        unsigned char b = (unsigned char)s[i];
        result[i * 2] = arco_raw_hex_digit(b >> 4);
        result[i * 2 + 1] = arco_raw_hex_digit(b & 15);
        i = i + 1;
    }
    result[len * 2] = 0;
    return result;
}

// Path.BaseName/DirName/Extension -- pure byte-oriented path string
// manipulation (no real filesystem syscall needed at all), every real
// edge case confirmed against the oracle directly: a trailing slash
// (`Path.BaseName("/a/b/")`) real empties to "" (matching a real
// last-path-COMPONENT semantic, not "strip the last slash then take
// what's left"); no slash at all keeps the whole string as the base
// name; no `.` at all is a real empty extension, not the whole name.
const char* arco_host_path_basename(const char* s) {
    arco_i64 len = arco_raw_strlen(s);
    arco_i64 lastSlash = -1;
    arco_i64 i = 0;
    while (i < len) {
        if (s[i] == '/') lastSlash = i;
        i = i + 1;
    }
    arco_i64 start = lastSlash + 1;
    arco_i64 resultLen = len - start;
    char* result = (char*)arco_raw_mmap((arco_u64)(resultLen + 1));
    arco_i64 j = 0;
    while (j < resultLen) { result[j] = s[start + j]; j = j + 1; }
    result[resultLen] = 0;
    return result;
}
const char* arco_host_path_dirname(const char* s) {
    arco_i64 len = arco_raw_strlen(s);
    arco_i64 lastSlash = -1;
    arco_i64 i = 0;
    while (i < len) {
        if (s[i] == '/') lastSlash = i;
        i = i + 1;
    }
    arco_i64 resultLen = lastSlash < 0 ? 0 : lastSlash;
    char* result = (char*)arco_raw_mmap((arco_u64)(resultLen + 1));
    arco_i64 j = 0;
    while (j < resultLen) { result[j] = s[j]; j = j + 1; }
    result[resultLen] = 0;
    return result;
}
const char* arco_host_path_extension(const char* s) {
    arco_i64 len = arco_raw_strlen(s);
    arco_i64 lastDot = -1;
    arco_i64 lastSlash = -1;
    arco_i64 i = 0;
    while (i < len) {
        if (s[i] == '.') lastDot = i;
        if (s[i] == '/') lastSlash = i;
        i = i + 1;
    }
    if (lastDot < 0 || lastDot < lastSlash) {
        char* empty = (char*)arco_raw_mmap(1);
        empty[0] = 0;
        return empty;
    }
    arco_i64 resultLen = len - lastDot;
    char* result = (char*)arco_raw_mmap((arco_u64)(resultLen + 1));
    arco_i64 j = 0;
    while (j < resultLen) { result[j] = s[lastDot + j]; j = j + 1; }
    result[resultLen] = 0;
    return result;
}

// --- Batch 3: real File I/O via raw Linux x86-64 syscalls
// (open/read/write/close/lseek/stat/mkdir/access/chmod) -- the "clear next
// candidate" this whole file's own header comment and the progress ledger
// both named after batch 2, feasible without any array-representation
// overhaul: File.ReadBytes/File.WriteBytes are the first host functions to
// touch a real ArcoBASIC Array value, but every array involved has its
// real size known BEFORE allocation (a file's own real byte length via
// `lseek`, or an array this program already built and never grows) -- no
// realloc/growth semantics needed at all, unlike Array.Push and friends
// (still a real, disclosed, unstarted gap, see this file's own header
// comment on why).
//
// File.ReadBytes/File.WriteBytes use the EXACT SAME array layout
// fission/amir/lower_x86_64.abas's own `array_alloc` subroutine already
// establishes (a plain 8-byte element-count header at [pointer], then N
// raw 8-byte element slots at [pointer+8]) -- allocated here via this same
// file's own `arco_raw_mmap`, a SEPARATE block from `array_alloc`'s own
// shared bump-allocated heap (never freed either way, the same
// "deliberately simple, no realloc, no free" discipline this whole native
// backend already uses everywhere). This is safe because nothing
// downstream (LEN/INDEX/STORE_INDEX/array_print/ForEach) ever assumes an
// array's own backing memory came from one particular allocator -- they
// only ever read/write through the pointer this function itself returns.
//
// Every syscall number/flag value below is the real, stable x86-64 Linux
// syscall ABI (not libc's own wrapper numbering, which differs on other
// architectures) -- confirmed against the kernel's own published syscall
// table, matching the same "raw syscall, no libc" discipline
// `arco_raw_mmap` above already established.

static inline __attribute__((always_inline)) long arco_raw_syscall1(long n, long a1) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}
static inline __attribute__((always_inline)) long arco_raw_syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}
static inline __attribute__((always_inline)) long arco_raw_syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("syscall" : "=a"(ret) : "a"(n), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

#define ARCO_SYS_READ 0
#define ARCO_SYS_WRITE 1
#define ARCO_SYS_OPEN 2
#define ARCO_SYS_CLOSE 3
#define ARCO_SYS_STAT 4
#define ARCO_SYS_LSEEK 8
#define ARCO_SYS_ACCESS 21
#define ARCO_SYS_MKDIR 83
#define ARCO_SYS_CHMOD 90

#define ARCO_O_RDONLY 0
#define ARCO_O_WRONLY 1
#define ARCO_O_CREAT 0100
#define ARCO_O_TRUNC 01000
#define ARCO_O_APPEND 02000

// A real `struct stat`'s own `st_mode` field, read directly by fixed byte
// offset (24, a stable part of the real x86-64 Linux ABI) rather than
// declaring the full struct -- the same "just the bytes this function
// actually needs" discipline the rest of this file already uses (e.g.
// reading a length header directly by offset instead of a struct).
static inline __attribute__((always_inline)) arco_i64 arco_raw_stat_mode(const char* path, unsigned int* modeOut) {
    char buf[144];
    long ret = arco_raw_syscall2(ARCO_SYS_STAT, (long)path, (long)buf);
    if (ret != 0) return 0;
    *modeOut = *(unsigned int*)(buf + 24);
    return 1;
}
#define ARCO_S_IFMT 0170000
#define ARCO_S_IFDIR 0040000

// File.Exists(path) -- real access(2) probe (F_OK).
arco_i64 arco_host_file_exists(const char* path) {
    long ret = arco_raw_syscall2(ARCO_SYS_ACCESS, (long)path, 0);
    return ret == 0 ? 1 : 0;
}

// Directory.Exists(path) -- real stat(2), checking the S_IFDIR bit.
arco_i64 arco_host_directory_exists(const char* path) {
    unsigned int mode = 0;
    if (arco_raw_stat_mode(path, &mode) == 0) return 0;
    return (mode & ARCO_S_IFMT) == ARCO_S_IFDIR ? 1 : 0;
}

// File.ReadText(path) -- a real new NUL-terminated string (mmap-allocated),
// the file's own real byte length read via lseek(SEEK_END) (no libc
// fstat/struct needed for this one -- just a size, not a mode bit). A
// missing/unreadable file returns a real empty string rather than
// crashing (this backend has no exception-propagation mechanism of its
// own to surface a real I/O error through yet -- a disclosed, deliberate
// simplification, not a claim this matches the oracle's own exception on
// a missing file).
const char* arco_host_file_read_text(const char* path) {
    long fd = arco_raw_syscall3(ARCO_SYS_OPEN, (long)path, ARCO_O_RDONLY, 0);
    if (fd < 0) {
        char* empty = (char*)arco_raw_mmap(1);
        empty[0] = 0;
        return empty;
    }
    long size = arco_raw_syscall3(ARCO_SYS_LSEEK, fd, 0, 2);
    if (size < 0) size = 0;
    arco_raw_syscall3(ARCO_SYS_LSEEK, fd, 0, 0);
    char* result = (char*)arco_raw_mmap((arco_u64)(size + 1));
    long totalRead = 0;
    while (totalRead < size) {
        long n = arco_raw_syscall3(ARCO_SYS_READ, fd, (long)(result + totalRead), size - totalRead);
        if (n <= 0) break;
        totalRead = totalRead + n;
    }
    result[totalRead] = 0;
    arco_raw_syscall1(ARCO_SYS_CLOSE, fd);
    return result;
}

// File.WriteText/File.AppendText share everything but the open() flags --
// a real shared `static inline` helper (forced-inlined into both real
// exported symbols below, the same "no real CALL between two functions in
// this file" discipline this file's own header comment already commits
// to, so this stays a zero-relocation `.o` exactly like the rest of it).
static inline __attribute__((always_inline)) arco_i64 arco_raw_write_text(const char* path, const char* text, long flags) {
    long fd = arco_raw_syscall3(ARCO_SYS_OPEN, (long)path, flags, 0644);
    if (fd < 0) return 0;
    long len = arco_raw_strlen(text);
    long written = 0;
    while (written < len) {
        long n = arco_raw_syscall3(ARCO_SYS_WRITE, fd, (long)(text + written), len - written);
        if (n <= 0) break;
        written = written + n;
    }
    arco_raw_syscall1(ARCO_SYS_CLOSE, fd);
    return written == len ? 1 : 0;
}
arco_i64 arco_host_file_write_text(const char* path, const char* text) {
    return arco_raw_write_text(path, text, ARCO_O_WRONLY | ARCO_O_CREAT | ARCO_O_TRUNC);
}
arco_i64 arco_host_file_append_text(const char* path, const char* text) {
    return arco_raw_write_text(path, text, ARCO_O_WRONLY | ARCO_O_CREAT | ARCO_O_APPEND);
}

// File.ReadBytes(path) -- a real Array:Number, this batch's own first
// real Array-producing host function (see this section's own header
// comment for the layout/allocator reasoning). A missing/unreadable file
// returns a real empty array (length 0), the same "no exception mechanism
// yet, fail soft" choice File.ReadText above already makes.
const void* arco_host_file_read_bytes(const char* path) {
    long fd = arco_raw_syscall3(ARCO_SYS_OPEN, (long)path, ARCO_O_RDONLY, 0);
    long size = 0;
    if (fd >= 0) {
        size = arco_raw_syscall3(ARCO_SYS_LSEEK, fd, 0, 2);
        if (size < 0) size = 0;
        arco_raw_syscall3(ARCO_SYS_LSEEK, fd, 0, 0);
    }
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((size + 1) * 8));
    result[0] = size;
    if (fd >= 0 && size > 0) {
        char* scratch = (char*)arco_raw_mmap((arco_u64)size);
        long totalRead = 0;
        while (totalRead < size) {
            long n = arco_raw_syscall3(ARCO_SYS_READ, fd, (long)(scratch + totalRead), size - totalRead);
            if (n <= 0) break;
            totalRead = totalRead + n;
        }
        long i = 0;
        while (i < totalRead) { result[1 + i] = (arco_i64)(unsigned char)scratch[i]; i = i + 1; }
        while (i < size) { result[1 + i] = 0; i = i + 1; }
    }
    if (fd >= 0) arco_raw_syscall1(ARCO_SYS_CLOSE, fd);
    return (const void*)result;
}

// File.WriteBytes(path, bytes) -- `bytes` is a real Array:Number, this
// batch's own first real Array-CONSUMING host function: `arr[0]` is the
// real element-count header array_alloc/File.ReadBytes both already
// write, `arr[1 + i]` each real element slot, each element's own low byte
// the real byte value written (matching the oracle's own real
// File.WriteBytes semantics: a Number 0-255 per element, confirmed via a
// direct probe against the oracle -- an out-of-0-255-range element is not
// specially validated here, matching the oracle's own lack of validation
// too, just masked to its own low byte the same way any other byte-sized
// write truncates a wider value).
arco_i64 arco_host_file_write_bytes(const char* path, const arco_i64* arr) {
    arco_i64 count = arr[0];
    long fd = arco_raw_syscall3(ARCO_SYS_OPEN, (long)path, ARCO_O_WRONLY | ARCO_O_CREAT | ARCO_O_TRUNC, 0644);
    if (fd < 0) return 0;
    arco_i64 ok = 1;
    if (count > 0) {
        char* scratch = (char*)arco_raw_mmap((arco_u64)count);
        arco_i64 i = 0;
        while (i < count) { scratch[i] = (char)(arr[1 + i] & 0xFF); i = i + 1; }
        long written = 0;
        while (written < count) {
            long n = arco_raw_syscall3(ARCO_SYS_WRITE, fd, (long)(scratch + written), count - written);
            if (n <= 0) { ok = 0; break; }
            written = written + n;
        }
        if (written != count) ok = 0;
    }
    arco_raw_syscall1(ARCO_SYS_CLOSE, fd);
    return ok;
}

// File.SetExecutable(path) -- real stat(2)+chmod(2), ADDING the
// owner/group/other execute bits onto the file's own EXISTING mode
// (matching the oracle's own real semantic exactly: `perm_options::add`,
// not a fixed mode overwrite) -- this exists for the SAME reason it was
// added to the oracle in the first place (see runtime.cpp's own comment):
// this native backend's own compiler writes a real ELF64 executable
// itself via File.WriteBytes, with no external `chmod` process to mark it
// runnable otherwise.
arco_i64 arco_host_file_set_executable(const char* path) {
    unsigned int mode = 0;
    if (arco_raw_stat_mode(path, &mode) == 0) return 0;
    unsigned int newMode = mode | 0111;
    long chmodRet = arco_raw_syscall2(ARCO_SYS_CHMOD, (long)path, (long)newMode);
    return chmodRet == 0 ? 1 : 0;
}

// Directory.Create(path) -- real RECURSIVE creation (matching the
// oracle's own `create_directories`, not a single-level `mkdir`):
// mkdir's each '/'-delimited prefix of a real mutable copy of `path` in
// turn (each individual mkdir's own error, including a segment that
// already exists, is real but harmless -- only the FINAL real
// Directory.Exists-style check below decides the real return value,
// matching the oracle's own "did it end up existing" contract exactly).
arco_i64 arco_host_directory_create(const char* path) {
    arco_i64 len = arco_raw_strlen(path);
    if (len == 0) return 0;
    char* buf = (char*)arco_raw_mmap((arco_u64)(len + 1));
    arco_i64 i = 0;
    while (i < len) { buf[i] = path[i]; i = i + 1; }
    buf[len] = 0;
    i = 1;
    while (i <= len) {
        if (i == len || buf[i] == '/') {
            char save = buf[i];
            buf[i] = 0;
            arco_raw_syscall2(ARCO_SYS_MKDIR, (long)buf, 0755);
            buf[i] = save;
        }
        i = i + 1;
    }
    unsigned int mode = 0;
    if (arco_raw_stat_mode(path, &mode) == 0) return 0;
    return (mode & ARCO_S_IFMT) == ARCO_S_IFDIR ? 1 : 0;
}


// --- Batch 4: real Random.*, plus Time.Timestamp and Sleep.
//
// Random.* ports the oracle's OWN Pcg32 generator (include/arco/random.hpp
// / src/runtime/random.cpp) byte-for-byte -- a real, standard PCG32 XSH-RR
// generator, confirmed identical via a direct unit-test harness comparing
// known seed/sequence outputs against the oracle's own class directly, not
// a "close enough" reimplementation. A "handle" here is simply a REAL
// POINTER to a 16-byte {state, increment} block this file's own
// `arco_raw_mmap` allocates (`Random.Create`'s own real return value) --
// NOT an opaque runtime-registry handle the way the oracle's own
// `RuntimeHandle`/`object_handles_` machinery works, since this native
// backend has no handle/resource-registry infrastructure of its own at
// all. `Random.Destroy` is not implemented here: there is nothing for it
// to meaningfully do (this whole backend never frees anything it
// allocates, the same deliberate "simple, no realloc, no free" discipline
// `array_alloc`/`str_concat` already established) -- calling
// `Random.Float`/`Integer`/etc. on a "destroyed" handle here just keeps
// working, a real, disclosed divergence from the oracle's own real
// validity tracking, consistent with this whole backend's already-
// established "no runtime error/exception mechanism yet" gap (the same
// one File.ReadText's own missing-file handling above already discloses).
//
// Real, disclosed scope: `Random.Float`/`Math.Random` are NOT implemented
// at all -- both return a real FRACTIONAL double in [0, 1), and this
// whole native backend's own Number representation is INTEGER-ONLY
// throughout (see `arco_host_number_to_string`'s own comment) -- there is
// no way to represent their real return value correctly, a pre-existing,
// separate, disclosed gap, not something Random.* itself should paper
// over with a wrong truncated/scaled integer. `Random.Integer`'s own
// result IS always a whole integer even though the oracle stores it as a
// C++ double, so it fits this backend's representation exactly, no
// truncation involved. `Random.Create`/`Random.Reseed`'s own OPTIONAL
// `sequence` parameter and `Random.Create`'s own ZERO-argument auto-seed
// form (which needs a real OS entropy source plus a process-lifetime
// serial counter to match the oracle's own `automatic_random_seed()`
// exactly -- itself real, persistent, mutable GLOBAL state, the same
// "needs a real static-data relocation, not yet supported" boundary this
// file's own header comment already flags) are not implemented either --
// only the explicit-seed, explicit-handle forms this backend's own
// fixed-arity host-table design (or, for `Random.Choice`/`Sample`/
// `Shuffle`, `Fission_X86_64LowerFunction`'s own real special-case
// dispatch -- see that file's own comment for why) can represent without
// guessing at an unsupported call shape.
typedef struct { arco_u64 state; arco_u64 increment; } arco_pcg32_state;

static inline __attribute__((always_inline)) unsigned int arco_pcg32_rotr32(unsigned int value, unsigned int rotation) {
    rotation = rotation & 31u;
    return (value >> rotation) | (value << ((0u - rotation) & 31u));
}
static inline __attribute__((always_inline)) unsigned int arco_pcg32_next(arco_pcg32_state* s) {
    arco_u64 old_state = s->state;
    s->state = old_state * 6364136223846793005ULL + s->increment;
    unsigned int xorshifted = (unsigned int)(((old_state >> 18) ^ old_state) >> 27);
    unsigned int rotation = (unsigned int)(old_state >> 59);
    return arco_pcg32_rotr32(xorshifted, rotation);
}
static inline __attribute__((always_inline)) void arco_pcg32_reseed(arco_pcg32_state* s, arco_u64 seed, arco_u64 sequence) {
    s->state = 0;
    s->increment = (sequence << 1) | 1;
    arco_pcg32_next(s);
    s->state = s->state + seed;
    arco_pcg32_next(s);
}
// Matches the oracle's own Lemire-style rejection loop exactly (`bound`
// real range [1, 2^32]; the oracle itself throws outside that range --
// not validated here, matching this whole batch's own "no exception
// mechanism yet" choice).
static inline __attribute__((always_inline)) unsigned int arco_pcg32_bounded(arco_pcg32_state* s, arco_u64 bound) {
    if (bound == (1ULL << 32)) return arco_pcg32_next(s);
    unsigned int width = (unsigned int)bound;
    unsigned int threshold = (unsigned int)(0u - width) % width;
    while (1) {
        unsigned int value = arco_pcg32_next(s);
        if (value >= threshold) return value % width;
    }
}
#define ARCO_PCG32_DEFAULT_SEQUENCE 54ULL

// Random.Create(seed) -- a real new handle (see this section's own header
// comment for what a "handle" really is here).
const void* arco_host_random_create(arco_i64 seed) {
    arco_pcg32_state* s = (arco_pcg32_state*)arco_raw_mmap(sizeof(arco_pcg32_state));
    arco_pcg32_reseed(s, (arco_u64)seed, ARCO_PCG32_DEFAULT_SEQUENCE);
    return (const void*)s;
}
// Random.Clone(handle) -- a real NEW handle carrying an exact COPY of the
// source generator's own state (the oracle's own `Random.Clone` does
// `std::make_shared<Pcg32>(*generator)`, a real value copy of the same
// two-field state) -- future draws from the clone and the original are
// fully independent from this point on, matching the oracle exactly.
const void* arco_host_random_clone(arco_pcg32_state* handle) {
    arco_pcg32_state* s = (arco_pcg32_state*)arco_raw_mmap(sizeof(arco_pcg32_state));
    s->state = handle->state;
    s->increment = handle->increment;
    return (const void*)s;
}
// Random.Destroy(handle) -- a real, disclosed simplification: the oracle
// invalidates the handle so any LATER use throws a real error; this
// backend has no handle-validity tracking at all (matching this whole
// file's own established "never frees anything it allocates" discipline
// elsewhere), so this is a real no-op that always reports success --
// using a handle after "destroying" it is undefined here rather than a
// real, reported error, the same disclosed shape as this file's other
// no-exception-mechanism-yet simplifications.
arco_i64 arco_host_random_destroy(arco_pcg32_state* handle) {
    (void)handle;
    return 1;
}
// Random.Reseed(handle, seed) -- real in-place reseeding of an EXISTING
// handle (matching the oracle's own real semantic: the same generator
// object, now producing a fresh stream).
arco_i64 arco_host_random_reseed(arco_pcg32_state* handle, arco_i64 seed) {
    arco_pcg32_reseed(handle, (arco_u64)seed, ARCO_PCG32_DEFAULT_SEQUENCE);
    return 1;
}
// Random.Integer(minimum, maximum, handle) -- real bounded rejection
// sampling, matching the oracle's own `minimum + generator->bounded(width)`
// exactly (`minimum > maximum`/an over-wide range are real oracle errors,
// not validated here either).
arco_i64 arco_host_random_integer(arco_i64 minimum, arco_i64 maximum, arco_pcg32_state* handle) {
    arco_u64 width = (arco_u64)(maximum - minimum + 1);
    return minimum + (arco_i64)arco_pcg32_bounded(handle, width);
}
// Random.Choice(array, handle) -- returns ONE raw element slot verbatim
// (works identically for a Number or a String array, see this section's
// own header comment -- the real result KIND is resolved at compile time
// by fission/amir/lower_x86_64.abas instead).
arco_i64 arco_host_random_choice(const arco_i64* arr, arco_pcg32_state* handle) {
    arco_i64 count = arr[0];
    arco_u64 index = arco_pcg32_bounded(handle, (arco_u64)count);
    return arr[1 + index];
}
// Random.Shuffle(array, handle) -- a real NEW array (the oracle's own
// Shuffle returns a fresh `Value::Array` by value, confirmed via a direct
// read of runtime.cpp's own implementation -- it copies `args[0]` before
// shuffling, never mutates the caller's own array in place), real
// Fisher-Yates, matching the oracle's own exact iteration order and RNG
// consumption.
const void* arco_host_random_shuffle(const arco_i64* arr, arco_pcg32_state* handle) {
    arco_i64 count = arr[0];
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 i = 0;
    while (i < count) { result[1 + i] = arr[1 + i]; i = i + 1; }
    arco_i64 remaining = count;
    while (remaining > 1) {
        arco_u64 selected = arco_pcg32_bounded(handle, (arco_u64)remaining);
        arco_i64 tmp = result[remaining];
        result[remaining] = result[1 + selected];
        result[1 + selected] = tmp;
        remaining = remaining - 1;
    }
    return (const void*)result;
}
// Random.Sample(array, count, handle) -- a real NEW array of length
// `count`, real PARTIAL Fisher-Yates matching the oracle's own exact
// algorithm and RNG consumption order (`for index in [0, count):
// selected = index + bounded(size - index); swap(values[index],
// values[selected])`, then truncate to `count`).
const void* arco_host_random_sample(const arco_i64* arr, arco_i64 count, arco_pcg32_state* handle) {
    arco_i64 total = arr[0];
    char* workRaw = (char*)arco_raw_mmap((arco_u64)(total > 0 ? total * 8 : 8));
    arco_i64* work = (arco_i64*)workRaw;
    arco_i64 i = 0;
    while (i < total) { work[i] = arr[1 + i]; i = i + 1; }
    i = 0;
    while (i < count) {
        arco_u64 selected = (arco_u64)i + arco_pcg32_bounded(handle, (arco_u64)(total - i));
        arco_i64 tmp = work[i];
        work[i] = work[selected];
        work[selected] = tmp;
        i = i + 1;
    }
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    i = 0;
    while (i < count) { result[1 + i] = work[i]; i = i + 1; }
    return (const void*)result;
}

// Time.Timestamp() -- real clock_gettime(CLOCK_REALTIME) syscall, whole
// seconds since the epoch (matching the oracle's own real
// `duration_cast<seconds>` truncation -- always a whole integer, fits
// this backend's own integer-only Number representation exactly).
arco_i64 arco_host_time_timestamp(void) {
    long ts[2];
    arco_raw_syscall2(228, 0, (long)ts);
    return (arco_i64)ts[0];
}

// Sleep(milliseconds) -- real nanosleep(2) syscall. Returns Bool (TRUE)
// rather than the oracle's own real NULL -- this whole native backend has
// no NULL/void value representation established yet (a real, disclosed,
// separate, pre-existing gap), and Sleep's own return value is never
// meaningfully used in real ArcoBASIC code (a statement-only call in
// practice). A single `nanosleep` call (not retried on EINTR) is a real,
// disclosed simplification versus `std::this_thread::sleep_for`'s own
// retry-until-elapsed behavior.
arco_i64 arco_host_sleep(arco_i64 milliseconds) {
    if (milliseconds > 0) {
        long ts[2];
        ts[0] = milliseconds / 1000;
        ts[1] = (milliseconds % 1000) * 1000000L;
        arco_raw_syscall2(35, (long)ts, 0);
    }
    return 1;
}


// --- Batch 5: real floating-point support. ArcoBASIC's own Number is
// ALWAYS a real double (see include/arco/value.hpp's own `Value::
// Storage`) -- there is no separate int/float type anywhere in the whole
// language. This whole native backend's own Number representation was,
// until now, always an INTEGER special case of that (correct as long as
// a value stayed within a double's own exact-integer range, but not the
// real thing) -- the compiler side of real floating-point support
// (decimal literals, real SSE arithmetic/comparisons, int<->double
// promotion) lives in fission/amir/lower_x86_64.abas/x86_64_assembler.abas;
// this file's own real, NEW piece is PRINT formatting -- the one part
// that genuinely needs runtime code, not just a literal's own
// COMPILE-TIME bit pattern (see Fission_X86_64FloatLiteralBits' own
// comment for why that lives in the self-hosted compiler instead).

static inline __attribute__((always_inline)) double arco_raw_trunc(double v) {
    double result;
    __asm__("roundsd $3, %1, %0" : "=x"(result) : "x"(v));
    return result;
}
static inline __attribute__((always_inline)) double arco_raw_round_even(double v) {
    double result;
    __asm__("roundsd $0, %1, %0" : "=x"(result) : "x"(v));
    return result;
}

// A real double CONSTANT (10.0, 2.0, ...) built from a genuine integer-
// to-double CONVERSION INSTRUCTION at runtime -- an ordinary `x <
// 9223372036854775808.0`-style floating-point literal comparison
// produces a REAL `.rodata`/PC32-relocation reference (there is no
// "load a 64-bit float immediate" x86-64 instruction at all), which
// this whole file's own host-function bridge cannot support yet (see
// fission/amir/elf_object.abas's own header comment: a real relocation
// against a SECTION symbol with an addend -- gcc's own convention for
// referencing static/local rodata -- is a real, disclosed, unimplemented
// case, deliberately deferred until a real host function needed it).
// This works around that architectural gap entirely rather than
// requiring it: every double constant this function needs is
// synthesized via a real `cvtsi2sd` from a small integer immediate
// instead. Real, hand-written INLINE ASM, not a `volatile`-local trick
// (tried first, and confirmed too fragile: GCC's own optimizer
// respected it, but clang's did not -- clang still saw straight through
// to the underlying compile-time-constant VALUE and folded the whole
// squaring chain built from it right back into a `.rodata` constant
// anyway, reintroducing the exact relocation this exists to avoid; a
// real compiler CHOICE this project's own build cannot assume, since
// RIVET.Toolchain.DetectCxx() tries clang++ before g++/c++ -- confirmed
// by directly reproducing the clang-only regression via a real `clang++
// -x c` compile of this exact file). Inline asm is opaque to every
// optimizer by definition -- neither compiler can see through it to
// fold anything derived from its result, verified directly against
// BOTH real compilers before trusting it, not assumed from one.
static inline __attribute__((always_inline)) double arco_raw_int_to_double(long n) {
    double result;
    __asm__("cvtsi2sd %1, %0" : "=x"(result) : "r"(n));
    return result;
}

// Real IEEE-754 sign-bit negation via raw INTEGER xor (a plain 64-bit
// immediate, needing no rodata at all) instead of C's own `-value`,
// which both GCC and clang compile to `xorpd` against a rodata-stored
// sign-bit mask -- the exact same rodata-relocation problem this
// function's own header comment already describes for floating-point
// literals, just reached through negation instead of a numeric
// constant. A first attempt built this via `__builtin_memcpy` +
// integer xor instead of `-v` directly -- correct on GCC, but a real,
// confirmed regression on clang: its optimizer saw straight through the
// memcpy round-trip to the underlying intent and reintroduced the exact
// same `xorpd`-against-`.rodata` form anyway. Real, hand-written INLINE
// ASM (opaque to every optimizer by definition, verified directly
// against BOTH real compilers) for the GP<->XMM moves; the actual XOR
// stays plain, compiler-visible integer arithmetic (which never needs
// rodata regardless of optimization level either way).
static inline __attribute__((always_inline)) double arco_raw_negate(double v) {
    arco_u64 bits;
    __asm__("movq %1, %0" : "=r"(bits) : "x"(v));
    bits = bits ^ 0x8000000000000000UL;
    double result;
    __asm__("movq %1, %0" : "=x"(result) : "r"(bits));
    return result;
}

// Real double -> decimal-string formatting, matching the oracle's own
// Value::to_string() exactly: a whole-number-valued double prints as a
// plain integer (no decimal point); anything else uses a real
// 6-significant-digit general (%g-equivalent) format -- round-half-to-
// EVEN (matching IEEE-754/glibc's own default rounding, confirmed
// necessary via a direct oracle probe: 123456.5 prints "123456", not
// "123457" -- a real tie broken toward the EVEN digit, not simple
// round-half-up), trailing zeros stripped, switching to scientific
// notation when the real decimal exponent is < -4 or >= 6 (the same
// %g threshold printf/ostream both use). Real hardware SSE4.1
// `roundsd` gives correctly-rounded truncation/round-to-nearest-even
// directly, no software bignum/dtoa library needed at all.
//
// Verified against a real C++ reimplementation of Value::to_string()
// itself (not just hand-picked cases) across tens of thousands of
// randomized values spanning magnitudes from 1e-20 to 1e20 plus known
// round-half-to-even tie cases -- zero mismatches within the oracle's
// own well-defined range. Real, disclosed, bounded divergence: for a
// whole-number-valued double >= 2^63 (~9.22e18), the oracle's OWN
// `static_cast<long long>(value)` is genuine undefined behavior in C++
// (the value does not fit in a `long long` at all) -- confirmed
// directly (the same stress test that verified everything else shows
// the oracle producing platform/compiler-dependent garbage there, e.g.
// INT64_MIN or a wrong digit string) -- this implementation instead
// produces a real, well-defined 6-significant-digit scientific-notation
// result for those magnitudes rather than replicating undefined
// behavior, the same "do not chase a genuinely broken oracle result"
// discipline already applied to StringToHex/HexToString/NUMBER("abc")
// elsewhere in this file. No real ArcoBASIC program reaches this
// magnitude without exponent-notation literals, which this whole
// language's own lexer does not even tokenize as a number.
const char* arco_host_format_double(double value) {
    char buf[64];
    int pos = 0;
    int negative = value < 0;
    double v = negative ? arco_raw_negate(value) : value;
    double zero = arco_raw_int_to_double(0);
    double one = arco_raw_int_to_double(1);

    // 2^63, built via 63 real EXACT doublings (multiplying by 2 never
    // rounds in IEEE-754) -- the real boundary where
    // `static_cast<long long>` stops being well-defined, see this
    // function's own header comment.
    double two63 = one;
    int di;
    for (di = 0; di < 63; di = di + 1) two63 = two63 + two63;

    if (arco_raw_trunc(v) == v && v < two63) {
        long long whole = (long long)v;
        char tmp[24];
        int tpos = 24;
        if (whole == 0) {
            tmp[--tpos] = '0';
        } else {
            while (whole != 0) {
                tmp[--tpos] = (char)('0' + (whole % 10));
                whole = whole / 10;
            }
        }
        if (negative) buf[pos++] = '-';
        while (tpos < 24) buf[pos++] = tmp[tpos++];
        buf[pos] = 0;
        char* result = (char*)arco_raw_mmap((arco_u64)(pos + 1));
        int i = 0;
        while (i <= pos) { result[i] = buf[i]; i = i + 1; }
        return result;
    }

    // Real powers of ten (10^1, 10^2, 10^4, ..., 10^256), each built by
    // one real EXACT-as-possible squaring of the previous -- minimizes
    // both the number of runtime rounding operations (versus a
    // factor-of-10-at-a-time normalization loop, which could accumulate
    // real error across hundreds of steps for extreme magnitudes) AND
    // avoids embedding nine separate floating-point literal constants
    // (each its own real rodata/relocation problem, see this function's
    // own header comment).
    // `pow10Exp[i]` (each real exponent this table's own doubling
    // reaches: 1, 2, 4, ..., 256) is deliberately NOT a second array --
    // a real, DIRECT confirmed regression: a plain `int[9] = {1, 2, 4,
    // ...}` local array with a compile-time-constant initializer list
    // got compiled (by clang, though NOT by gcc -- the same real
    // cross-compiler divergence arco_raw_int_to_double's own comment
    // already found) into a genuine static table IN `.rodata`, indexed
    // via an absolute (not RIP-relative) address -- the exact same
    // relocation problem, reached a completely different way. `1 << i`
    // is a real, plain SHIFT instruction, impossible to place in
    // `.rodata` at all.
    double pow10[9];
    pow10[0] = arco_raw_int_to_double(10);
    int pi;
    for (pi = 1; pi < 9; pi = pi + 1) pow10[pi] = pow10[pi - 1] * pow10[pi - 1];

    int exp10 = 0;
    int i;
    for (i = 8; i >= 0; i = i - 1) {
        while (v >= pow10[i]) {
            v = v / pow10[i];
            exp10 = exp10 + (1 << i);
        }
    }
    for (i = 8; i >= 0 && v > zero; i = i - 1) {
        while (v < one && v * pow10[i] < pow10[0]) {
            v = v * pow10[i];
            exp10 = exp10 - (1 << i);
        }
    }

    // v is now in [1, 10) (up to real floating-point slack); scale to a
    // real 6-significant-digit integer with correct round-half-to-even.
    // 10^5 = pow10[2] (10^4) * pow10[0] (10^1), reusing the SAME table
    // above rather than a tenth floating-point constant.
    double hundredThousand = pow10[2] * pow10[0];
    double scaled = arco_raw_round_even(v * hundredThousand);
    long long digits = (long long)scaled;
    if (digits >= 1000000) {
        digits = digits / 10;
        exp10 = exp10 + 1;
    }
    if (digits < 100000) {
        digits = digits * 10;
        exp10 = exp10 - 1;
    }

    char digitChars[6];
    long long d = digits;
    for (i = 5; i >= 0; i = i - 1) {
        digitChars[i] = (char)('0' + (d % 10));
        d = d / 10;
    }

    int useScientific = exp10 < -4 || exp10 >= 6;
    if (negative) buf[pos++] = '-';
    if (useScientific) {
        buf[pos++] = digitChars[0];
        int fracEnd = 6;
        while (fracEnd > 1 && digitChars[fracEnd - 1] == '0') fracEnd = fracEnd - 1;
        if (fracEnd > 1) {
            buf[pos++] = '.';
            for (i = 1; i < fracEnd; i = i + 1) buf[pos++] = digitChars[i];
        }
        buf[pos++] = 'e';
        int e = exp10;
        if (e < 0) { buf[pos++] = '-'; e = -e; } else { buf[pos++] = '+'; }
        char expDigits[8];
        int epos = 8;
        if (e == 0) {
            expDigits[--epos] = '0';
        } else {
            while (e != 0) { expDigits[--epos] = (char)('0' + (e % 10)); e = e / 10; }
        }
        int digitCount = 8 - epos;
        int padding = digitCount < 2 ? 2 - digitCount : 0;
        while (padding > 0) { buf[pos++] = '0'; padding = padding - 1; }
        while (epos < 8) buf[pos++] = expDigits[epos++];
    } else if (exp10 >= 0) {
        int intDigits = exp10 + 1;
        for (i = 0; i < intDigits; i = i + 1) buf[pos++] = digitChars[i];
        int fracEnd = 6;
        while (fracEnd > intDigits && digitChars[fracEnd - 1] == '0') fracEnd = fracEnd - 1;
        if (fracEnd > intDigits) {
            buf[pos++] = '.';
            for (i = intDigits; i < fracEnd; i = i + 1) buf[pos++] = digitChars[i];
        }
    } else {
        buf[pos++] = '0';
        int fracEnd = 6;
        while (fracEnd > 0 && digitChars[fracEnd - 1] == '0') fracEnd = fracEnd - 1;
        if (fracEnd > 0) {
            buf[pos++] = '.';
            int leadingZeros = -exp10 - 1;
            for (i = 0; i < leadingZeros; i = i + 1) buf[pos++] = '0';
            for (i = 0; i < fracEnd; i = i + 1) buf[pos++] = digitChars[i];
        }
    }
    buf[pos] = 0;
    char* result = (char*)arco_raw_mmap((arco_u64)(pos + 1));
    int j = 0;
    while (j <= pos) { result[j] = buf[j]; j = j + 1; }
    return result;
}


// --- Batch 6: finishing real floating-point support -- real fmod (for
// Float-kind MOD) and Random.Float (PCG32's own real unit_interval()
// algorithm, now expressible since real double arithmetic exists).
// Bit.*/SHIFT/SETBIT/etc. accepting a Float-kind argument needs NO new
// C code at all: the oracle's own value_to_int() is a plain
// `static_cast<long long>(value.as_number())` (confirmed directly in
// runtime.cpp) -- a real TRUNCATION, done entirely on the ArcoBASIC/
// codegen side via a real `cvttsd2si` before calling these SAME
// existing integer host functions unchanged (see
// fission/amir/lower_x86_64.abas's own comment on this).

static inline __attribute__((always_inline)) double arco_raw_pow2(int n) {
    double result = arco_raw_int_to_double(1);
    int i;
    for (i = 0; i < n; i = i + 1) result = result + result;
    return result;
}

// MOD -- real floating-point remainder, matching the oracle's own
// `std::fmod(left, divisor)` exactly (confirmed directly in
// src/compiler/fission.cpp's own comment: real fmod, NOT a truncating-
// integer modulo): `a - trunc(a / b) * b`, the standard real
// implementation fmod itself uses, real hardware `roundsd` truncation
// (via arco_raw_trunc, already defined above for
// arco_host_format_double) giving a correctly-rounded result -- no
// software bignum needed, this is the SysV (double, double) -> double
// signature already exactly matching XMM0/XMM1 in, XMM0 out, so this
// needs no special calling-convention handling anywhere.
double arco_host_fmod(double a, double b) {
    double q = arco_raw_trunc(a / b);
    return a - q * b;
}

// Random.Float(handle) -- PCG32's own real `unit_interval()` algorithm,
// ported byte-for-byte from src/runtime/random.cpp (confirmed identical
// via a direct probe against the oracle: seed 42 produces
// 0.6303102186/0.7270080560 on both). 2^26/2^53 are built the same
// "no floating-point literal, no static rodata" way every other real
// double constant in this file already is (see arco_raw_int_to_double's
// own comment for why) -- both are EXACT powers of two, so real
// doubling from 1.0 introduces zero rounding error either way.
double arco_host_random_float(arco_pcg32_state* handle) {
    unsigned int highRaw = arco_pcg32_next(handle) >> 5;
    unsigned int lowRaw = arco_pcg32_next(handle) >> 6;
    double high = arco_raw_int_to_double((long)highRaw);
    double low = arco_raw_int_to_double((long)lowRaw);
    double scale = arco_raw_pow2(26);
    double divisor = arco_raw_pow2(53);
    return (high * scale + low) / divisor;
}

// --- Batch 7: real Array.* -- the READ-ONLY / SAME-SIZE subset only.
//
// Real, disclosed scope decision: Array.Push/Pop/Shift/Unshift/Insert/
// RemoveAt/Remove/Clear/Resize/Extend are NOT implemented here -- every
// one of them needs the array to genuinely CHANGE LENGTH in place, real
// reference semantics (confirmed directly in the oracle: `array_push_
// function` takes `args[0]` BY VALUE but calls `.as_array()` on it,
// mutating the SAME underlying shared `Value::Array` every other alias
// of that array ALSO sees, then returns the NEW SIZE, not the array --
// a real, true "shared mutable object" semantic, not a "returns a new
// array, caller reassigns" one). This backend's own current array
// representation (`array_alloc`, fission/amir/lower_x86_64.abas's own
// comment) is a SINGLE fixed-size bump-allocated block -- `[length]
// [elem0]...[elemN-1]`, no spare capacity, no separate growable data
// pointer -- so a real Push that needs to grow past its own original
// allocation would have to move to a new address, silently breaking
// every OTHER alias of that same array (a field, another variable, a
// nested structure) still pointing at the old one. Supporting real
// growable arrays correctly needs a genuine representation change (a
// small, STABLE header holding {length, capacity, dataPointer}, so only
// the data pointer ever needs to move) -- a real, disclosed, larger,
// cross-cutting undertaking (touches Index/StoreIndex/LEN/ForEach/every
// existing array-consuming code path), deliberately not attempted here.
// Everything below either only READS an existing array, or builds a
// brand-NEW array of a size known once at the start of the call (never
// growing an EXISTING one), so none of it needs that redesign.
//
// Real, disclosed narrower scope for Find/Contains/Join specifically:
// Number and String element kinds only (matching every other Number-
// vs-String-kind-dispatched host function already in this file) --
// dispatched at COMPILE TIME by fission/amir/lower_x86_64.abas's own
// special-case handling (the array's own element kind is always known
// statically), the same real "which concrete symbol to call" pattern
// Random.Choice's own element-kind-agnostic design note already
// documents; a Float-kind array is a real, disclosed, narrower
// remaining gap for these three specifically.

// Array.First/Array.Last -- a raw 8-byte slot, verbatim (works
// identically for a Number, String, or Float-kind array -- the real
// result KIND is resolved at compile time by the caller, same as
// Random.Choice). 0 for an empty array: a real, disclosed simplification
// (this backend has no NULL/empty-Value runtime representation of its
// own yet), distinguishable from a genuine zero-valued element only via
// a real `LEN(arr) == 0` check first, matching how every other "no real
// exception mechanism yet" gap in this file is already handled.
arco_i64 arco_host_array_first(const arco_i64* arr) {
    arco_i64 count = arr[0];
    if (count == 0) return 0;
    return arr[1];
}
arco_i64 arco_host_array_last(const arco_i64* arr) {
    arco_i64 count = arr[0];
    if (count == 0) return 0;
    return arr[count];
}

// Array.Find -- real linear scan, INDEX of the first match or -1 if
// none, matching the oracle's own `array_find_function` exactly.
arco_i64 arco_host_array_find_number(const arco_i64* arr, arco_i64 value) {
    arco_i64 count = arr[0];
    arco_i64 i = 0;
    while (i < count) {
        if (arr[1 + i] == value) return i;
        i = i + 1;
    }
    return -1;
}
arco_i64 arco_host_array_find_string(const arco_i64* arr, const char* value) {
    arco_i64 count = arr[0];
    arco_i64 i = 0;
    while (i < count) {
        if (arco_raw_strings_equal((const char*)arr[1 + i], value)) return i;
        i = i + 1;
    }
    return -1;
}

// Array.Contains -- real linear scan, a real boolean 0/1, matching the
// oracle's own `array_contains_function` exactly.
arco_i64 arco_host_array_contains_number(const arco_i64* arr, arco_i64 value) {
    return arco_host_array_find_number(arr, value) >= 0 ? 1 : 0;
}
arco_i64 arco_host_array_contains_string(const arco_i64* arr, const char* value) {
    return arco_host_array_find_string(arr, value) >= 0 ? 1 : 0;
}

// Array.Reverse -- a real NEW array (the oracle's own `array_reverse_
// function` copies `args[0]` into a fresh `Value::Array` before
// reversing, confirmed directly -- it never mutates the caller's own
// array), raw slot copy (kind-agnostic, same as Random.Shuffle).
const void* arco_host_array_reverse(const arco_i64* arr) {
    arco_i64 count = arr[0];
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 i = 0;
    while (i < count) {
        result[1 + i] = arr[count - i];
        i = i + 1;
    }
    return (const void*)result;
}

// Array.Sort -- a real NEW array (the oracle's own `array_sort_function`
// copies `args[0]` into a fresh `Value::Array` before sorting, confirmed
// directly in its own body -- `Value::Array result = args[0].as_array();`
// -- it never mutates the caller's own array), ascending order, matching
// the oracle's own comparator exactly per element kind (Number sorted
// numerically, String sorted by ordinary byte-wise ordering, the SAME
// `std::string::operator<` semantics `arco_raw_strings_less_than` above
// implements). A real, hand-rolled insertion sort -- this file is built
// `-nostdlib` (no `qsort` available), and every fixture this backend's
// own test suite exercises is small enough that the simpler O(n^2)
// algorithm is the right call (this file's own established "simple over
// clever" discipline elsewhere). Not required to be stable (the oracle's
// own `std::sort` isn't either), so plain insertion sort's own natural
// stability is a bonus, never a requirement.
const void* arco_host_array_sort_number(const arco_i64* arr) {
    arco_i64 count = arr[0];
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 i = 0;
    while (i < count) { result[1 + i] = arr[1 + i]; i = i + 1; }
    i = 1;
    while (i < count) {
        arco_i64 key = result[1 + i];
        arco_i64 j = i - 1;
        while (j >= 0 && result[1 + j] > key) {
            result[1 + j + 1] = result[1 + j];
            j = j - 1;
        }
        result[1 + j + 1] = key;
        i = i + 1;
    }
    return (const void*)result;
}
const void* arco_host_array_sort_string(const arco_i64* arr) {
    arco_i64 count = arr[0];
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 i = 0;
    while (i < count) { result[1 + i] = arr[1 + i]; i = i + 1; }
    i = 1;
    while (i < count) {
        arco_i64 key = result[1 + i];
        arco_i64 j = i - 1;
        while (j >= 0 && arco_raw_strings_less_than((const char*)key, (const char*)result[1 + j])) {
            result[1 + j + 1] = result[1 + j];
            j = j - 1;
        }
        result[1 + j + 1] = key;
        i = i + 1;
    }
    return (const void*)result;
}

// Array.Join/String.Join -- confirmed byte-for-byte IDENTICAL logic in
// the oracle (`array_join_function`/`string_join_function` are two
// separate C++ functions with the exact same body, just different
// variable names) -- one real implementation per element kind serves
// both real ArcoBASIC names (fission/amir/lower_x86_64.abas's own
// dispatch registers both `ARRAY.JOIN` and `STRING.JOIN` against these
// SAME two symbols). Real two-pass allocate: pass 1 computes the exact
// total byte length (calling the SAME per-element formatter twice per
// element is simpler and, given this backend never frees anything it
// allocates anyway, no real cost worth avoiding -- matching this
// file's own established "simple over clever" discipline elsewhere),
// pass 2 fills the single real mmap'd result. Number-kind elements
// reuse `arco_host_number_to_string` (this backend's Number
// representation is integer-only, so this is real, complete coverage,
// same disclosed scope as String(n) above); a Float-kind array needs
// `arco_host_format_double` instead, a real, disclosed, narrower
// remaining gap for Join specifically (not yet dispatched to).
const char* arco_host_array_join_number(const arco_i64* arr, const char* sep) {
    arco_i64 count = arr[0];
    arco_i64 sepLen = arco_raw_strlen(sep);
    arco_i64 totalLen = 0;
    arco_i64 i = 0;
    while (i < count) {
        totalLen = totalLen + arco_raw_strlen(arco_host_number_to_string(arr[1 + i]));
        if (i != 0) totalLen = totalLen + sepLen;
        i = i + 1;
    }
    char* result = (char*)arco_raw_mmap((arco_u64)(totalLen + 1));
    arco_i64 pos = 0;
    i = 0;
    while (i < count) {
        if (i != 0) {
            arco_i64 j = 0;
            while (j < sepLen) { result[pos] = sep[j]; pos = pos + 1; j = j + 1; }
        }
        const char* piece = arco_host_number_to_string(arr[1 + i]);
        arco_i64 pieceLen = arco_raw_strlen(piece);
        arco_i64 j = 0;
        while (j < pieceLen) { result[pos] = piece[j]; pos = pos + 1; j = j + 1; }
        i = i + 1;
    }
    result[pos] = 0;
    return result;
}
const char* arco_host_array_join_string(const arco_i64* arr, const char* sep) {
    arco_i64 count = arr[0];
    arco_i64 sepLen = arco_raw_strlen(sep);
    arco_i64 totalLen = 0;
    arco_i64 i = 0;
    while (i < count) {
        totalLen = totalLen + arco_raw_strlen((const char*)arr[1 + i]);
        if (i != 0) totalLen = totalLen + sepLen;
        i = i + 1;
    }
    char* result = (char*)arco_raw_mmap((arco_u64)(totalLen + 1));
    arco_i64 pos = 0;
    i = 0;
    while (i < count) {
        if (i != 0) {
            arco_i64 j = 0;
            while (j < sepLen) { result[pos] = sep[j]; pos = pos + 1; j = j + 1; }
        }
        const char* piece = (const char*)arr[1 + i];
        arco_i64 pieceLen = arco_raw_strlen(piece);
        arco_i64 j = 0;
        while (j < pieceLen) { result[pos] = piece[j]; pos = pos + 1; j = j + 1; }
        i = i + 1;
    }
    result[pos] = 0;
    return result;
}

// Real byte-for-byte match test at a fixed position -- shared by
// String.Split's own two passes (count delimiter occurrences, then
// extract the pieces between them) below.
static inline __attribute__((always_inline)) int arco_raw_match_at(const char* text, arco_i64 textLen, arco_i64 pos, const char* needle, arco_i64 needleLen) {
    if (pos + needleLen > textLen) return 0;
    arco_i64 j = 0;
    while (j < needleLen) {
        if (text[pos + j] != needle[j]) return 0;
        j = j + 1;
    }
    return 1;
}
static inline __attribute__((always_inline)) char* arco_raw_copy_substring(const char* text, arco_i64 start, arco_i64 len) {
    char* piece = (char*)arco_raw_mmap((arco_u64)(len + 1));
    arco_i64 k = 0;
    while (k < len) { piece[k] = text[start + k]; k = k + 1; }
    piece[len] = 0;
    return piece;
}

// String.Split(text, delimiter) -- real, matching the oracle's own
// `string_split_function` exactly (a real, empty-delimiter diagnostic is
// the oracle's own behavior too -- not validated here, matching this
// whole file's own "no exception mechanism yet" gap -- an empty
// delimiter here would just loop forever finding a zero-width match at
// every position, a real, disclosed divergence, not silently wrong in a
// way that could be confused for a correct empty-delimiter semantic,
// since the oracle has none either).
const void* arco_host_string_split(const char* text, const char* delimiter) {
    arco_i64 textLen = arco_raw_strlen(text);
    arco_i64 delimLen = arco_raw_strlen(delimiter);
    arco_i64 count = 1;
    arco_i64 i = 0;
    while (i < textLen) {
        if (arco_raw_match_at(text, textLen, i, delimiter, delimLen)) {
            count = count + 1;
            i = i + delimLen;
        } else {
            i = i + 1;
        }
    }
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 partIndex = 0;
    arco_i64 partStart = 0;
    i = 0;
    while (i < textLen) {
        if (arco_raw_match_at(text, textLen, i, delimiter, delimLen)) {
            result[1 + partIndex] = (arco_i64)(long)arco_raw_copy_substring(text, partStart, i - partStart);
            partIndex = partIndex + 1;
            i = i + delimLen;
            partStart = i;
        } else {
            i = i + 1;
        }
    }
    result[1 + partIndex] = (arco_i64)(long)arco_raw_copy_substring(text, partStart, textLen - partStart);
    return (const void*)result;
}

// String.ToChars(text) -- real, matching the oracle's own
// `string_to_chars_function` exactly: one real UTF-8-codepoint-aware
// pass (a continuation byte is `10xxxxxx`, `(byte & 0xc0) == 0x80`),
// never splitting a multi-byte codepoint across two array elements.
const void* arco_host_string_to_chars(const char* text) {
    arco_i64 textLen = arco_raw_strlen(text);
    arco_i64 count = 0;
    arco_i64 i = 0;
    while (i < textLen) {
        i = i + 1;
        while (i < textLen && (((unsigned char)text[i]) & 0xc0) == 0x80) i = i + 1;
        count = count + 1;
    }
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 idx = 0;
    i = 0;
    while (i < textLen) {
        arco_i64 start = i;
        i = i + 1;
        while (i < textLen && (((unsigned char)text[i]) & 0xc0) == 0x80) i = i + 1;
        result[1 + idx] = (arco_i64)(long)arco_raw_copy_substring(text, start, i - start);
        idx = idx + 1;
    }
    return (const void*)result;
}

// String.Lines(text) -- real, matching the oracle's own
// `string_lines_function` exactly: splits on `\n`, and a trailing `\r`
// immediately before it (real Windows-style `\r\n` line endings) is
// stripped, matching `std::getline` + the oracle's own explicit
// `if (!line.empty() && line.back() == '\r') line.pop_back();`. A
// string with NO trailing newline still yields its own final line (the
// oracle's own `std::getline` loop condition already does this
// naturally); a string ending WITH one does not produce a spurious
// trailing empty line either, matching `std::getline`'s own real
// behavior exactly (confirmed via a direct oracle probe).
const void* arco_host_string_lines(const char* text) {
    arco_i64 textLen = arco_raw_strlen(text);
    arco_i64 count = 0;
    arco_i64 i = 0;
    while (i < textLen) {
        arco_i64 start = i;
        while (i < textLen && text[i] != '\n') i = i + 1;
        arco_i64 end = i;
        if (end > start && text[end - 1] == '\r') end = end - 1;
        count = count + 1;
        if (i < textLen) i = i + 1;
    }
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 idx = 0;
    i = 0;
    while (i < textLen) {
        arco_i64 start = i;
        while (i < textLen && text[i] != '\n') i = i + 1;
        arco_i64 end = i;
        if (end > start && text[end - 1] == '\r') end = end - 1;
        result[1 + idx] = (arco_i64)(long)arco_raw_copy_substring(text, start, end - start);
        idx = idx + 1;
        if (i < textLen) i = i + 1;
    }
    return (const void*)result;
}

// --- Batch 8: real Bytes.* -- a "Bytes" value is, confirmed directly in
// the oracle, just an ordinary `Value::Array` of Numbers each clamped
// into [0, 255] -- no separate runtime representation of its own at all
// (`Bytes.New`'s own C++ body literally constructs a plain
// `Value::Array`). So this backend's own existing Number-kind array
// representation already IS a real Bytes value -- these five functions
// are the only real gap. Bytes.SetU8 is a real, disclosed exception to
// this whole file's own "no array mutation" scope line (see Batch 7's
// own header comment): it MUTATES one EXISTING element in place, never
// changing the array's own LENGTH, so it needs none of the growable-
// array representation change real Push/Pop/etc. would -- confirmed
// directly in the oracle: `Bytes.SetU8` takes `args[0]` BY VALUE but
// mutates the array it holds a reference to, then returns that SAME
// array back (not a new size the way Array.Push does).
const void* arco_host_bytes_new(arco_i64 size, arco_i64 fill) {
    arco_i64 clampedFill = fill < 0 ? 0 : (fill > 255 ? 255 : fill);
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((size + 1) * 8));
    result[0] = size;
    arco_i64 i = 0;
    while (i < size) { result[1 + i] = clampedFill; i = i + 1; }
    return (const void*)result;
}
// Real, disclosed simplification: an out-of-range index returns 0
// (GetU8) or is silently ignored (SetU8) -- this backend's own existing
// plain `arr[i]` indexing is ALSO unchecked (no runtime bounds
// validation anywhere in this native backend yet, a real, separate,
// pre-existing, disclosed gap), so this matches that same established
// behavior rather than inventing a new, inconsistent exception-like
// mechanism just for Bytes.*.
arco_i64 arco_host_bytes_get_u8(const arco_i64* arr, arco_i64 index) {
    arco_i64 count = arr[0];
    if (index < 0 || index >= count) return 0;
    arco_i64 value = arr[1 + index];
    return value < 0 ? 0 : (value > 255 ? 255 : value);
}
const void* arco_host_bytes_set_u8(arco_i64* arr, arco_i64 index, arco_i64 value) {
    arco_i64 count = arr[0];
    if (index >= 0 && index < count) {
        arr[1 + index] = value < 0 ? 0 : (value > 255 ? 255 : value);
    }
    return (const void*)arr;
}
// Bytes.FromText/Bytes.ToText -- real RAW BYTES (confirmed directly in
// the oracle: `bytes_from_string`/`string_from_bytes` iterate `unsigned
// char` values, never UTF-8 codepoints -- a real, deliberate difference
// from String.ToChars above), matching exactly.
const void* arco_host_bytes_from_text(const char* text) {
    arco_i64 len = arco_raw_strlen(text);
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((len + 1) * 8));
    result[0] = len;
    arco_i64 i = 0;
    while (i < len) { result[1 + i] = (arco_i64)(unsigned char)text[i]; i = i + 1; }
    return (const void*)result;
}
const char* arco_host_bytes_to_text(const arco_i64* arr) {
    arco_i64 count = arr[0];
    char* result = (char*)arco_raw_mmap((arco_u64)(count + 1));
    arco_i64 i = 0;
    while (i < count) {
        arco_i64 value = arr[1 + i];
        result[i] = (char)(unsigned char)(value < 0 ? 0 : (value > 255 ? 255 : value));
        i = i + 1;
    }
    result[count] = 0;
    return result;
}

// --- Batch 9: real Path.Join -- confirmed directly against the oracle
// (real `std::filesystem::path::operator/=` semantics): if the RIGHT
// side is an ABSOLUTE path (starts with '/'), the result is just that
// right side, verbatim -- the left side is discarded entirely; otherwise
// the two are concatenated with EXACTLY one '/' between them (no doubled
// slash if the left side already ends with one, no separator at all if
// the left side is empty). Real, disclosed, narrower scope: only the
// real 2-argument form (`std::filesystem::path::operator/=` is real,
// variadic -- any number of additional segments), matching the
// established fixed-arity precedent this backend already uses for other
// genuinely variadic oracle functions. Path.Home is NOT implemented here
// -- it needs the real process ENVP block (`getenv("HOME")`), and this
// backend's own `_start` entry point does not capture argv/envp from the
// stack at all yet (a real, separate, disclosed, larger undertaking --
// touches every program's own prologue, not a one-function fix).
const char* arco_host_path_join(const char* a, const char* b) {
    if (b[0] == '/') {
        arco_i64 bLen = arco_raw_strlen(b);
        return arco_raw_copy_substring(b, 0, bLen);
    }
    arco_i64 aLen = arco_raw_strlen(a);
    int needsSep = aLen > 0 && a[aLen - 1] != '/';
    arco_i64 bLen = arco_raw_strlen(b);
    arco_i64 totalLen = aLen + (needsSep ? 1 : 0) + bLen;
    char* result = (char*)arco_raw_mmap((arco_u64)(totalLen + 1));
    arco_i64 pos = 0;
    arco_i64 i = 0;
    while (i < aLen) { result[pos] = a[i]; pos = pos + 1; i = i + 1; }
    if (needsSep) { result[pos] = '/'; pos = pos + 1; }
    i = 0;
    while (i < bLen) { result[pos] = b[i]; pos = pos + 1; i = i + 1; }
    result[pos] = 0;
    return result;
}

// --- Batch 10: real Array.Empty/Array.New, RANGE, and ISNULL/EXIT (the
// latter two implemented as real NATIVE inline dispatch in
// fission/amir/lower_x86_64.abas instead, needing no C code at all --
// see that file's own comment).
//
// RANGE(start, stop, step) -- the oracle's own real RANGE returns a
// genuinely DISTINCT lazy `RangeValue` (confirmed directly in
// include/arco/value.hpp: `{start, stop, step, length}`, never
// materialized into a real array unless iterated), so `PRINT
// Range(1, 5)` shows literal text `Range(1, 5)`, not `[1, 2, 3, 4]` --
// this backend has no lazy-range Value kind of its own, so this
// implementation materializes a real Array:Number of every value the
// real range would iterate instead (matching real `array_length` exactly,
// confirmed directly against include/arco/value.hpp's own
// `range_length`) -- a real, disclosed, narrower divergence: `FOR x IN
// Range(...)`/`LEN(Range(...))`/indexing all behave identically to the
// oracle, but `PRINT`ing a bare Range value directly does not. A zero
// step (the oracle's own real exception case, "no exception mechanism
// yet" throughout this file) produces a real, safe, EMPTY array here
// instead of crashing.
const void* arco_host_range(arco_i64 start, arco_i64 stop, arco_i64 step) {
    arco_i64 count = 0;
    if (step > 0 && start < stop) {
        arco_i64 distance = stop - start;
        count = (distance + step - 1) / step;
    } else if (step < 0 && start > stop) {
        arco_i64 distance = start - stop;
        arco_i64 stride = 0 - step;
        count = (distance + stride - 1) / stride;
    }
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 i = 0;
    arco_i64 value = start;
    while (i < count) {
        result[1 + i] = value;
        value = value + step;
        i = i + 1;
    }
    return (const void*)result;
}

// Array.Empty/Array.IsEmpty -- a real boolean 0/1, matching the oracle's
// own `array_empty_function` exactly.
arco_i64 arco_host_array_empty(const arco_i64* arr) {
    return arr[0] == 0 ? 1 : 0;
}

// Array.New(size, fill) -- real, matching the oracle's own
// `array_new_function` exactly (the real 0/1-argument forms -- an
// implicit empty array, or a `fill=NULL` default -- are a real,
// disclosed, narrower scope cut here, the same fixed-arity precedent
// Bytes.New already established). Raw slot fill, kind-agnostic (works
// identically for a Number, String, or Float-kind fill -- the real
// result KIND is resolved at compile time by the caller, same as
// Random.Choice/Array.First).
const void* arco_host_array_new(arco_i64 size, arco_i64 fill) {
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((size + 1) * 8));
    result[0] = size;
    arco_i64 i = 0;
    while (i < size) { result[1 + i] = fill; i = i + 1; }
    return (const void*)result;
}

// --- Batch 11: real HexToBytes -- the real inverse of
// `arco_host_bytes_to_hex`'s own already-existing hex-digit table, real
// matching the oracle's own `hex_to_bytes_function` exactly (a leading
// '0' is inserted for an odd-length input, matching a REAL, disclosed
// simplification: a genuinely INVALID hex digit character -- the
// oracle's own `std::stoll(..., 16)` real exception case, "no exception
// mechanism yet" throughout this file -- is treated as 0 here rather
// than crashing).
static inline __attribute__((always_inline)) int arco_raw_hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}
const void* arco_host_hex_to_bytes(const char* text) {
    arco_i64 len = arco_raw_strlen(text);
    int odd = (len % 2) != 0;
    arco_i64 count = (len + (odd ? 1 : 0)) / 2;
    arco_i64* result = (arco_i64*)arco_raw_mmap((arco_u64)((count + 1) * 8));
    result[0] = count;
    arco_i64 pos = 0;
    arco_i64 i = 0;
    if (odd) {
        result[1] = arco_raw_hex_value(text[0]);
        pos = 1;
        i = 1;
    }
    while (i < len) {
        int hi = arco_raw_hex_value(text[i]);
        int lo = arco_raw_hex_value(text[i + 1]);
        result[1 + pos] = (hi << 4) | lo;
        pos = pos + 1;
        i = i + 2;
    }
    return (const void*)result;
}
