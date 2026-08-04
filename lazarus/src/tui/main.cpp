#include "lazarus/core.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string human_size(std::uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 5) {
        value /= 1000.0;
        ++unit;
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << " " << units[unit];
    return out.str();
}

std::string human_duration(std::uint64_t total_seconds) {
    const auto hours = total_seconds / 3600;
    const auto minutes = (total_seconds % 3600) / 60;
    const auto seconds = total_seconds % 60;
    std::ostringstream out;
    if (hours > 0) {
        out << hours << "h" << std::setw(2) << std::setfill('0') << minutes << "m";
    } else if (minutes > 0) {
        out << minutes << "m" << std::setw(2) << std::setfill('0') << seconds << "s";
    } else {
        out << seconds << "s";
    }
    return out.str();
}

void clear_screen() {
    std::cout << "\033[2J\033[H";
}

void pause() {
    std::cout << "\nPress Enter to continue.";
    std::string line;
    std::getline(std::cin, line);
}

std::string prompt(const std::string& label) {
    std::cout << label;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

std::string prompt_default(const std::string& label, const std::string& fallback) {
    std::cout << label << " [" << fallback << "]: ";
    std::string value;
    std::getline(std::cin, value);
    return value.empty() ? fallback : value;
}

bool prompt_yes_no(const std::string& label) {
    const auto value = prompt(label + " [y/N]: ");
    return !value.empty() && (value[0] == 'y' || value[0] == 'Y');
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_case_insensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    return lowercase(haystack).find(lowercase(needle)) != std::string::npos;
}

int prompt_int(const std::string& label) {
    std::cout << label;
    int value = 0;
    if (!(std::cin >> value)) {
        std::cin.clear();
        value = 0;
    }
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    return value;
}

void print_header(const std::string& title) {
    clear_screen();
    std::cout << "Arcology Lazarus TUI\n";
    std::cout << "Offline Imaging | Recovery | Hardware Migration\n";
    std::cout << "================================================\n\n";
    std::cout << title << "\n\n";
}

void print_findings(const std::vector<lazarus::SafetyFinding>& findings) {
    for (const auto& finding : findings) {
        std::cout << "  [" << lazarus::to_string(finding.severity) << "] "
                  << finding.code << ": " << finding.observed << " "
                  << finding.action << "\n";
    }
}

std::vector<lazarus::DeviceIdentity> load_devices(const lazarus::BenchProfile& bench);

bool has_blocker(const std::vector<lazarus::SafetyFinding>& findings) {
    for (const auto& finding : findings) {
        if (finding.severity == lazarus::Severity::Blocker) {
            return true;
        }
    }
    return false;
}

bool has_imageable_layout(const lazarus::DiskInspection& inspection) {
    return inspection.gpt_header_valid || inspection.mbr_detected || !inspection.partitions.empty();
}

std::string identity_for_profile(const lazarus::DeviceIdentity& device) {
    if (!device.by_path.empty()) {
        return device.by_path;
    }
    if (!device.physical_path.empty()) {
        return device.physical_path;
    }
    if (!device.by_id_path.empty()) {
        return device.by_id_path;
    }
    return device.linux_path;
}

std::string describe_device_one_line(const lazarus::DeviceIdentity& device) {
    std::ostringstream out;
    out << device.linux_path << " | " << lazarus::to_string(device.bench_role)
        << " | " << human_size(device.size_bytes) << " | " << device.model;
    if (!device.serial_ending.empty()) {
        out << " | serial ending " << device.serial_ending;
    }
    return out.str();
}

bool contains_exact(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void remove_exact(std::vector<std::string>& values, const std::string& value) {
    values.erase(std::remove(values.begin(), values.end(), value), values.end());
}

bool ensure_writable_directory(const std::string& path, std::string& error) {
    if (path.empty()) {
        error = "Path is empty.";
        return false;
    }

    std::error_code fs_error;
    std::filesystem::create_directories(path, fs_error);
    if (fs_error) {
        error = "Could not create directory: " + fs_error.message();
        return false;
    }
    if (!std::filesystem::is_directory(path, fs_error)) {
        error = "Path is not a directory.";
        return false;
    }

    const auto test_path = std::filesystem::path(path) / ".lazarus-write-test";
    {
        std::ofstream out(test_path);
        if (!out) {
            error = "Could not create a test file in that directory.";
            return false;
        }
        out << "ok\n";
    }
    std::filesystem::remove(test_path, fs_error);
    error.clear();
    return true;
}

void assign_device_role(lazarus::BenchProfile& bench, const lazarus::DeviceIdentity& device, lazarus::DeviceRole role) {
    const auto identity = !device.port_path.empty() ? device.port_path : lazarus::physical_port_identity(device.by_path);
    if (identity.empty()) {
        return;
    }

    remove_exact(bench.source_only_paths, identity);
    remove_exact(bench.destination_only_paths, identity);
    remove_exact(bench.removable_media_paths, identity);
    remove_exact(bench.ignored_paths, identity);
    remove_exact(bench.image_storage_port_paths, identity);

    switch (role) {
        case lazarus::DeviceRole::SourceOnly:
            bench.source_only_paths.push_back(identity);
            break;
        case lazarus::DeviceRole::DestinationOnly:
            bench.destination_only_paths.push_back(identity);
            break;
        case lazarus::DeviceRole::ImageStorage:
            bench.image_storage_port_paths.push_back(identity);
            break;
        case lazarus::DeviceRole::RemovableMedia:
            bench.removable_media_paths.push_back(identity);
            break;
        case lazarus::DeviceRole::Ignored:
            bench.ignored_paths.push_back(identity);
            break;
        case lazarus::DeviceRole::Unknown:
        case lazarus::DeviceRole::SystemDisk:
            break;
    }
}

void upsert_port_label(lazarus::BenchProfile& bench, const std::string& identity, const std::string& label) {
    for (auto& port_label : bench.port_labels) {
        if (port_label.identity == identity) {
            port_label.label = label;
            return;
        }
    }
    bench.port_labels.push_back(lazarus::PortLabel{identity, label});
}

void print_profile_paths(const std::string& label, const std::vector<std::string>& paths) {
    std::cout << label << "\n";
    if (paths.empty()) {
        std::cout << "  (none)\n";
        return;
    }
    for (std::size_t i = 0; i < paths.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << paths[i] << "\n";
    }
}

void print_port_labels(const std::vector<lazarus::PortLabel>& labels) {
    std::cout << "PORT LABELS:\n";
    if (labels.empty()) {
        std::cout << "  (none)\n";
        return;
    }
    for (std::size_t i = 0; i < labels.size(); ++i) {
        std::cout << "  " << (i + 1) << ". " << labels[i].label << " -> " << labels[i].identity << "\n";
    }
}

std::string sanitize_path_component(const std::string& value) {
    std::string out;
    for (const char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            out.push_back(ch);
        } else if (ch == '-' || ch == '_') {
            out.push_back(ch);
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            out.push_back('_');
        }
    }
    return out.empty() ? "unknown" : out;
}

std::string timestamp_component() {
    const auto now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d_%H%M");
    return out.str();
}

std::string date_component(const std::string& value) {
    for (std::size_t offset = 0; offset + 10 <= value.size(); ++offset) {
        const auto digit = [&value](std::size_t index) {
            return std::isdigit(static_cast<unsigned char>(value[index])) != 0;
        };
        if (digit(offset) && digit(offset + 1) && digit(offset + 2) && digit(offset + 3) &&
            value[offset + 4] == '-' && digit(offset + 5) && digit(offset + 6) &&
            value[offset + 7] == '-' && digit(offset + 8) && digit(offset + 9)) {
            return value.substr(offset, 10);
        }
    }
    return "";
}

std::string file_date(const std::filesystem::path& path) {
    std::error_code error;
    const auto file_time = std::filesystem::last_write_time(path, error);
    if (error) return "unknown date";
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(system_time);
    std::tm local{};
    localtime_r(&time, &local);
    char date[16]{};
    std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
    return date;
}

std::string build_image_directory(const std::string& storage_root, const lazarus::JobInfo& job) {
    const auto ticket = sanitize_path_component(job.ticket_number);
    const auto customer = sanitize_path_component(job.customer_name);
    return (std::filesystem::path(storage_root) / ticket / customer / timestamp_component()).string();
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return "";
    }
    std::string text;
    std::getline(in, text, '\0');
    return text;
}

