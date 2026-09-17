#include "arco/pe_image.hpp"

#include <stdexcept>

namespace arco::systems {
namespace {

// See arcology-os/docs/systems/pe32-image.md for the derivation and sources behind every constant and field
// value below; layout offsets/sizes were verified against Microsoft's PE/COFF format reference
// before writing any of this.

constexpr std::uint32_t kFileAlignment = 0x1000;    // also used as SectionAlignment (see docs)
constexpr std::uint64_t kImageBase = 0x140000000ULL;
constexpr std::uint16_t kMachineAmd64 = 0x8664;
constexpr std::uint16_t kSubsystemEfiApplication = 10;
constexpr std::uint16_t kOptionalHeaderMagicPe32Plus = 0x020B;
constexpr int kNumberOfDataDirectories = 16;
constexpr int kNumberOfSections = 2;

constexpr std::uint16_t kCharacteristicsExecutableImage = 0x0002;
constexpr std::uint16_t kCharacteristicsLargeAddressAware = 0x0020;
constexpr std::uint16_t kCharacteristicsRelocsStripped = 0x0001;

constexpr std::uint32_t kSectionCode = 0x00000020;
constexpr std::uint32_t kSectionInitializedData = 0x00000040;
constexpr std::uint32_t kSectionMemExecute = 0x20000000;
constexpr std::uint32_t kSectionMemRead = 0x40000000;

std::uint32_t round_up(std::uint32_t value, std::uint32_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t value) { out.push_back(value); }

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    put_u8(out, static_cast<std::uint8_t>(value & 0xFF));
    put_u8(out, static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    put_u16(out, static_cast<std::uint16_t>(value & 0xFFFF));
    put_u16(out, static_cast<std::uint16_t>((value >> 16) & 0xFFFF));
}

void put_u64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    put_u32(out, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    put_u32(out, static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFULL));
}

void put_zeros(std::vector<std::uint8_t>& out, std::size_t count) {
    out.insert(out.end(), count, 0);
}

void put_section_name(std::vector<std::uint8_t>& out, const char* name) {
    std::size_t i = 0;
    for (; name[i] != '\0' && i < 8; ++i) {
        put_u8(out, static_cast<std::uint8_t>(name[i]));
    }
    put_zeros(out, 8 - i);
}

} // namespace

