#include "lazarus/core.hpp"

#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <cstdlib>

namespace {

void print_help() {
    std::cout << "Arcology Lazarus\n"
              << "\n"
              << "Usage:\n"
              << "  lazarus version\n"
              << "  lazarus devices\n"
              << "  lazarus bench-check PATH\n"
              << "  lazarus smart BENCH_PROFILE DEVICE\n"
              << "  lazarus inspect-source BENCH_PROFILE DEVICE\n"
              << "  lazarus image-source BENCH_PROFILE DEVICE OUTDIR TICKET CUSTOMER TECH PURPOSE\n"
              << "  lazarus verify-image IMAGE_DIR\n"
              << "  lazarus restore-image BENCH_PROFILE IMAGE_DIR DEVICE ERASE\n"
              << "  lazarus plan-demo\n"
              << "\n"
              << "Device discovery reads Linux sysfs and persistent /dev/disk links.\n";
}

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

lazarus::ProgressCallback make_progress_printer() {
    return [](const lazarus::ProgressEvent& event) {
        std::cerr << "[" << event.operation << "] " << event.phase;
        if (!event.message.empty()) {
            std::cerr << " - " << event.message;
        }
        if (event.indeterminate) {
            std::cerr << "\n";
            return;
        }
        if (event.bytes_total != 0) {
            const auto percent = (static_cast<double>(event.bytes_done) / static_cast<double>(event.bytes_total)) * 100.0;
            std::cerr << " " << human_size(event.bytes_done) << " / " << human_size(event.bytes_total)
                      << " (" << std::fixed << std::setprecision(1) << percent << "%)";
            if (event.bytes_per_second != 0) {
                std::cerr << " " << human_size(event.bytes_per_second) << "/s";
            }
            if (event.eta_seconds != 0) {
                std::cerr << " ETA " << human_duration(event.eta_seconds);
            }
        }
        if (event.chunks_total != 0) {
            std::cerr << " chunks " << event.chunks_done << "/" << event.chunks_total;
        }
        std::cerr << "\n";
    };
}

lazarus::CompressionMode compression_from_environment() {
    const char* value = std::getenv("LAZARUS_COMPRESSION");
    if (value != nullptr && std::string(value) == "zstd") {
        return lazarus::CompressionMode::Zstd;
    }
    return lazarus::CompressionMode::None;
}

int list_devices() {
    const auto devices = lazarus::discover_block_devices();
    if (devices.empty()) {
        std::cout << "No block devices discovered.\n";
        return 0;
    }

    for (const auto& device : devices) {
        std::cout << device.linux_path << "\n";
        std::cout << "  model: " << device.model << "\n";
        std::cout << "  size: " << human_size(device.size_bytes) << "\n";
        std::cout << "  role: " << lazarus::to_string(device.bench_role) << "\n";
        std::cout << "  system disk: " << (device.is_system_disk ? "yes" : "no") << "\n";
        std::cout << "  removable: " << (device.removable ? "yes" : "no") << "\n";
        std::cout << "  rotational: " << (device.rotational ? "yes" : "no") << "\n";
        if (!device.transport.empty()) {
            std::cout << "  transport: " << device.transport << "\n";
        }
        if (!device.serial_ending.empty()) {
            std::cout << "  serial ending: " << device.serial_ending << "\n";
        }
        std::cout << "  physical path: " << device.physical_path << "\n";
        if (!device.by_id_path.empty()) {
            std::cout << "  by-id: " << device.by_id_path << "\n";
        }
        if (!device.by_path.empty()) {
            std::cout << "  by-path: " << device.by_path << "\n";
        }
        if (!device.partitions.empty()) {
            std::cout << "  partitions:";
            for (const auto& partition : device.partitions) {
                std::cout << " " << partition;
            }
            std::cout << "\n";
        }
    }

    return 0;
}

void print_findings(const std::vector<lazarus::SafetyFinding>& findings) {
    for (const auto& finding : findings) {
        std::cout << "  [" << lazarus::to_string(finding.severity) << "] "
                  << finding.code << ": " << finding.observed << " "
                  << finding.action << "\n";
    }
}

int bench_check(const std::string& path) {
    const auto bench = lazarus::load_bench_profile(path);
    std::cout << "Bench: " << (bench.name.empty() ? "(unnamed)" : bench.name) << "\n";
    std::cout << "Image storage: " << (bench.image_storage_path.empty() ? "(unset)" : bench.image_storage_path) << "\n";
    if (!bench.port_labels.empty()) {
        std::cout << "Port labels:\n";
        for (const auto& port_label : bench.port_labels) {
            std::cout << "  " << port_label.label << " -> " << port_label.identity << "\n";
        }
    }

    const auto findings = lazarus::validate_bench_profile(bench);
    if (!findings.empty()) {
        std::cout << "Bench findings:\n";
        print_findings(findings);
    }

    const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
    std::cout << "Devices:\n";
    for (const auto& device : devices) {
        std::cout << "  " << device.linux_path << " -> " << lazarus::to_string(device.bench_role)
                  << " (" << device.model << ", " << human_size(device.size_bytes) << ")\n";
        const auto label = lazarus::label_for_device(bench, device);
        if (!label.empty()) {
            std::cout << "    label: " << label << "\n";
        }
        std::cout << "    physical path: " << device.physical_path << "\n";
    }
    return 0;
}

bool matches_device_selector(const lazarus::DeviceIdentity& device, const std::string& selector) {
    return device.linux_path == selector || device.physical_path == selector || device.by_id_path == selector || device.by_path == selector;
}

bool has_imageable_layout(const lazarus::DiskInspection& inspection) {
    return inspection.gpt_header_valid || inspection.mbr_detected || !inspection.partitions.empty();
}

std::vector<lazarus::DeviceIdentity> bench_devices(const std::string& bench_path) {
    const auto bench = lazarus::load_bench_profile(bench_path);
    return lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
}

std::optional<lazarus::DeviceIdentity> find_device(const std::string& bench_path, const std::string& selector) {
    const auto devices = bench_devices(bench_path);
    for (const auto& device : devices) {
        if (matches_device_selector(device, selector)) {
            return device;
        }
    }
    return std::nullopt;
}

int smart_diagnostics(const std::string& bench_path, const std::string& selector) {
    const auto selected = find_device(bench_path, selector);
    if (!selected) {
        std::cerr << "No discovered device matched: " << selector << "\n";
        return 1;
    }

    const auto smart = lazarus::collect_smart_diagnostics(*selected);
    std::cout << "SMART: " << selected->linux_path << "\n";
    std::cout << "  model: " << (smart.model.empty() ? selected->model : smart.model) << "\n";
    std::cout << "  health: " << smart.health << "\n";
    std::cout << "  smartctl available: " << (smart.smartctl_available ? "yes" : "no") << "\n";
    std::cout << "  command completed: " << (smart.command_completed ? "yes" : "no") << "\n";
    if (smart.power_on_hours.present) {
        std::cout << "  power-on hours: " << smart.power_on_hours.value << "\n";
    }
    if (smart.temperature_celsius.present) {
        std::cout << "  temperature: " << smart.temperature_celsius.value << " C\n";
    }
    if (smart.reallocated_sectors.present) {
        std::cout << "  reallocated sectors: " << smart.reallocated_sectors.value << "\n";
    }
    if (smart.pending_sectors.present) {
        std::cout << "  pending sectors: " << smart.pending_sectors.value << "\n";
    }
    if (smart.uncorrectable_errors.present) {
        std::cout << "  uncorrectable errors: " << smart.uncorrectable_errors.value << "\n";
    }
    if (!smart.facts.empty()) {
        std::cout << "Facts:\n";
        for (const auto& fact : smart.facts) {
            std::cout << "  " << fact << "\n";
        }
    }
    if (!smart.findings.empty()) {
        std::cout << "SMART findings:\n";
        print_findings(smart.findings);
    }
    return smart.health == "failed" ? 2 : 0;
}

int inspect_source(const std::string& bench_path, const std::string& selector) {
    const auto bench = lazarus::load_bench_profile(bench_path);
    const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
    auto selected = devices.end();
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        if (matches_device_selector(*it, selector)) {
            selected = it;
            break;
        }
    }

