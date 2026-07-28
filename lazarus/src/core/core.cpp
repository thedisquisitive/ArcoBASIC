#include "lazarus/core.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

#include <fcntl.h>
#include <linux/fs.h>
#include <openssl/sha.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zstd.h>

namespace lazarus {
namespace {

namespace fs = std::filesystem;

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

bool blank(const std::string& value) {
    return trim(value).empty();
}

bool contains_path(const std::vector<std::string>& paths, const std::string& physical_path) {
    return std::find(paths.begin(), paths.end(), physical_path) != paths.end();
}

bool contains_any_identity(const std::vector<std::string>& paths, const DeviceIdentity& device) {
    return contains_path(paths, device.physical_path) || contains_path(paths, device.by_path) || contains_path(paths, device.by_id_path) || contains_path(paths, device.linux_path);
}

bool contains_port_identity(const std::vector<std::string>& paths, const DeviceIdentity& device) {
    auto device_port = !device.port_path.empty() ? device.port_path : physical_port_identity(device.by_path);
    if (device_port.empty() && (device.physical_path.rfind("port:", 0) == 0 || device.physical_path.rfind("/port/", 0) == 0)) {
        device_port = device.physical_path;
    }
    if (device_port.empty()) return false;
    return std::any_of(paths.begin(), paths.end(), [&](const std::string& configured) {
        return configured == device_port || configured == device.physical_path || physical_port_identity(configured) == device_port;
    });
}

bool contains_legacy_device_role(const std::vector<std::string>& paths, const DeviceIdentity& device) {
    return std::any_of(paths.begin(), paths.end(), [&](const std::string& configured) {
        const bool legacy_disk_selector = configured.rfind("/dev/disk/by-id/", 0) == 0 ||
                                          (configured.rfind("/dev/", 0) == 0 && configured.find("/by-path/") == std::string::npos);
        return legacy_disk_selector && contains_any_identity({configured}, device);
    });
}

SafetyFinding finding(Severity severity, std::string code, std::string observed, std::string action) {
    return SafetyFinding{severity, std::move(code), std::move(observed), std::move(action)};
}

std::optional<std::string> read_text_file(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    std::string text;
    std::getline(file, text, '\0');
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

std::optional<std::vector<std::byte>> read_binary_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;
    const auto size = file.tellg();
    if (size < 0) return std::nullopt;
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file && !data.empty()) return std::nullopt;
    return data;
}

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out += "'";
    return out;
}

std::optional<std::string> find_executable(const std::string& name) {
    const char* path_env = std::getenv("PATH");
    std::vector<std::string> paths;
    if (path_env != nullptr) {
        std::istringstream path_stream(path_env);
        std::string path;
        while (std::getline(path_stream, path, ':')) {
            if (!path.empty()) {
                paths.push_back(path);
            }
        }
    }
    paths.push_back("/usr/local/sbin");
    paths.push_back("/usr/local/bin");
    paths.push_back("/usr/sbin");
    paths.push_back("/usr/bin");
    paths.push_back("/sbin");
    paths.push_back("/bin");

    for (const auto& directory : paths) {
        const auto candidate = fs::path(directory) / name;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return std::nullopt;
}

std::string json_string_value(const std::string& json, const std::string& key) {
    const auto key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return "";
    }
    const auto colon = json.find(':', key_pos);
    if (colon == std::string::npos) {
        return "";
    }
    auto pos = json.find_first_not_of(" \t\r\n", colon + 1);
    if (pos == std::string::npos || json[pos] != '"') {
        return "";
    }
    ++pos;
    std::string out;
    bool escape = false;
    for (; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (escape) {
            switch (ch) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    out.push_back(ch);
                    break;
            }
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') {
            break;
        }
        out.push_back(ch);
    }
    return out;
}

std::optional<std::int64_t> json_integer_after(const std::string& json, std::size_t start, const std::string& key) {
    const auto key_pos = json.find("\"" + key + "\"", start);
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto colon = json.find(':', key_pos);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    auto pos = json.find_first_not_of(" \t\r\n", colon + 1);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    bool negative = false;
    if (json[pos] == '-') {
        negative = true;
        ++pos;
    }
    if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos]))) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        value = (value * 10) + (json[pos] - '0');
        ++pos;
    }
    return negative ? -value : value;
}

std::optional<std::int64_t> json_integer_value(const std::string& json, const std::string& key) {
    return json_integer_after(json, 0, key);
}

std::optional<std::int64_t> json_object_integer_value(const std::string& json, const std::string& object_key, const std::string& value_key) {
    const auto object_pos = json.find("\"" + object_key + "\"");
    if (object_pos == std::string::npos) {
        return std::nullopt;
    }
    return json_integer_after(json, object_pos, value_key);
}

std::optional<std::int64_t> ata_attribute_raw_value(const std::string& json, const std::string& attribute_name) {
    const auto name_pos = json.find("\"name\"");
    std::size_t pos = 0;
    while (true) {
        const auto next_name = json.find("\"name\"", pos);
        if (next_name == std::string::npos) {
            return std::nullopt;
        }
        const auto value = json_string_value(json.substr(next_name), "name");
        if (value == attribute_name) {
            const auto raw_pos = json.find("\"raw\"", next_name);
            if (raw_pos == std::string::npos) {
                return std::nullopt;
            }
            return json_integer_after(json, raw_pos, "value");
        }
        pos = next_name + 6;
    }
    (void)name_pos;
}

void set_attribute(SmartAttribute& attribute, std::optional<std::int64_t> value) {
    if (!value) {
        return;
    }
    attribute.value = *value;
    attribute.present = true;
}

std::uint64_t read_u64_file(const fs::path& path, std::uint64_t fallback = 0) {
    auto text = read_text_file(path);
    if (!text) {
        return fallback;
    }

    try {
        return std::stoull(*text);
    } catch (...) {
        return fallback;
    }
}

std::uint32_t read_u32_file(const fs::path& path, std::uint32_t fallback = 0) {
    const auto value = read_u64_file(path, fallback);
    if (value > UINT32_MAX) {
        return fallback;
    }
    return static_cast<std::uint32_t>(value);
}

std::string basename(const std::string& path) {
    return fs::path(path).filename().string();
}

bool starts_with(const std::string& text, const std::string& prefix) {
    return text.rfind(prefix, 0) == 0;
}

bool is_partition_name_for_disk(const std::string& candidate, const std::string& disk) {
    if (candidate == disk) {
        return true;
    }
    if (starts_with(disk, "nvme") || starts_with(disk, "mmcblk") || starts_with(disk, "nbd")) {
        return starts_with(candidate, disk + "p");
    }
    return starts_with(candidate, disk) && candidate.size() > disk.size() && std::isdigit(static_cast<unsigned char>(candidate[disk.size()]));
}

std::set<std::string> mounted_major_minor_ids() {
    std::set<std::string> ids;
    std::ifstream file("/proc/self/mountinfo");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::string token;
        for (int index = 0; index < 3 && stream >> token; ++index) {
            if (index == 2) {
                ids.insert(token);
            }
        }
    }
    return ids;
}

std::set<std::string> mounted_device_basenames() {
    std::set<std::string> names;
    std::ifstream file("/proc/self/mountinfo");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream stream(line);
        std::vector<std::string> tokens;
        std::string token;
        while (stream >> token) {
            tokens.push_back(token);
        }
        auto separator = std::find(tokens.begin(), tokens.end(), "-");
        if (separator == tokens.end()) {
            continue;
        }
        const auto source = separator + 2;
        if (source != tokens.end() && starts_with(*source, "/dev/")) {
            names.insert(basename(*source));
        }
    }
    return names;
}

std::set<std::string> collect_system_disk_names() {
    std::set<std::string> names;
    const auto mounted_ids = mounted_major_minor_ids();
    const auto mounted_names = mounted_device_basenames();
    const fs::path sys_block("/sys/block");

    if (!fs::exists(sys_block)) {
        return names;
    }

    for (const auto& entry : fs::directory_iterator(sys_block)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto disk = entry.path().filename().string();
        const auto disk_major_minor = read_text_file(entry.path() / "dev");
        if (disk_major_minor && mounted_ids.contains(*disk_major_minor)) {
            names.insert(disk);
            continue;
        }

        for (const auto& child : fs::directory_iterator(entry.path())) {
            if (!child.is_directory()) {
                continue;
            }
            const auto part = child.path().filename().string();
            if (!is_partition_name_for_disk(part, disk)) {
                continue;
            }
            const auto part_major_minor = read_text_file(child.path() / "dev");
            if (part_major_minor && mounted_ids.contains(*part_major_minor)) {
                names.insert(disk);
            }
        }

        for (const auto& mounted : mounted_names) {
            if (is_partition_name_for_disk(mounted, disk)) {
                names.insert(disk);
            }
        }
    }

    return names;
}

std::string find_symlink_for_disk(const fs::path& directory, const std::string& disk) {
    if (!fs::exists(directory)) {
        return "";
    }

    std::string best;
    for (const auto& entry : fs::directory_iterator(directory)) {
        std::error_code error;
        if (!entry.is_symlink(error)) {
            continue;
        }
        const auto target = fs::canonical(entry.path(), error);
        if (error || target.filename() != disk) {
            continue;
        }
        const auto candidate = entry.path().string();
        if (best.empty() || candidate.size() < best.size()) {
            best = candidate;
        }
    }
    return best;
}

std::string serial_ending_from(const std::string& serial) {
    if (serial.empty()) {
        return "";
    }
    if (serial.size() <= 4) {
        return serial;
    }
    return serial.substr(serial.size() - 4);
}

bool skip_block_device(const std::string& disk) {
    return starts_with(disk, "loop") || starts_with(disk, "ram") || starts_with(disk, "fd");
}

std::string detect_transport(const fs::path& disk_path) {
    std::error_code error;
    auto real_path = fs::canonical(disk_path, error).string();
    if (error) {
        real_path = disk_path.string();
    }
    if (real_path.find("/usb") != std::string::npos) {
        return "usb";
    }
    if (real_path.find("/nvme") != std::string::npos) {
        return "nvme";
    }
    if (real_path.find("/ata") != std::string::npos || real_path.find("/host") != std::string::npos) {
        return "sata";
    }
    return "";
}

std::vector<std::string> list_partitions(const fs::path& disk_path, const std::string& disk) {
    std::vector<std::string> partitions;
    for (const auto& child : fs::directory_iterator(disk_path)) {
        if (!child.is_directory()) {
            continue;
        }
        const auto name = child.path().filename().string();
        if (is_partition_name_for_disk(name, disk)) {
            partitions.push_back("/dev/" + name);
        }
    }
    std::sort(partitions.begin(), partitions.end());
    return partitions;
}

bool has_blocker(const std::vector<SafetyFinding>& findings) {
    return std::any_of(findings.begin(), findings.end(), [](const SafetyFinding& finding) {
        return finding.severity == Severity::Blocker;
    });
}

bool has_imageable_layout(const DiskInspection& inspection) {
    return inspection.gpt_header_valid || inspection.mbr_detected || !inspection.partitions.empty();
}

std::uint64_t ceil_div(std::uint64_t value, std::uint64_t divisor) {
    if (divisor == 0) {
        return 0;
    }
    return (value + divisor - 1) / divisor;
}

bool should_emit_progress(std::uint64_t chunks_done, std::uint64_t chunks_total) {
    if (chunks_done == 0 || chunks_done == chunks_total) {
        return true;
    }
    if (chunks_total <= 64) {
        return true;
    }
    const auto interval = std::max<std::uint64_t>(1, chunks_total / 100);
    return chunks_done % interval == 0;
}

void emit_progress(const ProgressCallback& progress, ProgressEvent event) {
    if (progress) {
        progress(event);
    }
}

std::string strip_comment(std::string line) {
    // A '#' starts a comment only at the beginning of a line or after
    // whitespace. This preserves hexadecimal values such as #cf3b2b.
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '#' && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1])))) {
            line.erase(i);
            break;
        }
    }
    return trim(line);
}

std::uint16_t le16(const std::vector<std::byte>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned char>(data[offset])) |
           static_cast<std::uint16_t>(std::to_integer<unsigned char>(data[offset + 1]) << 8);
}

std::uint32_t le32(const std::vector<std::byte>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[offset])) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[offset + 1])) << 8) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[offset + 2])) << 16) |
           (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[offset + 3])) << 24);
}

std::uint64_t le64(const std::vector<std::byte>& data, std::size_t offset) {
    return static_cast<std::uint64_t>(le32(data, offset)) |
           (static_cast<std::uint64_t>(le32(data, offset + 4)) << 32);
}

