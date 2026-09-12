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