    if (selected == devices.end()) {
        std::cerr << "No discovered device matched: " << selector << "\n";
        return 1;
    }

    auto open_result = lazarus::open_source_read_only(bench, *selected);
    if (!open_result.findings.empty()) {
        std::cout << "Source findings:\n";
        print_findings(open_result.findings);
    }
    if (!open_result.handle.is_open()) {
        return 2;
    }

    const auto inspection = lazarus::inspect_source_disk(open_result.handle);
    std::cout << "Inspection: " << inspection.source.linux_path << "\n";
    std::cout << "  first sector read: " << (inspection.first_sector_read ? "yes" : "no") << "\n";
    std::cout << "  MBR signature valid: " << (inspection.mbr_signature_valid ? "yes" : "no") << "\n";
    std::cout << "  MBR detected: " << (inspection.mbr_detected ? "yes" : "no") << "\n";
    std::cout << "  protective MBR: " << (inspection.protective_mbr ? "yes" : "no") << "\n";
    std::cout << "  GPT detected: " << (inspection.gpt_detected ? "yes" : "no") << "\n";
    std::cout << "  GPT header valid: " << (inspection.gpt_header_valid ? "yes" : "no") << "\n";
    if (!inspection.facts.empty()) {
        std::cout << "Facts:\n";
        for (const auto& fact : inspection.facts) {
            std::cout << "  " << fact << "\n";
        }
    }
    if (!inspection.partitions.empty()) {
        std::cout << "Partitions:\n";
        for (const auto& partition : inspection.partitions) {
            std::cout << "  #" << partition.number << " " << lazarus::to_string(partition.kind)
                      << " " << human_size(partition.size_bytes);
            if (!partition.name.empty()) {
                std::cout << " \"" << partition.name << "\"";
            }
            if (partition.filesystem != lazarus::FileSystemKind::Unknown) {
                std::cout << " " << lazarus::to_string(partition.filesystem);
            }
            std::cout << "\n";
            std::cout << "     LBA " << partition.first_lba << "-" << partition.last_lba << "\n";
            std::cout << "     type " << partition.type_guid << "\n";
        }
    }
    if (!inspection.findings.empty()) {
        std::cout << "Inspection findings:\n";
        print_findings(inspection.findings);
    }