std::uint32_t crc32_bytes(const std::vector<std::byte>& data, std::size_t length) {
    std::uint32_t crc = 0xFFFFFFFFU;
    const auto count = std::min(length, data.size());
    for (std::size_t index = 0; index < count; ++index) {
        crc ^= std::to_integer<unsigned char>(data[index]);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

std::string hex2(unsigned value) {
    const char* digits = "0123456789abcdef";
    std::string out;
    out.push_back(digits[(value >> 4) & 0xF]);
    out.push_back(digits[value & 0xF]);
    return out;
}

std::string format_guid(const std::vector<std::byte>& data, std::size_t offset) {
    const auto b = [&](std::size_t index) {
        return std::to_integer<unsigned char>(data[offset + index]);
    };
    std::string guid;
    guid += hex2(b(3)) + hex2(b(2)) + hex2(b(1)) + hex2(b(0));
    guid += "-";
    guid += hex2(b(5)) + hex2(b(4));
    guid += "-";
    guid += hex2(b(7)) + hex2(b(6));
    guid += "-";
    guid += hex2(b(8)) + hex2(b(9));
    guid += "-";
    guid += hex2(b(10)) + hex2(b(11)) + hex2(b(12)) + hex2(b(13)) + hex2(b(14)) + hex2(b(15));
    return guid;
}

bool guid_is_zero(const std::vector<std::byte>& data, std::size_t offset) {
    for (std::size_t i = 0; i < 16; ++i) {
        if (data[offset + i] != std::byte{0}) {
            return false;
        }
    }
    return true;
}

PartitionKind partition_kind_from_guid(const std::string& guid) {
    if (guid == "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") {
        return PartitionKind::EfiSystem;
    }
    if (guid == "e3c9e316-0b5c-4db8-817d-f92df00215ae") {
        return PartitionKind::MicrosoftReserved;
    }
    if (guid == "ebd0a0a2-b9e5-4433-87c0-68b6b72699c7") {
        return PartitionKind::WindowsBasicData;
    }
    if (guid == "de94bba4-06d1-4d40-a16a-bfd50179d6ac") {
        return PartitionKind::WindowsRecovery;
    }
    return PartitionKind::Unknown;
}

PartitionKind partition_kind_from_mbr_type(unsigned char type) {
    switch (type) {
        case 0x01:
        case 0x04:
        case 0x06:
        case 0x0B:
        case 0x0C:
        case 0x0E:
            return PartitionKind::WindowsBasicData;
        case 0x07:
            return PartitionKind::WindowsBasicData;
        case 0x27:
            return PartitionKind::WindowsRecovery;
        default:
            return PartitionKind::Unknown;
    }
}

bool is_extended_mbr_type(unsigned char type) {
    return type == 0x05 || type == 0x0F || type == 0x85;
}

std::string mbr_type_string(unsigned char type) {
    std::ostringstream out;
    out << "mbr:0x" << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(type);
    return out.str();
}

CompressionMode compression_from_string(const std::string& value) {
    if (value == "zstd") {
        return CompressionMode::Zstd;
    }
    return CompressionMode::None;
}

std::vector<std::byte> compress_chunk(const std::vector<std::byte>& data, CompressionMode mode, std::string& error) {
    if (mode == CompressionMode::None) {
        return data;
    }
    if (mode == CompressionMode::Zstd) {
        const auto bound = ZSTD_compressBound(data.size());
        std::vector<std::byte> compressed(bound);
        const auto rc = ZSTD_compress(compressed.data(), compressed.size(), data.data(), data.size(), 1);
        if (ZSTD_isError(rc)) {
            error = ZSTD_getErrorName(rc);
            return {};
        }
        compressed.resize(rc);
        return compressed;
    }
    error = "Unsupported compression mode.";
    return {};
}

std::vector<std::byte> decompress_chunk(const std::vector<std::byte>& data, std::uint64_t expected_size, CompressionMode mode, std::string& error) {
    if (mode == CompressionMode::None) {
        if (data.size() != expected_size) {
            error = "Uncompressed chunk size does not match its source size.";
            return {};
        }
        return data;
    }
    if (mode == CompressionMode::Zstd) {
        std::vector<std::byte> decompressed(static_cast<std::size_t>(expected_size));
        const auto rc = ZSTD_decompress(decompressed.data(), decompressed.size(), data.data(), data.size());
        if (ZSTD_isError(rc)) {
            error = ZSTD_getErrorName(rc);
            return {};
        }
        if (rc != expected_size) {
            error = "Decompressed chunk size does not match its source size.";
            return {};
        }
        return decompressed;
    }
    error = "Unsupported compression mode.";
    return {};
}

bool is_ntfs_boot_sector(const std::vector<std::byte>& data) {
    if (data.size() < 512 || data[510] != std::byte{0x55} || data[511] != std::byte{0xAA}) {
        return false;
    }
    constexpr char signature[] = "NTFS    ";
    for (std::size_t i = 0; i < 8; ++i) {
        if (std::to_integer<char>(data[3 + i]) != signature[i]) {
            return false;
        }
    }
    return true;
}

bool is_valid_fat_bytes_per_sector(std::uint16_t bytes_per_sector) {
    return bytes_per_sector == 512 || bytes_per_sector == 1024 || bytes_per_sector == 2048 || bytes_per_sector == 4096;
}

bool is_power_of_two_byte(unsigned char value) {
    return value != 0 && (value & (value - 1)) == 0;
}

bool is_exfat_boot_sector(const std::vector<std::byte>& data) {
    if (data.size() < 512 || data[510] != std::byte{0x55} || data[511] != std::byte{0xAA}) {
        return false;
    }
    constexpr char signature[] = "EXFAT   ";
    for (std::size_t i = 0; i < 8; ++i) {
        if (std::to_integer<char>(data[3 + i]) != signature[i]) {
            return false;
        }
    }
    return true;
}

bool byte_string_equals_at(const std::vector<std::byte>& data, std::size_t offset, const char* text) {
    for (std::size_t i = 0; text[i] != '\0'; ++i) {
        if (offset + i >= data.size() || std::to_integer<char>(data[offset + i]) != text[i]) {
            return false;
        }
    }
    return true;
}

FileSystemKind detect_fat_boot_sector(const std::vector<std::byte>& data) {
    if (data.size() < 512 || data[510] != std::byte{0x55} || data[511] != std::byte{0xAA}) {
        return FileSystemKind::Unknown;
    }

    const auto bytes_per_sector = le16(data, 11);
    const auto sectors_per_cluster = std::to_integer<unsigned char>(data[13]);
    const auto reserved_sectors = le16(data, 14);
    const auto fat_count = std::to_integer<unsigned char>(data[16]);
    const auto total_sectors_16 = le16(data, 19);
    const auto total_sectors_32 = le32(data, 32);
    if (!is_valid_fat_bytes_per_sector(bytes_per_sector) || !is_power_of_two_byte(sectors_per_cluster) || reserved_sectors == 0 || fat_count == 0 || (total_sectors_16 == 0 && total_sectors_32 == 0)) {
        return FileSystemKind::Unknown;
    }

    if (byte_string_equals_at(data, 82, "FAT32   ")) {
        return FileSystemKind::Fat32;
    }
    if (byte_string_equals_at(data, 54, "FAT16   ")) {
        return FileSystemKind::Fat16;
    }
    if (byte_string_equals_at(data, 54, "FAT12   ")) {
        return FileSystemKind::Fat12;
    }
    return FileSystemKind::Unknown;
}

FileSystemKind detect_filesystem_boot_sector(const std::vector<std::byte>& data) {
    if (is_ntfs_boot_sector(data)) {
        return FileSystemKind::Ntfs;
    }
    if (is_exfat_boot_sector(data)) {
        return FileSystemKind::Exfat;
    }
    return detect_fat_boot_sector(data);
}

std::string filesystem_display_name(FileSystemKind kind) {
    switch (kind) {
        case FileSystemKind::Ntfs:
            return "NTFS";
        case FileSystemKind::Fat12:
            return "FAT12";
        case FileSystemKind::Fat16:
            return "FAT16";
        case FileSystemKind::Fat32:
            return "FAT32";
        case FileSystemKind::Exfat:
            return "exFAT";
        case FileSystemKind::Unknown:
            return "";
    }
    return "";
}

std::string utf16le_name_to_ascii(const std::vector<std::byte>& data, std::size_t offset, std::size_t byte_count) {
    std::string name;
    for (std::size_t i = 0; i + 1 < byte_count; i += 2) {
        const auto codepoint = le16(data, offset + i);
        if (codepoint == 0) {
            break;
        }
        if (codepoint >= 32 && codepoint <= 126) {
            name.push_back(static_cast<char>(codepoint));
        } else {
            name.push_back('?');
        }
    }
    return name;
}

bool byte_string_equals(const std::vector<std::byte>& data, std::size_t offset, const char* text) {
    for (std::size_t i = 0; text[i] != '\0'; ++i) {
        if (offset + i >= data.size() || std::to_integer<char>(data[offset + i]) != text[i]) {
            return false;
        }
    }
    return true;
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    for (const char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string local_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    char timestamp[32]{};
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S%z", &local);
    return timestamp;
}

std::optional<std::uint64_t> extract_json_u64(const std::string& text, const std::string& key);
std::optional<std::string> extract_json_string(const std::string& text, const std::string& key);

std::string render_metadata_json(const JobInfo& job, const SourceReadHandle& source, const DiskInspection& inspection, const ImageWriteOptions& options, std::uint64_t bytes_written, std::uint64_t bytes_stored, std::uint64_t chunks_written) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"format\": \"laz-dir\",\n";
    out << "  \"format_version\": 1,\n";
    out << "  \"image_state\": \"finalized\",\n";
    out << "  \"created_at\": \"" << local_timestamp() << "\",\n";
    out << "  \"job\": {\n";
    out << "    \"ticket_number\": \"" << json_escape(job.ticket_number) << "\",\n";
    out << "    \"customer_name\": \"" << json_escape(job.customer_name) << "\",\n";
    out << "    \"technician\": \"" << json_escape(job.technician) << "\",\n";
    out << "    \"purpose\": \"" << json_escape(job.purpose) << "\"\n";
    out << "  },\n";
    out << "  \"source\": {\n";
    out << "    \"linux_path\": \"" << json_escape(source.device().linux_path) << "\",\n";
    out << "    \"physical_path\": \"" << json_escape(source.device().physical_path) << "\",\n";
    out << "    \"by_id_path\": \"" << json_escape(source.device().by_id_path) << "\",\n";
    out << "    \"by_path\": \"" << json_escape(source.device().by_path) << "\",\n";
    out << "    \"model\": \"" << json_escape(source.device().model) << "\",\n";
    out << "    \"serial\": \"" << json_escape(source.device().serial) << "\",\n";
    out << "    \"serial_ending\": \"" << json_escape(source.device().serial_ending) << "\",\n";
    out << "    \"size_bytes\": " << source.device().size_bytes << ",\n";
    out << "    \"logical_block_size\": " << source.device().logical_block_size << "\n";
    out << "  },\n";
    out << "  \"imaging\": {\n";
    out << "    \"mode\": \"" << to_string(options.mode) << "\",\n";
    out << "    \"chunk_size\": " << options.chunk_size << ",\n";
    out << "    \"bytes_written\": " << bytes_written << ",\n";
    out << "    \"bytes_stored\": " << bytes_stored << ",\n";
    out << "    \"chunks_written\": " << chunks_written << ",\n";
    out << "    \"compression\": \"" << to_string(options.compression) << "\",\n";
    out << "    \"hash_algorithm\": \"sha256\"\n";
    out << "  },\n";
    out << "  \"inspection\": {\n";
    out << "    \"mbr_signature_valid\": " << (inspection.mbr_signature_valid ? "true" : "false") << ",\n";
    out << "    \"mbr_detected\": " << (inspection.mbr_detected ? "true" : "false") << ",\n";
    out << "    \"protective_mbr\": " << (inspection.protective_mbr ? "true" : "false") << ",\n";
    out << "    \"gpt_detected\": " << (inspection.gpt_detected ? "true" : "false") << ",\n";
    out << "    \"gpt_header_valid\": " << (inspection.gpt_header_valid ? "true" : "false") << ",\n";
    out << "    \"partitions\": [\n";
    for (std::size_t i = 0; i < inspection.partitions.size(); ++i) {
        const auto& partition = inspection.partitions[i];
            out << "      {\"number\": " << partition.number
            << ", \"kind\": \"" << to_string(partition.kind)
            << "\", \"name\": \"" << json_escape(partition.name)
            << "\", \"type_guid\": \"" << partition.type_guid
            << "\", \"filesystem\": \"" << to_string(partition.filesystem)
            << "\", \"ntfs_detected\": " << (partition.ntfs_detected ? "true" : "false")
            << ", \"first_lba\": " << partition.first_lba
            << ", \"last_lba\": " << partition.last_lba
            << ", \"size_bytes\": " << partition.size_bytes << "}";
        if (i + 1 < inspection.partitions.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "    ]\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

std::string render_source_identity_json(const DeviceIdentity& source) {
    std::ostringstream out;
    out << "{\n"
        << "  \"format_version\": 1,\n"
        << "  \"linux_path\": \"" << json_escape(source.linux_path) << "\",\n"
        << "  \"physical_path\": \"" << json_escape(source.physical_path) << "\",\n"
        << "  \"by_id_path\": \"" << json_escape(source.by_id_path) << "\",\n"
        << "  \"by_path\": \"" << json_escape(source.by_path) << "\",\n"
        << "  \"model\": \"" << json_escape(source.model) << "\",\n"
        << "  \"serial\": \"" << json_escape(source.serial) << "\",\n"
        << "  \"serial_ending\": \"" << json_escape(source.serial_ending) << "\",\n"
        << "  \"size_bytes\": " << source.size_bytes << ",\n"
        << "  \"logical_block_size\": " << source.logical_block_size << "\n"
        << "}\n";
    return out.str();
}

std::string render_job_journal_json(const JobInfo& job, const DeviceIdentity& source,
                                    const ImageWriteOptions& options) {
    std::ostringstream out;
    out << "{\n"
        << "  \"format\": \"lazarus-job-journal\",\n"
        << "  \"format_version\": 1,\n"
        << "  \"state\": \"interrupted-or-running\",\n"
        << "  \"created_at\": \"" << local_timestamp() << "\",\n"
        << "  \"ticket_number\": \"" << json_escape(job.ticket_number) << "\",\n"
        << "  \"customer_name\": \"" << json_escape(job.customer_name) << "\",\n"
        << "  \"technician\": \"" << json_escape(job.technician) << "\",\n"
        << "  \"purpose\": \"" << json_escape(job.purpose) << "\",\n"
        << "  \"imaging_mode\": \"" << to_string(options.mode) << "\",\n"
        << "  \"compression\": \"" << to_string(options.compression) << "\",\n"
        << "  \"source\": {\n"
        << "    \"linux_path\": \"" << json_escape(source.linux_path) << "\",\n"
        << "    \"physical_path\": \"" << json_escape(source.physical_path) << "\",\n"
        << "    \"by_id_path\": \"" << json_escape(source.by_id_path) << "\",\n"
        << "    \"by_path\": \"" << json_escape(source.by_path) << "\",\n"
        << "    \"model\": \"" << json_escape(source.model) << "\",\n"
        << "    \"serial\": \"" << json_escape(source.serial) << "\",\n"
        << "    \"serial_ending\": \"" << json_escape(source.serial_ending) << "\",\n"
        << "    \"size_bytes\": " << source.size_bytes << ",\n"
        << "    \"logical_block_size\": " << source.logical_block_size << "\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

struct ChunkHashRecord {
    std::uint64_t index = 0;
    std::uint64_t source_offset = 0;
    std::uint64_t source_size = 0;
    std::uint64_t stored_offset = 0;
    std::uint64_t stored_size = 0;
    std::string source_hash;
    std::string stored_hash;
};

std::string sha256_hex(const std::vector<std::byte>& data) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest);

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digest) {
        out << std::setw(2) << static_cast<unsigned>(byte);
    }
    return out.str();
}

std::optional<ChunkHashRecord> parse_hash_record_line(const std::string& line) {
    if (line.empty() || line.find('=') != std::string::npos) {
        return std::nullopt;
    }
    std::istringstream stream(line);
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) {
        fields.push_back(field);
    }
    ChunkHashRecord record;
    try {
        if (fields.size() == 4) {
            record.index = std::stoull(fields[0]);
            record.source_offset = std::stoull(fields[1]);
            record.source_size = std::stoull(fields[2]);
            record.source_hash = fields[3];
            record.stored_offset = record.source_offset;
            record.stored_size = record.source_size;
            record.stored_hash = record.source_hash;
            return record;
        }
        if (fields.size() == 7) {
            record.index = std::stoull(fields[0]);
            record.source_offset = std::stoull(fields[1]);
            record.source_size = std::stoull(fields[2]);
            record.stored_offset = std::stoull(fields[3]);
            record.stored_size = std::stoull(fields[4]);
            record.source_hash = fields[5];
            record.stored_hash = fields[6];
            return record;
        }
    } catch (...) {
        record.stored_offset = record.source_offset;
    }
    return std::nullopt;
}

std::vector<ChunkHashRecord> read_hash_records(const fs::path& path) {
    std::vector<ChunkHashRecord> records;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        auto record = parse_hash_record_line(trim(line));
        if (record) {
            records.push_back(*record);
        }
    }
    return records;
}

void write_hash_header(std::ofstream& out, CompressionMode compression) {
    out << "algorithm=sha256\n";
    out << "stream=disk.raw\n";
    out << "compression=" << to_string(compression) << "\n";
    out << "columns=index source_offset source_size stored_offset stored_size source_sha256 stored_sha256\n";
}

void write_hash_record(std::ofstream& out, const ChunkHashRecord& record) {
    out << record.index << " " << record.source_offset << " " << record.source_size << " "
        << record.stored_offset << " " << record.stored_size << " "
        << record.source_hash << " " << record.stored_hash << "\n";
}

std::optional<std::uint64_t> extract_json_u64(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    auto pos = colon + 1;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    const auto start = pos;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    if (start == pos) {
        return std::nullopt;
    }
    try {
        return std::stoull(text.substr(start, pos - start));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> extract_json_string(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return std::nullopt;
    }
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return std::nullopt;
    }
    const auto first_quote = text.find('"', colon + 1);
    if (first_quote == std::string::npos) {
        return std::nullopt;
    }
    const auto second_quote = text.find('"', first_quote + 1);
    if (second_quote == std::string::npos) {
        return std::nullopt;
    }
    return text.substr(first_quote + 1, second_quote - first_quote - 1);
}

CompressionMode read_hash_compression(const fs::path& path) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        constexpr const char* prefix = "compression=";
        if (line.rfind(prefix, 0) == 0) {
            return compression_from_string(line.substr(std::strlen(prefix)));
        }
    }
    return CompressionMode::None;
}

bool write_text_file(const fs::path& path, const std::string& text) {
    const auto temporary = path.string() + ".tmp-" + std::to_string(::getpid());
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return false;
    }
    std::size_t written = 0;
    while (written < text.size()) {
        const auto count = ::write(fd, text.data() + written, text.size() - written);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            ::close(fd);
            ::unlink(temporary.c_str());
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    const bool synced = ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    if (!synced || !closed || ::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        return false;
    }
    const auto parent = path.has_parent_path() ? path.parent_path() : fs::path{"."};
    const int directory_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        return false;
    }
    const bool directory_synced = ::fsync(directory_fd) == 0;
    ::close(directory_fd);
    return directory_synced;
}

bool sync_file(const fs::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return false;
    }
    const bool synced = ::fsync(fd) == 0;
    ::close(fd);
    return synced;
}

bool sync_directory(const fs::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    const bool synced = ::fsync(fd) == 0;
    ::close(fd);
    return synced;
}

bool safe_regular_file(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    return !error && fs::is_regular_file(status) && !fs::is_symlink(status);
}

bool source_identity_matches(const DeviceIdentity& source, const std::string& journal, std::string& reason) {
    const auto expected_size = extract_json_u64(journal, "size_bytes");
    const auto expected_block = extract_json_u64(journal, "logical_block_size");
    if (!expected_size || *expected_size != source.size_bytes || !expected_block || *expected_block != source.logical_block_size) {
        reason = "Source capacity or logical block size differs from the interrupted image.";
        return false;
    }
    const auto expected_serial = extract_json_string(journal, "serial").value_or("");
    const auto expected_by_id = extract_json_string(journal, "by_id_path").value_or("");
    if (!expected_serial.empty()) {
        if (source.serial.empty() || source.serial != expected_serial) {
            reason = "Source serial number differs from the interrupted image.";
            return false;
        }
        return true;
    }
    if (!expected_by_id.empty()) {
        if (source.by_id_path.empty() || source.by_id_path != expected_by_id) {
            reason = "Persistent source device identity differs from the interrupted image.";
            return false;
        }
        return true;
    }
    const auto expected_model = extract_json_string(journal, "model").value_or("");
    const auto expected_serial_ending = extract_json_string(journal, "serial_ending").value_or("");
    if (expected_model != source.model || expected_serial_ending.empty() || expected_serial_ending != source.serial_ending) {
        reason = "The source lacks a matching persistent serial identity.";
        return false;
    }
    return true;
}

bool destructive_identity_matches(const DeviceIdentity& expected, const DeviceIdentity& current, std::string& reason) {
    if (expected.size_bytes != current.size_bytes || expected.logical_block_size != current.logical_block_size) {
        reason = "The destination capacity or logical block size changed after selection.";
        return false;
    }
    if (!expected.physical_path.empty() && expected.physical_path != current.physical_path) {
        reason = "The destination is no longer connected to the selected physical port.";
        return false;
    }
    if (!expected.by_path.empty() && expected.by_path != current.by_path) {
        reason = "The destination physical path changed after selection.";
        return false;
    }
    if (!expected.by_id_path.empty() && expected.by_id_path != current.by_id_path) {
        reason = "The persistent destination device identity changed after selection.";
        return false;
    }
    if (!expected.serial.empty() && expected.serial != current.serial) {
        reason = "The destination serial number changed after selection.";
        return false;
    }
    if (expected.by_id_path.empty() && expected.serial.empty() &&
        (expected.model != current.model || expected.serial_ending.empty() || expected.serial_ending != current.serial_ending)) {
        reason = "The destination lacks a matching persistent device identity.";
        return false;
    }
    return true;
}

std::optional<DeviceIdentity> rediscover_destructive_destination(const BenchProfile& bench, const DeviceIdentity& expected, std::string& reason) {
    for (auto current : apply_bench_policy(bench, discover_block_devices())) {
        std::string mismatch;
        const bool same_stable_device = (!expected.by_id_path.empty() && current.by_id_path == expected.by_id_path) ||
                                        (!expected.serial.empty() && current.serial == expected.serial) ||
                                        (expected.by_id_path.empty() && expected.serial.empty() && current.linux_path == expected.linux_path);
        if (!same_stable_device) {
            continue;
        }
        if (!destructive_identity_matches(expected, current, mismatch)) {
            reason = mismatch;
            return std::nullopt;
        }
        const auto findings = validate_destination_device(bench, current);
        if (has_blocker(findings)) {
            reason = "The rediscovered destination no longer passes destination-only bench policy.";
            return std::nullopt;
        }
        return current;
    }
    reason = "The selected destination could not be rediscovered by persistent identity.";
    return std::nullopt;
}

bool read_exact_at(int fd, std::uint64_t offset, std::vector<std::byte>& data, std::string& error) {
    std::size_t completed = 0;
    while (completed < data.size()) {
        const auto count = ::pread(fd, data.data() + completed, data.size() - completed, static_cast<off_t>(offset + completed));
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            error = std::strerror(errno);
            return false;
        }
        if (count == 0) {
            error = "Unexpected end of device during readback.";
            return false;
        }
        completed += static_cast<std::size_t>(count);
    }
    return true;
}

void add_unreadable_range(std::vector<UnreadableRange>& ranges, std::uint64_t offset, std::uint64_t length) {
    if (!ranges.empty() && ranges.back().offset + ranges.back().length == offset) {
        ranges.back().length += length;
    } else {
        ranges.push_back({offset, length});
    }
}

std::string render_bad_sector_map(const std::vector<UnreadableRange>& ranges) {
    std::ostringstream out;
    out << "format=offset-length-v1\n";
    for (const auto& range : ranges) out << range.offset << " " << range.length << "\n";
    return out.str();
}

std::vector<UnreadableRange> read_bad_sector_map(const fs::path& path) {
    std::vector<UnreadableRange> ranges;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.find('=') != std::string::npos) continue;
        std::istringstream fields(line);
        UnreadableRange range;
        if (fields >> range.offset >> range.length && range.length != 0) ranges.push_back(range);
    }
    return ranges;
}

bool read_source_with_policy(const SourceReadHandle& source, std::uint64_t offset, std::size_t length,
                             const ImageWriteOptions& options, std::vector<std::byte>& output,
                             std::vector<UnreadableRange>& unreadable, std::string& error) {
    for (std::uint32_t attempt = 0; attempt <= options.rescue_retries; ++attempt) {
        auto read = source.read_at(offset, length);
        if (read.error.empty() && read.data.size() == length) {
            output = std::move(read.data);
            return true;
        }
        error = read.error.empty() ? "The source returned fewer bytes than requested." : read.error;
        if (options.mode != ImagingMode::Rescue) {
            return false;
        }
    }
    const auto minimum = std::max<std::size_t>(source.device().logical_block_size, options.rescue_minimum_read);
    if (length <= minimum) {
        output.assign(length, std::byte{0});
        add_unreadable_range(unreadable, offset, length);
        return true;
    }
    std::size_t first_length = (length / 2 / minimum) * minimum;
    if (first_length == 0 || first_length >= length) first_length = length / 2;
    std::vector<std::byte> first;
    std::vector<std::byte> second;
    std::string first_error;
    std::string second_error;
    if (!read_source_with_policy(source, offset, first_length, options, first, unreadable, first_error) ||
        !read_source_with_policy(source, offset + first_length, length - first_length, options, second, unreadable, second_error)) {
        error = !first_error.empty() ? first_error : second_error;
        return false;
    }
    output = std::move(first);
    output.insert(output.end(), second.begin(), second.end());
    return true;
}

bool read_logical_image_range(std::ifstream& disk, const std::vector<ChunkHashRecord>& records,
                              CompressionMode compression, std::uint64_t offset, std::size_t length,
                              std::vector<std::byte>& output, std::string& error) {
    output.assign(length, std::byte{0});
    std::size_t copied = 0;
    const auto end = offset + length;
    for (const auto& record : records) {
        const auto record_end = record.source_offset + record.source_size;
        if (record_end <= offset) continue;
        if (record.source_offset >= end) break;
        std::vector<std::byte> stored(static_cast<std::size_t>(record.stored_size));
        disk.clear();
        disk.seekg(static_cast<std::streamoff>(record.stored_offset));
        disk.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
        if (!disk || static_cast<std::uint64_t>(disk.gcount()) != record.stored_size || sha256_hex(stored) != record.stored_hash) {
            error = "A stored image chunk failed while reading a logical range.";
            return false;
        }
        std::string decompress_error;
        const auto source_data = decompress_chunk(stored, record.source_size, compression, decompress_error);
        if (!decompress_error.empty() || sha256_hex(source_data) != record.source_hash) {
            error = decompress_error.empty() ? "A logical image chunk hash did not match." : decompress_error;
            return false;
        }
        const auto copy_start = std::max(offset, record.source_offset);
        const auto copy_end = std::min(end, record_end);
        const auto count = static_cast<std::size_t>(copy_end - copy_start);
        std::copy_n(source_data.begin() + static_cast<std::ptrdiff_t>(copy_start - record.source_offset), count,
                    output.begin() + static_cast<std::ptrdiff_t>(copy_start - offset));
        copied += count;
    }
    if (copied != length) {
        error = "The image hash map did not cover a requested logical range.";
        return false;
    }
    return true;
}