std::vector<std::uint8_t> write_pe32plus_efi_image(const MachineCodeImage& image) {
    if (image.text.empty()) {
        throw std::runtime_error("cannot write a PE image with an empty .text section");
    }

    const std::uint32_t text_raw_size = round_up(static_cast<std::uint32_t>(image.text.size()), kFileAlignment);
    const std::uint32_t rdata_raw_size = round_up(static_cast<std::uint32_t>(image.rdata.size()), kFileAlignment);

    // Headers occupy the first file-aligned chunk; sections follow, RVA == file offset throughout
    // because SectionAlignment == FileAlignment (arcology-os/docs/systems/pe32-image.md "Alignment").
    constexpr std::uint32_t kDosHeaderSize = 64;
    constexpr std::uint32_t kPeSignatureSize = 4;
    constexpr std::uint32_t kCoffHeaderSize = 20;
    constexpr std::uint32_t kOptionalHeaderSize = 112 + kNumberOfDataDirectories * 8;  // 240
    constexpr std::uint32_t kSectionHeaderSize = 40;
    const std::uint32_t headers_raw_size =
        kDosHeaderSize + kPeSignatureSize + kCoffHeaderSize + kOptionalHeaderSize + kSectionHeaderSize * kNumberOfSections;
    const std::uint32_t size_of_headers = round_up(headers_raw_size, kFileAlignment);

    const std::uint32_t text_rva = size_of_headers;
    const std::uint32_t text_file_offset = size_of_headers;
    const std::uint32_t rdata_rva = text_rva + text_raw_size;
    const std::uint32_t rdata_file_offset = text_file_offset + text_raw_size;
    const std::uint32_t size_of_image = round_up(rdata_rva + rdata_raw_size, kFileAlignment);

    std::vector<std::uint8_t> out;
    out.reserve(size_of_headers + text_raw_size + rdata_raw_size);

    // IMAGE_DOS_HEADER: only e_magic and e_lfanew are meaningful; everything else is zero (no
    // MS-DOS stub program -- the PE header immediately follows).
    put_u8(out, 'M');
    put_u8(out, 'Z');
    put_zeros(out, 0x3C - 2);
    put_u32(out, kDosHeaderSize);  // e_lfanew: PE header starts right after this 64-byte header

    // PE signature
    put_u8(out, 'P');
    put_u8(out, 'E');
    put_u8(out, 0);
    put_u8(out, 0);

    // IMAGE_FILE_HEADER (COFF header)
    put_u16(out, kMachineAmd64);
    put_u16(out, static_cast<std::uint16_t>(kNumberOfSections));
    put_u32(out, 0);  // TimeDateStamp
    put_u32(out, 0);  // PointerToSymbolTable
    put_u32(out, 0);  // NumberOfSymbols
    put_u16(out, static_cast<std::uint16_t>(kOptionalHeaderSize));
    put_u16(out, static_cast<std::uint16_t>(kCharacteristicsExecutableImage | kCharacteristicsLargeAddressAware));

    // IMAGE_OPTIONAL_HEADER64
    put_u16(out, kOptionalHeaderMagicPe32Plus);
    put_u8(out, 0);  // MajorLinkerVersion
    put_u8(out, 0);  // MinorLinkerVersion
    put_u32(out, text_raw_size);                        // SizeOfCode
    put_u32(out, rdata_raw_size);                        // SizeOfInitializedData
    put_u32(out, 0);                                     // SizeOfUninitializedData
    put_u32(out, text_rva);                              // AddressOfEntryPoint: start of .text
    put_u32(out, text_rva);                              // BaseOfCode
    put_u64(out, kImageBase);
    put_u32(out, kFileAlignment);                        // SectionAlignment
    put_u32(out, kFileAlignment);                        // FileAlignment
    put_u16(out, 0);                                     // MajorOperatingSystemVersion
    put_u16(out, 0);                                     // MinorOperatingSystemVersion
    put_u16(out, 0);                                     // MajorImageVersion
    put_u16(out, 0);                                     // MinorImageVersion
    put_u16(out, 0);                                     // MajorSubsystemVersion
    put_u16(out, 0);                                     // MinorSubsystemVersion
    put_u32(out, 0);                                     // Win32VersionValue
    put_u32(out, size_of_image);
    put_u32(out, size_of_headers);
    put_u32(out, 0);                                     // CheckSum: not validated for EFI images
    put_u16(out, kSubsystemEfiApplication);
    put_u16(out, 0);                                     // DllCharacteristics
    put_u64(out, 0x100000);                              // SizeOfStackReserve
    put_u64(out, 0x1000);                                // SizeOfStackCommit
    put_u64(out, 0x100000);                              // SizeOfHeapReserve
    put_u64(out, 0x1000);                                // SizeOfHeapCommit
    put_u32(out, 0);                                     // LoaderFlags
    put_u32(out, static_cast<std::uint32_t>(kNumberOfDataDirectories));
    // All 16 data directories are zero: no exports, no imports (Packet WP-009 "absence of host
    // runtime imports"), no BASERELOC (IMAGE_FILE_RELOCS_STRIPPED is set instead), nothing else.
    put_zeros(out, kNumberOfDataDirectories * 8);

    // IMAGE_SECTION_HEADER entries
    put_section_name(out, ".text");
    put_u32(out, static_cast<std::uint32_t>(image.text.size()));  // VirtualSize (unpadded)
    put_u32(out, text_rva);
    put_u32(out, text_raw_size);
    put_u32(out, text_file_offset);
    put_u32(out, 0);  // PointerToRelocations
    put_u32(out, 0);  // PointerToLinenumbers
    put_u16(out, 0);  // NumberOfRelocations
    put_u16(out, 0);  // NumberOfLinenumbers
    put_u32(out, kSectionCode | kSectionMemExecute | kSectionMemRead);

    put_section_name(out, ".rdata");
    put_u32(out, static_cast<std::uint32_t>(image.rdata.size()));  // VirtualSize (unpadded)
    put_u32(out, rdata_rva);
    put_u32(out, rdata_raw_size);
    put_u32(out, rdata_file_offset);
    put_u32(out, 0);
    put_u32(out, 0);
    put_u16(out, 0);
    put_u16(out, 0);
    put_u32(out, kSectionInitializedData | kSectionMemRead);

    put_zeros(out, size_of_headers - out.size());

    // .text, with every relocation patched against the final RVA layout.
    const std::size_t text_start = out.size();
    out.insert(out.end(), image.text.begin(), image.text.end());
    for (const auto& relocation : image.relocations) {
        const std::int64_t target_rva = static_cast<std::int64_t>(rdata_rva) + static_cast<std::int64_t>(relocation.rdata_offset);
        const std::int64_t instruction_end_rva =
            static_cast<std::int64_t>(text_rva) + static_cast<std::int64_t>(relocation.instruction_end_offset);
        const std::int64_t displacement = target_rva - instruction_end_rva;
        if (displacement < INT32_MIN || displacement > INT32_MAX) {
            throw std::runtime_error("RIP-relative displacement does not fit in 32 bits");
        }
        const std::uint32_t patched = static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement));
        const std::size_t field_offset = text_start + relocation.text_offset;
        out[field_offset] = static_cast<std::uint8_t>(patched & 0xFF);
        out[field_offset + 1] = static_cast<std::uint8_t>((patched >> 8) & 0xFF);
        out[field_offset + 2] = static_cast<std::uint8_t>((patched >> 16) & 0xFF);
        out[field_offset + 3] = static_cast<std::uint8_t>((patched >> 24) & 0xFF);
    }
    put_zeros(out, text_file_offset + text_raw_size - out.size());

    out.insert(out.end(), image.rdata.begin(), image.rdata.end());
    put_zeros(out, rdata_file_offset + rdata_raw_size - out.size());

    return out;
}

} // namespace arco::systems