    return has_imageable_layout(inspection) ? 0 : 3;
}

std::string join_args(int argc, char** argv, int start) {
    std::string joined;
    for (int i = start; i < argc; ++i) {
        if (!joined.empty()) {
            joined += " ";
        }
        joined += argv[i];
    }
    return joined;
}

int image_source(int argc, char** argv) {
    if (argc < 9) {
        std::cerr << "image-source requires BENCH_PROFILE DEVICE OUTDIR TICKET CUSTOMER TECH PURPOSE.\n";
        return 1;
    }

    const std::string bench_path = argv[2];
    const std::string selector = argv[3];
    const std::string output_dir = argv[4];
    const lazarus::JobInfo job{
        argv[5],
        argv[6],
        argv[7],
        join_args(argc, argv, 8),
    };

    const auto bench = lazarus::load_bench_profile(bench_path);
    const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
    auto selected = devices.end();
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        if (matches_device_selector(*it, selector)) {
            selected = it;
            break;
        }
    }

    if (selected == devices.end()) {
        std::cerr << "No discovered device matched: " << selector << "\n";
        return 1;
    }

    auto open_result = lazarus::open_source_read_only(bench, *selected);
    if (!open_result.findings.empty()) {
        std::cout << "Source findings:\n";
        print_findings(open_result.findings);
    }
    if (!open_result.handle.is_open()) {
        return 2;
    }

    const auto inspection = lazarus::inspect_source_disk(open_result.handle);
    if (!inspection.findings.empty()) {
        std::cout << "Inspection findings:\n";
        print_findings(inspection.findings);
    }
    lazarus::ImageWriteOptions options{
        output_dir,
        4 * 1024 * 1024,
        0,
    };
    options.compression = compression_from_environment();
    options.progress = make_progress_printer();
    const auto result = lazarus::write_directory_image(job, open_result.handle, inspection, options);
    std::cout << "Image output: " << result.output_directory << "\n";
    std::cout << "Bytes written: " << result.bytes_written << "\n";
    std::cout << "Bytes stored: " << result.bytes_stored << "\n";
    std::cout << "Zero-filled bytes skipped: " << result.zero_bytes_elided << "\n";
    std::cout << "Compression: " << lazarus::to_string(options.compression) << "\n";
    std::cout << "Chunks written: " << result.chunks_written << "\n";
    std::cout << "Finalized: " << (result.finalized ? "yes" : "no") << "\n";
    if (!result.facts.empty()) {
        std::cout << "Facts:\n";
        for (const auto& fact : result.facts) {
            std::cout << "  " << fact << "\n";
        }
    }
    if (!result.findings.empty()) {
        std::cout << "Image findings:\n";
        print_findings(result.findings);
    }

    return result.finalized ? 0 : 4;
}

