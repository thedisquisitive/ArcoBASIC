#include "lazarum/viewer.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void usage() {
    std::cout <<
        "Lazarum - cross-platform Lazarus Drive Viewer\n\n"
        "Usage:\n"
        "  lazarum capabilities\n"
        "  lazarum mount [DEVICE]\n"
        "  lazarum scan STORAGE_ROOT\n"
        "  lazarum reports IMAGE_DIRECTORY\n"
        "  lazarum show-report IMAGE_DIRECTORY REPORT_NAME\n"
        "  lazarum extract-report IMAGE_DIRECTORY REPORT_NAME DESTINATION\n"
        "  lazarum data-status IMAGE_DIRECTORY\n"
        "  lazarum volumes IMAGE_DIRECTORY\n"
        "  lazarum files IMAGE_DIRECTORY VOLUME_INDEX [RELATIVE_PATH]\n"
        "  lazarum extract-files IMAGE_DIRECTORY VOLUME_INDEX DESTINATION RELATIVE_PATH...\n";
}

std::string human_bytes(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream output;
    output << std::fixed << std::setprecision(unit == 0 ? 0 : 1) << value << ' ' << units[unit];
    return output.str();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string command = argv[1];
    if (command == "capabilities") {
        const auto capability = lazarum::mount_capability();
        std::cout << "Platform: " << lazarum::to_string(capability.platform) << "\n"
                  << "Read-only ext4 mount: " << (capability.native_read_only_mount ? "available" : "not implemented") << "\n"
                  << "Mount provider: " << capability.detail << "\n"
                  << "Reports: view and safe extract available\n"
                  << "Image filesystem explorer: on-demand verified read-only Linux provider available\n";
        return 0;
    }
    if (command == "mount") {
        std::optional<fs::path> device;
        if (argc == 3) device = fs::path(argv[2]);
        else device = lazarum::discover_lazarus_storage_device();
        if (!device) {
            std::cerr << "No LAZARUS_STORAGE device was found. Pass its /dev path explicitly.\n";
            return 1;
        }
        const auto result = lazarum::mount_ext4_read_only(*device);
        if (!result.mounted) {
            std::cerr << result.error << "\n";
            return 1;
        }
        std::cout << "Mounted read-only";
        if (!result.mount_point.empty()) std::cout << " at " << result.mount_point.string();
        std::cout << "\n";
        return 0;
    }
    if (command == "scan" && argc == 3) {
        const auto scan = lazarum::scan_storage(argv[2]);
        for (const auto& warning : scan.warnings) std::cerr << "Warning: " << warning << "\n";
        for (const auto& image : scan.images) {
            std::cout << (image.finalized && !image.incomplete ? "READY" : "INCOMPLETE")
                      << "\t" << image.ticket_number << "\t" << image.customer_name
                      << "\t" << image.created_at << "\t" << human_bytes(image.logical_bytes)
                      << "\t" << image.directory.string() << "\n";
        }
        return scan.warnings.empty() ? 0 : 1;
    }
    if (command == "reports" && argc == 3) {
        for (const auto& report : lazarum::list_reports(argv[2])) {
            std::cout << report.name << "\t" << human_bytes(report.size_bytes) << "\n";
        }
        return 0;
    }
    if (command == "show-report" && argc == 4) {
        std::string error;
        const auto contents = lazarum::read_report(argv[2], argv[3], error);
        if (!error.empty()) {
            std::cerr << error << "\n";
            return 1;
        }
        std::cout << contents;
        return 0;
    }
    if (command == "extract-report" && argc == 5) {
        fs::path written;
        std::string error;
        if (!lazarum::extract_report(argv[2], argv[3], argv[4], written, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        std::cout << "Extracted without modifying the image: " << written.string() << "\n";
        return 0;
    }
    if (command == "data-status" && argc == 3) {
        const auto status = lazarum::image_data_capability(argv[2]);
        std::cout << status.detail << "\n";
        return status.image_recognized ? 0 : 1;
    }
    if ((command == "volumes" && argc == 3) ||
        (command == "files" && (argc == 4 || argc == 5)) ||
        (command == "extract-files" && argc >= 6)) {
        auto provider = lazarum::make_image_data_provider();
        provider->set_progress_callback([](std::string message) {
            std::cerr << message << "\n";
        });
        auto volumes = provider->list_volumes(argv[2]);
        if (!volumes.completed) {
            std::cerr << volumes.error << "\n";
            return 1;
        }
        if (command == "volumes") {
            for (std::size_t index = 0; index < volumes.volumes.size(); ++index) {
                const auto& volume = volumes.volumes[index];
                std::cout << index << "\t" << volume.label << "\t" << volume.filesystem
                          << "\t" << human_bytes(volume.size_bytes) << "\n";
            }
            return 0;
        }
        std::size_t volume_index = 0;
        try {
            std::size_t consumed = 0;
            volume_index = std::stoull(argv[3], &consumed);
            if (consumed != std::string(argv[3]).size()) throw std::invalid_argument("suffix");
        } catch (...) {
            std::cerr << "VOLUME_INDEX must be a non-negative integer from 'lazarum volumes'.\n";
            return 2;
        }
        if (volume_index >= volumes.volumes.size()) {
            std::cerr << "VOLUME_INDEX is outside the available volume list.\n";
            return 1;
        }
        const auto& volume = volumes.volumes[volume_index];
        if (command == "files") {
            const std::string relative = argc == 5 ? argv[4] : "";
            const auto listing = provider->list_directory(argv[2], volume.id, relative);
            if (!listing.completed) {
                std::cerr << listing.error << "\n";
                return 1;
            }
            for (const auto& entry : listing.entries) {
                std::cout << entry.type << "\t" << entry.size_bytes << "\t" << entry.relative_path << "\n";
            }
            return 0;
        }
        std::vector<std::string> paths;
        for (int index = 5; index < argc; ++index) paths.emplace_back(argv[index]);
        const auto extraction = provider->extract(argv[2], volume.id, paths, argv[4]);
        if (!extraction.completed) {
            std::cerr << extraction.error << "\n";
            return 1;
        }
        for (const auto& path : extraction.extracted_paths) std::cout << path.string() << "\n";
        return 0;
    }
    usage();
    return 2;
}
