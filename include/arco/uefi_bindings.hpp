#pragma once

#include <optional>
#include <string>
#include <vector>

namespace arco::systems {

// The smallest safe UEFI text-output plus watchdog binding surface (Packets WP-006 and WP-023).
// Field offsets are computed
// for x86-64 natural alignment (8-byte pointers) from the TianoCore EDK2 reference implementation
// of the UEFI Specification -- MdePkg/Include/Uefi/UefiSpec.h (EFI_SYSTEM_TABLE),
// MdePkg/Include/Uefi/UefiMultiPhase.h (EFI_TABLE_HEADER), and
// MdePkg/Include/Protocol/SimpleTextOut.h (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, EFI_TEXT_STRING) --
// see docs/systems/uefi-bindings.md for the full field-by-field derivation and sources. This does
// not attempt to bind the rest of the UEFI specification: only the fields listed here exist.

struct UefiField {
    std::string arcobasic_name;    // the name ArcoBASIC source uses, e.g. "ConsoleOut"
    std::string uefi_name;         // the real UEFI Specification field name, e.g. "ConOut"
    int offset_bytes = 0;          // byte offset within the containing struct on x86-64
    std::string result_type;       // systems type this field resolves to, for further chaining
    bool is_method = false;        // true if calling this field invokes a function pointer
    bool implicit_this_argument = false;  // true if the real C signature takes "This" as arg 0
    std::string return_type;       // return type when is_method is true (documentation/WP-008 use)
};

struct UefiType {
    std::string name;              // e.g. "UEFI.SystemTable"
    int size_bytes = 0;            // sizeof() on x86-64 (documentation/future use, not enforced yet)
    std::vector<UefiField> fields;

    const UefiField* find_field(const std::string& arcobasic_name) const {
        for (const auto& field : fields) {
            if (field.arcobasic_name == arcobasic_name) {
                return &field;
            }
        }
        return nullptr;
    }
};

inline std::optional<UefiType> lookup_uefi_type(const std::string& name) {
    if (name == "UEFI.Handle") {
        return UefiType{"UEFI.Handle", 8, {}};
    }
    if (name == "UEFI.SystemTable") {
        return UefiType{
            "UEFI.SystemTable",
            120,
            {
                UefiField{"ConsoleOut", "ConOut", 0x40, "UEFI.SimpleTextOutputProtocol", false, false, ""},
                UefiField{"BootServices", "BootServices", 0x60, "UEFI.BootServices", false, false, ""},
            },
        };
    }
    if (name == "UEFI.BootServices") {
        return UefiType{
            "UEFI.BootServices",
            376,
            {
                // EFI_SET_WATCHDOG_TIMER is a service-table function, not a protocol method:
                // its four explicit parameters begin in RCX and there is no implicit This.
                UefiField{"SetWatchdogTimer", "SetWatchdogTimer", 0x100, "", true, false, "U64"},
            },
        };
    }
    if (name == "UEFI.SimpleTextOutputProtocol") {
        return UefiType{
            "UEFI.SimpleTextOutputProtocol",
            80,
            {
                UefiField{"Write", "OutputString", 0x08, "", true, true, "U64"},
            },
        };
    }
    return std::nullopt;
}

} // namespace arco::systems
