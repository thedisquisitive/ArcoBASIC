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
// see arcology-os/docs/systems/uefi-bindings.md for the full field-by-field derivation and sources. This does
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
                // EFI_BOOT_SERVICES.GetMemoryMap (table index 6, offset 0x38). The raw ABI
                // exposes buffer size, buffer, map key, descriptor size, and version pointers.
                UefiField{"GetMemoryMap", "GetMemoryMap", 0x38, "", true, false, "U64"},
                // EFI_BOOT_SERVICES.AllocatePages (table index 5, offset 0x28). The raw
                // bootstrap binding keeps the physical output pointer explicit.
                UefiField{"AllocatePages", "AllocatePages", 0x28, "", true, false, "U64"},
                // EFI_BOOT_SERVICES.AllocatePool / FreePool (offsets 0x40 / 0x48). These are
                // recorded for the bootstrap allocator layer; typed buffer wrappers remain next.
                UefiField{"AllocatePool", "AllocatePool", 0x40, "", true, false, "U64"},
                UefiField{"FreePool", "FreePool", 0x48, "", true, false, "U64"},
                // EFI_BOOT_SERVICES.LocateProtocol (table index 37, offset 0x140).
                // The raw ABI takes a protocol GUID pointer, an optional registration key,
                // and an output interface pointer. A typed convenience wrapper remains a
                // follow-up; this binding records the verified firmware entry point.
                UefiField{"LocateProtocol", "LocateProtocol", 0x140, "", true, false, "U64"},
                // EFI_BOOT_SERVICES.HandleProtocol (table index 11, offset 0x98). RFC-0044:
                // resolves a specific EFI_HANDLE (e.g. one returned by LocateHandle below) to
                // its real protocol interface pointer -- the counterpart LocateProtocol itself
                // cannot provide, since LocateProtocol returns at most one implementation-chosen
                // handle's own interface, never a specific handle among several.
                UefiField{"HandleProtocol", "HandleProtocol", 0x98, "", true, false, "U64"},
                // EFI_BOOT_SERVICES.LocateHandle (table index 20, offset 0xB0). RFC-0044: real
                // multi-handle enumeration, closing RFC-0038 Section 17.5's own "does not attempt
                // multi-handle enumeration" scope reduction. Takes a caller-provided, fixed-size
                // buffer (IN/OUT BufferSize, OUT Buffer of EFI_HANDLE) rather than
                // LocateHandleBuffer's own pool-allocated one -- no heap allocator exists on this
                // backend, matching the same "fixed scratch buffer, not dynamic allocation"
                // precedent every other table in this project already uses.
                UefiField{"LocateHandle", "LocateHandle", 0xB0, "", true, false, "U64"},
                // EFI_BOOT_SERVICES.ExitBootServices (table index 29, offset 0xE8). The
                // image handle and memory-map key are explicit UINTN arguments; the service
                // table pointer is not an implicit C++/protocol `This` parameter.
                UefiField{"ExitBootServices", "ExitBootServices", 0xE8, "", true, false, "U64"},
            },
        };
    }
    if (name == "UEFI.GraphicsOutputProtocol") {
        return UefiType{
            "UEFI.GraphicsOutputProtocol",
            32,
            {
                UefiField{"Mode", "Mode", 0x18, "UEFI.GraphicsOutputMode", false, false, ""},
            },
        };
    }
    if (name == "UEFI.GraphicsOutputMode") {
        return UefiType{
            "UEFI.GraphicsOutputMode",
            40,
            {
                UefiField{"MaxMode", "MaxMode", 0x00, "U32", false, false, ""},
                UefiField{"Mode", "Mode", 0x04, "U32", false, false, ""},
                UefiField{"Info", "Info", 0x08, "UEFI.GraphicsOutputModeInformation", false, false, ""},
                UefiField{"SizeOfInfo", "SizeOfInfo", 0x10, "U64", false, false, ""},
                UefiField{"FrameBufferBase", "FrameBufferBase", 0x18, "PHYSICALPTR", false, false, ""},
                UefiField{"FrameBufferSize", "FrameBufferSize", 0x20, "U64", false, false, ""},
            },
        };
    }
    if (name == "UEFI.GraphicsOutputModeInformation") {
        return UefiType{
            "UEFI.GraphicsOutputModeInformation",
            36,
            {
                UefiField{"Version", "Version", 0x00, "U32", false, false, ""},
                UefiField{"HorizontalResolution", "HorizontalResolution", 0x04, "U32", false, false, ""},
                UefiField{"VerticalResolution", "VerticalResolution", 0x08, "U32", false, false, ""},
                UefiField{"PixelFormat", "PixelFormat", 0x0C, "U32", false, false, ""},
                UefiField{"PixelsPerScanLine", "PixelsPerScanLine", 0x20, "U32", false, false, ""},
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
    // EFI_BLOCK_IO_PROTOCOL (RFC-0038 Section 17.5's own named stop condition, closed here) --
    // verified field-by-field against MdePkg/Include/Protocol/BlockIo.h (TianoCore edk2), the
    // same primary-source discipline every other binding in this file already follows. Natural
    // C alignment, 48 bytes total: Revision(U64,0), Media(EFI_BLOCK_IO_MEDIA*,8),
    // Reset/ReadBlocks/WriteBlocks/FlushBlocks (function pointers, 8 bytes each, 16/24/32/40).
    // ReadBlocks/WriteBlocks/Reset/FlushBlocks are real protocol methods (implicit This, real
    // EFIAPI signatures verified against the same header: Reset(This, ExtendedVerification),
    // ReadBlocks/WriteBlocks(This, MediaId, Lba, BufferSize, Buffer), FlushBlocks(This)) --
    // callable through the SAME generic CallExternal mechanism systemTable.BootServices.* already
    // uses (including its own stack-argument handling for ReadBlocks/WriteBlocks' 5th argument),
    // so no new calling-convention codegen was needed for the calls themselves, only for
    // discovery (see UEFI.BLOCKIO.DISCOVER in fission.cpp, mirroring UEFI.GOP.DISCOVER's own
    // LocateProtocol pattern with EFI_BLOCK_IO_PROTOCOL_GUID instead).
    if (name == "UEFI.BlockIoProtocol") {
        return UefiType{
            "UEFI.BlockIoProtocol",
            48,
            {
                UefiField{"Revision", "Revision", 0x00, "U64", false, false, ""},
                UefiField{"Media", "Media", 0x08, "UEFI.BlockIoMedia", false, false, ""},
                UefiField{"Reset", "Reset", 0x10, "", true, true, "U64"},
                UefiField{"ReadBlocks", "ReadBlocks", 0x18, "", true, true, "U64"},
                UefiField{"WriteBlocks", "WriteBlocks", 0x20, "", true, true, "U64"},
                UefiField{"FlushBlocks", "FlushBlocks", 0x28, "", true, true, "U64"},
            },
        };
    }
    // EFI_BLOCK_IO_MEDIA, verified against the same header. 48 bytes, natural C alignment:
    // MediaId(UINT32,0), RemovableMedia/MediaPresent/LogicalPartition/ReadOnly (BOOLEAN, 1 byte
    // each, 4/5/6/7 -- packed, no gap), WriteCaching(BOOLEAN,8, 3 bytes trailing padding to the
    // next UINT32-aligned offset), BlockSize(UINT32,12), IoAlign(UINT32,16),
    // LastBlock(EFI_LBA/UINT64,24 -- 4 bytes padding before it for 8-byte alignment),
    // LowestAlignedLba(UINT64,32), LogicalBlocksPerPhysicalBlock(UINT32,40),
    // OptimalTransferLengthGranularity(UINT32,44). Documentation-only entry (not consulted by
    // the hand-rolled UEFI.BLOCKIO.MEDIA* accessors in fission.cpp, matching how
    // UEFI.GraphicsOutputMode's own registry entry is likewise unused by GOP's hand-rolled
    // accessors -- the generic CallExternal path only resolves terminal METHOD calls, not plain
    // data fields, so a struct that is read-only data throughout still needs hand-rolled
    // accessors regardless of what is registered here).
    if (name == "UEFI.BlockIoMedia") {
        return UefiType{
            "UEFI.BlockIoMedia",
            48,
            {
                UefiField{"MediaId", "MediaId", 0x00, "U32", false, false, ""},
                UefiField{"BlockSize", "BlockSize", 0x0C, "U32", false, false, ""},
                UefiField{"LastBlock", "LastBlock", 0x18, "U64", false, false, ""},
            },
        };
    }
    return std::nullopt;
}

} // namespace arco::systems