std::string extract_json_string(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return "";
    }
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return "";
    }
    const auto first_quote = text.find('"', colon + 1);
    if (first_quote == std::string::npos) {
        return "";
    }
    const auto second_quote = text.find('"', first_quote + 1);
    if (second_quote == std::string::npos) {
        return "";
    }
    return text.substr(first_quote + 1, second_quote - first_quote - 1);
}

struct BackupSummary {
    std::string image_directory;
    std::string ticket_number;
    std::string customer_name;
    std::string technician;
    std::string purpose;
    std::string created_date;
    bool finalized = false;
    bool incomplete = false;
};

bool looks_like_lazarus_image(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error) &&
           std::filesystem::exists(path / "metadata.json", error) &&
           std::filesystem::exists(path / "disk.raw", error) &&
           std::filesystem::exists(path / "hashes.dat", error);
}

BackupSummary read_backup_summary(const std::filesystem::path& path) {
    const auto metadata = read_text_file(path / "metadata.json");
    std::error_code error;
    BackupSummary summary;
    summary.image_directory = path.string();
    summary.ticket_number = extract_json_string(metadata, "ticket_number");
    summary.customer_name = extract_json_string(metadata, "customer_name");
    summary.technician = extract_json_string(metadata, "technician");
    summary.purpose = extract_json_string(metadata, "purpose");
    summary.created_date = date_component(extract_json_string(metadata, "created_at"));
    if (summary.created_date.empty()) summary.created_date = date_component(path.filename().string());
    if (summary.created_date.empty()) summary.created_date = file_date(path / "metadata.json");
    summary.finalized = std::filesystem::exists(path / "FINALIZED", error);
    summary.incomplete = std::filesystem::exists(path / "INCOMPLETE", error);
    return summary;
}