bool write_exact_at(int fd, std::uint64_t offset, const std::vector<std::byte>& data) {
    std::size_t completed = 0;
    while (completed < data.size()) {
        const auto count = ::pwrite(fd, data.data() + completed, data.size() - completed, static_cast<off_t>(offset + completed));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        completed += static_cast<std::size_t>(count);
    }
    return true;
}

void validate_image_recoverability(const fs::path& disk_path, const std::vector<ChunkHashRecord>& records,
                                   CompressionMode compression, std::uint64_t expected_bytes,
                                   std::uint32_t logical_block_size, ImageVerificationResult& result) {
    if (expected_bytes < 512 || records.empty()) return;
    logical_block_size = logical_block_size == 0 ? 512 : logical_block_size;
    std::ifstream disk(disk_path, std::ios::binary);
    std::string error;
    const auto prefix_size = static_cast<std::size_t>(std::min<std::uint64_t>(expected_bytes, 1024ULL * 1024ULL));
    std::vector<std::byte> prefix;
    if (!read_logical_image_range(disk, records, compression, 0, prefix_size, prefix, error)) {
        result.findings.push_back(finding(Severity::Blocker, "verify.partition_snapshot_read_failed", "Lazarus could not reconstruct the image partition-table region.", error));
        return;
    }
    const int memory_fd = ::memfd_create("lazarus-verify-layout", MFD_CLOEXEC);
    if (memory_fd < 0 || ::ftruncate(memory_fd, static_cast<off_t>(expected_bytes)) != 0 || !write_exact_at(memory_fd, 0, prefix)) {
        if (memory_fd >= 0) ::close(memory_fd);
        result.findings.push_back(finding(Severity::Blocker, "verify.layout_workspace_failed", "Lazarus could not create its read-only layout-validation workspace.", std::strerror(errno)));
        return;
    }

    std::vector<std::uint64_t> partition_starts;
    if (prefix.size() >= logical_block_size * 2ULL && byte_string_equals(prefix, logical_block_size, "EFI PART")) {
        const auto header_offset = static_cast<std::size_t>(logical_block_size);
        const auto entries_lba = le64(prefix, header_offset + 72);
        const auto entry_count = le32(prefix, header_offset + 80);
        const auto entry_size = le32(prefix, header_offset + 84);
        const auto entries_length = static_cast<std::uint64_t>(entry_count) * entry_size;
        if (entry_count == 0 || entry_size < 128 || entries_length > 4ULL * 1024ULL * 1024ULL ||
            entries_lba > UINT64_MAX / logical_block_size || entries_lba * logical_block_size + entries_length > expected_bytes) {
            ::close(memory_fd);
            result.findings.push_back(finding(Severity::Blocker, "verify.gpt_entries_invalid", "The image GPT entry-table location is invalid.", "Treat this image as corrupt."));
            return;
        }
        std::vector<std::byte> entries;
        if (!read_logical_image_range(disk, records, compression, entries_lba * logical_block_size,
                                      static_cast<std::size_t>(entries_length), entries, error) ||
            !write_exact_at(memory_fd, entries_lba * logical_block_size, entries)) {
            ::close(memory_fd);
            result.findings.push_back(finding(Severity::Blocker, "verify.gpt_entries_read_failed", "The image GPT entries could not be reconstructed.", error));
            return;
        }
        for (std::uint32_t index = 0; index < entry_count; ++index) {
            const auto entry_offset = static_cast<std::size_t>(index) * entry_size;
            if (entry_offset + 128 > entries.size()) break;
            if (!guid_is_zero(entries, entry_offset)) partition_starts.push_back(le64(entries, entry_offset + 32));
        }
        const auto backup_lba = le64(prefix, header_offset + 32);
        std::vector<std::byte> backup;
        if (backup_lba >= expected_bytes / logical_block_size ||
            !read_logical_image_range(disk, records, compression, backup_lba * logical_block_size,
                                      logical_block_size, backup, error) ||
            !write_exact_at(memory_fd, backup_lba * logical_block_size, backup)) {
            ::close(memory_fd);
            result.findings.push_back(finding(Severity::Blocker, "verify.gpt_backup_read_failed", "The backup GPT header could not be reconstructed from the image.", error));
            return;
        }
    } else if (prefix.size() >= 512 && prefix[510] == std::byte{0x55} && prefix[511] == std::byte{0xAA}) {
        for (std::size_t index = 0; index < 4; ++index) {
            const auto entry = 446 + index * 16;
            if (std::to_integer<unsigned char>(prefix[entry + 4]) != 0 && le32(prefix, entry + 12) != 0)
                partition_starts.push_back(le32(prefix, entry + 8));
        }
    }

    for (const auto lba : partition_starts) {
        if (lba >= expected_bytes / logical_block_size) continue;
        std::vector<std::byte> boot;
        if (!read_logical_image_range(disk, records, compression, lba * logical_block_size, logical_block_size, boot, error) ||
            !write_exact_at(memory_fd, lba * logical_block_size, boot)) {
            ::close(memory_fd);
            result.findings.push_back(finding(Severity::Blocker, "verify.partition_boot_read_failed", "A partition boot sector could not be reconstructed from the image.", error));
            return;
        }
    }

    DeviceIdentity image_device;
    image_device.linux_path = "memory:image-layout";
    image_device.physical_path = "memory:image-layout";
    image_device.size_bytes = expected_bytes;
    image_device.logical_block_size = logical_block_size;
    SourceReadHandle image_source(memory_fd, image_device);
    const auto inspection = inspect_source_disk(image_source);
    result.partition_table_valid = has_imageable_layout(inspection) && !has_blocker(inspection.findings);
    if (!result.partition_table_valid) {
        result.findings.insert(result.findings.end(), inspection.findings.begin(), inspection.findings.end());
        return;
    }

    bool recognized_filesystem = false;
    bool all_ntfs_mfts_readable = true;
    bool found_ntfs = false;
    for (const auto& partition : inspection.partitions) {
        std::vector<std::byte> boot;
        if (!read_logical_image_range(disk, records, compression, partition.first_lba * logical_block_size,
                                      logical_block_size, boot, error)) continue;
        if (byte_string_equals_at(boot, 3, "-FVE-FS-")) {
            result.bitlocker_detected = true;
            result.findings.push_back(finding(Severity::Blocker, "verify.bitlocker_locked", "A BitLocker-protected partition was detected, but Lazarus cannot yet unlock and browse it.", "Keep the raw image, obtain the recovery key, and verify it with a BitLocker-capable recovery workflow."));
            continue;
        }
        const auto filesystem = detect_filesystem_boot_sector(boot);
        if (filesystem != FileSystemKind::Unknown) recognized_filesystem = true;
        if (filesystem != FileSystemKind::Ntfs) continue;
        found_ntfs = true;
        if (boot.size() < 512) {
            all_ntfs_mfts_readable = false;
            continue;
        }
        const auto bytes_per_sector = le16(boot, 11);
        const auto sectors_per_cluster = std::to_integer<unsigned char>(boot[13]);
        const auto mft_lcn = le64(boot, 48);
        const auto record_code = static_cast<std::int8_t>(std::to_integer<unsigned char>(boot[64]));
        if (!is_valid_fat_bytes_per_sector(bytes_per_sector) || sectors_per_cluster == 0 || mft_lcn == 0) {
            all_ntfs_mfts_readable = false;
            continue;
        }
        const auto cluster_size = static_cast<std::uint64_t>(bytes_per_sector) * sectors_per_cluster;
        const auto record_size = record_code < 0 ? (1ULL << static_cast<unsigned>(-record_code)) : cluster_size * static_cast<unsigned char>(record_code);
        const auto mft_offset = partition.first_lba * logical_block_size + mft_lcn * cluster_size;
        if (record_size < 512 || record_size > 64 * 1024 || mft_offset + record_size > expected_bytes) {
            all_ntfs_mfts_readable = false;
            continue;
        }
        std::vector<std::byte> mft;
        if (!read_logical_image_range(disk, records, compression, mft_offset, static_cast<std::size_t>(record_size), mft, error) ||
            !byte_string_equals(mft, 0, "FILE")) all_ntfs_mfts_readable = false;
    }
    result.ntfs_mft_readable = found_ntfs && all_ntfs_mfts_readable;
    result.filesystem_readable = recognized_filesystem && (!found_ntfs || all_ntfs_mfts_readable) && !result.bitlocker_detected;
    if (found_ntfs && !all_ntfs_mfts_readable)
        result.findings.push_back(finding(Severity::Blocker, "verify.ntfs_mft_unreadable", "An NTFS partition was detected but its first MFT record could not be read.", "Do not treat this image as recoverable."));
    if (!recognized_filesystem && !result.bitlocker_detected)
        result.findings.push_back(finding(Severity::Blocker, "verify.filesystem_unrecognized", "No supported filesystem could be reopened from the image.", "Do not treat this image as recoverable."));
}

std::uint64_t safe_partition_size_bytes(std::uint64_t first_lba, std::uint64_t last_lba, std::uint32_t sector_size) {
    if (last_lba < first_lba) {
        return 0;
    }
    return (last_lba - first_lba + 1) * static_cast<std::uint64_t>(sector_size);
}

std::uint64_t disk_lba_count(std::uint64_t size_bytes, std::uint32_t sector_size) {
    if (sector_size == 0) {
        return 0;
    }
    return size_bytes / static_cast<std::uint64_t>(sector_size);
}

}  // namespace

SourceReadHandle::SourceReadHandle(int fd, DeviceIdentity device)
    : fd_(fd), device_(std::move(device)) {}

SourceReadHandle::~SourceReadHandle() {
    close();
}

SourceReadHandle::SourceReadHandle(SourceReadHandle&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), device_(std::move(other.device_)) {}

SourceReadHandle& SourceReadHandle::operator=(SourceReadHandle&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = std::exchange(other.fd_, -1);
        device_ = std::move(other.device_);
    }
    return *this;
}

bool SourceReadHandle::is_open() const {
    return fd_ >= 0;
}

int SourceReadHandle::native_handle() const {
    return fd_;
}

const DeviceIdentity& SourceReadHandle::device() const {
    return device_;
}

SourceRead SourceReadHandle::read_at(std::uint64_t offset, std::size_t byte_count) const {
    SourceRead result;
    if (!is_open()) {
        result.error = "Source handle is not open.";
        return result;
    }

    result.data.resize(byte_count);
    const auto bytes_read = ::pread(fd_, result.data.data(), result.data.size(), static_cast<off_t>(offset));
    if (bytes_read < 0) {
        result.data.clear();
        result.error = std::strerror(errno);
        return result;
    }
    result.data.resize(static_cast<std::size_t>(bytes_read));
    return result;
}

void SourceReadHandle::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

DestinationWriteHandle::DestinationWriteHandle(int fd, DeviceIdentity device)
    : fd_(fd), device_(std::move(device)) {}

DestinationWriteHandle::~DestinationWriteHandle() {
    close();
}

DestinationWriteHandle::DestinationWriteHandle(DestinationWriteHandle&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)), device_(std::move(other.device_)) {}

DestinationWriteHandle& DestinationWriteHandle::operator=(DestinationWriteHandle&& other) noexcept {
    if (this != &other) {
        close();
        fd_ = std::exchange(other.fd_, -1);
        device_ = std::move(other.device_);
    }
    return *this;
}

bool DestinationWriteHandle::is_open() const {
    return fd_ >= 0;
}

int DestinationWriteHandle::native_handle() const {
    return fd_;
}

const DeviceIdentity& DestinationWriteHandle::device() const {
    return device_;
}

bool DestinationWriteHandle::write_at(std::uint64_t offset, const std::vector<std::byte>& data, std::string& error) const {
    if (!is_open()) {
        error = "Destination handle is not open.";
        return false;
    }

    std::size_t written = 0;
    while (written < data.size()) {
        const auto rc = ::pwrite(fd_, data.data() + written, data.size() - written, static_cast<off_t>(offset + written));
        if (rc < 0) {
            error = std::strerror(errno);
            return false;
        }
        if (rc == 0) {
            error = "Destination accepted zero bytes during write.";
            return false;
        }
        written += static_cast<std::size_t>(rc);
    }
    return true;
}

bool DestinationWriteHandle::flush(std::string& error) const {
    if (!is_open()) {
        error = "Destination handle is not open.";
        return false;
    }
    if (::fsync(fd_) != 0) {
        error = std::strerror(errno);
        return false;
    }
    return true;
}

void DestinationWriteHandle::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

std::string version() {
    return "Arcology Lazarus 0.1.0-scaffold";
}

std::string to_string(DeviceRole role) {
    switch (role) {
        case DeviceRole::Unknown:
            return "unknown";
        case DeviceRole::SourceOnly:
            return "source-only";
        case DeviceRole::DestinationOnly:
            return "destination-only";
        case DeviceRole::ImageStorage:
            return "image-storage";
        case DeviceRole::RemovableMedia:
            return "removable-media";
        case DeviceRole::Ignored:
            return "ignored";
        case DeviceRole::SystemDisk:
            return "system-disk";
    }
    return "unknown";
}

std::string to_string(ImagingMode mode) {
    switch (mode) {
        case ImagingMode::Standard:
            return "standard";
        case ImagingMode::Raw:
            return "raw";
        case ImagingMode::Rescue:
            return "rescue";
    }
    return "standard";
}

std::string to_string(CompressionMode mode) {
    switch (mode) {
        case CompressionMode::None:
            return "none";
        case CompressionMode::Zstd:
            return "zstd";
    }
    return "none";
}

std::string to_string(Severity severity) {
    switch (severity) {
        case Severity::Info:
            return "info";
        case Severity::Warning:
            return "warning";
        case Severity::Blocker:
            return "blocker";
    }
    return "info";
}

std::string to_string(PartitionKind kind) {
    switch (kind) {
        case PartitionKind::Unknown:
            return "unknown";
        case PartitionKind::EfiSystem:
            return "efi-system";
        case PartitionKind::MicrosoftReserved:
            return "microsoft-reserved";
        case PartitionKind::WindowsBasicData:
            return "windows-basic-data";
        case PartitionKind::WindowsRecovery:
            return "windows-recovery";
    }
    return "unknown";
}

std::string to_string(FileSystemKind kind) {
    switch (kind) {
        case FileSystemKind::Unknown:
            return "unknown";
        case FileSystemKind::Ntfs:
            return "ntfs";
        case FileSystemKind::Fat12:
            return "fat12";
        case FileSystemKind::Fat16:
            return "fat16";
        case FileSystemKind::Fat32:
            return "fat32";
        case FileSystemKind::Exfat:
            return "exfat";
    }
    return "unknown";
}

bool is_complete(const JobInfo& job) {
    return !blank(job.ticket_number) && !blank(job.customer_name) && !blank(job.technician) && !blank(job.purpose);
}

std::vector<SafetyFinding> validate_job(const JobInfo& job) {
    std::vector<SafetyFinding> findings;
    if (blank(job.ticket_number)) {
        findings.push_back(finding(Severity::Blocker, "job.ticket_missing", "No ticket number was provided.", "Enter the repair ticket number before continuing."));
    }
    if (blank(job.customer_name)) {
        findings.push_back(finding(Severity::Blocker, "job.customer_missing", "No customer name was provided.", "Enter the customer name before continuing."));
    }
    if (blank(job.technician)) {
        findings.push_back(finding(Severity::Blocker, "job.technician_missing", "No technician identity was provided.", "Enter the technician identity before continuing."));
    }
    if (blank(job.purpose)) {
        findings.push_back(finding(Severity::Blocker, "job.purpose_missing", "No job purpose was selected.", "Select the purpose of this Lazarus job."));
    }
    return findings;
}

std::vector<SafetyFinding> validate_bench_profile(const BenchProfile& bench) {
    std::vector<SafetyFinding> findings;
    if (blank(bench.name)) {
        findings.push_back(finding(Severity::Blocker, "bench.name_missing", "The bench profile has no name.", "Set a bench profile name before using Bench Mode."));
    }
    if (bench.image_storage_paths.empty()) {
        findings.push_back(finding(Severity::Blocker, "bench.storage_missing", "The bench profile has no image storage path.", "Set image storage before imaging."));
    }
    if (bench.source_only_paths.empty()) {
        findings.push_back(finding(Severity::Blocker, "bench.sources_missing", "The bench profile has no source-only ports.", "Define at least one source-only port."));
    }
    if (bench.destination_only_paths.empty()) {
        findings.push_back(finding(Severity::Warning, "bench.destinations_missing", "The bench profile has no destination-only ports.", "Define destination-only ports before enabling restore or clone workflows."));
    }

    struct RolePaths {
        const char* name;
        const std::vector<std::string>* paths;
    };
    const std::vector<RolePaths> role_paths = {
        {"source-only", &bench.source_only_paths},
        {"destination-only", &bench.destination_only_paths},
        {"image-storage", &bench.image_storage_port_paths},
        {"removable-media", &bench.removable_media_paths},
        {"ignored", &bench.ignored_paths},
    };
    for (std::size_t left = 0; left < role_paths.size(); ++left) {
        for (std::size_t right = left + 1; right < role_paths.size(); ++right) {
            for (const auto& identity : *role_paths[left].paths) {
                if (contains_path(*role_paths[right].paths, identity)) {
                    findings.push_back(finding(
                        Severity::Blocker,
                        "bench.role_conflict",
                        "A physical path is configured as both " + std::string(role_paths[left].name) +
                            " and " + role_paths[right].name + ".",
                        "Assign each physical port exactly one role."));
                }
            }
        }
    }
    return findings;
}

std::string physical_port_identity(const std::string& by_path) {
    if (by_path.empty()) return {};
    if (by_path.rfind("port:", 0) == 0) return by_path;

    std::string name = std::filesystem::path(by_path).filename().string();
    name = std::regex_replace(name, std::regex("-part[0-9]+$"), "");
    const auto usb = name.find("-usb-");
    if (usb != std::string::npos) {
        const auto bus_separator = name.find(':', usb + 5);
        const auto interface_separator = bus_separator == std::string::npos
            ? std::string::npos : name.find(':', bus_separator + 1);
        if (interface_separator != std::string::npos) {
            name.resize(interface_separator);
        }
    }
    return "port:" + name;
}

DeviceRole role_for_device(const BenchProfile& bench, const DeviceIdentity& device) {
    if (device.is_system_disk || device.bench_role == DeviceRole::SystemDisk) {
        return DeviceRole::SystemDisk;
    }
    if (contains_port_identity(bench.image_storage_port_paths, device) ||
        (bench.image_storage_port_paths.empty() && !bench.image_storage_device.empty() &&
         contains_any_identity({bench.image_storage_device}, device))) {
        return DeviceRole::ImageStorage;
    }
    if (contains_port_identity(bench.ignored_paths, device) || contains_legacy_device_role(bench.ignored_paths, device)) {
        return DeviceRole::Ignored;
    }
    if (contains_port_identity(bench.source_only_paths, device) || contains_legacy_device_role(bench.source_only_paths, device)) {
        return DeviceRole::SourceOnly;
    }
    if (contains_port_identity(bench.destination_only_paths, device) || contains_legacy_device_role(bench.destination_only_paths, device)) {
        return DeviceRole::DestinationOnly;
    }
    if (contains_port_identity(bench.removable_media_paths, device) || contains_legacy_device_role(bench.removable_media_paths, device)) {
        return DeviceRole::RemovableMedia;
    }
    return DeviceRole::Unknown;
}

std::string label_for_device(const BenchProfile& bench, const DeviceIdentity& device) {
    for (const auto& port_label : bench.port_labels) {
        if (contains_port_identity({port_label.identity}, device)) {
            return port_label.label;
        }
    }
    return "";
}

DeviceIdentity apply_bench_policy(const BenchProfile& bench, DeviceIdentity device) {
    device.bench_role = role_for_device(bench, device);
    return device;
}

std::vector<DeviceIdentity> apply_bench_policy(const BenchProfile& bench, const std::vector<DeviceIdentity>& devices) {
    std::vector<DeviceIdentity> assigned;
    assigned.reserve(devices.size());
    for (auto device : devices) {
        assigned.push_back(apply_bench_policy(bench, std::move(device)));
    }
    return assigned;
}

