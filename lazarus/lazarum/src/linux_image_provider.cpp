#include "lazarum/viewer.hpp"

#if defined(__linux__) && defined(LAZARUM_WITH_LAZARUS_CORE)

#include "lazarus/core.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <chrono>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;

namespace lazarum {
namespace {

struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

ProcessResult run_process(const std::vector<std::string>& arguments) {
    if (arguments.empty()) return {-1, "No helper command was provided."};
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) return {-1, std::strerror(errno)};
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        return {-1, std::strerror(errno)};
    }
    if (child == 0) {
        ::dup2(descriptors[1], STDOUT_FILENO);
        ::dup2(descriptors[1], STDERR_FILENO);
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());
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
    if (::waitpid(child, &status, 0) < 0) return {-1, output + std::strerror(errno)};
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, output};
}

bool executable_available(const char* name) {
    const char* path = std::getenv("PATH");
    if (path == nullptr) return false;
    std::stringstream directories(path);
    std::string directory;
    while (std::getline(directories, directory, ':')) {
        if (directory.empty()) directory = ".";
        const auto candidate = fs::path(directory) / name;
        if (::access(candidate.c_str(), X_OK) == 0) return true;
    }
    return false;
}

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string decode_lsblk_value(const std::string& value) {
    std::string decoded;
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\\' && index + 3 < value.size() && value[index + 1] == 'x') {
            unsigned parsed = 0;
            const auto first = value.data() + index + 2;
            const auto last = first + 2;
            const auto result = std::from_chars(first, last, parsed, 16);
            if (result.ec == std::errc{} && result.ptr == last) {
                decoded.push_back(static_cast<char>(parsed));
                index += 3;
                continue;
            }
        }
        decoded.push_back(value[index]);
    }
    return decoded;
}

std::map<std::string, std::string> parse_pairs(const std::string& line) {
    std::map<std::string, std::string> fields;
    std::size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) ++position;
        const auto equals = line.find('=', position);
        if (equals == std::string::npos || equals + 1 >= line.size() || line[equals + 1] != '"') break;
        const auto key = line.substr(position, equals - position);
        position = equals + 2;
        std::string value;
        while (position < line.size()) {
            if (line[position] == '"' && (position == 0 || line[position - 1] != '\\')) {
                ++position;
                break;
            }
            value.push_back(line[position++]);
        }
        fields[key] = decode_lsblk_value(value);
    }
    return fields;
}

bool supported_filesystem(const std::string& filesystem) {
    static const std::set<std::string> supported{
        "ntfs", "ntfs3", "vfat", "fat", "exfat", "ext2", "ext3", "ext4",
    };
    return supported.contains(filesystem);
}

std::uint64_t parse_u64(const std::string& value) {
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} ? result : 0;
}

bool safe_relative_path(const std::string& value, fs::path& clean, std::string& error) {
    if (value.find('\0') != std::string::npos) {
        error = "The requested path contains an invalid character.";
        return false;
    }
    clean = fs::path(value).lexically_normal();
    if (clean.is_absolute() || clean.has_root_path()) {
        error = "The requested path is not relative to the selected volume.";
        return false;
    }
    for (const auto& component : clean) {
        if (component == "..") {
            error = "The requested path attempts to leave the selected volume.";
            return false;
        }
    }
    if (clean == ".") clean.clear();
    return true;
}

bool resolve_beneath(const fs::path& root, const std::string& relative, fs::path& resolved, std::string& error) {
    fs::path clean;
    if (!safe_relative_path(relative, clean, error)) return false;
    std::error_code ec;
    const auto canonical_root = fs::canonical(root, ec);
    if (ec) {
        error = "The read-only volume root is unavailable: " + ec.message();
        return false;
    }
    resolved = fs::weakly_canonical(canonical_root / clean, ec);
    if (ec) {
        error = "The requested image path is unavailable: " + ec.message();
        return false;
    }
    const auto relative_to_root = resolved.lexically_relative(canonical_root);
    if (relative_to_root.empty() && resolved != canonical_root) {
        error = "The requested path is outside the selected volume.";
        return false;
    }
    for (const auto& component : relative_to_root) {
        if (component == "..") {
            error = "The requested path is outside the selected volume.";
            return false;
        }
    }
    return true;
}

fs::path nbd_helper_path() {
    if (const char* configured = std::getenv("LAZARUM_NBD_HELPER"); configured && *configured) {
        return configured;
    }
    std::error_code ec;
    const auto executable = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        const auto sibling = executable.parent_path() / "lazarum-nbd";
        if (::access(sibling.c_str(), X_OK) == 0) return sibling;
    }
    return "lazarum-nbd";
}