std::vector<BackupSummary> list_backups(const lazarus::BenchProfile& bench) {
    std::vector<BackupSummary> backups;
    for (const auto& storage : bench.image_storage_paths) {
        std::error_code error;
        if (!std::filesystem::exists(storage, error)) {
            continue;
        }
        std::filesystem::recursive_directory_iterator it(storage, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && it != end) {
            const auto path = it->path();
            if (looks_like_lazarus_image(path)) {
                backups.push_back(read_backup_summary(path));
                it.disable_recursion_pending();
            }
            it.increment(error);
        }
    }
    std::sort(backups.begin(), backups.end(), [](const BackupSummary& left, const BackupSummary& right) {
        return left.image_directory < right.image_directory;
    });
    return backups;
}

std::string backup_search_text(const BackupSummary& backup) {
    return backup.image_directory + "\n" + backup.ticket_number + "\n" + backup.customer_name + "\n" + backup.technician + "\n" + backup.purpose;
}

void print_backup_option(std::size_t index, const BackupSummary& backup) {
    std::cout << "  " << (index + 1) << ". "
              << (backup.ticket_number.empty() ? "No ticket" : backup.ticket_number) << " | "
              << (backup.customer_name.empty() ? "Unknown customer" : backup.customer_name) << " | "
              << (backup.created_date.empty() ? "unknown date" : backup.created_date);
    std::cout << "\n     " << backup.image_directory << "\n";
}

std::optional<std::string> choose_backup_image(const lazarus::BenchProfile& bench, const std::string& title) {
    std::cout << title << "\n";
    const auto query = prompt("Search ticket, customer, technician, or Enter to list all: ");
    const auto all_backups = list_backups(bench);
    if (all_backups.empty()) {
        std::cout << "No Lazarus backups were found in configured image storage.\n";
        return std::nullopt;
    }

    std::vector<BackupSummary> matches;
    for (const auto& backup : all_backups) {
        if (contains_case_insensitive(backup_search_text(backup), query)) {
            matches.push_back(backup);
        }
    }
    if (matches.empty()) {
        std::cout << "\nNo matches found. Listing all backups in configured image storage.\n";
        matches = all_backups;
    }

    std::cout << "\nBackups:\n";
    for (std::size_t i = 0; i < matches.size(); ++i) {
        print_backup_option(i, matches[i]);
    }
    const int choice = prompt_int("\nSelect backup number, or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > matches.size()) {
        return std::nullopt;
    }
    return matches[static_cast<std::size_t>(choice - 1)].image_directory;
}

bool save_bench_profile(const lazarus::BenchProfile& bench, const std::string& path, std::string& error) {
    for (const auto& storage : bench.image_storage_paths) {
        if (!ensure_writable_directory(storage, error)) {
            error = "Image storage '" + storage + "' is not writable: " + error;
            return false;
        }
    }

    const auto findings = lazarus::validate_bench_profile(bench);
    if (has_blocker(findings)) {
        error = "Bench profile has blocking safety findings. Use Bench Check for details.";
        return false;
    }

    const std::filesystem::path profile_path(path);
    std::error_code fs_error;
    if (profile_path.has_parent_path()) {
        std::filesystem::create_directories(profile_path.parent_path(), fs_error);
        if (fs_error) {
            error = fs_error.message();
            return false;
        }
    }

    std::ofstream out(profile_path);
    if (!out) {
        error = "Could not open profile for writing.";
        return false;
    }

    out << "# Arcology Lazarus bench profile.\n";
    out << "# Generated by lazarus-tui. Review source and destination roles before real work.\n\n";
    out << "name=" << bench.name << "\n";
    for (const auto& storage : bench.image_storage_paths) {
        out << "image_storage=" << storage << "\n";
    }
    out << "\n";
    for (const auto& source : bench.source_only_paths) {
        out << "source=" << source << "\n";
    }
    for (const auto& destination : bench.destination_only_paths) {
        out << "destination=" << destination << "\n";
    }
    if (!bench.image_storage_device.empty()) {
        out << "image_storage_device=" << bench.image_storage_device << "\n";
    }
    if (!bench.image_storage_volume.empty()) {
        out << "image_storage_volume=" << bench.image_storage_volume << "\n";
    }
    for (const auto& port : bench.image_storage_port_paths) {
        out << "image_storage_port=" << port << "\n";
    }
    if (!bench.nas_storage_protocol.empty()) {
        out << "nas_storage_protocol=" << bench.nas_storage_protocol << "\n";
        out << "nas_storage_server=" << bench.nas_storage_server << "\n";
        out << "nas_storage_share=" << bench.nas_storage_share << "\n";
        if (!bench.nas_storage_username.empty()) out << "nas_storage_username=" << bench.nas_storage_username << "\n";
        if (!bench.nas_storage_domain.empty()) out << "nas_storage_domain=" << bench.nas_storage_domain << "\n";
    }
    for (const auto& removable : bench.removable_media_paths) {
        out << "removable_media=" << removable << "\n";
    }
    for (const auto& ignored : bench.ignored_paths) {
        out << "ignored=" << ignored << "\n";
    }
    for (const auto& port_label : bench.port_labels) {
        out << "port_label=" << port_label.identity << "|" << port_label.label << "\n";
    }
    if (!out) {
        error = "Failed while writing profile.";
        return false;
    }
    return true;
}