BenchProfile load_bench_profile(const std::string& path) {
    BenchProfile bench;
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        line = strip_comment(line);
        if (line.empty()) {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        auto key = trim(line.substr(0, equals));
        auto value = trim(line.substr(equals + 1));
        if (key == "name") {
            bench.name = value;
        } else if (key == "branding_theme") {
            bench.branding.name = value;
        } else if (key == "branding_product_name") {
            bench.branding.product_name = value;
        } else if (key == "branding_subtitle") {
            bench.branding.subtitle = value;
        } else if (key == "branding_accent") {
            bench.branding.accent = value;
        } else if (key == "branding_background") {
            bench.branding.background = value;
        } else if (key == "branding_surface") {
            bench.branding.surface = value;
        } else if (key == "branding_text") {
            bench.branding.text = value;
        } else if (key == "branding_icon") {
            bench.branding.icon_color = value;
        } else if (key == "branding_logo") {
            bench.branding.logo_path = value;
        } else if (key == "branding_report_footer") {
            bench.branding.report_footer = value;
        } else if (key == "image_storage") {
            if (bench.image_storage_path.empty()) {
                bench.image_storage_path = value;
            }
            bench.image_storage_paths.push_back(value);
        } else if (key == "image_storage_device") {
            bench.image_storage_device = value;
        } else if (key == "image_storage_volume") {
            bench.image_storage_volume = value;
        } else if (key == "image_storage_port") {
            bench.image_storage_port_paths.push_back(value);
        } else if (key == "source") {
            bench.source_only_paths.push_back(value);
        } else if (key == "destination") {
            bench.destination_only_paths.push_back(value);
        } else if (key == "removable_media" || key == "removable") {
            bench.removable_media_paths.push_back(value);
        } else if (key == "ignored") {
            bench.ignored_paths.push_back(value);
        } else if (key == "port_label" || key == "label") {
            const auto separator = value.find('|');
            if (separator != std::string::npos) {
                PortLabel port_label;
                port_label.identity = trim(value.substr(0, separator));
                port_label.label = trim(value.substr(separator + 1));
                if (!port_label.identity.empty() && !port_label.label.empty()) {
                    bench.port_labels.push_back(std::move(port_label));
                }
            }
        }
    }
    if (bench.image_storage_path.empty() && !bench.image_storage_paths.empty()) {
        bench.image_storage_path = bench.image_storage_paths.front();
    }
    return bench;
}

std::vector<SafetyFinding> validate_source_device(const BenchProfile& bench, const DeviceIdentity& device) {
    std::vector<SafetyFinding> findings;
    const auto assigned_role = role_for_device(bench, device);
    if (device.is_system_disk || assigned_role == DeviceRole::SystemDisk) {
        findings.push_back(finding(Severity::Blocker, "source.system_disk", "The selected source appears to be the running system disk.", "Select an offline customer drive connected to a source-only port."));
    }
    if (assigned_role == DeviceRole::DestinationOnly) {
        findings.push_back(finding(Severity::Blocker, "source.destination_port", "The selected source is connected to a destination-only port.", "Move the customer drive to a source-only port."));
    }
    if (assigned_role == DeviceRole::Ignored) {
        findings.push_back(finding(Severity::Blocker, "source.ignored_port", "The selected source is connected to an ignored port.", "Move the customer drive to a source-only port."));
    }
    const bool marked_source = assigned_role == DeviceRole::SourceOnly;
    if (!marked_source) {
        findings.push_back(finding(Severity::Blocker, "source.not_source_port", "The selected drive is not on a source-only port.", "Connect the customer drive to a configured source-only port."));
    }
    if (blank(device.linux_path) || blank(device.physical_path)) {
        findings.push_back(finding(Severity::Blocker, "source.identity_incomplete", "The selected drive does not have enough persistent identity information.", "Rescan devices and require a persistent physical path before continuing."));
    }
    return findings;
}

std::vector<SafetyFinding> validate_destination_device(const BenchProfile& bench, const DeviceIdentity& device) {
    std::vector<SafetyFinding> findings;
    const auto assigned_role = role_for_device(bench, device);
    if (device.is_system_disk || assigned_role == DeviceRole::SystemDisk) {
        findings.push_back(finding(Severity::Blocker, "destination.system_disk", "The selected destination appears to be the running system disk.", "Select an offline destination drive connected to a destination-only port."));
    }
    if (assigned_role == DeviceRole::SourceOnly) {
        findings.push_back(finding(Severity::Blocker, "destination.source_port", "The selected destination is connected to a source-only port.", "Move the destination drive to a destination-only port."));
    }
    if (assigned_role == DeviceRole::Ignored) {
        findings.push_back(finding(Severity::Blocker, "destination.ignored_port", "The selected destination is connected to an ignored port.", "Move the destination drive to a destination-only port."));
    }
    if (assigned_role != DeviceRole::DestinationOnly) {
        findings.push_back(finding(Severity::Blocker, "destination.not_destination_port", "The selected drive is not on a destination-only port.", "Connect the destination drive to a configured destination-only port."));
    }
    if (blank(device.linux_path) || blank(device.physical_path)) {
        findings.push_back(finding(Severity::Blocker, "destination.identity_incomplete", "The selected drive does not have enough persistent identity information.", "Rescan devices and require a persistent physical path before continuing."));
    }
    if (device.size_bytes == 0) {
        findings.push_back(finding(Severity::Blocker, "destination.size_unknown", "The selected destination reports zero bytes.", "Reconnect the destination and verify the kernel reports its full capacity."));
    }
    return findings;
}

ImagePlan create_backup_plan(const JobInfo& job, const BenchProfile& bench, const DeviceIdentity& source, ImagingMode mode) {
    ImagePlan plan;
    plan.job = job;
    plan.source = apply_bench_policy(bench, source);
    plan.image_storage_path = bench.image_storage_path;
    plan.mode = mode;
    plan.source_open_read_only = true;

    auto job_findings = validate_job(job);
    plan.findings.insert(plan.findings.end(), job_findings.begin(), job_findings.end());

    auto source_findings = validate_source_device(bench, plan.source);
    plan.findings.insert(plan.findings.end(), source_findings.begin(), source_findings.end());

    if (bench.image_storage_paths.empty()) {
        plan.findings.push_back(finding(Severity::Blocker, "storage.missing", "No image storage path is configured.", "Configure central Lazarus image storage before creating a backup."));
    }

    return plan;
}

SourceOpenResult open_source_read_only(const BenchProfile& bench, DeviceIdentity source) {
    SourceOpenResult result;
    source = apply_bench_policy(bench, std::move(source));
    result.findings = validate_bench_profile(bench);
    auto source_findings = validate_source_device(bench, source);
    result.findings.insert(result.findings.end(), source_findings.begin(), source_findings.end());
    if (has_blocker(result.findings)) {
        return result;
    }

    struct stat before{};
    if (::lstat(source.linux_path.c_str(), &before) != 0 || S_ISLNK(before.st_mode)) {
        result.findings.push_back(finding(Severity::Blocker, "source.path_unsafe", "The selected source path is missing or is a symbolic link.", "Rescan and open the canonical block-device node."));
        return result;
    }
    const int fd = ::open(source.linux_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        result.findings.push_back(finding(Severity::Blocker, "source.open_failed", "Lazarus could not open the selected source read-only.", std::strerror(errno)));
        return result;
    }
    struct stat after{};
    if (::fstat(fd, &after) != 0 || before.st_dev != after.st_dev || before.st_ino != after.st_ino || before.st_rdev != after.st_rdev) {
        ::close(fd);
        result.findings.push_back(finding(Severity::Blocker, "source.path_changed", "The source device node changed while it was being opened.", "Rescan devices before continuing."));
        return result;
    }

    result.handle = SourceReadHandle(fd, std::move(source));
    result.findings.push_back(finding(Severity::Info, "source.open_read_only", "The source was opened with read-only flags.", "Continue using the read-only source handle."));
    return result;
}

DestinationOpenResult open_destination_write_only(const BenchProfile& bench, DeviceIdentity destination) {
    DestinationOpenResult result;
    destination = apply_bench_policy(bench, std::move(destination));
    result.findings = validate_destination_device(bench, destination);
    if (has_blocker(result.findings)) {
        return result;
    }

    struct stat before{};
    if (::lstat(destination.linux_path.c_str(), &before) != 0 || S_ISLNK(before.st_mode)) {
        result.findings.push_back(finding(Severity::Blocker, "destination.path_unsafe", "The destination path is missing or is a symbolic link.", "Rescan and use the canonical block-device node."));
        return result;
    }
    const int fd = ::open(destination.linux_path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_EXCL);
    if (fd < 0) {
        result.findings.push_back(finding(Severity::Blocker, "destination.open_failed", "Lazarus could not open the selected destination write-only.", std::strerror(errno)));
        return result;
    }
    struct stat after{};
    if (::fstat(fd, &after) != 0 || before.st_dev != after.st_dev || before.st_ino != after.st_ino || before.st_rdev != after.st_rdev) {
        ::close(fd);
        result.findings.push_back(finding(Severity::Blocker, "destination.path_changed", "The destination device node changed while it was being opened.", "No writes occurred; rescan devices before continuing."));
        return result;
    }
    if (S_ISBLK(after.st_mode)) {
        std::uint64_t actual_size = 0;
        if (::ioctl(fd, BLKGETSIZE64, &actual_size) != 0 || actual_size != destination.size_bytes) {
            ::close(fd);
            result.findings.push_back(finding(Severity::Blocker, "destination.open_size_mismatch", "The opened destination capacity does not match the selected device.", "No writes occurred; rescan devices and confirm the destination identity."));
            return result;
        }
    }

    result.handle = DestinationWriteHandle(fd, std::move(destination));
    result.findings.push_back(finding(Severity::Info, "destination.open_write_only", "The destination was opened with write-only flags.", "Only continue after explicit destructive confirmation."));
    return result;
}

DiskInspection inspect_source_disk(const SourceReadHandle& source) {
    DiskInspection inspection;
    inspection.source = source.device();
    inspection.logical_block_size = inspection.source.logical_block_size == 0 ? 512 : inspection.source.logical_block_size;

    if (!source.is_open()) {
        inspection.findings.push_back(finding(Severity::Blocker, "inspect.source_closed", "The source handle is not open.", "Open an approved source read-only before inspecting it."));
        return inspection;
    }

    const auto first_sector = source.read_at(0, inspection.logical_block_size);
    if (!first_sector.error.empty() || first_sector.data.size() < 512) {
        inspection.findings.push_back(finding(Severity::Blocker, "inspect.first_sector_failed", "Lazarus could not read the first sector.", first_sector.error.empty() ? "Retry with a stable connection or Rescue Mode." : first_sector.error));
        return inspection;
    }

    inspection.first_sector_read = true;
    inspection.facts.push_back("First sector was read successfully.");
    inspection.mbr_signature_valid = first_sector.data[510] == std::byte{0x55} && first_sector.data[511] == std::byte{0xAA};
    inspection.protective_mbr = inspection.mbr_signature_valid && first_sector.data[450] == std::byte{0xEE};
    if (inspection.protective_mbr) {
        inspection.facts.push_back("Protective MBR marker was detected.");
    } else if (inspection.mbr_signature_valid) {
        inspection.facts.push_back("MBR boot-sector signature was detected.");
    } else {
        inspection.findings.push_back(finding(Severity::Blocker, "inspect.mbr_signature_missing", "No MBR boot-sector signature was detected in the first sector.", "This disk layout is not supported by the MVP inspector."));
        return inspection;
    }

    const auto header_offset = static_cast<std::uint64_t>(inspection.logical_block_size);
    const auto header = source.read_at(header_offset, inspection.logical_block_size);
    if (!header.error.empty() || header.data.size() < 92) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.header_read_failed", "Lazarus could not read the GPT header sector.", header.error.empty() ? "Retry with a stable connection or Rescue Mode." : header.error));
        return inspection;
    }

    if (!byte_string_equals(header.data, 0, "EFI PART")) {
        const auto whole_device_filesystem = detect_filesystem_boot_sector(first_sector.data);
        if (whole_device_filesystem != FileSystemKind::Unknown) {
            PartitionInfo partition;
            partition.number = 0;
            partition.kind = PartitionKind::WindowsBasicData;
            partition.type_guid = "whole-disk";
            partition.name = filesystem_display_name(whole_device_filesystem);
            partition.first_lba = 0;
            const auto disk_lbas = disk_lba_count(inspection.source.size_bytes, inspection.logical_block_size);
            partition.last_lba = disk_lbas == 0 ? 0 : disk_lbas - 1;
            partition.size_bytes = inspection.source.size_bytes;
            partition.filesystem = whole_device_filesystem;
            partition.ntfs_detected = whole_device_filesystem == FileSystemKind::Ntfs;
            inspection.partitions.push_back(partition);
            inspection.facts.push_back("GPT header was not detected.");
            inspection.facts.push_back("A whole-device filesystem boot sector was detected at LBA 0.");
            return inspection;
        }

        inspection.mbr_detected = true;
        inspection.facts.push_back("GPT header was not detected; parsing primary MBR partition entries.");
        const auto disk_lbas = disk_lba_count(inspection.source.size_bytes, inspection.logical_block_size);

        for (std::uint32_t index = 0; index < 4; ++index) {
            const auto entry = 446 + (index * 16);
            const auto type = std::to_integer<unsigned char>(first_sector.data[entry + 4]);
            const auto first_lba = static_cast<std::uint64_t>(le32(first_sector.data, entry + 8));
            const auto sector_count = static_cast<std::uint64_t>(le32(first_sector.data, entry + 12));
            if (type == 0 || sector_count == 0) {
                continue;
            }
            if (type == 0xEE) {
                inspection.findings.push_back(finding(Severity::Warning, "mbr.protective_without_gpt", "A protective MBR partition exists but no GPT header was detected.", "Treat this disk layout as suspicious until inspected with a dedicated partition tool."));
                continue;
            }
            if (is_extended_mbr_type(type)) {
                inspection.findings.push_back(finding(Severity::Warning, "mbr.extended_partition_unsupported", "An extended MBR partition was detected.", "Logical partitions are not parsed by the MVP inspector yet."));
            }

            PartitionInfo partition;
            partition.number = index + 1;
            partition.kind = partition_kind_from_mbr_type(type);
            partition.type_guid = mbr_type_string(type);
            partition.name = partition.kind == PartitionKind::WindowsBasicData ? "MBR Windows/NTFS candidate" : "MBR partition";
            partition.first_lba = first_lba;
            partition.last_lba = first_lba + sector_count - 1;
            partition.size_bytes = static_cast<std::uint64_t>(sector_count) * inspection.logical_block_size;

            if (partition.first_lba == 0 || partition.last_lba < partition.first_lba || partition.size_bytes == 0 || (disk_lbas != 0 && partition.last_lba >= disk_lbas)) {
                inspection.findings.push_back(finding(Severity::Blocker, "mbr.partition_bounds_invalid", "An MBR partition entry points outside the source disk.", "Stop before imaging this disk with the MVP path; the partition table may be stale or corrupt."));
                continue;
            }

            const auto boot_sector = source.read_at(partition.first_lba * static_cast<std::uint64_t>(inspection.logical_block_size), inspection.logical_block_size);
            if (!boot_sector.error.empty()) {
                inspection.findings.push_back(finding(Severity::Warning, "filesystem.boot_sector_read_failed", "Lazarus could not read a partition boot sector.", boot_sector.error));
            } else {
                if (byte_string_equals_at(boot_sector.data, 3, "-FVE-FS-")) {
                    inspection.findings.push_back(finding(Severity::Warning, "windows.bitlocker_detected", "A BitLocker volume signature was detected.", "Raw imaging can continue, but filesystem recovery requires the BitLocker recovery key."));
                }
                partition.filesystem = detect_filesystem_boot_sector(boot_sector.data);
                partition.ntfs_detected = partition.filesystem == FileSystemKind::Ntfs;
                if (partition.filesystem != FileSystemKind::Unknown) {
                    partition.kind = PartitionKind::WindowsBasicData;
                    partition.name = filesystem_display_name(partition.filesystem);
                }
            }

            inspection.partitions.push_back(partition);
        }

        if (inspection.partitions.empty()) {
            inspection.findings.push_back(finding(Severity::Blocker, "mbr.no_partitions", "MBR was detected but no supported primary partition entries were found.", "This disk layout is not supported by the MVP inspector."));
        } else {
            inspection.facts.push_back("Primary MBR partition entries were parsed successfully.");
            const auto has_supported_filesystem = std::any_of(inspection.partitions.begin(), inspection.partitions.end(), [](const PartitionInfo& partition) {
                return partition.filesystem != FileSystemKind::Unknown;
            });
            if (has_supported_filesystem) {
                inspection.facts.push_back("At least one supported filesystem boot sector was detected.");
            } else {
                inspection.findings.push_back(finding(Severity::Warning, "filesystem.not_detected", "No supported filesystem boot sector was detected.", "Raw imaging can continue, but filesystem-specific recovery is not available for this layout yet."));
            }
        }
        return inspection;
    }

    inspection.gpt_detected = true;
    inspection.facts.push_back("GPT header signature was detected at LBA 1.");

    const auto header_size = le32(header.data, 12);
    const auto current_lba = le64(header.data, 24);
    const auto backup_lba = le64(header.data, 32);
    inspection.first_usable_lba = le64(header.data, 40);
    inspection.last_usable_lba = le64(header.data, 48);
    const auto entries_lba = le64(header.data, 72);
    const auto entry_count = le32(header.data, 80);
    const auto entry_size = le32(header.data, 84);
    const auto expected_header_crc = le32(header.data, 16);
    const auto expected_entries_crc = le32(header.data, 88);
    const auto disk_lbas = disk_lba_count(inspection.source.size_bytes, inspection.logical_block_size);

    if (header_size < 92 || header_size > inspection.logical_block_size) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.header_size_invalid", "The GPT header size is outside the supported range.", "Stop and inspect the disk layout with a dedicated partition tool."));
        return inspection;
    }
    if (current_lba != 1) {
        inspection.findings.push_back(finding(Severity::Warning, "gpt.current_lba_unexpected", "The GPT header did not report current LBA 1.", "Treat this disk layout as suspicious until verified."));
    }
    if (backup_lba == 0 || inspection.last_usable_lba < inspection.first_usable_lba ||
        (disk_lbas != 0 && (backup_lba >= disk_lbas || inspection.last_usable_lba >= disk_lbas))) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.bounds_invalid", "The GPT usable LBA range is invalid.", "Stop and inspect the disk layout with a dedicated partition tool."));
        return inspection;
    }

    auto header_for_crc = header.data;
    header_for_crc[16] = std::byte{0};
    header_for_crc[17] = std::byte{0};
    header_for_crc[18] = std::byte{0};
    header_for_crc[19] = std::byte{0};
    if (expected_header_crc == 0 || crc32_bytes(header_for_crc, header_size) != expected_header_crc) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.header_crc_invalid", "The primary GPT header CRC does not match its contents.", "Stop and recover the partition table before relying on this disk layout."));
        return inspection;
    }
    if (entries_lba == 0 || entry_count == 0 || entry_size < 128 || entry_size > 4096) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.entries_invalid", "The GPT partition-entry table metadata is invalid.", "Stop and inspect the disk layout with a dedicated partition tool."));
        return inspection;
    }

    const auto max_entries_to_read = std::min<std::uint32_t>(entry_count, 1024);
    const auto entries_bytes = static_cast<std::uint64_t>(entry_count) * entry_size;
    if (entries_bytes > 4ULL * 1024ULL * 1024ULL) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.entries_too_large", "The GPT partition-entry table is larger than the MVP inspection limit.", "Stop and inspect the disk layout with a dedicated partition tool."));
        return inspection;
    }

    const auto entries = source.read_at(entries_lba * static_cast<std::uint64_t>(inspection.logical_block_size), static_cast<std::size_t>(entries_bytes));
    if (!entries.error.empty() || entries.data.size() < entries_bytes) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.entries_read_failed", "Lazarus could not read the GPT partition-entry table.", entries.error.empty() ? "Retry with a stable connection or Rescue Mode." : entries.error));
        return inspection;
    }
    if (expected_entries_crc == 0 || crc32_bytes(entries.data, static_cast<std::size_t>(entries_bytes)) != expected_entries_crc) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.entries_crc_invalid", "The primary GPT partition-entry array CRC does not match its contents.", "Stop and recover the partition table before relying on this disk layout."));
        return inspection;
    }

    const auto backup = source.read_at(backup_lba * static_cast<std::uint64_t>(inspection.logical_block_size), inspection.logical_block_size);
    if (!backup.error.empty() || backup.data.size() < inspection.logical_block_size || !byte_string_equals(backup.data, 0, "EFI PART")) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.backup_header_missing", "The backup GPT header could not be read at the declared final LBA.", "Stop and repair the GPT before relying on this disk layout."));
        return inspection;
    }
    const auto backup_header_size = le32(backup.data, 12);
    const auto backup_expected_crc = le32(backup.data, 16);
    auto backup_for_crc = backup.data;
    backup_for_crc[16] = std::byte{0};
    backup_for_crc[17] = std::byte{0};
    backup_for_crc[18] = std::byte{0};
    backup_for_crc[19] = std::byte{0};
    if (backup_header_size < 92 || backup_header_size > inspection.logical_block_size ||
        backup_expected_crc == 0 || crc32_bytes(backup_for_crc, backup_header_size) != backup_expected_crc ||
        le64(backup.data, 24) != backup_lba || le64(backup.data, 32) != 1) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.backup_header_invalid", "The backup GPT header failed CRC or cross-reference validation.", "Stop and repair the GPT before relying on this disk layout."));
        return inspection;
    }
    inspection.gpt_header_valid = true;
    inspection.facts.push_back("Primary and backup GPT headers and the partition-entry array passed CRC validation.");

    for (std::uint32_t index = 0; index < max_entries_to_read; ++index) {
        const auto offset = static_cast<std::size_t>(index) * entry_size;
        if (offset + 128 > entries.data.size()) {
            break;
        }
        if (guid_is_zero(entries.data, offset)) {
            continue;
        }

        PartitionInfo partition;
        partition.number = index + 1;
        partition.type_guid = format_guid(entries.data, offset);
        partition.unique_guid = format_guid(entries.data, offset + 16);
        partition.kind = partition_kind_from_guid(partition.type_guid);
        partition.first_lba = le64(entries.data, offset + 32);
        partition.last_lba = le64(entries.data, offset + 40);
        partition.size_bytes = safe_partition_size_bytes(partition.first_lba, partition.last_lba, inspection.logical_block_size);
        partition.name = utf16le_name_to_ascii(entries.data, offset + 56, std::min<std::uint32_t>(entry_size - 56, 72));

        if (partition.first_lba < inspection.first_usable_lba || partition.last_lba > inspection.last_usable_lba || partition.last_lba < partition.first_lba) {
            inspection.findings.push_back(finding(Severity::Blocker, "gpt.partition_bounds_invalid", "A GPT partition entry has invalid boundaries.", "Stop before imaging this disk with the MVP path."));
        }

        const auto boot_sector = source.read_at(partition.first_lba * static_cast<std::uint64_t>(inspection.logical_block_size), inspection.logical_block_size);
        if (!boot_sector.error.empty()) {
            inspection.findings.push_back(finding(Severity::Warning, "filesystem.boot_sector_read_failed", "Lazarus could not read a partition boot sector.", boot_sector.error));
        } else {
            if (byte_string_equals_at(boot_sector.data, 3, "-FVE-FS-")) {
                inspection.findings.push_back(finding(Severity::Warning, "windows.bitlocker_detected", "A BitLocker volume signature was detected.", "Raw imaging can continue, but filesystem recovery requires the BitLocker recovery key."));
            }
            partition.filesystem = detect_filesystem_boot_sector(boot_sector.data);
            partition.ntfs_detected = partition.filesystem == FileSystemKind::Ntfs;
            if (partition.name.empty() && partition.filesystem != FileSystemKind::Unknown) {
                partition.name = filesystem_display_name(partition.filesystem);
            }
        }

        inspection.partitions.push_back(partition);
    }

    if (inspection.partitions.empty()) {
        inspection.findings.push_back(finding(Severity::Blocker, "gpt.no_partitions", "GPT was detected but no partition entries were found.", "Stop and inspect the disk layout with a dedicated partition tool."));
    } else {
        inspection.facts.push_back("GPT partition entries were parsed successfully.");
    }

    const auto has_efi = std::any_of(inspection.partitions.begin(), inspection.partitions.end(), [](const PartitionInfo& partition) {
        return partition.kind == PartitionKind::EfiSystem;
    });
    const auto has_windows_data = std::any_of(inspection.partitions.begin(), inspection.partitions.end(), [](const PartitionInfo& partition) {
        return partition.kind == PartitionKind::WindowsBasicData;
    });
    if (!has_efi) {
        inspection.findings.push_back(finding(Severity::Warning, "windows.efi_missing", "No EFI System Partition was identified.", "This may not be a standard Windows UEFI disk."));
    }
    if (!has_windows_data) {
        inspection.findings.push_back(finding(Severity::Warning, "windows.basic_data_missing", "No Windows Basic Data partition was identified.", "Filesystem inspection is required before treating this as a Windows system image."));
    }
    const auto has_supported_filesystem = std::any_of(inspection.partitions.begin(), inspection.partitions.end(), [](const PartitionInfo& partition) {
        return partition.filesystem != FileSystemKind::Unknown;
    });
    if (has_supported_filesystem) {
        inspection.facts.push_back("At least one supported filesystem boot sector was detected.");
    }

    return inspection;
}

