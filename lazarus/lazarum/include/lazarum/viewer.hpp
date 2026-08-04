#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lazarum {

enum class HostPlatform {
    Linux,
    Windows,
    MacOS,
    Unknown,
};

struct MountCapability {
    HostPlatform platform = HostPlatform::Unknown;
    bool native_read_only_mount = false;
    bool helper_required = true;
    std::string detail;
};

struct MountResult {
    bool mounted = false;
    std::filesystem::path mount_point;
    std::string error;
};

struct ReportInfo {
    std::string name;
    std::filesystem::path path;
    std::uint64_t size_bytes = 0;
};

struct ImageSummary {
    std::filesystem::path directory;
    std::string ticket_number;
    std::string customer_name;
    std::string technician;
    std::string purpose;
    std::string created_at;
    std::string source_model;
    std::string source_serial_ending;
    std::string compression;
    std::uint64_t logical_bytes = 0;
    std::uint64_t stored_bytes = 0;
    std::uint64_t format_version = 0;
    bool finalized = false;
    bool incomplete = false;
    bool structurally_recognized = false;
    std::vector<ReportInfo> reports;
    std::vector<std::string> warnings;
};

struct ScanResult {
    std::filesystem::path storage_root;
    std::vector<ImageSummary> images;
    std::vector<std::string> warnings;
};

struct DataAccessCapability {
    bool image_recognized = false;
    bool raw_reconstruction_available = false;
    bool filesystem_explorer_available = false;
    bool file_extraction_available = false;
    std::string detail;
};

struct ImageVolume {
    std::string id;
    std::string label;
    std::string filesystem;
    std::uint64_t size_bytes = 0;
    bool encrypted = false;
};

struct ImageFileEntry {
    std::string name;
    std::string relative_path;
    std::uint64_t size_bytes = 0;
    bool directory = false;
    bool extractable = false;
    std::string type;
};

struct DataOperationResult {
    bool completed = false;
    std::vector<ImageVolume> volumes;
    std::vector<ImageFileEntry> entries;
    std::vector<std::filesystem::path> extracted_paths;
    std::string error;
};

// Platform image providers implement authenticated on-demand logical-disk reads,
// read-only partition/filesystem access, and confined regular-file extraction.
// Keeping this boundary separate prevents a UI stub from claiming extraction
// before a real provider has completed and reported its output paths.
class ImageDataProvider {
public:
    virtual ~ImageDataProvider() = default;
    virtual void set_progress_callback(std::function<void(std::string)> callback) = 0;
    virtual DataAccessCapability capability(const std::filesystem::path& image_directory) const = 0;
    virtual DataOperationResult list_volumes(const std::filesystem::path& image_directory) = 0;
    virtual DataOperationResult list_directory(const std::filesystem::path& image_directory,
                                               const std::string& volume_id,
                                               const std::string& relative_path) = 0;
    virtual DataOperationResult extract(const std::filesystem::path& image_directory,
                                        const std::string& volume_id,
                                        const std::vector<std::string>& relative_paths,
                                        const std::filesystem::path& destination) = 0;
};

HostPlatform host_platform();
std::string to_string(HostPlatform platform);
MountCapability mount_capability();
std::optional<std::filesystem::path> discover_lazarus_storage_device();

// Mounts only through an argument-vector process call; no user value is passed
// through a command shell. Linux currently uses UDisks and always requests ro.
MountResult mount_ext4_read_only(const std::filesystem::path& device);

ScanResult scan_storage(const std::filesystem::path& storage_root);
ImageSummary inspect_image(const std::filesystem::path& image_directory);
std::vector<ReportInfo> list_reports(const std::filesystem::path& image_directory);
std::string read_report(const std::filesystem::path& image_directory,
                        const std::string& report_name,
                        std::string& error);
bool extract_report(const std::filesystem::path& image_directory,
                    const std::string& report_name,
                    const std::filesystem::path& destination,
                    std::filesystem::path& written_path,
                    std::string& error);
DataAccessCapability image_data_capability(const std::filesystem::path& image_directory);
std::unique_ptr<ImageDataProvider> make_image_data_provider();

}  // namespace lazarum