std::optional<std::string> choose_image_storage(const lazarus::BenchProfile& bench) {
    if (bench.image_storage_paths.empty()) {
        std::cout << "No image storage directories are configured in this bench profile.\n";
        return std::nullopt;
    }
    std::cout << "Choose image storage:\n";
    for (std::size_t i = 0; i < bench.image_storage_paths.size(); ++i) {
        const auto& path = bench.image_storage_paths[i];
        std::cout << "  " << (i + 1) << ". " << path;
        std::error_code error;
        if (std::filesystem::exists(path, error)) {
            std::cout << " (available)";
        } else {
            std::cout << " (will be created)";
        }
        std::cout << "\n";
    }
    const int choice = prompt_int("\nSelect storage number, or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > bench.image_storage_paths.size()) {
        return std::nullopt;
    }
    return bench.image_storage_paths[static_cast<std::size_t>(choice - 1)];
}

lazarus::CompressionMode choose_compression() {
    std::cout << "\nCompression:\n";
    std::cout << "  1. zstd\n";
    std::cout << "  2. none\n";
    const int choice = prompt_int("\nSelect compression, or Enter for zstd: ");
    return choice == 2 ? lazarus::CompressionMode::None : lazarus::CompressionMode::Zstd;
}

void add_storage_path(lazarus::BenchProfile& bench) {
    const auto path = prompt("Image storage path: ");
    if (path.empty()) {
        return;
    }
    std::string error;
    if (!ensure_writable_directory(path, error)) {
        std::cout << "Image storage was not added: " << error << "\n";
        pause();
        return;
    }
    if (contains_exact(bench.image_storage_paths, path)) {
        std::cout << "Image storage is already configured.\n";
        pause();
        return;
    }
    bench.image_storage_paths.push_back(path);
    if (bench.image_storage_path.empty()) {
        bench.image_storage_path = path;
    }
    std::cout << "Added writable image storage: " << path << "\n";
    pause();
}

void remove_profile_path(std::vector<std::string>& paths, const std::string& label) {
    print_profile_paths(label, paths);
    if (paths.empty()) {
        pause();
        return;
    }
    const int choice = prompt_int("\nRemove which entry, or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > paths.size()) {
        return;
    }
    paths.erase(paths.begin() + (choice - 1));
}

void remove_port_label(std::vector<lazarus::PortLabel>& labels) {
    print_port_labels(labels);
    if (labels.empty()) {
        pause();
        return;
    }
    const int choice = prompt_int("\nRemove which label, or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > labels.size()) {
        return;
    }
    labels.erase(labels.begin() + (choice - 1));
}

void add_device_to_role(lazarus::BenchProfile& bench, std::vector<std::string>& role_paths, const std::string& role_label) {
    const auto devices = load_devices(bench);
    if (devices.empty()) {
        std::cout << "No block devices discovered.\n";
        pause();
        return;
    }

    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        std::cout << "  " << (i + 1) << ". " << describe_device_one_line(device) << "\n";
        std::cout << "     profile identity: " << identity_for_profile(device) << "\n";
    }
    const int choice = prompt_int("\nAdd which device to " + role_label + ", or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > devices.size()) {
        return;
    }

    const auto identity = identity_for_profile(devices[static_cast<std::size_t>(choice - 1)]);
    if (identity.empty()) {
        std::cout << "Selected device has no usable identity.\n";
        pause();
        return;
    }
    if (role_label != "ignored" && devices[static_cast<std::size_t>(choice - 1)].is_system_disk) {
        std::cout << "That is a running system disk. It can be ignored, but it cannot be assigned as a source or destination.\n";
        pause();
        return;
    }
    remove_exact(bench.source_only_paths, identity);
    remove_exact(bench.destination_only_paths, identity);
    remove_exact(bench.removable_media_paths, identity);
    remove_exact(bench.ignored_paths, identity);
    if (bench.image_storage_device == identity) bench.image_storage_device.clear();
    role_paths.push_back(identity);
    std::cout << "Added: " << identity << "\n";
    pause();
}