ImageWriteResult write_directory_image(const JobInfo& job, const SourceReadHandle& source, const DiskInspection& inspection, const ImageWriteOptions& options) {
    ImageWriteResult result;
    result.output_directory = options.output_directory;

    auto job_findings = validate_job(job);
    result.findings.insert(result.findings.end(), job_findings.begin(), job_findings.end());
    if (!source.is_open()) {
        result.findings.push_back(finding(Severity::Blocker, "image.source_closed", "The source handle is not open.", "Open an approved source read-only before imaging."));
    }
    if (blank(options.output_directory)) {
        result.findings.push_back(finding(Severity::Blocker, "image.output_missing", "No image output directory was provided.", "Choose an output directory for this Lazarus image."));
    }
    if (options.chunk_size == 0) {
        result.findings.push_back(finding(Severity::Blocker, "image.chunk_size_invalid", "The image chunk size is zero.", "Use a positive chunk size."));
    }
    if (!has_imageable_layout(inspection)) {
        result.findings.push_back(finding(Severity::Blocker, "image.inspect_not_valid", "The disk inspection did not validate an imageable disk layout or whole-device filesystem.", "Inspect and validate the source layout before imaging with the MVP path."));
    }
    for (const auto& inspect_finding : inspection.findings) {
        if (inspect_finding.severity == Severity::Blocker) {
            result.findings.push_back(inspect_finding);
        }
    }
    if (has_blocker(result.findings)) {
        return result;
    }

    const fs::path output_dir(options.output_directory);
    std::error_code error;
    fs::create_directories(output_dir, error);
    if (error) {
        result.findings.push_back(finding(Severity::Blocker, "image.output_create_failed", "Lazarus could not create the image output directory.", error.message()));
        return result;
    }

    const auto incomplete_path = output_dir / "INCOMPLETE";
    const auto finalized_path = output_dir / "FINALIZED";
    const auto disk_path = output_dir / "disk.raw";
    const auto hashes_path = output_dir / "hashes.dat";
    const auto partition_table_path = output_dir / "partition-table.bin";
    const auto metadata_path = output_dir / "metadata.json";
    const auto source_identity_path = output_dir / "source-identity.json";
    const auto job_journal_path = output_dir / "job-journal.json";
    const auto bad_sector_map_path = output_dir / "bad-sector-map.dat";
    const auto log_path = output_dir / "imaging.log";

    if (fs::exists(finalized_path)) {
        result.findings.push_back(finding(Severity::Blocker, "image.already_finalized", "The output directory already contains a finalized image.", "Choose a new output directory or verify the existing image."));
        return result;
    }

    const bool has_incomplete = fs::exists(incomplete_path);
    const bool can_resume = has_incomplete && fs::exists(disk_path) && fs::exists(hashes_path);
    const bool has_existing_payload = fs::exists(disk_path) || fs::exists(hashes_path) || fs::exists(metadata_path) || fs::exists(partition_table_path);
    if (has_existing_payload && !can_resume) {
        result.findings.push_back(finding(Severity::Blocker, "image.unsafe_existing_payload", "The output directory contains image files without a resumable INCOMPLETE state.", "Use a new empty output directory; do not append to ambiguous image data."));
        return result;
    }
    if (can_resume && (!safe_regular_file(disk_path) || !safe_regular_file(hashes_path) || !safe_regular_file(source_identity_path))) {
        result.findings.push_back(finding(Severity::Blocker, "image.resume_files_unsafe", "The interrupted image is missing its source identity journal or contains non-regular image files.", "Do not resume this image; preserve it for investigation and start a new image directory."));
        return result;
    }
    if (can_resume) {
        const auto identity_journal = read_text_file(source_identity_path);
        std::string mismatch_reason;
        if (!identity_journal || !source_identity_matches(source.device(), *identity_journal, mismatch_reason)) {
            result.findings.push_back(finding(Severity::Blocker, "image.resume_source_mismatch", mismatch_reason.empty() ? "The current source could not be matched to the interrupted image." : mismatch_reason, "Reconnect the exact original source drive before resuming."));
            return result;
        }
        if (fs::exists(bad_sector_map_path)) result.unreadable_ranges = read_bad_sector_map(bad_sector_map_path);
    } else {
        if (!write_text_file(source_identity_path, render_source_identity_json(source.device()))) {
            result.findings.push_back(finding(Severity::Blocker, "image.source_identity_write_failed", "Lazarus could not create the source identity journal.", "Do not begin imaging until the output storage is writable and durable."));
            return result;
        }
        if (!write_text_file(job_journal_path, render_job_journal_json(job, source.device(), options))) {
            result.findings.push_back(finding(Severity::Blocker, "image.job_journal_write_failed", "Lazarus could not create the persistent job journal.", "Do not begin imaging until job metadata can be saved durably."));
            return result;
        }
    }
    if (!write_text_file(incomplete_path, "Image is incomplete. Do not treat this image as verified or recoverable yet.\n")) {
        result.findings.push_back(finding(Severity::Blocker, "image.incomplete_marker_failed", "Lazarus could not create the incomplete-image marker.", "Check write access to the output directory."));
        return result;
    }
    result.incomplete_marker_created = true;

    std::ofstream log(log_path, std::ios::app);
    log << "Lazarus raw MVP imaging started.\n";
    log << "Source opened read-only: " << source.device().linux_path << "\n";
    log << "Compression: " << to_string(options.compression) << "\n";

    const auto source_size = options.max_bytes != 0 ? options.max_bytes : source.device().size_bytes;
    const auto total_chunks = source_size == 0 ? 0 : ceil_div(source_size, static_cast<std::uint64_t>(options.chunk_size));
    emit_progress(options.progress, ProgressEvent{
        "image",
        "start",
        "Raw image write started.",
        0,
        source_size,
        0,
        total_chunks,
        source_size == 0,
    });
    std::uint64_t offset = 0;
    std::uint64_t stored_offset = 0;
    std::vector<ChunkHashRecord> verified_records;
    if (can_resume) {
        const auto existing_compression = read_hash_compression(hashes_path);
        if (existing_compression != options.compression) {
            result.findings.push_back(finding(Severity::Blocker, "image.resume_compression_mismatch", "The incomplete image uses a different compression mode than the requested imaging options.", "Resume with the original compression mode or start a new image directory."));
            return result;
        }
        std::ifstream existing_disk(disk_path, std::ios::binary);
        const auto existing_records = read_hash_records(hashes_path);
        for (const auto& record : existing_records) {
            if (record.index != verified_records.size() || record.source_offset != offset || record.stored_offset != stored_offset || record.source_size == 0 || record.stored_size == 0) {
                break;
            }
            if (source_size != 0 && record.source_offset + record.source_size > source_size) {
                break;
            }
            std::vector<std::byte> stored_data(static_cast<std::size_t>(record.stored_size));
            existing_disk.seekg(static_cast<std::streamoff>(record.stored_offset));
            existing_disk.read(reinterpret_cast<char*>(stored_data.data()), static_cast<std::streamsize>(stored_data.size()));
            if (!existing_disk || static_cast<std::uint64_t>(existing_disk.gcount()) != record.stored_size) {
                break;
            }
            if (sha256_hex(stored_data) != record.stored_hash) {
                break;
            }
            std::string decompress_error;
            const auto source_data = decompress_chunk(stored_data, record.source_size, options.compression, decompress_error);
            if (!decompress_error.empty() || sha256_hex(source_data) != record.source_hash) {
                break;
            }
            const auto current_source = source.read_at(record.source_offset, static_cast<std::size_t>(record.source_size));
            if (!current_source.error.empty() || current_source.data.size() != record.source_size || sha256_hex(current_source.data) != record.source_hash) {
                result.findings.push_back(finding(Severity::Blocker, "image.resume_source_prefix_mismatch", "Previously captured bytes do not match the currently connected source drive.", "Reconnect the exact original source drive; never resume this image with a replacement disk."));
                return result;
            }
            verified_records.push_back(record);
            offset += record.source_size;
            stored_offset += record.stored_size;
        }

        if (!verified_records.empty()) {
            result.resumed = true;
            result.resumed_chunks = verified_records.size();
            result.resumed_bytes = offset;
            result.bytes_read = offset;
            result.bytes_written = offset;
            result.bytes_stored = stored_offset;
            result.chunks_written = verified_records.size();
            fs::resize_file(disk_path, stored_offset, error);
            if (error) {
                result.findings.push_back(finding(Severity::Blocker, "image.resume_truncate_failed", "Lazarus could not trim disk.raw to the verified resume point.", error.message()));
                return result;
            }
            log << "Resuming from verified offset " << offset << " with " << verified_records.size() << " chunks.\n";
            emit_progress(options.progress, ProgressEvent{
                "image",
                "resume",
                "Existing image prefix verified; appending new chunks.",
                result.bytes_written,
                source_size,
                result.chunks_written,
                total_chunks,
                source_size == 0,
            });
        }
    }

    emit_progress(options.progress, ProgressEvent{
        "image",
        "partition-table",
        "Reading partition-table snapshot.",
        result.bytes_written,
        source_size,
        result.chunks_written,
        total_chunks,
        source_size == 0,
    });
    const auto partition_table_bytes = std::min<std::uint64_t>(source.device().size_bytes == 0 ? 1024ULL * 1024ULL : source.device().size_bytes, 1024ULL * 1024ULL);
    const auto partition_table = source.read_at(0, static_cast<std::size_t>(partition_table_bytes));
    if (!partition_table.error.empty() || partition_table.data.size() != partition_table_bytes) {
        result.findings.push_back(finding(Severity::Blocker, "image.partition_table_read_failed", "Lazarus could not read the partition-table snapshot.", partition_table.error.empty() ? "The source returned fewer bytes than expected." : partition_table.error));
        log << "Partition-table snapshot failed.\n";
        return result;
    }
    {
        std::ofstream out(partition_table_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(partition_table.data.data()), static_cast<std::streamsize>(partition_table.data.size()));
        if (!out) {
            result.findings.push_back(finding(Severity::Blocker, "image.partition_table_write_failed", "Lazarus could not write partition-table.bin.", "Check destination storage health and permissions."));
            return result;
        }
    }
    if (!sync_file(partition_table_path)) {
        result.findings.push_back(finding(Severity::Blocker, "image.partition_table_sync_failed", "Lazarus could not commit partition-table.bin to stable storage.", "Treat this image as incomplete and inspect image storage health."));
        return result;
    }

    std::ofstream disk_out(disk_path, std::ios::binary | std::ios::app);
    std::ofstream hashes_out(hashes_path);
    if (!disk_out) {
        result.findings.push_back(finding(Severity::Blocker, "image.disk_stream_open_failed", "Lazarus could not open disk.raw for writing.", "Check destination storage health and permissions."));
        return result;
    }
    if (!hashes_out) {
        result.findings.push_back(finding(Severity::Blocker, "image.hash_stream_open_failed", "Lazarus could not open hashes.dat for writing.", "Check destination storage health and permissions."));
        return result;
    }

    write_hash_header(hashes_out, options.compression);
    for (const auto& record : verified_records) {
        write_hash_record(hashes_out, record);
    }

    while (source_size == 0 || offset < source_size) {
        const auto remaining = source_size == 0 ? static_cast<std::uint64_t>(options.chunk_size) : std::min<std::uint64_t>(options.chunk_size, source_size - offset);
        if (remaining == 0) {
            break;
        }

        std::vector<std::byte> chunk_data;
        const auto unreadable_before = result.unreadable_ranges.size();
        std::string read_error;
        if (!read_source_with_policy(source, offset, static_cast<std::size_t>(remaining), options, chunk_data, result.unreadable_ranges, read_error)) {
            result.findings.push_back(finding(Severity::Blocker, "image.chunk_read_failed", "Lazarus could not read a source chunk.", read_error));
            log << "Read failed at offset " << offset << ": " << read_error << "\n";
            return result;
        }
        if (result.unreadable_ranges.size() != unreadable_before && !write_text_file(bad_sector_map_path, render_bad_sector_map(result.unreadable_ranges))) {
            result.findings.push_back(finding(Severity::Blocker, "image.bad_sector_map_write_failed", "Lazarus could not durably record an unreadable source range.", "Imaging stopped; preserve the incomplete image and inspect storage health."));
            return result;
        }
        if (chunk_data.empty()) {
            if (source_size == 0) {
                break;
            }
            result.findings.push_back(finding(Severity::Blocker, "image.chunk_short_read", "The source returned end-of-file before the expected size was imaged.", "Treat this image as incomplete."));
            log << "Unexpected EOF at offset " << offset << "\n";
            return result;
        }
        if (source_size != 0 && chunk_data.size() != remaining) {
            result.findings.push_back(finding(Severity::Blocker, "image.chunk_short_read", "The source returned fewer bytes than expected.", "Treat this image as incomplete."));
            log << "Short read at offset " << offset << "\n";
            return result;
        }

        std::string compression_error;
        const auto stored_chunk = compress_chunk(chunk_data, options.compression, compression_error);
        if (!compression_error.empty()) {
            result.findings.push_back(finding(Severity::Blocker, "image.chunk_compress_failed", "Lazarus could not compress a source chunk.", compression_error));
            return result;
        }

        disk_out.write(reinterpret_cast<const char*>(stored_chunk.data()), static_cast<std::streamsize>(stored_chunk.size()));
        if (!disk_out) {
            result.findings.push_back(finding(Severity::Blocker, "image.chunk_write_failed", "Lazarus could not write a chunk to disk.raw.", "Check destination storage health and permissions."));
            log << "Write failed at offset " << offset << "\n";
            return result;
        }

        const ChunkHashRecord record{
            result.chunks_written,
            offset,
            static_cast<std::uint64_t>(chunk_data.size()),
            stored_offset,
            static_cast<std::uint64_t>(stored_chunk.size()),
            sha256_hex(chunk_data),
            sha256_hex(stored_chunk),
        };
        write_hash_record(hashes_out, record);
        if (!hashes_out) {
            result.findings.push_back(finding(Severity::Blocker, "image.hash_write_failed", "Lazarus could not write a chunk hash.", "Check destination storage health and permissions."));
            return result;
        }

        offset += chunk_data.size();
        stored_offset += stored_chunk.size();
        result.bytes_read += chunk_data.size();
        result.bytes_written += chunk_data.size();
        result.bytes_stored += stored_chunk.size();
        ++result.chunks_written;
        if (should_emit_progress(result.chunks_written, total_chunks)) {
            emit_progress(options.progress, ProgressEvent{
                "image",
                "write",
                "Writing raw image stream.",
                result.bytes_written,
                source_size,
                result.chunks_written,
                total_chunks,
                source_size == 0,
            });
        }
    }

    emit_progress(options.progress, ProgressEvent{
        "image",
        "flush",
        "Flushing image streams.",
        result.bytes_written,
        source_size,
        result.chunks_written,
        total_chunks,
        source_size == 0,
    });
    disk_out.close();
    hashes_out.close();
    if (!disk_out || !hashes_out || !sync_file(disk_path) || !sync_file(hashes_path)) {
        result.findings.push_back(finding(Severity::Blocker, "image.final_flush_failed", "Lazarus could not flush all image data to storage.", "Treat this image as incomplete."));
        return result;
    }

    emit_progress(options.progress, ProgressEvent{
        "image",
        "finalize",
        "Writing metadata and finalization marker.",
        result.bytes_written,
        source_size,
        result.chunks_written,
        total_chunks,
        source_size == 0,
    });
    if (!write_text_file(metadata_path, render_metadata_json(job, source, inspection, options, result.bytes_written, result.bytes_stored, result.chunks_written))) {
        result.findings.push_back(finding(Severity::Blocker, "image.metadata_write_failed", "Lazarus could not write metadata.json.", "Treat this image as incomplete."));
        return result;
    }

    if (!write_text_file(bad_sector_map_path, render_bad_sector_map(result.unreadable_ranges))) {
        result.findings.push_back(finding(Severity::Blocker, "image.bad_sector_map_write_failed", "Lazarus could not commit bad-sector-map.dat.", "Treat this image as incomplete."));
        return result;
    }
    if (!result.unreadable_ranges.empty()) {
        result.completed_with_warnings = true;
        result.findings.push_back(finding(Severity::Warning, "image.unreadable_ranges_recorded", std::to_string(result.unreadable_ranges.size()) + " unreadable source range(s) were zero-filled and recorded.", "Affected files may be damaged; review bad-sector-map.dat and recovery results."));
        log << "Unreadable source ranges: " << result.unreadable_ranges.size() << "\n";
    }

    if (!write_text_file(finalized_path, "Image write completed. Verification has not been performed yet.\n")) {
        result.findings.push_back(finding(Severity::Blocker, "image.finalized_marker_failed", "Lazarus could not create the finalized marker.", "Treat this image as incomplete."));
        return result;
    }
    fs::remove(incomplete_path, error);
    if (error) {
        result.findings.push_back(finding(Severity::Warning, "image.incomplete_marker_remove_failed", "The image completed but Lazarus could not remove the incomplete marker.", "Inspect the output directory before treating the image as finalized."));
    } else {
        if (!sync_directory(output_dir)) {
            result.findings.push_back(finding(Severity::Blocker, "image.final_directory_sync_failed", "Lazarus could not commit the finalized directory state to storage.", "Treat this image as incomplete after an unexpected power loss."));
        } else {
            result.finalized = true;
        }
    }

    log << "Image write completed.\n";
    log << "Bytes written: " << result.bytes_written << "\n";
    log << "Bytes stored: " << result.bytes_stored << "\n";
    log << "Chunks written: " << result.chunks_written << "\n";
    result.facts.push_back("Source was read through the approved read-only source handle.");
    if (result.resumed) {
        result.facts.push_back("Existing image data was verified and resumed before appending new chunks.");
    }
    result.facts.push_back("Image stream was written to disk.raw.");
    if (options.compression != CompressionMode::None) {
        result.facts.push_back("Image chunks were compressed before storage.");
    }
    result.facts.push_back("SHA-256 chunk hashes were written to hashes.dat.");
    result.facts.push_back("Partition-table snapshot was written to partition-table.bin.");
    if (result.finalized) {
        result.facts.push_back("FINALIZED marker was created and INCOMPLETE marker was removed.");
    }
    emit_progress(options.progress, ProgressEvent{
        "image",
        "complete",
        "Raw image write completed.",
        result.bytes_written,
        source_size,
        result.chunks_written,
        total_chunks,
        source_size == 0,
    });
    return result;
}

