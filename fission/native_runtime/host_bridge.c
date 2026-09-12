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