void configure_detected_device(lazarus::BenchProfile& bench) {
    const auto devices = load_devices(bench);
    if (devices.empty()) {
        std::cout << "No block devices discovered.\n";
        pause();
        return;
    }

    print_header("Configure Port Roles");
    std::cout << "Choose a detected device, then assign exactly one bench role.\n\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        const auto identity = identity_for_profile(device);
        const auto label = lazarus::label_for_device(bench, device);
        std::cout << "  " << (i + 1) << ". " << describe_device_one_line(device);
        if (!label.empty()) {
            std::cout << " | label: " << label;
        }
        std::cout << "\n";
        std::cout << "     identity: " << identity << "\n";
    }

    const int choice = prompt_int("\nConfigure which device, or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > devices.size()) {
        return;
    }
    const auto& device = devices[static_cast<std::size_t>(choice - 1)];
    const auto identity = identity_for_profile(device);
    if (identity.empty()) {
        std::cout << "Selected device has no usable persistent identity.\n";
        pause();
        return;
    }

    std::cout << "\nSelected:\n  " << describe_device_one_line(device) << "\n  " << identity << "\n\n";
    if (device.is_system_disk) {
        std::cout << "This is a running system disk. Source-only and destination-only are blocked.\n";
    }
    std::cout << "  1. Source-only customer drive port\n";
    std::cout << "  2. Destination-only erase/restore port\n";
    std::cout << "  3. Image-storage connection\n";
    std::cout << "  4. Removable-media/export connection\n";
    std::cout << "  5. Ignored/internal/support device\n";
    std::cout << "  6. Unconfigured\n";
    const int role = prompt_int("\nSelect role: ");
    if (role >= 1 && role <= 4 && device.is_system_disk) {
        std::cout << "The running system disk cannot be assigned an operational bench role.\n";
        pause();
        return;
    }
    switch (role) {
        case 1:
            assign_device_role(bench, device, lazarus::DeviceRole::SourceOnly);
            break;
        case 2:
            assign_device_role(bench, device, lazarus::DeviceRole::DestinationOnly);
            break;
        case 3:
            assign_device_role(bench, device, lazarus::DeviceRole::ImageStorage);
            break;
        case 4:
            assign_device_role(bench, device, lazarus::DeviceRole::RemovableMedia);
            break;
        case 5:
            assign_device_role(bench, device, lazarus::DeviceRole::Ignored);
            break;
        case 6:
            assign_device_role(bench, device, lazarus::DeviceRole::Unknown);
            break;
        default:
            return;
    }

    if (prompt_yes_no("Add or update a friendly port label now")) {
        const auto label = prompt("Friendly label, e.g. Left USB3: ");
        if (!label.empty()) {
            upsert_port_label(bench, identity, label);
        }
    }
    std::cout << "Configuration updated. Save the profile before leaving the editor.\n";
    pause();
}

void add_port_label(lazarus::BenchProfile& bench) {
    const auto devices = load_devices(bench);
    if (devices.empty()) {
        std::cout << "No block devices discovered.\n";
        pause();
        return;
    }

    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        const auto label = lazarus::label_for_device(bench, device);
        std::cout << "  " << (i + 1) << ". " << describe_device_one_line(device);
        if (!label.empty()) {
            std::cout << " | label: " << label;
        }
        std::cout << "\n";
        std::cout << "     profile identity: " << identity_for_profile(device) << "\n";
    }
    const int choice = prompt_int("\nLabel which device, or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > devices.size()) {
        return;
    }

    const auto identity = identity_for_profile(devices[static_cast<std::size_t>(choice - 1)]);
    const auto label = prompt("Label, e.g. Left USB3: ");
    if (identity.empty() || label.empty()) {
        return;
    }
    bench.port_labels.push_back(lazarus::PortLabel{identity, label});
    std::cout << "Added label: " << label << "\n";
    pause();
}

lazarus::ProgressCallback make_progress_printer() {
    return [](const lazarus::ProgressEvent& event) {
        std::cout << "\r\033[K";
        std::cout << std::left << std::setw(12) << ("[" + event.operation + "]")
                  << std::setw(18) << event.phase;
        if (event.indeterminate || event.bytes_total == 0) {
            std::cout << event.message << std::flush;
            return;
        }

        const auto percent = (static_cast<double>(event.bytes_done) / static_cast<double>(event.bytes_total)) * 100.0;
        std::cout << std::right << std::setw(6) << std::fixed << std::setprecision(1) << percent << "%  "
                  << human_size(event.bytes_done) << " / " << human_size(event.bytes_total);
        if (event.bytes_per_second != 0) {
            std::cout << "  " << human_size(event.bytes_per_second) << "/s";
        }
        if (event.eta_seconds != 0) {
            std::cout << "  ETA " << human_duration(event.eta_seconds);
        }
        if (event.chunks_total != 0) {
            std::cout << "  chunks " << event.chunks_done << "/" << event.chunks_total;
        }
        if (event.phase == "complete" || event.phase == "failed") {
            std::cout << "\n";
        }
        std::cout << std::flush;
    };
}