ImageVerificationResult verify_directory_image(const std::string& image_directory) {
    return verify_directory_image(image_directory, {});
}

ImageVerificationResult verify_directory_image(const std::string& image_directory, ProgressCallback progress) {
    ImageVerificationResult result;
    result.image_directory = image_directory;
    const fs::path image_dir(image_directory);
    const auto incomplete_path = image_dir / "INCOMPLETE";
    const auto finalized_path = image_dir / "FINALIZED";
    const auto disk_path = image_dir / "disk.raw";
    const auto hashes_path = image_dir / "hashes.dat";
    const auto metadata_path = image_dir / "metadata.json";
    const auto partition_table_path = image_dir / "partition-table.bin";
    const auto source_identity_path = image_dir / "source-identity.json";
    const auto bad_sector_map_path = image_dir / "bad-sector-map.dat";
    const auto verification_path = image_dir / "verification.json";

    if (blank(image_directory)) {
        result.findings.push_back(finding(Severity::Blocker, "verify.path_missing", "No image directory was provided.", "Select a Lazarus image directory to verify."));
        return result;
    }
    emit_progress(progress, ProgressEvent{
        "verify",
        "start",
        "Image verification started.",
        0,
        0,
        0,
        0,
        true,
    });
    if (!fs::exists(image_dir) || !fs::is_directory(image_dir)) {
        result.findings.push_back(finding(Severity::Blocker, "verify.directory_missing", "The image directory does not exist.", "Select an existing Lazarus image directory."));
        return result;
    }
    for (const auto& required : {disk_path, hashes_path, metadata_path, finalized_path, partition_table_path, source_identity_path, bad_sector_map_path}) {
        if (fs::exists(required) && !safe_regular_file(required)) {
            result.findings.push_back(finding(Severity::Blocker, "verify.unsafe_image_component", "An image component is not a regular file or is a symbolic link.", "Do not verify or restore this image; preserve it for investigation."));
        }
    }
    if (!safe_regular_file(source_identity_path))
        result.findings.push_back(finding(Severity::Blocker, "verify.source_identity_missing", "The image has no trusted source identity journal.", "Do not treat this image as a current Lazarus recoverable image."));
    if (!safe_regular_file(bad_sector_map_path))
        result.findings.push_back(finding(Severity::Blocker, "verify.bad_sector_map_missing", "The image has no bad-sector map.", "Do not claim that unreadable source ranges were assessed."));
    else {
        result.unreadable_ranges = read_bad_sector_map(bad_sector_map_path);
        if (!result.unreadable_ranges.empty())
            result.findings.push_back(finding(Severity::Warning, "verify.unreadable_ranges_present", std::to_string(result.unreadable_ranges.size()) + " unreadable source range(s) are recorded in this image.", "Recovered files intersecting these ranges may be damaged."));
    }

    result.finalized_marker_present = fs::exists(finalized_path);
    result.incomplete_marker_present = fs::exists(incomplete_path);
    if (!result.finalized_marker_present) {
        result.findings.push_back(finding(Severity::Blocker, "verify.finalized_missing", "The image does not contain a FINALIZED marker.", "Do not treat this image as complete."));
    }
    if (result.incomplete_marker_present) {
        result.findings.push_back(finding(Severity::Blocker, "verify.incomplete_present", "The image still contains an INCOMPLETE marker.", "Resume or restart imaging before verification."));
    }

    const auto metadata = read_text_file(metadata_path);
    CompressionMode compression = CompressionMode::None;
    std::uint32_t logical_block_size = 512;
    if (!metadata) {
        result.findings.push_back(finding(Severity::Blocker, "verify.metadata_missing", "metadata.json could not be read.", "Do not treat this image as recoverable."));
    } else {
        result.metadata_read = true;
        result.expected_bytes = extract_json_u64(*metadata, "bytes_written").value_or(0);
        logical_block_size = static_cast<std::uint32_t>(extract_json_u64(*metadata, "logical_block_size").value_or(512));
        compression = compression_from_string(extract_json_string(*metadata, "compression").value_or("none"));
        result.facts.push_back("metadata.json was read successfully.");
    }

    std::error_code error;
    if (!fs::exists(disk_path)) {
        result.findings.push_back(finding(Severity::Blocker, "verify.disk_stream_missing", "disk.raw is missing.", "The image cannot be restored or browsed as a raw stream."));
    } else {
        result.stored_bytes = fs::file_size(disk_path, error);
        if (error) {
            result.findings.push_back(finding(Severity::Blocker, "verify.disk_stream_stat_failed", "Lazarus could not read the size of disk.raw.", error.message()));
        } else {
            result.raw_stream_length_valid = true;
            result.facts.push_back("disk.raw was found and its stored length was read.");
        }
    }

    std::ifstream hashes_in(hashes_path);
    if (!hashes_in) {
        result.findings.push_back(finding(Severity::Blocker, "verify.hashes_missing", "hashes.dat could not be read.", "The image cannot be chunk-verified."));
    }

    bool hash_algorithm_ok = false;
    std::string line;
    while (std::getline(hashes_in, line)) {
        line = trim(line);
        if (line == "algorithm=sha256") {
            hash_algorithm_ok = true;
        }
    }
    if (hashes_in.bad()) {
        result.findings.push_back(finding(Severity::Blocker, "verify.hashes_read_failed", "Lazarus failed while reading hashes.dat.", "Treat this image as corrupt."));
    }
    if (!hash_algorithm_ok) {
        result.findings.push_back(finding(Severity::Blocker, "verify.hash_algorithm_unsupported", "hashes.dat does not declare SHA-256.", "Reimage with the current Lazarus image writer."));
    }
    const auto hash_compression = read_hash_compression(hashes_path);
    if (hash_compression != compression) {
        result.findings.push_back(finding(Severity::Blocker, "verify.compression_mismatch", "metadata.json and hashes.dat disagree about the image compression mode.", "Treat this image as corrupt."));
    }

    const auto records = read_hash_records(hashes_path);
    if (records.empty()) {
        result.findings.push_back(finding(Severity::Blocker, "verify.hash_records_missing", "No chunk hash records were found.", "The image cannot be chunk-verified."));
    }

    emit_progress(progress, ProgressEvent{
        "verify",
        "hash",
        "Verifying image chunk hashes.",
        0,
        result.expected_bytes,
        0,
        records.size(),
        records.empty(),
    });
    std::ifstream disk_in(disk_path, std::ios::binary);
    std::uint64_t source_offset = 0;
    std::uint64_t stored_offset = 0;
    bool hashes_valid = !records.empty() && hash_algorithm_ok && disk_in.good();
    for (std::size_t i = 0; i < records.size() && hashes_valid; ++i) {
        const auto& record = records[i];
        if (record.index != i || record.source_offset != source_offset || record.stored_offset != stored_offset || record.source_size == 0 || record.stored_size == 0) {
            result.findings.push_back(finding(Severity::Blocker, "verify.hash_map_not_contiguous", "hashes.dat does not describe a contiguous image stream.", "Treat this image as corrupt."));
            hashes_valid = false;
            break;
        }
        if (record.source_offset + record.source_size > result.expected_bytes) {
            result.findings.push_back(finding(Severity::Blocker, "verify.hash_range_outside_source_stream", "A chunk hash record points beyond the expected source stream.", "Treat this image as corrupt."));
            hashes_valid = false;
            break;
        }
        if (record.stored_offset + record.stored_size > result.stored_bytes) {
            result.findings.push_back(finding(Severity::Blocker, "verify.hash_range_outside_stream", "A chunk hash record points beyond disk.raw.", "Treat this image as corrupt."));
            hashes_valid = false;
            break;
        }

        std::vector<std::byte> stored_data(static_cast<std::size_t>(record.stored_size));
        disk_in.seekg(static_cast<std::streamoff>(record.stored_offset));
        disk_in.read(reinterpret_cast<char*>(stored_data.data()), static_cast<std::streamsize>(stored_data.size()));
        if (!disk_in || static_cast<std::uint64_t>(disk_in.gcount()) != record.stored_size) {
            result.findings.push_back(finding(Severity::Blocker, "verify.chunk_read_failed", "Lazarus could not read a chunk from disk.raw.", "Treat this image as corrupt."));
            hashes_valid = false;
            break;
        }
        if (sha256_hex(stored_data) != record.stored_hash) {
            result.findings.push_back(finding(Severity::Blocker, "verify.stored_chunk_hash_mismatch", "A stored disk.raw chunk did not match hashes.dat.", "Treat this image as corrupt."));
            hashes_valid = false;
            break;
        }
        std::string decompress_error;
        const auto source_data = decompress_chunk(stored_data, record.source_size, compression, decompress_error);
        if (!decompress_error.empty()) {
            result.findings.push_back(finding(Severity::Blocker, "verify.chunk_decompress_failed", "Lazarus could not decompress a stored image chunk.", decompress_error));
            hashes_valid = false;
            break;
        }
        if (sha256_hex(source_data) != record.source_hash) {
            result.findings.push_back(finding(Severity::Blocker, "verify.source_chunk_hash_mismatch", "A decompressed image chunk did not match hashes.dat.", "Treat this image as corrupt."));
            hashes_valid = false;
            break;
        }
        source_offset += record.source_size;
        stored_offset += record.stored_size;
        ++result.chunks_verified;
        if (should_emit_progress(result.chunks_verified, records.size())) {
            emit_progress(progress, ProgressEvent{
                "verify",
                "hash",
                "Verifying image chunk hashes.",
                source_offset,
                result.expected_bytes,
                result.chunks_verified,
                records.size(),
                false,
            });
        }
    }

    result.actual_bytes = source_offset;
    if (hashes_valid && source_offset != result.expected_bytes) {
        result.findings.push_back(finding(Severity::Blocker, "verify.hash_map_short", "hashes.dat does not cover the entire source stream.", "Treat this image as incomplete."));
        hashes_valid = false;
    }
    if (hashes_valid && stored_offset != result.stored_bytes) {
        result.findings.push_back(finding(Severity::Blocker, "verify.stored_hash_map_short", "hashes.dat does not cover the entire stored image stream.", "Treat this image as incomplete."));
        hashes_valid = false;
    }
    result.hashes_valid = hashes_valid;
    if (result.hashes_valid) {
        result.facts.push_back("Every stored image chunk and decompressed source chunk matched its SHA-256 hash.");
        const auto partition_snapshot = read_binary_file(partition_table_path);
        std::ifstream logical_disk(disk_path, std::ios::binary);
        std::vector<std::byte> logical_prefix;
        std::string prefix_error;
        if (!partition_snapshot || !read_logical_image_range(logical_disk, records, compression, 0, partition_snapshot->size(), logical_prefix, prefix_error) ||
            logical_prefix != *partition_snapshot) {
            result.findings.push_back(finding(Severity::Blocker, "verify.partition_snapshot_mismatch", "partition-table.bin does not match the beginning of the logical image stream.", prefix_error.empty() ? "Treat this image as corrupt." : prefix_error));
        }
        validate_image_recoverability(disk_path, records, compression, result.expected_bytes, logical_block_size, result);
        if (result.partition_table_valid) result.facts.push_back("The partition table was reconstructed from the image and passed structural validation.");
        if (result.ntfs_mft_readable) result.facts.push_back("The first NTFS MFT record was reopened successfully from the image.");
    }

    result.verified = result.finalized_marker_present && !result.incomplete_marker_present && result.metadata_read &&
                      result.raw_stream_length_valid && result.hashes_valid && result.partition_table_valid &&
                      result.filesystem_readable && !has_blocker(result.findings);
    std::ostringstream verification;
    verification << "{\n";
    verification << "  \"verified\": " << (result.verified ? "true" : "false") << ",\n";
    verification << "  \"expected_bytes\": " << result.expected_bytes << ",\n";
    verification << "  \"actual_bytes\": " << result.actual_bytes << ",\n";
    verification << "  \"stored_bytes\": " << result.stored_bytes << ",\n";
    verification << "  \"chunks_verified\": " << result.chunks_verified << ",\n";
    verification << "  \"partition_table_valid\": " << (result.partition_table_valid ? "true" : "false") << ",\n";
    verification << "  \"filesystem_readable\": " << (result.filesystem_readable ? "true" : "false") << ",\n";
    verification << "  \"ntfs_mft_readable\": " << (result.ntfs_mft_readable ? "true" : "false") << ",\n";
    verification << "  \"bitlocker_detected\": " << (result.bitlocker_detected ? "true" : "false") << ",\n";
    verification << "  \"unreadable_ranges\": " << result.unreadable_ranges.size() << ",\n";
    verification << "  \"compression\": \"" << to_string(compression) << "\",\n";
    verification << "  \"hash_algorithm\": \"sha256\"\n";
    verification << "}\n";
    write_text_file(verification_path, verification.str());

    emit_progress(progress, ProgressEvent{
        "verify",
        result.verified ? "complete" : "failed",
        result.verified ? "Image verification completed." : "Image verification failed.",
        result.actual_bytes,
        result.expected_bytes,
        result.chunks_verified,
        records.size(),
        false,
    });
    return result;
}

