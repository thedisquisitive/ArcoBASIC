#include "lazarum/viewer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <system_error>

#if defined(__linux__)
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace lazarum {

std::unique_ptr<ImageDataProvider> make_linux_image_data_provider();

namespace {

constexpr std::uintmax_t kMaximumMetadataBytes = 4 * 1024 * 1024;
constexpr std::uintmax_t kMaximumReportBytes = 16 * 1024 * 1024;

std::optional<std::string> read_small_regular_file(const fs::path& path, std::uintmax_t maximum,
                                                   std::string& error) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec || !fs::is_regular_file(status)) {
        error = "Not a regular file: " + path.string();
        return std::nullopt;
    }
    const auto size = fs::file_size(path, ec);
    if (ec || size > maximum) {
        error = ec ? ec.message() : "File exceeds the viewer safety limit.";
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Could not open " + path.string();
        return std::nullopt;
    }
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) {
        error = "Could not completely read " + path.string();
        return std::nullopt;
    }
    error.clear();
    return contents;
}

std::optional<std::size_t> json_value_start(const std::string& json, const std::string& key) {
    const auto quoted = "\"" + key + "\"";
    auto position = json.find(quoted);
    if (position == std::string::npos) return std::nullopt;
    position = json.find(':', position + quoted.size());
    if (position == std::string::npos) return std::nullopt;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    return position;
}

