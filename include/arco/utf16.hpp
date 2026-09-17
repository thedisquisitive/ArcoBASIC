#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace arco::systems {

// Encodes UTF-8 source text as null-terminated UTF-16 (arcology-os/docs/systems/utf16-encoding.md), the
// CHAR16* string encoding required by UEFI's EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL.OutputString (see
// arcology-os/docs/systems/uefi-bindings.md). Rejects malformed UTF-8 and embedded NUL bytes (Packet WP-007
// "reject unsupported source cases clearly"): an embedded NUL would silently truncate a
// null-terminated string, corrupting the visible output with no error at all if left unchecked.
inline std::vector<char16_t> encode_utf16_null_terminated(const std::string& utf8_text) {
    std::vector<char16_t> units;
    units.reserve(utf8_text.size() + 1);

    std::size_t i = 0;
    while (i < utf8_text.size()) {
        const unsigned char lead = static_cast<unsigned char>(utf8_text[i]);
        char32_t code_point = 0;
        std::size_t extra_bytes = 0;

        if (lead == 0x00) {
            throw std::runtime_error(
                "string contains an embedded NUL byte, which would silently truncate a null-terminated UTF-16 string");
        } else if ((lead & 0x80) == 0x00) {
            code_point = lead;
            extra_bytes = 0;
        } else if ((lead & 0xE0) == 0xC0) {
            code_point = lead & 0x1F;
            extra_bytes = 1;
        } else if ((lead & 0xF0) == 0xE0) {
            code_point = lead & 0x0F;
            extra_bytes = 2;
        } else if ((lead & 0xF8) == 0xF0) {
            code_point = lead & 0x07;
            extra_bytes = 3;
        } else {
            throw std::runtime_error("string contains a byte that is not valid UTF-8");
        }

        if (i + extra_bytes >= utf8_text.size()) {
            throw std::runtime_error("string contains a truncated UTF-8 sequence");
        }
        for (std::size_t k = 1; k <= extra_bytes; ++k) {
            const unsigned char continuation = static_cast<unsigned char>(utf8_text[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                throw std::runtime_error("string contains an invalid UTF-8 continuation byte");
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        i += extra_bytes + 1;

        if (code_point >= 0xD800 && code_point <= 0xDFFF) {
            throw std::runtime_error("string contains a UTF-8 encoding of a surrogate code point, which is not valid Unicode text");
        }
        if (code_point > 0x10FFFF) {
            throw std::runtime_error("string contains a code point outside the valid Unicode range");
        }

        if (code_point <= 0xFFFF) {
            units.push_back(static_cast<char16_t>(code_point));
        } else {
            const char32_t adjusted = code_point - 0x10000;
            units.push_back(static_cast<char16_t>(0xD800 + (adjusted >> 10)));
            units.push_back(static_cast<char16_t>(0xDC00 + (adjusted & 0x3FF)));
        }
    }

    units.push_back(u'\0');
    return units;
}

} // namespace arco::systems