ImageBrowseCacheResult prepare_image_browse_cache(const ImageBrowseCacheOptions& options) {
    ImageBrowseCacheResult result;
    result.image_directory = options.image_directory;
    result.output_path = options.output_path;
    if (blank(options.image_directory) || blank(options.output_path)) {
        result.findings.push_back(finding(Severity::Blocker, "browse.cache_path_missing",
                                          "The image directory or browse-cache path is missing.",
                                          "Select a Lazarus image before preparing file recovery."));
        return result;
    }

    emit_progress(options.progress, ProgressEvent{
        "browse", "verify", "Verifying the image before read-only browsing.", 0, 0, 0, 0, true,
    });
    const auto verification = verify_directory_image(options.image_directory, options.progress);
    result.image_verified = verification.verified;
    result.findings = verification.findings;
    if (!verification.verified) {
        result.findings.push_back(finding(Severity::Blocker, "browse.image_not_verified",
                                          "The image did not pass verification and cannot be browsed.",
                                          "Review the verification findings before attempting file recovery."));
        return result;
    }
    result.logical_bytes = verification.actual_bytes;

    const fs::path image_dir(options.image_directory);
    const fs::path output_path(options.output_path);
    const fs::path marker_path(options.output_path + ".ready");
    const auto metadata = read_text_file(image_dir / "metadata.json");
    const auto hashes_text = read_text_file(image_dir / "hashes.dat");
    if (!metadata || !hashes_text) {
        result.findings.push_back(finding(Severity::Blocker, "browse.image_metadata_unreadable",
                                          "The verified image metadata could not be reopened for browsing.",
                                          "Keep the image unchanged and run verification again."));
        return result;
    }
    std::vector<std::byte> fingerprint_bytes(metadata->size() + hashes_text->size() + 1);
    std::memcpy(fingerprint_bytes.data(), metadata->data(), metadata->size());
    fingerprint_bytes[metadata->size()] = std::byte{0};
    std::memcpy(fingerprint_bytes.data() + metadata->size() + 1, hashes_text->data(), hashes_text->size());
    const auto fingerprint = sha256_hex(fingerprint_bytes);

    std::error_code fs_error;
    const auto marker = read_text_file(marker_path);
    if (marker && trim(*marker) == fingerprint && safe_regular_file(output_path) &&
        fs::file_size(output_path, fs_error) == result.logical_bytes && !fs_error) {
        ::chmod(output_path.c_str(), 0440);
        result.reused_existing_cache = true;
        result.prepared = true;
        result.facts.push_back("The existing read-only browse cache matches the verified image fingerprint.");
        emit_progress(options.progress, ProgressEvent{
            "browse", "complete", "Read-only browse cache is ready.",
            result.logical_bytes, result.logical_bytes, 0, 0, false,
        });
        return result;
    }

    if (fs::exists(output_path, fs_error) && !safe_regular_file(output_path)) {
        result.findings.push_back(finding(Severity::Blocker, "browse.cache_path_unsafe",
                                          "The browse-cache path exists but is not a regular file.",
                                          "Remove the unsafe cache path before retrying."));
        return result;
    }
    const auto parent = output_path.has_parent_path() ? output_path.parent_path() : fs::path{"."};
    fs::create_directories(parent, fs_error);
    if (fs_error) {
        result.findings.push_back(finding(Severity::Blocker, "browse.cache_directory_failed",
                                          "Lazarus could not create its browse-cache directory.", fs_error.message()));
        return result;
    }

    const auto records = read_hash_records(image_dir / "hashes.dat");
    const auto compression = read_hash_compression(image_dir / "hashes.dat");
    if (records.empty()) {
        result.findings.push_back(finding(Severity::Blocker, "browse.hash_records_missing",
                                          "No chunk records are available for browse-cache reconstruction.",
                                          "Do not browse this image."));
        return result;
    }
    const fs::path temporary_path(options.output_path + ".tmp-" + std::to_string(::getpid()));
    fs::remove(temporary_path, fs_error);
    const int output_fd = ::open(temporary_path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (output_fd < 0) {
        result.findings.push_back(finding(Severity::Blocker, "browse.cache_open_failed",
                                          "Lazarus could not create its browse-cache file.", std::strerror(errno)));
        return result;
    }
    const auto fail_cache = [&](const std::string& code, const std::string& observed, const std::string& action) {
        ::close(output_fd);
        ::unlink(temporary_path.c_str());
        result.findings.push_back(finding(Severity::Blocker, code, observed, action));
    };
    if (::ftruncate(output_fd, static_cast<off_t>(result.logical_bytes)) != 0) {
        fail_cache("browse.cache_resize_failed", "Lazarus could not size the sparse browse cache.", std::strerror(errno));
        return result;
    }

    std::ifstream disk(image_dir / "disk.raw", std::ios::binary);
    if (!disk) {
        fail_cache("browse.disk_stream_open_failed", "Lazarus could not reopen the image data stream.",
                   "Keep the image unchanged and run verification again.");
        return result;
    }
    emit_progress(options.progress, ProgressEvent{
        "browse", "reconstruct", "Preparing a sparse read-only browse cache.",
        0, result.logical_bytes, 0, records.size(), false,
    });
    for (const auto& record : records) {
        std::vector<std::byte> stored(static_cast<std::size_t>(record.stored_size));
        disk.clear();
        disk.seekg(static_cast<std::streamoff>(record.stored_offset));
        disk.read(reinterpret_cast<char*>(stored.data()), static_cast<std::streamsize>(stored.size()));
        if (!disk || static_cast<std::uint64_t>(disk.gcount()) != record.stored_size ||
            sha256_hex(stored) != record.stored_hash) {
            fail_cache("browse.stored_chunk_invalid", "A stored image chunk failed verification during browse preparation.",
                       "Do not browse this image; preserve it for investigation.");
            return result;
        }
        std::string decompress_error;
        const auto source = decompress_chunk(stored, record.source_size, compression, decompress_error);
        if (!decompress_error.empty() || sha256_hex(source) != record.source_hash) {
            fail_cache("browse.source_chunk_invalid", "A reconstructed source chunk failed verification during browse preparation.",
                       decompress_error.empty() ? "Do not browse this image." : decompress_error);
            return result;
        }
        const bool all_zero = std::all_of(source.begin(), source.end(), [](std::byte value) {
            return value == std::byte{0};
        });
        if (!all_zero) {
            if (!write_exact_at(output_fd, record.source_offset, source)) {
                fail_cache("browse.cache_write_failed", "Lazarus could not write the next browse-cache chunk.", std::strerror(errno));
                return result;
            }
            result.allocated_bytes_written += source.size();
        }
        ++result.chunks_reconstructed;
        if (should_emit_progress(result.chunks_reconstructed, records.size())) {
            emit_progress(options.progress, ProgressEvent{
                "browse", "reconstruct", "Preparing a sparse read-only browse cache.",
                record.source_offset + record.source_size, result.logical_bytes,
                result.chunks_reconstructed, records.size(), false,
            });
        }
    }
    const bool cache_synced = ::fsync(output_fd) == 0;
    const bool cache_protected = ::fchmod(output_fd, 0440) == 0;
    const bool cache_closed = ::close(output_fd) == 0;
    if (!cache_synced || !cache_protected || !cache_closed) {
        ::unlink(temporary_path.c_str());
        result.findings.push_back(finding(Severity::Blocker, "browse.cache_flush_failed",
                                          "Lazarus could not flush the browse cache.", std::strerror(errno)));
        return result;
    }
    fs::rename(temporary_path, output_path, fs_error);
    if (fs_error || !write_text_file(marker_path, fingerprint + "\n")) {
        fs::remove(temporary_path, fs_error);
        fs::remove(output_path, fs_error);
        fs::remove(marker_path, fs_error);
        result.findings.push_back(finding(Severity::Blocker, "browse.cache_finalize_failed",
                                          "Lazarus could not finalize the read-only browse cache.",
                                          fs_error ? fs_error.message() : "The cache marker could not be written."));
        return result;
    }
    result.prepared = true;
    result.facts.push_back("Every reconstructed browse-cache chunk matched the image SHA-256 records.");
    result.facts.push_back("Zero-filled source chunks were retained as sparse cache ranges.");
    emit_progress(options.progress, ProgressEvent{
        "browse", "complete", "Read-only browse cache is ready.",
        result.logical_bytes, result.logical_bytes, result.chunks_reconstructed, records.size(), false,
    });
    return result;
}

ImageRestoreResult restore_directory_image(const BenchProfile& bench, DeviceIdentity destination, const ImageRestoreOptions& options) {
    ImageRestoreResult result;
    result.image_directory = options.image_directory;
    result.destination = apply_bench_policy(bench, std::move(destination));

    if (options.confirmation != "ERASE") {
        result.findings.push_back(finding(Severity::Blocker, "restore.confirmation_missing", "The restore command did not receive the exact confirmation token ERASE.", "Type ERASE only after confirming the destination drive identity."));
        return result;
    }
    if (options.chunk_size == 0) {
        result.findings.push_back(finding(Severity::Blocker, "restore.chunk_size_invalid", "The restore chunk size is zero.", "Use a positive chunk size."));
        return result;
    }

    emit_progress(options.progress, ProgressEvent{
        "restore",
        "verify",
        "Verifying image before restore.",
        0,
        0,
        0,
        0,
        true,
    });
    const auto verification = verify_directory_image(options.image_directory, options.progress);
    result.image_verified_before_restore = verification.verified;
    result.findings.insert(result.findings.end(), verification.findings.begin(), verification.findings.end());
    if (!verification.verified) {
        result.findings.push_back(finding(Severity::Blocker, "restore.image_not_verified", "The image did not pass verification.", "Do not restore an unverified image."));
        return result;
    }

    if (result.destination.size_bytes < verification.actual_bytes) {
        result.findings.push_back(finding(Severity::Blocker, "restore.destination_too_small", "The destination is smaller than disk.raw.", "Use a destination drive at least as large as the image raw stream."));
        return result;
    }

    const fs::path image_dir(options.image_directory);
    const auto hashes_path = image_dir / "hashes.dat";
    const auto metadata = read_text_file(image_dir / "metadata.json");
    const auto compression = compression_from_string(metadata ? extract_json_string(*metadata, "compression").value_or("none") : "none");
    const auto records = read_hash_records(hashes_path);
    if (records.empty()) {
        result.findings.push_back(finding(Severity::Blocker, "restore.hash_records_missing", "No chunk hash records were found for restore.", "Do not restore until the image verifies with readable hash records."));
        return result;
    }

    const auto total_chunks = records.size();
    emit_progress(options.progress, ProgressEvent{
        "restore",
        "open-destination",
        "Opening destination write-only.",
        0,
        verification.actual_bytes,
        0,
        total_chunks,
        false,
    });
    if (result.destination.linux_path.rfind("/dev/", 0) == 0) {
        std::string identity_error;
        const auto rediscovered = rediscover_destructive_destination(bench, result.destination, identity_error);
        if (!rediscovered) {
            result.findings.push_back(finding(Severity::Blocker, "restore.destination_identity_changed", identity_error, "No writes occurred; rescan and explicitly select the destination again."));
            return result;
        }
        result.destination = *rediscovered;
    }
    auto open_result = open_destination_write_only(bench, result.destination);
    result.findings.insert(result.findings.end(), open_result.findings.begin(), open_result.findings.end());
    if (!open_result.handle.is_open()) {
        return result;
    }
    result.destination = open_result.handle.device();

    const fs::path disk_path = image_dir / "disk.raw";
    std::ifstream disk_in(disk_path, std::ios::binary);
    if (!disk_in) {
        result.findings.push_back(finding(Severity::Blocker, "restore.disk_stream_open_failed", "Lazarus could not reopen disk.raw for restore.", "Do not restore until the image storage path is readable."));
        return result;
    }

    emit_progress(options.progress, ProgressEvent{
        "restore",
        "write",
        "Writing disk.raw to destination.",
        0,
        verification.actual_bytes,
        0,
        total_chunks,
        false,
    });
    for (const auto& record : records) {
        std::vector<std::byte> stored_chunk(static_cast<std::size_t>(record.stored_size));
        disk_in.seekg(static_cast<std::streamoff>(record.stored_offset));
        disk_in.read(reinterpret_cast<char*>(stored_chunk.data()), static_cast<std::streamsize>(stored_chunk.size()));
        if (!disk_in || static_cast<std::uint64_t>(disk_in.gcount()) != record.stored_size) {
            result.findings.push_back(finding(Severity::Blocker, "restore.disk_stream_read_failed", "Lazarus could not read the next restore chunk from disk.raw.", "Destination restore stopped; the destination contents are incomplete."));
            return result;
        }
        if (sha256_hex(stored_chunk) != record.stored_hash) {
            result.findings.push_back(finding(Severity::Blocker, "restore.stored_chunk_hash_mismatch", "A stored disk.raw chunk did not match hashes.dat during restore.", "Destination restore stopped; the destination contents are incomplete."));
            return result;
        }
        std::string decompress_error;
        const auto chunk = decompress_chunk(stored_chunk, record.source_size, compression, decompress_error);
        if (!decompress_error.empty()) {
            result.findings.push_back(finding(Severity::Blocker, "restore.chunk_decompress_failed", "Lazarus could not decompress a stored image chunk during restore.", decompress_error));
            return result;
        }
        if (sha256_hex(chunk) != record.source_hash) {
            result.findings.push_back(finding(Severity::Blocker, "restore.source_chunk_hash_mismatch", "A decompressed image chunk did not match hashes.dat during restore.", "Destination restore stopped; the destination contents are incomplete."));
            return result;
        }

        std::string error;
        if (!open_result.handle.write_at(record.source_offset, chunk, error)) {
            result.findings.push_back(finding(Severity::Blocker, "restore.destination_write_failed", "Lazarus could not write a restore chunk to the destination.", error));
            return result;
        }

        result.bytes_written += chunk.size();
        ++result.chunks_written;
        if (should_emit_progress(result.chunks_written, total_chunks)) {
            emit_progress(options.progress, ProgressEvent{
                "restore",
                "write",
                "Writing disk.raw to destination.",
                result.bytes_written,
                verification.actual_bytes,
                result.chunks_written,
                total_chunks,
                false,
            });
        }
    }

    emit_progress(options.progress, ProgressEvent{
        "restore",
        "flush",
        "Flushing destination writes.",
        result.bytes_written,
        verification.actual_bytes,
        result.chunks_written,
        total_chunks,
        false,
    });
    std::string flush_error;
    if (!open_result.handle.flush(flush_error)) {
        result.findings.push_back(finding(Severity::Blocker, "restore.flush_failed", "Lazarus could not flush restore writes to the destination.", flush_error));
        return result;
    }
    result.flushed = true;
    const auto destination_path = result.destination.linux_path;
    open_result.handle.close();

    emit_progress(options.progress, ProgressEvent{
        "restore",
        "readback",
        "Reading the restored destination back and comparing every chunk.",
        0,
        verification.actual_bytes,
        0,
        total_chunks,
        false,
    });
    const int readback_fd = ::open(destination_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (readback_fd < 0) {
        result.findings.push_back(finding(Severity::Blocker, "restore.readback_open_failed", "Lazarus could not reopen the destination read-only after writing.", std::strerror(errno)));
        return result;
    }
    std::uint64_t readback_chunks = 0;
    for (const auto& record : records) {
        std::vector<std::byte> restored_chunk(static_cast<std::size_t>(record.source_size));
        std::string read_error;
        if (!read_exact_at(readback_fd, record.source_offset, restored_chunk, read_error) || sha256_hex(restored_chunk) != record.source_hash) {
            ::close(readback_fd);
            result.findings.push_back(finding(Severity::Blocker, "restore.readback_mismatch", "Destination readback did not match the image at source offset " + std::to_string(record.source_offset) + ".", read_error.empty() ? "Do not boot or deliver this destination; repeat restore on known-good media." : read_error));
            return result;
        }
        result.bytes_verified += record.source_size;
        ++readback_chunks;
        if (should_emit_progress(readback_chunks, total_chunks)) {
            emit_progress(options.progress, ProgressEvent{
                "restore",
                "readback",
                "Reading the restored destination back and comparing every chunk.",
                result.bytes_verified,
                verification.actual_bytes,
                readback_chunks,
                total_chunks,
                false,
            });
        }
    }
    result.readback_verified = true;

    SourceReadHandle restored_source(readback_fd, result.destination);
    const auto restored_inspection = inspect_source_disk(restored_source);
    result.destination_layout_validated = has_imageable_layout(restored_inspection) && !has_blocker(restored_inspection.findings);
    if (!result.destination_layout_validated) {
        result.findings.push_back(finding(Severity::Blocker, "restore.destination_layout_invalid", "The restored destination bytes matched, but its partition layout did not pass inspection.", "Do not boot or deliver this destination until the partition findings are reviewed."));
        result.findings.insert(result.findings.end(), restored_inspection.findings.begin(), restored_inspection.findings.end());
        return result;
    }
    result.restored = true;
    result.facts.push_back("The image was verified before restore began.");
    result.facts.push_back("The destination was opened with write-only flags after destination-only bench policy passed.");
    result.facts.push_back("disk.raw was written to the destination from offset zero.");
    result.facts.push_back("Restore writes were flushed to the destination.");
    result.facts.push_back("Every restored destination chunk was read back and matched its source SHA-256 hash.");
    result.facts.push_back("The restored destination partition layout was reopened and inspected.");
    emit_progress(options.progress, ProgressEvent{
        "restore",
        "complete",
        "Restore completed.",
        result.bytes_written,
        verification.actual_bytes,
        result.chunks_written,
        total_chunks,
        false,
    });
    return result;
}

SmartDiagnosticResult parse_smartctl_json(const DeviceIdentity& device, const std::string& json, int exit_code) {
    SmartDiagnosticResult result;
    result.device = device;
    result.smartctl_available = true;
    result.command_completed = !json.empty();
    result.exit_code = exit_code;
    result.raw_json = json;
    result.model = json_string_value(json, "model_name");
    result.serial = json_string_value(json, "serial_number");

    const auto passed_pos = json.find("\"passed\"");
    if (passed_pos != std::string::npos) {
        const auto colon = json.find(':', passed_pos);
        const auto value_pos = colon == std::string::npos ? std::string::npos : json.find_first_not_of(" \t\r\n", colon + 1);
        if (value_pos != std::string::npos && json.compare(value_pos, 4, "true") == 0) {
            result.health = "passed";
            result.facts.push_back("SMART overall-health self-assessment reports passed.");
        } else if (value_pos != std::string::npos && json.compare(value_pos, 5, "false") == 0) {
            result.health = "failed";
            result.findings.push_back(finding(Severity::Blocker, "smart.health_failed", "SMART overall-health self-assessment reports failed.", "Treat the source as unstable and prefer Rescue Mode before additional stress."));
        }
    }

    set_attribute(result.power_on_hours, json_object_integer_value(json, "power_on_time", "hours"));
    set_attribute(result.power_on_hours, json_integer_value(json, "power_on_hours"));
    set_attribute(result.temperature_celsius, json_object_integer_value(json, "temperature", "current"));
    set_attribute(result.temperature_celsius, json_integer_value(json, "temperature"));
    set_attribute(result.reallocated_sectors, ata_attribute_raw_value(json, "Reallocated_Sector_Ct"));
    set_attribute(result.pending_sectors, ata_attribute_raw_value(json, "Current_Pending_Sector"));
    set_attribute(result.uncorrectable_errors, ata_attribute_raw_value(json, "Offline_Uncorrectable"));
    set_attribute(result.uncorrectable_errors, ata_attribute_raw_value(json, "Reported_Uncorrect"));
    set_attribute(result.uncorrectable_errors, json_integer_value(json, "media_errors"));

    if (!result.model.empty()) {
        result.facts.push_back("SMART device model: " + result.model + ".");
    }
    if (!result.serial.empty()) {
        result.facts.push_back("SMART serial number was reported by the device.");
    }
    if (result.power_on_hours.present) {
        result.facts.push_back("SMART power-on hours: " + std::to_string(result.power_on_hours.value) + ".");
    }
    if (result.temperature_celsius.present) {
        result.facts.push_back("SMART temperature: " + std::to_string(result.temperature_celsius.value) + " C.");
        if (result.temperature_celsius.value >= 60) {
            result.findings.push_back(finding(Severity::Warning, "smart.temperature_high", "SMART reports a high drive temperature.", "Improve cooling before long imaging jobs."));
        }
    }
    if (result.reallocated_sectors.present && result.reallocated_sectors.value > 0) {
        result.findings.push_back(finding(Severity::Warning, "smart.reallocated_sectors", "SMART reports reallocated sectors.", "Prefer verified imaging and consider Rescue Mode if reads slow or fail."));
    }
    if (result.pending_sectors.present && result.pending_sectors.value > 0) {
        result.findings.push_back(finding(Severity::Warning, "smart.pending_sectors", "SMART reports pending sectors.", "Use Rescue Mode to reduce stress and record unreadable ranges."));
    }
    if (result.uncorrectable_errors.present && result.uncorrectable_errors.value > 0) {
        result.findings.push_back(finding(Severity::Warning, "smart.uncorrectable_errors", "SMART reports uncorrectable errors.", "Expect possible damaged files and verify the image before restore."));
    }
    if (result.health == "unknown") {
        result.findings.push_back(finding(Severity::Warning, "smart.health_unknown", "SMART overall-health result was not available.", "Do not infer that the drive is healthy from missing SMART data."));
    }
    if (json.empty()) {
        result.findings.push_back(finding(Severity::Warning, "smart.output_empty", "smartctl returned no JSON output.", "Check smartctl support for this device or USB bridge."));
    }
    if (json.find("Permission denied") != std::string::npos) {
        result.findings.push_back(finding(Severity::Blocker, "smart.permission_denied", "smartctl could not open the device because permission was denied.", "Run lazarus-service as root or grant the service the required block-device permissions."));
    }
    return result;
}

SmartDiagnosticResult collect_smart_diagnostics(DeviceIdentity device) {
    SmartDiagnosticResult result;
    result.device = device;
    if (device.linux_path.empty()) {
        result.findings.push_back(finding(Severity::Blocker, "smart.device_missing", "No device path was provided for SMART diagnostics.", "Select a discovered block device first."));
        return result;
    }

    const auto smartctl = find_executable("smartctl");
    if (!smartctl) {
        result.findings.push_back(finding(Severity::Warning, "smart.smartctl_missing", "Lazarus could not find smartctl in PATH or standard sbin locations.", "Install smartmontools or add smartctl to the Lazarus service PATH."));
        return result;
    }

    const std::string command = shell_quote(*smartctl) + " -a -j " + shell_quote(device.linux_path) + " 2>&1";
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        result.findings.push_back(finding(Severity::Warning, "smart.smartctl_unavailable", "Lazarus found smartctl but could not start it.", "Check Lazarus service permissions and the recovery environment PATH."));
        return result;
    }

    std::string output;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }
    const int status = ::pclose(pipe);
    auto parsed = parse_smartctl_json(device, output, status);
    parsed.smartctl_available = true;
    if (status != 0) {
        parsed.findings.push_back(finding(Severity::Warning, "smart.smartctl_exit_nonzero", "smartctl returned a nonzero status.", "Review SMART facts and warnings; some USB bridges return warnings even when partial SMART data is available."));
    }
    return parsed;
}

