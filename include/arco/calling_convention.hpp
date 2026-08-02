#pragma once

#include <array>
#include <string>
#include <vector>

namespace arco::systems {

// Microsoft x64 calling convention (docs/systems/calling-conventions.md), the ABI required by
// x86-64 UEFI (docs/systems/uefi-target.md section 4). Scope is deliberately limited to
// integer/pointer-class arguments and returns -- no floating-point argument classification
// (XMM registers), matching this milestone's non-goals.

constexpr int kShadowSpaceBytes = 32;
constexpr int kStackAlignmentAtCallBytes = 16;
constexpr int kEntryRspMod16 = 8;  // RSP % 16 at function entry, after CALL pushes the return address

inline const std::array<std::string, 4>& integer_argument_registers() {
    static const std::array<std::string, 4> registers = {"RCX", "RDX", "R8", "R9"};
    return registers;
}

inline const std::string& integer_return_register() {
    static const std::string register_name = "RAX";
    return register_name;
}

inline const std::vector<std::string>& callee_saved_registers() {
    static const std::vector<std::string> registers = {
        "RBX", "RBP", "RDI", "RSI", "R12", "R13", "R14", "R15",
        "XMM6", "XMM7", "XMM8", "XMM9", "XMM10", "XMM11", "XMM12", "XMM13", "XMM14", "XMM15",
    };
    return registers;
}

inline const std::vector<std::string>& caller_saved_registers() {
    static const std::vector<std::string> registers = {
        "RAX", "RCX", "RDX", "R8", "R9", "R10", "R11",
        "XMM0", "XMM1", "XMM2", "XMM3", "XMM4", "XMM5",
    };
    return registers;
}

struct ArgumentLocation {
    bool in_register = false;
    std::string register_name;   // valid when in_register
    int stack_offset_bytes = 0;  // valid when !in_register: offset from RSP at function entry
};

// Assigns each of `argument_count` integer/pointer-class arguments (by position, 0-based) a
// location per the Microsoft x64 convention. Argument types are not considered: this milestone
// has no floating-point argument classification, so every argument in scope uses the same
// integer/pointer register sequence, regardless of its fixed-width type.
inline std::vector<ArgumentLocation> assign_argument_locations(int argument_count) {
    std::vector<ArgumentLocation> locations;
    if (argument_count <= 0) {
        return locations;
    }
    locations.reserve(static_cast<std::size_t>(argument_count));
    const auto& registers = integer_argument_registers();
    const int register_count = static_cast<int>(registers.size());
    for (int position = 0; position < argument_count; ++position) {
        ArgumentLocation location;
        if (position < register_count) {
            location.in_register = true;
            location.register_name = registers[static_cast<std::size_t>(position)];
        } else {
            location.in_register = false;
            location.stack_offset_bytes = kShadowSpaceBytes + 8 + 8 * (position - register_count);
        }
        locations.push_back(location);
    }
    return locations;
}

} // namespace arco::systems