std::vector<lazarus::DeviceIdentity> load_devices(const lazarus::BenchProfile& bench) {
    return lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
}

std::optional<lazarus::DeviceIdentity> choose_device(const lazarus::BenchProfile& bench, const std::string& title) {
    const auto devices = load_devices(bench);
    if (devices.empty()) {
        std::cout << "No block devices discovered.\n";
        return std::nullopt;
    }

    std::cout << title << "\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& device = devices[i];
        const auto label = lazarus::label_for_device(bench, device);
        std::cout << "  " << (i + 1) << ". " << device.linux_path
                  << " | " << lazarus::to_string(device.bench_role)
                  << " | " << human_size(device.size_bytes)
                  << " | " << device.model;
        if (!label.empty()) {
            std::cout << " | " << label;
        }
        if (!device.serial_ending.empty()) {
            std::cout << " | serial ending " << device.serial_ending;
        }
        std::cout << "\n";
        std::cout << "     " << device.physical_path << "\n";
    }

    const int choice = prompt_int("\nSelect device number, or 0 to cancel: ");
    if (choice <= 0 || static_cast<std::size_t>(choice) > devices.size()) {
        return std::nullopt;
    }
    return devices[static_cast<std::size_t>(choice - 1)];
}

void show_devices(const lazarus::BenchProfile& bench) {
    print_header("Devices");
    const auto devices = load_devices(bench);
    if (devices.empty()) {
        std::cout << "No block devices discovered.\n";
        pause();
        return;
    }

    for (const auto& device : devices) {
        std::cout << device.linux_path << "\n";
        const auto label = lazarus::label_for_device(bench, device);
        if (!label.empty()) {
            std::cout << "  label: " << label << "\n";
        }
        std::cout << "  role: " << lazarus::to_string(device.bench_role) << "\n";
        std::cout << "  model: " << device.model << "\n";
        std::cout << "  size: " << human_size(device.size_bytes) << "\n";
        std::cout << "  system disk: " << (device.is_system_disk ? "yes" : "no") << "\n";
        std::cout << "  removable: " << (device.removable ? "yes" : "no") << "\n";
        std::cout << "  physical path: " << device.physical_path << "\n";
        if (!device.by_id_path.empty()) {
            std::cout << "  by-id: " << device.by_id_path << "\n";
        }
        if (!device.by_path.empty()) {
            std::cout << "  by-path: " << device.by_path << "\n";
        }
        std::cout << "\n";
    }
    pause();
}

void inspect_source(const lazarus::BenchProfile& bench) {
    print_header("Inspect Source");
    auto selected = choose_device(bench, "Choose a source-only customer drive.");
    if (!selected) {
        return;
    }

    auto open_result = lazarus::open_source_read_only(bench, *selected);
    if (!open_result.findings.empty()) {
        std::cout << "\nSource findings:\n";
        print_findings(open_result.findings);
    }
    if (!open_result.handle.is_open()) {
        pause();
        return;
    }

    const auto inspection = lazarus::inspect_source_disk(open_result.handle);
    std::cout << "\nInspection facts:\n";
    for (const auto& fact : inspection.facts) {
        std::cout << "  " << fact << "\n";
    }
    std::cout << "\nPartitions:\n";
    for (const auto& partition : inspection.partitions) {
        std::cout << "  #" << partition.number << " " << lazarus::to_string(partition.kind)
                  << " " << human_size(partition.size_bytes);
        if (partition.filesystem != lazarus::FileSystemKind::Unknown) {
            std::cout << " " << lazarus::to_string(partition.filesystem);
        }
        std::cout << "\n";
    }
    if (!inspection.findings.empty()) {
        std::cout << "\nInspection findings:\n";
        print_findings(inspection.findings);
    }
    pause();
}