DriverPackageInspection inspect_driver_package(const std::string& package_root) {
    DriverPackageInspection result;
    result.package_root = package_root;
    const fs::path root(package_root);
    std::error_code filesystem_error;
    if (!fs::is_directory(root, filesystem_error)) {
        result.findings.push_back(finding(
            Severity::Blocker, "driver.package_missing",
            "The selected driver package directory does not exist.",
            "Select an extracted INF-style driver package."));
        return result;
    }

    const auto upper = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value;
    };
    const auto value_text = [](std::string value) {
        value = trim(value);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        return trim(value);
    };
    const auto read_inf = [](const fs::path& path) {
        std::ifstream stream(path, std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xff &&
            static_cast<unsigned char>(bytes[1]) == 0xfe) {
            std::string ascii;
            ascii.reserve(bytes.size() / 2);
            for (std::size_t index = 2; index + 1 < bytes.size(); index += 2) {
                const unsigned char low = static_cast<unsigned char>(bytes[index]);
                const unsigned char high = static_cast<unsigned char>(bytes[index + 1]);
                ascii.push_back(high == 0 && low < 0x80 ? static_cast<char>(low) : '?');
            }
            return ascii;
        }
        return bytes;
    };
    const auto case_insensitive_child = [&upper](const fs::path& directory, const std::string& name) {
        std::error_code error;
        const auto wanted = upper(name);
        for (const auto& entry : fs::directory_iterator(directory, error)) {
            if (error) break;
            if (upper(entry.path().filename().string()) == wanted) return entry.path();
        }
        return fs::path{};
    };
    const auto collect_ids = [&upper](const std::string& line, std::vector<std::string>& output) {
        const auto uppercase = upper(line);
        static const std::array<std::string, 7> prefixes = {
            "PCI\\", "ACPI\\", "SCSI\\", "USBSTOR\\", "NVME\\", "ROOT\\", "VMBUS\\"};
        for (const auto& prefix : prefixes) {
            std::size_t position = 0;
            while ((position = uppercase.find(prefix, position)) != std::string::npos) {
                std::size_t end = position + prefix.size();
                while (end < uppercase.size()) {
                    const unsigned char ch = static_cast<unsigned char>(uppercase[end]);
                    if (!(std::isalnum(ch) || ch == '_' || ch == '&' || ch == '.' || ch == '-' || ch == '\\')) break;
                    ++end;
                }
                const auto id = uppercase.substr(position, end - position);
                if (id.size() > prefix.size() && std::find(output.begin(), output.end(), id) == output.end()) {
                    output.push_back(id);
                }
                position = std::max(end, position + prefix.size());
            }
        }
    };

    for (fs::recursive_directory_iterator iterator(root, fs::directory_options::skip_permission_denied, filesystem_error), end;
         iterator != end; iterator.increment(filesystem_error)) {
        if (filesystem_error) {
            filesystem_error.clear();
            continue;
        }
        const auto status = iterator->symlink_status(filesystem_error);
        if (filesystem_error) {
            filesystem_error.clear();
            continue;
        }
        if (fs::is_symlink(status)) {
            result.symlink_detected = true;
            if (iterator->is_directory(filesystem_error)) iterator.disable_recursion_pending();
            continue;
        }
        if (!fs::is_regular_file(status) || upper(iterator->path().extension().string()) != ".INF") continue;
        if (iterator->file_size(filesystem_error) > 4 * 1024 * 1024 || filesystem_error) {
            filesystem_error.clear();
            result.findings.push_back(finding(
                Severity::Warning, "driver.inf_too_large",
                "An INF file was too large for the bounded package inspector: " + iterator->path().filename().string(),
                "Inspect this package manually before servicing."));
            continue;
        }

        DriverInfMetadata metadata;
        metadata.path = fs::relative(iterator->path(), root, filesystem_error).generic_string();
        if (filesystem_error) {
            filesystem_error.clear();
            metadata.path = iterator->path().filename().string();
        }
        const auto text = read_inf(iterator->path());
        std::unordered_map<std::string, std::vector<std::string>> sections;
        std::unordered_map<std::string, std::string> strings;
        {
            std::istringstream section_lines(text);
            std::string section;
            std::string section_line;
            while (std::getline(section_lines, section_line)) {
                if (const auto comment = section_line.find(';'); comment != std::string::npos) section_line.erase(comment);
                section_line = trim(section_line);
                if (section_line.empty()) continue;
                if (section_line.front() == '[' && section_line.back() == ']') {
                    section = upper(trim(section_line.substr(1, section_line.size() - 2)));
                    continue;
                }
                if (section.empty()) continue;
                sections[section].push_back(section_line);
                if (section == "STRINGS") {
                    const auto equals = section_line.find('=');
                    if (equals != std::string::npos) {
                        strings[upper(trim(section_line.substr(0, equals)))] =
                            value_text(section_line.substr(equals + 1));
                    }
                }
            }
        }
        const auto expand_strings = [&strings, &upper, &value_text](std::string value) {
            for (std::size_t start = 0; (start = value.find('%', start)) != std::string::npos;) {
                const auto end = value.find('%', start + 1);
                if (end == std::string::npos) break;
                const auto found = strings.find(upper(value.substr(start + 1, end - start - 1)));
                if (found == strings.end()) {
                    start = end + 1;
                    continue;
                }
                value.replace(start, end - start + 1, found->second);
                start += found->second.size();
            }
            return value_text(value);
        };
        const auto split_inf_values = [&expand_strings](const std::string& value) {
            std::vector<std::string> fields;
            std::string field;
            bool quoted = false;
            for (const char character : value) {
                if (character == '"') quoted = !quoted;
                if (character == ',' && !quoted) {
                    fields.push_back(expand_strings(field));
                    field.clear();
                } else {
                    field.push_back(character);
                }
            }
            fields.push_back(expand_strings(field));
            return fields;
        };
        const auto parse_inf_number = [](const std::string& value, std::uint32_t& output) {
            try {
                std::size_t used = 0;
                const auto parsed = std::stoul(value, &used, 0);
                if (used != value.size()) return false;
                output = static_cast<std::uint32_t>(parsed);
                return true;
            } catch (...) {
                return false;
            }
        };
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            if (const auto comment = line.find(';'); comment != std::string::npos) line.erase(comment);
            line = trim(line);
            if (line.empty()) continue;
            const auto uppercase_line = upper(line);
            if (uppercase_line.find(".NTAMD64") != std::string::npos) metadata.amd64_decorated = true;
            collect_ids(line, metadata.hardware_ids);
            const auto equals = line.find('=');
            if (equals == std::string::npos) continue;
            const auto key = upper(trim(line.substr(0, equals)));
            const auto value = value_text(line.substr(equals + 1));
            if (key == "SIGNATURE") metadata.windows_nt_signature = upper(value).find("WINDOWS NT") != std::string::npos;
            else if (key == "PROVIDER") metadata.provider = value;
            else if (key == "CLASS") metadata.class_name = value;
            else if (key == "CLASSGUID") metadata.class_guid = value;
            else if (key == "DRIVERVER") metadata.driver_version = value;
            else if (key.rfind("CATALOGFILE", 0) == 0 && metadata.catalog_file.empty()) metadata.catalog_file = value;
        }
        for (const auto& [section_name, section_lines] : sections) {
            if (!section_name.ends_with(".SERVICES")) continue;
            for (const auto& service_line : section_lines) {
                const auto equals = service_line.find('=');
                if (equals == std::string::npos || upper(trim(service_line.substr(0, equals))) != "ADDSERVICE") continue;
                const auto fields = split_inf_values(service_line.substr(equals + 1));
                if (fields.size() < 3 || fields[0].empty() || fields[2].empty()) continue;
                const auto service_section = sections.find(upper(fields[2]));
                if (service_section == sections.end()) continue;

                std::string binary;
                std::string group;
                std::uint32_t service_type = 0;
                std::uint32_t start_type = 0xffffffffU;
                std::uint32_t error_control = 1;
                for (const auto& setting : service_section->second) {
                    const auto setting_equals = setting.find('=');
                    if (setting_equals == std::string::npos) continue;
                    const auto key = upper(trim(setting.substr(0, setting_equals)));
                    const auto value = expand_strings(setting.substr(setting_equals + 1));
                    if (key == "SERVICEBINARY") binary = value;
                    else if (key == "LOADORDERGROUP") group = value;
                    else if (key == "SERVICETYPE") parse_inf_number(value, service_type);
                    else if (key == "STARTTYPE") parse_inf_number(value, start_type);
                    else if (key == "ERRORCONTROL") parse_inf_number(value, error_control);
                }
                if (service_type != 1 || start_type != 0 || binary.empty()) continue;
                std::replace(binary.begin(), binary.end(), '\\', '/');
                const auto slash = binary.find_last_of('/');
                const auto binary_name = slash == std::string::npos ? binary : binary.substr(slash + 1);
                if (binary_name.empty() || upper(fs::path(binary_name).extension().string()) != ".SYS") continue;

                fs::path binary_path;
                std::error_code binary_error;
                for (fs::recursive_directory_iterator binary_iterator(
                         iterator->path().parent_path(), fs::directory_options::skip_permission_denied, binary_error), binary_end;
                     !binary_error && binary_iterator != binary_end; binary_iterator.increment(binary_error)) {
                    const auto status = binary_iterator->symlink_status(binary_error);
                    if (binary_error) break;
                    if (fs::is_symlink(status)) {
                        if (fs::is_directory(status)) binary_iterator.disable_recursion_pending();
                        continue;
                    }
                    if (fs::is_regular_file(status) && upper(binary_iterator->path().filename().string()) == upper(binary_name)) {
                        binary_path = binary_iterator->path();
                        break;
                    }
                }
                if (binary_path.empty()) continue;
                metadata.service_name = fields[0];
                metadata.service_binary = binary_name;
                metadata.service_binary_path = fs::relative(binary_path, root, binary_error).generic_string();
                if (binary_error) metadata.service_binary_path = binary_path.string();
                metadata.load_order_group = group.empty() ? "SCSI miniport" : group;
                metadata.service_type = service_type;
                metadata.start_type = start_type;
                metadata.error_control = error_control;
                metadata.boot_service_present = true;
                break;
            }
            if (metadata.boot_service_present) break;
        }
        const auto driver_class = upper(metadata.class_name);
        metadata.storage_controller_driver = driver_class == "SCSIADAPTER" || driver_class == "HDC";
        if (!metadata.catalog_file.empty()) {
            metadata.catalog_present = !case_insensitive_child(iterator->path().parent_path(), metadata.catalog_file).empty();
        }
        result.contains_storage_controller_driver = result.contains_storage_controller_driver || metadata.storage_controller_driver;
        for (const auto& id : metadata.hardware_ids) {
            if (std::find(result.hardware_ids.begin(), result.hardware_ids.end(), id) == result.hardware_ids.end()) {
                result.hardware_ids.push_back(id);
            }
        }
        result.inf_files.push_back(std::move(metadata));
    }

    if (result.inf_files.empty()) {
        result.findings.push_back(finding(
            Severity::Blocker, "driver.inf_missing",
            "No INF files were found in the selected package.",
            "Extract the manufacturer driver package before importing it."));
    }
    if (result.symlink_detected) {
        result.findings.push_back(finding(
            Severity::Blocker, "driver.package_symlink",
            "The selected package contains a symbolic link.",
            "Import a self-contained extracted package without links outside its directory."));
    }
    bool servicing_candidate = false;
    for (const auto& inf : result.inf_files) {
        if (!inf.windows_nt_signature) {
            result.findings.push_back(finding(
                Severity::Blocker, "driver.inf_signature_missing",
                inf.path + " does not declare the Windows NT INF signature.",
                "Use an INF-style Windows driver package."));
        }
        if (inf.catalog_file.empty() || !inf.catalog_present) {
            result.findings.push_back(finding(
                Severity::Blocker, "driver.catalog_missing",
                inf.path + " does not have its declared catalog beside the INF.",
                "Obtain the complete signed package from the hardware manufacturer."));
        }
        if (inf.storage_controller_driver && inf.windows_nt_signature && inf.catalog_present &&
            !inf.hardware_ids.empty() && inf.boot_service_present) {
            servicing_candidate = true;
        }
        if (inf.storage_controller_driver && !inf.boot_service_present) {
            result.findings.push_back(finding(
                Severity::Blocker, "driver.boot_service_missing",
                inf.path + " does not expose a complete boot-start kernel service and SYS payload.",
                "Import the complete extracted storage-controller package, including its boot driver binary."));
        }
    }
    result.ready_for_windows_signature_verification = servicing_candidate && !result.symlink_detected && !has_blocker(result.findings);
    result.facts.push_back(std::to_string(result.inf_files.size()) + " INF file(s) were inspected locally.");
    result.facts.push_back(std::to_string(result.hardware_ids.size()) + " unique hardware ID(s) were indexed.");
    if (result.contains_storage_controller_driver) {
        result.facts.push_back("At least one SCSIAdapter or HDC driver package was identified.");
    }
    if (result.ready_for_windows_signature_verification) {
        result.facts.push_back("Package structure contains a boot-start storage service suitable for Lazarus offline bootstrap injection.");
        result.findings.push_back(finding(
            Severity::Info, "driver.signature_verification_pending",
            "Linux-side inspection found the declared catalog but did not claim a Windows trust-chain result.",
            "Use manufacturer-supplied signed packages; Windows Code Integrity remains authoritative on first boot."));
    }
    return result;
}

DriverMigrationPlan create_driver_migration_plan(
    const DriverMigrationPlanOptions& options,
    const std::vector<DriverPackageInspection>& packages) {
    DriverMigrationPlan plan;
    const auto upper = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return value;
    };
    for (const auto& id : options.target_hardware_ids) {
        const auto normalized = upper(trim(id));
        if (!normalized.empty() && std::find(plan.target_hardware_ids.begin(), plan.target_hardware_ids.end(), normalized) == plan.target_hardware_ids.end()) {
            plan.target_hardware_ids.push_back(normalized);
        }
    }
    if (plan.target_hardware_ids.empty()) {
        plan.findings.push_back(finding(
            Severity::Blocker, "migration.hardware_ids_missing",
            "No replacement storage-controller hardware IDs were provided.",
            "Create a hardware profile on the replacement computer before selecting drivers."));
    }

    const auto ids_match = [](const std::string& required, const std::string& supported) {
        if (required == supported) return true;
        if (required.size() > supported.size() && required.rfind(supported + "&", 0) == 0) return true;
        return supported.size() > required.size() && supported.rfind(required + "&", 0) == 0;
    };
    for (const auto& package : packages) {
        for (const auto& inf : package.inf_files) {
            if (!inf.storage_controller_driver || !inf.windows_nt_signature || !inf.catalog_present) continue;
            DriverPlanItem item;
            item.action = "add";
            item.inf_path = (fs::path(package.package_root) / inf.path).string();
            for (const auto& target : plan.target_hardware_ids) {
                for (const auto& supported : inf.hardware_ids) {
                    if (ids_match(target, upper(supported))) {
                        item.matching_hardware_ids.push_back(target);
                        break;
                    }
                }
            }
            if (item.matching_hardware_ids.empty()) continue;
            item.reason = "The package declares a storage-controller model matching the replacement hardware profile.";
            item.service_name = inf.service_name;
            item.service_binary = inf.service_binary;
            item.service_binary_path = (fs::path(package.package_root) / inf.service_binary_path).string();
            item.load_order_group = inf.load_order_group;
            item.service_type = inf.service_type;
            item.start_type = inf.start_type;
            item.error_control = inf.error_control;
            plan.actions.push_back(std::move(item));
            plan.matching_storage_driver_found = true;
        }
    }
    if (!plan.matching_storage_driver_found) {
        plan.findings.push_back(finding(
            Severity::Blocker, "migration.storage_driver_missing",
            "No complete storage-controller INF matched the replacement hardware IDs.",
            "Import the signed VMD, RST, RAID, AHCI, or NVMe controller package supplied for the replacement computer."));
    }

    const auto valid_published_name = [](const std::string& value) {
        const auto lowered = [&value] {
            std::string copy = value;
            std::transform(copy.begin(), copy.end(), copy.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return copy;
        }();
        if (lowered.size() < 8 || lowered.rfind("oem", 0) != 0 || lowered.substr(lowered.size() - 4) != ".inf") return false;
        return std::all_of(lowered.begin() + 3, lowered.end() - 4, [](unsigned char ch) { return std::isdigit(ch); });
    };
    if (!options.requested_removals.empty() && !options.removal_risk_acknowledged) {
        plan.findings.push_back(finding(
            Severity::Blocker, "migration.removal_not_acknowledged",
            "Driver removal was requested without acknowledging the boot-critical removal risk.",
            "Preserve existing storage drivers, or explicitly approve each DISM published name after reviewing the offline inventory."));
    }
    for (const auto& removal : options.requested_removals) {
        if (!valid_published_name(removal)) {
            plan.findings.push_back(finding(
                Severity::Blocker, "migration.removal_name_invalid",
                "A requested driver removal is not a DISM published OEM INF name: " + removal,
                "Enumerate the offline driver store and select an exact oem<number>.inf name."));
            continue;
        }
        DriverPlanItem item;
        item.action = "remove";
        item.published_name = removal;
        item.reason = "Explicit administrator request; execute only after all additions succeed.";
        plan.actions.push_back(std::move(item));
    }
    if (options.requested_removals.empty()) {
        plan.facts.push_back("Existing storage drivers will be preserved as fallback boot paths.");
    } else if (options.removal_risk_acknowledged) {
        plan.findings.push_back(finding(
            Severity::Warning, "migration.removal_requested",
            "Third-party driver removal is scheduled after driver additions.",
            "Confirm that every published OEM INF is incompatible and that a verified image remains available."));
    }
    plan.facts.push_back("Driver additions are ordered before any requested removals.");
    plan.facts.push_back("Lazarus OS will install the boot-critical driver directly into the restored offline Windows installation.");
    plan.ready_for_servicing = plan.matching_storage_driver_found && !has_blocker(plan.findings);
    return plan;
}

SupportBundleManifest create_support_bundle_manifest() {
    return SupportBundleManifest{
        {
            "application.log",
            "imaging.log",
            "verification.json",
            "source-smart.json",
            "destination-smart.json",
            "hardware-profile.json",
            "port-profile.json",
            "image-metadata.json",
            "migration-plan.json",
        },
        {
            "customer file contents",
            "directory listings unless explicitly collected for local recovery",
            "browser history contents",
            "private registry values unrelated to boot recovery",
        },
    };
}

std::vector<DeviceIdentity> discover_block_devices() {
    std::vector<DeviceIdentity> devices;
    const fs::path sys_block("/sys/block");
    if (!fs::exists(sys_block)) {
        return devices;
    }

    const auto system_disks = collect_system_disk_names();

    for (const auto& entry : fs::directory_iterator(sys_block)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto disk = entry.path().filename().string();
        if (skip_block_device(disk)) {
            continue;
        }

        const auto sectors = read_u64_file(entry.path() / "size");

        DeviceIdentity device;
        device.linux_path = "/dev/" + disk;
        device.by_id_path = find_symlink_for_disk("/dev/disk/by-id", disk);
        device.by_path = find_symlink_for_disk("/dev/disk/by-path", disk);
        device.port_path = physical_port_identity(device.by_path);
        device.physical_path = !device.by_path.empty() ? device.by_path : device.by_id_path;
        if (device.physical_path.empty()) {
            device.physical_path = entry.path().string();
        }
        device.model = read_text_file(entry.path() / "device/model").value_or(disk);
        const auto serial = read_text_file(entry.path() / "device/serial").value_or(read_text_file(entry.path() / "device/wwid").value_or(""));
        device.serial = serial;
        device.serial_ending = serial_ending_from(serial);
        device.transport = detect_transport(entry.path());
        device.logical_block_size = read_u32_file(entry.path() / "queue/logical_block_size", 512);
        device.removable = read_u64_file(entry.path() / "removable") != 0;
        if (sectors == 0 && !device.removable) {
            continue;
        }
        device.size_bytes = sectors * static_cast<std::uint64_t>(device.logical_block_size == 0 ? 512 : device.logical_block_size);
        device.rotational = read_u64_file(entry.path() / "queue/rotational") != 0;
        device.partitions = list_partitions(entry.path(), disk);
        device.is_system_disk = system_disks.contains(disk);
        device.bench_role = device.is_system_disk ? DeviceRole::SystemDisk : DeviceRole::Unknown;
        devices.push_back(device);
    }

    std::sort(devices.begin(), devices.end(), [](const DeviceIdentity& left, const DeviceIdentity& right) {
        return left.linux_path < right.linux_path;
    });
    return devices;
}

}  // namespace lazarus
