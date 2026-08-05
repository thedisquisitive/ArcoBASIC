#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace arco::systems {

// Fixed-width systems type metadata, per arcology-os/docs/systems/uefi-target.md section 3.
// Bounds are stored as an unsigned magnitude plus a sign flag so that
// exact-value range checks never round-trip through a floating-point value:
// U64's maximum (2^64 - 1) and I64's minimum magnitude (2^63) both exceed
// what a 53-bit double mantissa can represent exactly.
struct FixedWidthType {
    std::string name;
    int size_bytes = 0;
    int alignment_bytes = 0;
    bool is_signed = false;
    bool is_bool = false;
    bool is_pointer = false;
    // For signed types: valid magnitudes are [0, max_magnitude] when
    // negated, [0, positive_max] when positive. For unsigned types:
    // valid values are [0, positive_max].
    std::uint64_t positive_max = 0;
    std::uint64_t negative_magnitude_max = 0;
};

inline std::optional<FixedWidthType> lookup_fixed_width_type(const std::string& name) {
    if (name == "U8") return FixedWidthType{"U8", 1, 1, false, false, false, 255ULL, 0ULL};
    if (name == "U16") return FixedWidthType{"U16", 2, 2, false, false, false, 65535ULL, 0ULL};
    if (name == "U32") return FixedWidthType{"U32", 4, 4, false, false, false, 4294967295ULL, 0ULL};
    if (name == "U64") return FixedWidthType{"U64", 8, 8, false, false, false, 18446744073709551615ULL, 0ULL};
    if (name == "I8") return FixedWidthType{"I8", 1, 1, true, false, false, 127ULL, 128ULL};
    if (name == "I16") return FixedWidthType{"I16", 2, 2, true, false, false, 32767ULL, 32768ULL};
    if (name == "I32") return FixedWidthType{"I32", 4, 4, true, false, false, 2147483647ULL, 2147483648ULL};
    if (name == "I64") return FixedWidthType{"I64", 8, 8, true, false, false, 9223372036854775807ULL, 9223372036854775808ULL};
    if (name == "BOOL") return FixedWidthType{"BOOL", 1, 1, false, true, false, 1ULL, 0ULL};
    if (name == "PTR") return FixedWidthType{"PTR", 8, 8, false, false, true, 0ULL, 0ULL};
    return std::nullopt;
}

// Parses an unsigned decimal digit string exactly (no double round-trip).
// Returns nullopt if the text is not all digits or overflows 64 bits.
inline std::optional<std::uint64_t> parse_u64_decimal_exact(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t result = 0;
    for (char c : text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const unsigned digit = static_cast<unsigned>(c - '0');
        if (result > (UINT64_MAX - digit) / 10ULL) {
            return std::nullopt;
        }
        result = result * 10ULL + digit;
    }
    return result;
}

} // namespace arco::systems