void create_backup(const lazarus::BenchProfile& bench) {
    print_header("Create Backup");
    lazarus::JobInfo job;
    job.ticket_number = prompt("Ticket number: ");
    job.customer_name = prompt("Customer name: ");
    job.technician = prompt("Technician: ");
    job.purpose = prompt("Purpose: ");
    const auto storage = choose_image_storage(bench);
    if (!storage) {
        pause();
        return;
    }
    std::string storage_error;
    if (!ensure_writable_directory(*storage, storage_error)) {
        std::cout << "\nSelected image storage is not writable: " << storage_error << "\n";
        pause();
        return;
    }
    const auto output_dir = build_image_directory(*storage, job);
    std::cout << "\nImage directory:\n  " << output_dir << "\n";

    auto selected = choose_device(bench, "\nChoose a source-only customer drive.");
    if (!selected) {
        return;
    }

    auto open_result = lazarus::open_source_read_only(bench, *selected);
    if (!open_result.findings.empty()) {
        std::cout << "\nSource findings:\n";
        print_findings(open_result.findings);
    }
    if (!open_result.handle.is_open()) {
        pause();
        return;
    }

    const auto inspection = lazarus::inspect_source_disk(open_result.handle);
    if (!has_imageable_layout(inspection) || has_blocker(inspection.findings)) {
        std::cout << "\nThe source layout requires specialist escalation. Lazarus will preserve it as a raw image first.\n";
        if (!inspection.facts.empty()) {
            std::cout << "\nFacts:\n";
            for (const auto& fact : inspection.facts) {
                std::cout << "  " << fact << "\n";
            }
        }
        print_findings(inspection.findings);
    }

    lazarus::ImageWriteOptions options;
    options.output_directory = output_dir;
    options.compression = choose_compression();
    options.progress = make_progress_printer();
    std::cout << "\nStarting image. Progress updates will appear below.\n";
    const auto result = lazarus::write_directory_image(job, open_result.handle, inspection, options);
    std::cout << "\n\nImage output: " << result.output_directory << "\n";
    std::cout << "Bytes written: " << result.bytes_written << "\n";
    std::cout << "Bytes stored: " << result.bytes_stored << "\n";
    std::cout << "Zero-filled bytes skipped: " << result.zero_bytes_elided << "\n";
    std::cout << "Compression: " << lazarus::to_string(options.compression) << "\n";
    std::cout << "Chunks written: " << result.chunks_written << "\n";
    std::cout << "Finalized: " << (result.finalized ? "yes" : "no") << "\n";
    if (!result.findings.empty()) {
        std::cout << "\nImage findings:\n";
        print_findings(result.findings);
    }
    pause();
}

void verify_image(const lazarus::BenchProfile& bench) {
    print_header("Verify Backup");
    const auto image_dir = choose_backup_image(bench, "Choose backup to verify.");
    if (!image_dir) {
        pause();
        return;
    }
    std::cout << "\nStarting verification.\n";
    const auto result = lazarus::verify_directory_image(*image_dir, make_progress_printer());
    std::cout << "\nImage: " << result.image_directory << "\n";
    std::cout << "Verified: " << (result.verified ? "yes" : "no") << "\n";
    std::cout << "Expected bytes: " << result.expected_bytes << "\n";
    std::cout << "Actual bytes: " << result.actual_bytes << "\n";
    std::cout << "Stored bytes: " << result.stored_bytes << "\n";
    std::cout << "Chunks verified: " << result.chunks_verified << "\n";
    if (!result.findings.empty()) {
        std::cout << "\nVerification findings:\n";
        print_findings(result.findings);
    }
    pause();
}

void restore_backup(const lazarus::BenchProfile& bench) {
    print_header("Restore Backup");
    const auto image_dir = choose_backup_image(bench, "Choose backup to restore.");
    if (!image_dir) {
        pause();
        return;
    }
    auto selected = choose_device(bench, "\nChoose a destination-only drive. This drive will be erased.");
    if (!selected) {
        return;
    }

    std::cout << "\nThe following drive will be erased:\n";
    std::cout << "  " << selected->linux_path << "\n";
    const auto selected_label = lazarus::label_for_device(bench, *selected);
    if (!selected_label.empty()) {
        std::cout << "  label: " << selected_label << "\n";
    }
    std::cout << "  " << selected->model << "\n";
    std::cout << "  " << human_size(selected->size_bytes) << "\n";
    std::cout << "  role: " << lazarus::to_string(selected->bench_role) << "\n";
    std::cout << "  physical path: " << selected->physical_path << "\n";
    const auto confirmation = prompt("\nType ERASE to continue: ");

    lazarus::ImageRestoreOptions options;
    options.image_directory = *image_dir;
    options.confirmation = confirmation;
    options.progress = make_progress_printer();
    std::cout << "\nStarting restore. Progress updates will appear below.\n";
    const auto result = lazarus::restore_directory_image(bench, *selected, options);
    std::cout << "\nImage: " << result.image_directory << "\n";
    std::cout << "Bytes written: " << result.bytes_written << "\n";
    std::cout << "Chunks written: " << result.chunks_written << "\n";
    std::cout << "Restored: " << (result.restored ? "yes" : "no") << "\n";
    if (!result.findings.empty()) {
        std::cout << "\nRestore findings:\n";
        print_findings(result.findings);
    }
    pause();
}

void list_backup_images(const lazarus::BenchProfile& bench) {
    print_header("Backups");
    const auto image_dir = choose_backup_image(bench, "Search or list configured backups.");
    if (image_dir) {
        std::cout << "\nSelected:\n  " << *image_dir << "\n";
    }
    pause();
}

