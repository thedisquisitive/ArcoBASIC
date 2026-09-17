#pragma once

#include <array>
#include <string>
#include <vector>

namespace arco::systems {

// Microsoft x64 calling convention (arcology-os/docs/systems/calling-conventions.md), the ABI required by
// x86-64 UEFI (arcology-os/docs/systems/uefi-target.md section 4). Scope is deliberately limited to
// integer/pointer-class arguments and returns -- no floating-point argument classification
// (XMM registers), matching this milestone's non-goals.
//
// System V AMD64 (the Linux/hosted ABI -- real gcc/clang/ld all assume it, so any generated code calling
// into a normally-compiled C++ function, e.g. the native ArcoSH runtime shim, must honor it exactly) was
// added alongside it below rather than as a separate file: the two conventions differ only in which
// registers carry arguments and whether the caller reserves shadow space, not in the overall shape of
// "where does argument N live" -- see CallingConvention/assign_argument_locations below.

constexpr int kShadowSpaceBytes = 32;
constexpr int kStackAlignmentAtCallBytes = 16;
constexpr int kEntryRspMod16 = 8;  // RSP % 16 at function entry, after CALL pushes the return address

enum class CallingConvention {
    MicrosoftX64,  // UEFI and Windows: RCX/RDX/R8/R9, 32-byte caller-reserved shadow space
    SystemV,       // Linux/hosted: RDI/RSI/RDX/RCX/R8/R9, no shadow space
};

inline const std::array<std::string, 4>& integer_argument_registers() {
    static const std::array<std::string, 4> registers = {"RCX", "RDX", "R8", "R9"};
    return registers;
}

inline const std::array<std::string, 6>& sysv_integer_argument_registers() {
    static const std::array<std::string, 6> registers = {"RDI", "RSI", "RDX", "RCX", "R8", "R9"};
    return registers;
}

// Number of integer/pointer argument registers a convention has before it spills to the stack.
inline int argument_register_count(CallingConvention convention) {
    return convention == CallingConvention::MicrosoftX64
        ? static_cast<int>(integer_argument_registers().size())
        : static_cast<int>(sysv_integer_argument_registers().size());
}

// Bytes the CALLER must reserve below its outgoing arguments before the CALL instruction --
// Microsoft x64's mandatory 32-byte shadow space; System V has no equivalent requirement.
inline int shadow_space_bytes(CallingConvention convention) {
    return convention == CallingConvention::MicrosoftX64 ? kShadowSpaceBytes : 0;
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

// Convention-aware overload -- Microsoft x64 delegates to the function above (kept so existing UEFI
// callers that never pass a convention keep compiling and behaving identically); System V uses its own
// six-register sequence and has no shadow space to add before the first stack-spilled argument (the
// caller writes stack arguments starting right where RSP points at the CALL instruction; System V's
// `stack_offset_bytes` is expressed the same way as Microsoft x64's -- relative to RSP at the CALLEE's
// entry, i.e. after CALL has pushed the 8-byte return address -- which is why it starts at 8, not 0).
inline std::vector<ArgumentLocation> assign_argument_locations(CallingConvention convention, int argument_count) {
    if (convention == CallingConvention::MicrosoftX64) {
        return assign_argument_locations(argument_count);
    }
    std::vector<ArgumentLocation> locations;
    if (argument_count <= 0) {
        return locations;
    }
    locations.reserve(static_cast<std::size_t>(argument_count));
    const auto& registers = sysv_integer_argument_registers();
    const int register_count = static_cast<int>(registers.size());
    for (int position = 0; position < argument_count; ++position) {
        ArgumentLocation location;
        if (position < register_count) {
            location.in_register = true;
            location.register_name = registers[static_cast<std::size_t>(position)];
        } else {
            location.in_register = false;
            location.stack_offset_bytes = 8 + 8 * (position - register_count);
        }
        locations.push_back(location);
    }
    return locations;
}

} // namespace arco::systems