int verify_image(const std::string& image_dir) {
    const auto result = lazarus::verify_directory_image(image_dir, make_progress_printer());
    std::cout << "Image: " << result.image_directory << "\n";
    std::cout << "Verified: " << (result.verified ? "yes" : "no") << "\n";
    std::cout << "Expected bytes: " << result.expected_bytes << "\n";
    std::cout << "Actual bytes: " << result.actual_bytes << "\n";
    std::cout << "Stored bytes: " << result.stored_bytes << "\n";
    std::cout << "Chunks verified: " << result.chunks_verified << "\n";
    if (!result.facts.empty()) {
        std::cout << "Facts:\n";
        for (const auto& fact : result.facts) {
            std::cout << "  " << fact << "\n";
        }
    }
    if (!result.findings.empty()) {
        std::cout << "Verification findings:\n";
        print_findings(result.findings);
    }
    return result.verified ? 0 : 2;
}

int restore_image(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "restore-image requires BENCH_PROFILE IMAGE_DIR DEVICE ERASE.\n";
        return 1;
    }

    const std::string bench_path = argv[2];
    const std::string image_dir = argv[3];
    const std::string selector = argv[4];
    const std::string confirmation = argv[5];

    const auto bench = lazarus::load_bench_profile(bench_path);
    const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
    auto selected = devices.end();
    for (auto it = devices.begin(); it != devices.end(); ++it) {
        if (matches_device_selector(*it, selector)) {
            selected = it;
            break;
        }
    }

    if (selected == devices.end()) {
        std::cerr << "No discovered device matched: " << selector << "\n";
        return 1;
    }

    std::cout << "RESTORE DESTINATION\n";
    std::cout << "  device: " << selected->linux_path << "\n";
    std::cout << "  model: " << selected->model << "\n";
    std::cout << "  size: " << human_size(selected->size_bytes) << "\n";
    std::cout << "  serial ending: " << selected->serial_ending << "\n";
    std::cout << "  role: " << lazarus::to_string(selected->bench_role) << "\n";
    const auto label = lazarus::label_for_device(bench, *selected);
    if (!label.empty()) {
        std::cout << "  label: " << label << "\n";
    }
    std::cout << "  physical path: " << selected->physical_path << "\n";

    lazarus::ImageRestoreOptions options{
        image_dir,
        4 * 1024 * 1024,
        confirmation,
    };
    options.progress = make_progress_printer();
    const auto result = lazarus::restore_directory_image(bench, *selected, options);
    std::cout << "Image: " << result.image_directory << "\n";
    std::cout << "Bytes written: " << result.bytes_written << "\n";
    std::cout << "Chunks written: " << result.chunks_written << "\n";
    std::cout << "Restored: " << (result.restored ? "yes" : "no") << "\n";
    if (!result.facts.empty()) {
        std::cout << "Facts:\n";
        for (const auto& fact : result.facts) {
            std::cout << "  " << fact << "\n";
        }
    }
    if (!result.findings.empty()) {
        std::cout << "Restore findings:\n";
        print_findings(result.findings);
    }
    return result.restored ? 0 : 4;
}