void bench_check(const lazarus::BenchProfile& bench) {
    print_header("Bench Profile");
    std::cout << "Name: " << (bench.name.empty() ? "(unnamed)" : bench.name) << "\n";
    print_profile_paths("IMAGE STORAGE:", bench.image_storage_paths);
    std::cout << "\n";
    print_port_labels(bench.port_labels);
    std::cout << "\n";
    const auto findings = lazarus::validate_bench_profile(bench);
    if (findings.empty()) {
        std::cout << "No bench profile findings.\n";
    } else {
        print_findings(findings);
    }
    pause();
}

void edit_bench_profile(lazarus::BenchProfile& bench, const std::string& bench_path) {
    while (true) {
        print_header("Edit Bench Profile");
        std::cout << "Profile path: " << bench_path << "\n";
        std::cout << "Name: " << (bench.name.empty() ? "(unnamed)" : bench.name) << "\n";
        print_profile_paths("IMAGE STORAGE:", bench.image_storage_paths);
        print_profile_paths("\nSOURCE ONLY:", bench.source_only_paths);
        print_profile_paths("\nDESTINATION ONLY:", bench.destination_only_paths);
        print_profile_paths("\nIGNORED:", bench.ignored_paths);
        std::cout << "\n";
        print_port_labels(bench.port_labels);
        std::cout << "\n";
        std::cout << "  1. Set name\n";
        std::cout << "  2. Add image storage\n";
        std::cout << "  3. Remove image storage\n";
        std::cout << "  4. Configure detected device role/label\n";
        std::cout << "  5. Add source-only device\n";
        std::cout << "  6. Add destination-only device\n";
        std::cout << "  7. Add ignored device\n";
        std::cout << "  8. Remove source-only entry\n";
        std::cout << "  9. Remove destination-only entry\n";
        std::cout << "  10. Remove ignored entry\n";
        std::cout << "  11. Add port label\n";
        std::cout << "  12. Remove port label\n";
        std::cout << "  13. Save profile\n";
        std::cout << "  0. Back\n\n";

        switch (prompt_int("Select editor action: ")) {
            case 1:
                bench.name = prompt_default("Bench name", bench.name);
                break;
            case 2:
                add_storage_path(bench);
                break;
            case 3:
                remove_profile_path(bench.image_storage_paths, "IMAGE STORAGE:");
                bench.image_storage_path = bench.image_storage_paths.empty() ? "" : bench.image_storage_paths.front();
                break;
            case 4:
                configure_detected_device(bench);
                break;
            case 5:
                add_device_to_role(bench, bench.source_only_paths, "source-only");
                break;
            case 6:
                add_device_to_role(bench, bench.destination_only_paths, "destination-only");
                break;
            case 7:
                add_device_to_role(bench, bench.ignored_paths, "ignored");
                break;
            case 8:
                remove_profile_path(bench.source_only_paths, "SOURCE ONLY:");
                break;
            case 9:
                remove_profile_path(bench.destination_only_paths, "DESTINATION ONLY:");
                break;
            case 10:
                remove_profile_path(bench.ignored_paths, "IGNORED:");
                break;
            case 11:
                add_port_label(bench);
                break;
            case 12:
                remove_port_label(bench.port_labels);
                break;
            case 13: {
                std::string error;
                if (save_bench_profile(bench, bench_path, error)) {
                    std::cout << "Saved profile: " << bench_path << "\n";
                } else {
                    std::cout << "Profile was not saved: " << error << "\n";
                }
                pause();
                break;
            }
            case 0:
                return;
            default:
                break;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string bench_path = argc >= 2 ? argv[1] : "lazarus/examples/bench-alpha.profile";
    auto bench = lazarus::load_bench_profile(bench_path);

    while (true) {
        print_header("Main Menu");
        std::cout << "Bench profile: " << bench_path << "\n";
        std::cout << "Bench name: " << (bench.name.empty() ? "(unnamed)" : bench.name) << "\n\n";
        std::cout << "  1. Create Backup\n";
        std::cout << "  2. Restore Backup\n";
        std::cout << "  3. Verify Backup\n";
        std::cout << "  4. Inspect Source\n";
        std::cout << "  5. Show Devices\n";
        std::cout << "  6. List Backups\n";
        std::cout << "  7. Bench Check\n";
        std::cout << "  8. Edit Bench Profile\n";
        std::cout << "  0. Exit\n\n";

        switch (prompt_int("Select workflow: ")) {
            case 1:
                create_backup(bench);
                break;
            case 2:
                restore_backup(bench);
                break;
            case 3:
                verify_image(bench);
                break;
            case 4:
                inspect_source(bench);
                break;
            case 5:
                show_devices(bench);
                break;
            case 6:
                list_backup_images(bench);
                break;
            case 7:
                bench_check(bench);
                break;
            case 8:
                edit_bench_profile(bench, bench_path);
                bench = lazarus::load_bench_profile(bench_path);
                break;
            case 0:
                return 0;
            default:
                break;
        }
    }
}