std::optional<std::string> json_string(const std::string& json, const std::string& key) {
    const auto start = json_value_start(json, key);
    if (!start || *start >= json.size() || json[*start] != '"') return std::nullopt;
    std::string result;
    bool escaped = false;
    for (std::size_t i = *start + 1; i < json.size(); ++i) {
        const char character = json[i];
        if (escaped) {
            switch (character) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                default: return std::nullopt;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return result;
        } else {
            result.push_back(character);
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> json_u64(const std::string& json, const std::string& key) {
    const auto start = json_value_start(json, key);
    if (!start || *start >= json.size() || !std::isdigit(static_cast<unsigned char>(json[*start]))) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (std::size_t i = *start; i < json.size() && std::isdigit(static_cast<unsigned char>(json[i])); ++i) {
        const unsigned digit = static_cast<unsigned>(json[i] - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return std::nullopt;
        value = value * 10 + digit;
    }
    return value;
}

bool known_report_name(const std::string& name) {
    static const std::set<std::string> exact{
        "completion-report.html", "completion-report.txt",
        "escalation-report.html", "escalation-report.txt",
        "image-creation-report.html", "image-creation-report.txt",
        "restore-report.html", "restore-report.txt",
        "verification.json", "imaging.log",
    };
    return exact.contains(name);
}

std::optional<fs::path> confined_report(const fs::path& image_directory, const std::string& report_name,
                                        std::string& error) {
    if (!known_report_name(report_name) || fs::path(report_name).filename() != report_name) {
        error = "The requested file is not a recognized Lazarus job report.";
        return std::nullopt;
    }
    std::error_code ec;
    const auto image = fs::canonical(image_directory, ec);
    if (ec) {
        error = "Could not resolve the image directory: " + ec.message();
        return std::nullopt;
    }
    const auto candidate = image / report_name;
    const auto status = fs::symlink_status(candidate, ec);
    if (ec || !fs::is_regular_file(status)) {
        error = "The selected report does not exist or is not a regular file.";
        return std::nullopt;
    }
    error.clear();
    return candidate;
}

#if defined(__linux__)
struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

ProcessResult run_udisksctl(const fs::path& device) {
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) return {-1, "Could not create the mount-helper pipe."};
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        return {-1, "Could not start the mount helper."};
    }
    if (child == 0) {
        ::dup2(descriptors[1], STDOUT_FILENO);
        ::dup2(descriptors[1], STDERR_FILENO);
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        ::execlp("udisksctl", "udisksctl", "mount", "--no-user-interaction", "--options",
                 "ro,noload,nosuid,nodev,noexec", "--block-device", device.c_str(), nullptr);
        _exit(127);
    }
    ::close(descriptors[1]);
    std::string output;
    std::array<char, 4096> buffer{};
    for (;;) {
        const auto count = ::read(descriptors[0], buffer.data(), buffer.size());
        if (count <= 0) break;
        output.append(buffer.data(), static_cast<std::size_t>(count));
    }
    ::close(descriptors[0]);
    int status = 0;
    if (::waitpid(child, &status, 0) < 0) return {-1, output + "Could not wait for the mount helper."};
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
}
#endif

}  // namespace

HostPlatform host_platform() {
#if defined(__linux__)
    return HostPlatform::Linux;
#elif defined(_WIN32)
    return HostPlatform::Windows;
#elif defined(__APPLE__)
    return HostPlatform::MacOS;
#else
    return HostPlatform::Unknown;
#endif
}

std::string to_string(HostPlatform platform) {
    switch (platform) {
        case HostPlatform::Linux: return "Linux";
        case HostPlatform::Windows: return "Windows";
        case HostPlatform::MacOS: return "macOS";
        case HostPlatform::Unknown: return "Unknown";
    }
    return "Unknown";
}

MountCapability mount_capability() {
    MountCapability result;
    result.platform = host_platform();
    switch (result.platform) {
        case HostPlatform::Linux:
            result.native_read_only_mount = true;
            result.helper_required = true;
            result.detail = "Uses UDisks/udisksctl with ro,noload,nosuid,nodev,noexec; authorization is handled by the OS.";
            break;
        case HostPlatform::Windows:
            result.detail = "Ext4 mount adapter reserved for a signed Lazarum helper or WSL integration; not enabled in this stub.";
            break;
        case HostPlatform::MacOS:
            result.detail = "Ext4 mount adapter reserved for a read-only ext4/FUSE provider; not enabled in this stub.";
            break;
        case HostPlatform::Unknown:
            result.detail = "No ext4 mount provider is defined for this platform.";
            break;
    }
    return result;
}

std::optional<fs::path> discover_lazarus_storage_device() {
#if defined(__linux__)
    const fs::path label("/dev/disk/by-label/LAZARUS_STORAGE");
    std::error_code ec;
    if (fs::exists(label, ec)) return label;
#endif
    return std::nullopt;
}

MountResult mount_ext4_read_only(const fs::path& device) {
    MountResult result;
#if defined(__linux__)
    if (!device.is_absolute() || device.string().rfind("/dev/", 0) != 0) {
        result.error = "Choose an absolute block-device path below /dev.";
        return result;
    }
    struct stat status {};
    if (::stat(device.c_str(), &status) != 0 || !S_ISBLK(status.st_mode)) {
        result.error = "The selected path is not a block device: " + device.string();
        return result;
    }
    const auto process = run_udisksctl(device);
    if (process.exit_code != 0) {
        result.error = process.output.empty() ? "The read-only UDisks mount failed." : process.output;
        return result;
    }
    const auto marker = process.output.rfind(" at ");
    if (marker != std::string::npos) {
        auto mount = process.output.substr(marker + 4);
        while (!mount.empty() && (mount.back() == '.' || std::isspace(static_cast<unsigned char>(mount.back())))) {
            mount.pop_back();
        }
        result.mount_point = mount;
    }
    result.mounted = true;
    if (result.mount_point.empty()) {
        result.error = "The drive mounted read-only, but UDisks did not report its mount point.";
    }
#else
    (void)device;
    result.error = mount_capability().detail;
#endif
    return result;
}

std::vector<ReportInfo> list_reports(const fs::path& image_directory) {
    std::vector<ReportInfo> reports;
    std::error_code ec;
    for (fs::directory_iterator it(image_directory, fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec)) {
        const auto status = it->symlink_status(ec);
        if (ec) break;
        const auto name = it->path().filename().string();
        if (!fs::is_regular_file(status) || !known_report_name(name)) continue;
        const auto size = it->file_size(ec);
        if (ec) break;
        reports.push_back({name, it->path(), size});
    }
    std::sort(reports.begin(), reports.end(), [](const auto& left, const auto& right) {
        return left.name < right.name;
    });
    return reports;
}

ImageSummary inspect_image(const fs::path& image_directory) {
    ImageSummary result;
    result.directory = image_directory;
    std::error_code ec;
    result.finalized = fs::is_regular_file(fs::symlink_status(image_directory / "FINALIZED", ec));
    ec.clear();
    result.incomplete = fs::is_regular_file(fs::symlink_status(image_directory / "INCOMPLETE", ec));
    ec.clear();
    const bool disk_present = fs::is_regular_file(fs::symlink_status(image_directory / "disk.raw", ec));
    ec.clear();
    const bool hashes_present = fs::is_regular_file(fs::symlink_status(image_directory / "hashes.dat", ec));

    std::string error;
    const auto metadata = read_small_regular_file(image_directory / "metadata.json", kMaximumMetadataBytes, error);
    if (!metadata) {
        result.warnings.push_back(error);
        return result;
    }
    result.structurally_recognized = json_string(*metadata, "format").value_or("") == "laz-dir" &&
                                     disk_present && hashes_present;
    result.format_version = json_u64(*metadata, "format_version").value_or(0);
    result.ticket_number = json_string(*metadata, "ticket_number").value_or("");
    result.customer_name = json_string(*metadata, "customer_name").value_or("");
    result.technician = json_string(*metadata, "technician").value_or("");
    result.purpose = json_string(*metadata, "purpose").value_or("");
    result.created_at = json_string(*metadata, "created_at").value_or("");
    result.source_model = json_string(*metadata, "model").value_or("");
    result.source_serial_ending = json_string(*metadata, "serial_ending").value_or("");
    result.compression = json_string(*metadata, "compression").value_or("unknown");
    result.logical_bytes = json_u64(*metadata, "bytes_written").value_or(0);
    result.stored_bytes = json_u64(*metadata, "bytes_stored").value_or(0);
    result.reports = list_reports(image_directory);
    if (!result.structurally_recognized) result.warnings.push_back("Required Lazarus image files are missing or unrecognized.");
    if (!result.finalized) result.warnings.push_back("Image is not finalized.");
    if (result.incomplete) result.warnings.push_back("INCOMPLETE marker is present.");
    return result;
}

ScanResult scan_storage(const fs::path& storage_root) {
    ScanResult result;
    result.storage_root = storage_root;
    std::error_code ec;
    const auto root_status = fs::symlink_status(storage_root, ec);
    if (ec || !fs::is_directory(root_status)) {
        result.warnings.push_back("Storage root is unavailable or is not a directory: " + storage_root.string());
        return result;
    }

    fs::recursive_directory_iterator iterator(storage_root, fs::directory_options::skip_permission_denied, ec), end;
    while (!ec && iterator != end) {
        if (iterator.depth() > 8) iterator.disable_recursion_pending();
        const auto status = iterator->symlink_status(ec);
        if (ec) break;
        if (fs::is_symlink(status)) {
            iterator.disable_recursion_pending();
        } else if (fs::is_regular_file(status) && iterator->path().filename() == "metadata.json") {
            auto image = inspect_image(iterator->path().parent_path());
            if (image.structurally_recognized || !image.ticket_number.empty()) {
                result.images.push_back(std::move(image));
                iterator.disable_recursion_pending();
            }
        }
        iterator.increment(ec);
    }
    if (ec) result.warnings.push_back("The storage scan ended early: " + ec.message());
    std::sort(result.images.begin(), result.images.end(), [](const auto& left, const auto& right) {
        if (left.created_at != right.created_at) return left.created_at > right.created_at;
        return left.directory < right.directory;
    });
    return result;
}

std::string read_report(const fs::path& image_directory, const std::string& report_name, std::string& error) {
    const auto path = confined_report(image_directory, report_name, error);
    if (!path) return {};
    return read_small_regular_file(*path, kMaximumReportBytes, error).value_or("");
}

bool extract_report(const fs::path& image_directory, const std::string& report_name,
                    const fs::path& destination, fs::path& written_path, std::string& error) {
    const auto source = confined_report(image_directory, report_name, error);
    if (!source) return false;
    std::error_code ec;
    const auto destination_status = fs::symlink_status(destination, ec);
    if (!ec && fs::is_directory(destination_status)) {
        written_path = destination / report_name;
    } else {
        ec.clear();
        written_path = destination;
    }
    if (written_path.empty() || fs::exists(written_path, ec)) {
        error = "The extraction destination already exists or is invalid; Lazarum will not overwrite it.";
        return false;
    }
    const auto parent = written_path.parent_path();
    if (!parent.empty() && !fs::is_directory(parent, ec)) {
        error = "The extraction destination directory does not exist.";
        return false;
    }
    if (!fs::copy_file(*source, written_path, fs::copy_options::none, ec)) {
        error = "Could not extract the report: " + ec.message();
        return false;
    }
    error.clear();
    return true;
}

DataAccessCapability image_data_capability(const fs::path& image_directory) {
    DataAccessCapability result;
    const auto image = inspect_image(image_directory);
    result.image_recognized = image.structurally_recognized;
    if (!image.structurally_recognized) {
        result.detail = "This directory is not a recognized Lazarus image.";
    } else if (!image.finalized || image.incomplete) {
        result.detail = "Lazarum will not mount an incomplete image. Reports and metadata remain viewable.";
    } else {
#if defined(__linux__) && defined(LAZARUM_WITH_LAZARUS_CORE)
        result.raw_reconstruction_available = true;
        result.filesystem_explorer_available = true;
        result.file_extraction_available = true;
        result.detail = "Ready to verify, attach, and browse this image read-only.";
#else
        result.detail = "Image recognized, but this build has no image-filesystem provider.";
#endif
    }
    return result;
}

namespace {

class ScaffoldImageDataProvider final : public ImageDataProvider {
public:
    void set_progress_callback(std::function<void(std::string)> callback) override {
        (void)callback;
    }

    DataAccessCapability capability(const fs::path& image_directory) const override {
        return image_data_capability(image_directory);
    }

    DataOperationResult list_volumes(const fs::path&) override {
        return unavailable();
    }

    DataOperationResult list_directory(const fs::path&, const std::string&, const std::string&) override {
        return unavailable();
    }

    DataOperationResult extract(const fs::path&, const std::string&, const std::vector<std::string>&,
                                const fs::path&) override {
        return unavailable();
    }

private:
    static DataOperationResult unavailable() {
        DataOperationResult result;
        result.error = "No verified Lazarum image-filesystem provider is installed. Nothing was extracted.";
        return result;
    }
};

}  // namespace

std::unique_ptr<ImageDataProvider> make_image_data_provider() {
#if defined(__linux__) && defined(LAZARUM_WITH_LAZARUS_CORE)
    if (auto provider = make_linux_image_data_provider()) return provider;
#endif
    return std::make_unique<ScaffoldImageDataProvider>();
}

}  // namespace lazarum