std::string nbd_device_from_output(const std::string& output) {
    const auto marker = output.find("device=/dev/nbd");
    if (marker == std::string::npos) return {};
    const auto start = marker + 7;
    const auto end = output.find_first_of("\r\n ", start);
    const auto device = output.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (device.rfind("/dev/nbd", 0) != 0 || device.find('/', 5) != std::string::npos) return {};
    return device;
}

bool wait_for_nbd_online(const std::string& device) {
    const auto sectors_path = fs::path("/sys/class/block") / fs::path(device).filename() / "size";
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        std::ifstream sectors(sectors_path);
        std::uint64_t count = 0;
        if (sectors >> count && count != 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

bool copy_regular_file(const fs::path& source, const fs::path& destination, std::uint64_t& bytes,
                       std::string& error) {
    const int input = ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0) {
        error = "Could not open " + source.filename().string() + " read-only: " + std::strerror(errno);
        return false;
    }
    struct stat status {};
    if (::fstat(input, &status) != 0 || !S_ISREG(status.st_mode)) {
        error = "Only regular files can be extracted.";
        ::close(input);
        return false;
    }
    const int output = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (output < 0) {
        error = "Could not create " + destination.string() + " without overwriting data: " + std::strerror(errno);
        ::close(input);
        return false;
    }
    std::array<char, 1024 * 1024> buffer{};
    bool copied = true;
    for (;;) {
        const auto count = ::read(input, buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) {
            error = "Read failed while extracting " + source.filename().string() + ": " + std::strerror(errno);
            copied = false;
            break;
        }
        ssize_t offset = 0;
        while (offset < count) {
            const auto written = ::write(output, buffer.data() + offset, static_cast<std::size_t>(count - offset));
            if (written <= 0) {
                error = "Write failed while extracting " + destination.filename().string() + ": " + std::strerror(errno);
                copied = false;
                break;
            }
            offset += written;
            bytes += static_cast<std::uint64_t>(written);
        }
        if (!copied) break;
    }
    if (copied && ::fsync(output) != 0) {
        error = "Could not flush " + destination.filename().string() + ": " + std::strerror(errno);
        copied = false;
    }
    if (::close(input) != 0 && copied) {
        error = "Could not finish reading " + source.filename().string() + ".";
        copied = false;
    }
    if (::close(output) != 0 && copied) {
        error = "Could not finish writing " + destination.filename().string() + ".";
        copied = false;
    }
    if (!copied) {
        std::error_code ignored;
        fs::remove(destination, ignored);
    }
    return copied;
}

bool copy_tree(const fs::path& source, const fs::path& destination, std::uint64_t& bytes,
               unsigned depth, std::string& error) {
    if (depth > 128) {
        error = "The selected directory is nested too deeply to extract safely.";
        return false;
    }
    std::error_code ec;
    const auto status = fs::symlink_status(source, ec);
    if (ec || fs::is_symlink(status)) {
        error = "Symbolic links cannot be extracted.";
        return false;
    }
    if (fs::is_regular_file(status)) return copy_regular_file(source, destination, bytes, error);
    if (!fs::is_directory(status)) {
        error = "Special files cannot be extracted.";
        return false;
    }
    if (!fs::create_directory(destination, ec) || ec) {
        error = "Could not create " + destination.string() + " without overwriting data: " + ec.message();
        return false;
    }
    for (fs::directory_iterator iterator(source, ec), end; !ec && iterator != end; iterator.increment(ec)) {
        if (!copy_tree(iterator->path(), destination / iterator->path().filename(), bytes, depth + 1, error)) {
            return false;
        }
    }
    if (ec) {
        error = "Could not enumerate " + source.string() + ": " + ec.message();
        return false;
    }
    const int directory = ::open(destination.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory >= 0) {
        (void)::fsync(directory);
        ::close(directory);
    }
    return true;
}

struct VolumeState {
    ImageVolume summary;
    std::string device;
    fs::path mount_point;
};

struct Session {
    fs::path image_directory;
    std::string nbd_device;
    fs::path nbd_helper;
    std::vector<VolumeState> volumes;
};

class LinuxImageDataProvider final : public ImageDataProvider {
public:
    ~LinuxImageDataProvider() override {
        for (auto& [key, session] : sessions_) {
            (void)key;
            close_session(session);
        }
    }

    void set_progress_callback(std::function<void(std::string)> callback) override {
        progress_ = std::move(callback);
    }

    DataAccessCapability capability(const fs::path& image_directory) const override {
        auto result = image_data_capability(image_directory);
        const auto image = inspect_image(image_directory);
        if (!image.structurally_recognized || !image.finalized || image.incomplete) return result;
        result.raw_reconstruction_available = true;
        const auto nbd_helper = nbd_helper_path();
        const bool nbd_available = nbd_helper.has_parent_path()
            ? ::access(nbd_helper.c_str(), X_OK) == 0
            : executable_available(nbd_helper.c_str());
        const bool helpers = executable_available("mount") && executable_available("umount") &&
                             executable_available("lsblk") &&
                             executable_available("udevadm") && executable_available("findmnt") &&
                             executable_available("sudo") && nbd_available;
        result.filesystem_explorer_available = helpers;
        result.file_extraction_available = helpers;
        result.detail = helpers
            ? "Ready for immediate read-only browsing with on-demand chunk verification."
            : "On-demand browsing requires lazarum-nbd, sudo, mount, umount, lsblk, udevadm, and findmnt.";
        return result;
    }

    DataOperationResult list_volumes(const fs::path& image_directory) override {
        DataOperationResult result;
        Session* session = ensure_session(image_directory, result.error);
        if (session == nullptr) return result;
        for (const auto& volume : session->volumes) result.volumes.push_back(volume.summary);
        result.completed = true;
        return result;
    }

    DataOperationResult list_directory(const fs::path& image_directory, const std::string& volume_id,
                                       const std::string& relative_path) override {
        DataOperationResult result;
        Session* session = ensure_session(image_directory, result.error);
        if (session == nullptr) return result;
        VolumeState* volume = find_volume(*session, volume_id);
        if (volume == nullptr) {
            result.error = "The selected image volume no longer exists.";
            return result;
        }
        if (!mount_volume(*volume, result.error)) return result;
        fs::path directory;
        if (!resolve_beneath(volume->mount_point, relative_path, directory, result.error)) return result;
        std::error_code ec;
        const auto directory_status = fs::symlink_status(directory, ec);
        if (ec || !fs::is_directory(directory_status) || fs::is_symlink(directory_status)) {
            result.error = "The requested image path is not a directory.";
            return result;
        }
        for (fs::directory_iterator iterator(directory, ec), end;
             !ec && iterator != end && result.entries.size() < 10000; iterator.increment(ec)) {
            const auto status = iterator->symlink_status(ec);
            if (ec) break;
            ImageFileEntry entry;
            entry.name = iterator->path().filename().string();
            const auto child = (fs::path(relative_path) / iterator->path().filename()).lexically_normal();
            entry.relative_path = child == "." ? std::string{} : child.string();
            if (fs::is_directory(status)) {
                entry.directory = true;
                entry.extractable = true;
                entry.type = "directory";
            } else if (fs::is_regular_file(status)) {
                entry.extractable = true;
                entry.type = "file";
                entry.size_bytes = iterator->file_size(ec);
                if (ec) break;
            } else if (fs::is_symlink(status)) {
                entry.type = "link";
            } else {
                entry.type = "special";
            }
            result.entries.push_back(std::move(entry));
        }
        if (ec) {
            result.error = "Could not enumerate the image directory: " + ec.message();
            result.entries.clear();
            return result;
        }
        std::sort(result.entries.begin(), result.entries.end(), [](const auto& left, const auto& right) {
            if (left.directory != right.directory) return left.directory;
            return left.name < right.name;
        });
        result.completed = true;
        return result;
    }

    DataOperationResult extract(const fs::path& image_directory, const std::string& volume_id,
                                const std::vector<std::string>& relative_paths,
                                const fs::path& destination) override {
        DataOperationResult result;
        if (relative_paths.empty()) {
            result.error = "Select at least one regular file or directory to extract.";
            return result;
        }
        Session* session = ensure_session(image_directory, result.error);
        if (session == nullptr) return result;
        VolumeState* volume = find_volume(*session, volume_id);
        if (volume == nullptr) {
            result.error = "The selected image volume no longer exists.";
            return result;
        }
        if (!mount_volume(*volume, result.error)) return result;
        std::error_code ec;
        const auto destination_root = fs::canonical(destination, ec);
        if (ec || !fs::is_directory(destination_root, ec)) {
            result.error = "Choose an existing extraction destination directory.";
            return result;
        }
        std::uint64_t bytes = 0;
        for (const auto& relative : relative_paths) {
            fs::path source;
            if (!resolve_beneath(volume->mount_point, relative, source, result.error)) return result;
            const auto status = fs::symlink_status(source, ec);
            if (ec || fs::is_symlink(status) ||
                (!fs::is_regular_file(status) && !fs::is_directory(status))) {
                result.error = "Symbolic links and special files cannot be extracted.";
                return result;
            }
            const auto output = destination_root / source.filename();
            if (fs::exists(output, ec)) {
                result.error = "Lazarum will not overwrite the existing destination: " + output.string();
                return result;
            }
            notify("Extracting " + source.filename().string() + "…");
            if (!copy_tree(source, output, bytes, 0, result.error)) {
                std::error_code cleanup_error;
                fs::remove_all(output, cleanup_error);
                return result;
            }
            result.extracted_paths.push_back(output);
        }
        result.completed = true;
        notify("Extraction completed and flushed to disk.");
        return result;
    }

private:
    void notify(const std::string& message) const {
        if (progress_) progress_(message);
    }

    Session* ensure_session(const fs::path& requested, std::string& error) {
        std::error_code ec;
        const auto image_directory = fs::canonical(requested, ec);
        if (ec) {
            error = "Could not resolve the selected image directory: " + ec.message();
            return nullptr;
        }
        const auto key = image_directory.string();
        if (const auto found = sessions_.find(key); found != sessions_.end()) return &found->second;
        const auto capability_state = capability(image_directory);
        if (!capability_state.filesystem_explorer_available) {
            error = capability_state.detail;
            return nullptr;
        }
        Session session;
        session.image_directory = image_directory;
        session.nbd_helper = nbd_helper_path();
        notify("Opening image instantly with on-demand verification…");
        const auto attached = run_process({"sudo", "-n", session.nbd_helper.string(),
                                           "attach", image_directory.string()});
        if (attached.exit_code != 0) {
            error = "Could not expose the image read-only: " + trim(attached.output);
            return nullptr;
        }
        session.nbd_device = nbd_device_from_output(attached.output);
        if (session.nbd_device.empty()) {
            error = "The on-demand image helper did not report its read-only device.";
            return nullptr;
        }
        if (!wait_for_nbd_online(session.nbd_device)) {
            error = "The on-demand image device did not become ready within two seconds.";
            close_session(session);
            return nullptr;
        }
        (void)run_process({"sudo", "-n", "blockdev", "--rereadpt", session.nbd_device});
        (void)run_process({"sudo", "-n", "udevadm", "trigger", "--action=change", "--settle",
                           (fs::path("/sys/class/block") / fs::path(session.nbd_device).filename()).string()});
        (void)run_process({"udevadm", "settle", "--timeout=10"});
        const auto layout = run_process({"lsblk", "-P", "-p", "-b", "-n", "-o",
                                         "NAME,TYPE,FSTYPE,LABEL,SIZE", session.nbd_device});
        if (layout.exit_code != 0) {
            error = "Could not enumerate image partitions: " + trim(layout.output);
            close_session(session);
            return nullptr;
        }
        std::vector<std::map<std::string, std::string>> rows;
        std::istringstream lines(layout.output);
        std::string line;
        while (std::getline(lines, line)) {
            auto fields = parse_pairs(line);
            if (!fields.empty() && (fields["TYPE"] == "part" || fields["TYPE"] == "disk")) {
                rows.push_back(std::move(fields));
            }
        }
        const bool has_partitions = std::any_of(rows.begin(), rows.end(), [](const auto& fields) {
            return fields.at("TYPE") == "part";
        });
        std::size_t number = 0;
        for (const auto& fields : rows) {
            if (has_partitions && fields.at("TYPE") == "disk") continue;
            VolumeState volume;
            volume.device = fields.at("NAME");
            volume.summary.id = volume.device;
            volume.summary.filesystem = fields.at("FSTYPE");
            if (volume.summary.filesystem.empty()) {
                const auto probed = run_process({"sudo", "-n", "blkid", "-p", "-s", "TYPE",
                                                 "-o", "value", volume.device});
                if (probed.exit_code == 0) volume.summary.filesystem = trim(probed.output);
            }
            volume.summary.label = fields.at("LABEL").empty()
                ? "Volume " + std::to_string(++number)
                : fields.at("LABEL");
            volume.summary.size_bytes = parse_u64(fields.at("SIZE"));
            volume.summary.encrypted = volume.summary.filesystem == "BitLocker";
            session.volumes.push_back(std::move(volume));
        }
        if (session.volumes.empty()) {
            error = "The image attached read-only, but no volume candidates were found. Review its escalation report.";
            close_session(session);
            return nullptr;
        }
        auto [inserted, ok] = sessions_.emplace(key, std::move(session));
        (void)ok;
        return &inserted->second;
    }

    static VolumeState* find_volume(Session& session, const std::string& id) {
        const auto found = std::find_if(session.volumes.begin(), session.volumes.end(),
                                        [&](const auto& volume) { return volume.summary.id == id; });
        return found == session.volumes.end() ? nullptr : &*found;
    }

    bool mount_volume(VolumeState& volume, std::string& error) {
        if (!volume.mount_point.empty()) return true;
        if (!supported_filesystem(volume.summary.filesystem)) {
            error = "The " + (volume.summary.filesystem.empty() ? std::string("unknown") : volume.summary.filesystem) +
                    " filesystem is not supported. NTFS, FAT, exFAT, and ext2/3/4 are supported.";
            return false;
        }
        notify("Mounting " + volume.summary.label + " read-only…");
        const bool ext = volume.summary.filesystem == "ext2" || volume.summary.filesystem == "ext3" ||
                         volume.summary.filesystem == "ext4";
        const bool ownership_options = volume.summary.filesystem == "ntfs" ||
                                       volume.summary.filesystem == "ntfs3" ||
                                       volume.summary.filesystem == "vfat" ||
                                       volume.summary.filesystem == "fat" ||
                                       volume.summary.filesystem == "exfat";
        std::string options = ext ? "ro,noload,nosuid,nodev,noexec" : "ro,nosuid,nodev,noexec";
        if (ownership_options) {
            options += ",uid=" + std::to_string(::getuid()) + ",gid=" + std::to_string(::getgid()) +
                       ",umask=022";
        }
        volume.mount_point = fs::path("/run/lazarum") / std::to_string(::getuid()) /
                             (std::to_string(::getpid()) + "-" + fs::path(volume.device).filename().string());
        const auto prepared = run_process({"sudo", "-n", "install", "-d", "-o",
                                           std::to_string(::getuid()), "-g", std::to_string(::getgid()),
                                           "-m", "0700", volume.mount_point.string()});
        if (prepared.exit_code != 0) {
            error = "Could not create the private read-only mount point: " + trim(prepared.output);
            volume.mount_point.clear();
            return false;
        }
        const auto mounted = run_process({"sudo", "-n", "mount", "-o", options,
                                          volume.device, volume.mount_point.string()});
        if (mounted.exit_code != 0) {
            error = "Could not mount the image volume read-only: " + trim(mounted.output);
            (void)run_process({"sudo", "-n", "rmdir", volume.mount_point.string()});
            volume.mount_point.clear();
            return false;
        }
        const auto verified = run_process({"findmnt", "-rn", "-T", volume.mount_point.string(),
                                           "-O", "ro", "-o", "TARGET"});
        if (verified.exit_code != 0 || trim(verified.output) != volume.mount_point.string()) {
            (void)run_process({"sudo", "-n", "umount", volume.mount_point.string()});
            (void)run_process({"sudo", "-n", "rmdir", volume.mount_point.string()});
            error = "The image volume did not confirm a read-only mount; it was detached.";
            volume.mount_point.clear();
            return false;
        }
        return true;
    }

    static void close_session(Session& session) {
        for (auto& volume : session.volumes) {
            if (!volume.mount_point.empty()) {
                (void)run_process({"sudo", "-n", "umount", volume.mount_point.string()});
                (void)run_process({"sudo", "-n", "rmdir", volume.mount_point.string()});
                volume.mount_point.clear();
            }
        }
        if (!session.nbd_device.empty()) {
            (void)run_process({"sudo", "-n", session.nbd_helper.string(),
                               "detach", session.nbd_device});
            session.nbd_device.clear();
        }
    }

    std::function<void(std::string)> progress_;
    std::unordered_map<std::string, Session> sessions_;
};

}  // namespace

std::unique_ptr<ImageDataProvider> make_linux_image_data_provider() {
    return std::make_unique<LinuxImageDataProvider>();
}

}  // namespace lazarum

#else

namespace lazarum {

std::unique_ptr<ImageDataProvider> make_linux_image_data_provider() {
    return {};
}

}  // namespace lazarum

#endif