int plan_demo() {
    const lazarus::JobInfo job{
        "45127",
        "Smith John",
        "bench-tech",
        "Backup Before Repair",
    };
    const lazarus::BenchProfile bench{
        "Bench Alpha",
        "/mnt/lazarus-storage",
        {"/mnt/lazarus-storage"},
        {"/pci/0000:00/usb1/1-1/source-left"},
        {"/pci/0000:00/usb1/1-2/destination-right"},
        {"/pci/0000:00/internal/system"},
    };
    const lazarus::DeviceIdentity source{
        "/dev/sdb",
        "/pci/0000:00/usb1/1-1/source-left",
        "",
        "",
        "Samsung SSD 860 EVO",
        "S3Z9NB0K12344K2A",
        "4K2A",
        "usb",
        500ULL * 1000ULL * 1000ULL * 1000ULL,
        512,
        lazarus::DeviceRole::SourceOnly,
        false,
        true,
        false,
        {"/dev/sdb1", "/dev/sdb2", "/dev/sdb3"},
    };

    const auto plan = lazarus::create_backup_plan(job, bench, source, lazarus::ImagingMode::Standard);

    std::cout << lazarus::version() << "\n";
    std::cout << "Job: " << plan.job.ticket_number << " / " << plan.job.purpose << "\n";
    std::cout << "Source: " << plan.source.model << " serial ending " << plan.source.serial_ending << "\n";
    std::cout << "Source role: " << lazarus::to_string(plan.source.bench_role) << "\n";
    std::cout << "Source open mode: " << (plan.source_open_read_only ? "read-only" : "write-enabled") << "\n";
    std::cout << "Image storage: " << plan.image_storage_path << "\n";
    std::cout << "Image format: " << plan.image_extension << "\n";
    std::cout << "Mode: " << lazarus::to_string(plan.mode) << "\n";
    std::cout << "Findings: " << plan.findings.size() << "\n";
    for (const auto& finding : plan.findings) {
        std::cout << "  [" << lazarus::to_string(finding.severity) << "] "
                  << finding.code << ": " << finding.observed << " "
                  << finding.action << "\n";
    }
    return plan.findings.empty() ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string command = argc >= 2 ? argv[1] : "--help";

    if (command == "version") {
        std::cout << lazarus::version() << "\n";
        return 0;
    }
    if (command == "devices") {
        return list_devices();
    }
    if (command == "bench-check") {
        if (argc < 3) {
            std::cerr << "bench-check requires a profile path.\n";
            return 1;
        }
        return bench_check(argv[2]);
    }
    if (command == "smart") {
        if (argc < 4) {
            std::cerr << "smart requires a bench profile path and device selector.\n";
            return 1;
        }
        return smart_diagnostics(argv[2], argv[3]);
    }
    if (command == "inspect-source") {
        if (argc < 4) {
            std::cerr << "inspect-source requires a bench profile path and device selector.\n";
            return 1;
        }
        return inspect_source(argv[2], argv[3]);
    }
    if (command == "image-source") {
        return image_source(argc, argv);
    }
    if (command == "verify-image") {
        if (argc < 3) {
            std::cerr << "verify-image requires an image directory.\n";
            return 1;
        }
        return verify_image(argv[2]);
    }
    if (command == "restore-image") {
        return restore_image(argc, argv);
    }
    if (command == "plan-demo") {
        return plan_demo();
    }
    if (command == "--help" || command == "-h" || command == "help") {
        print_help();
        return 0;
    }

    std::cerr << "Unknown Lazarus command: " << command << "\n\n";
    print_help();
    return 1;
}
