#include "lazarum/viewer.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void write(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    output << contents;
    require(static_cast<bool>(output), "write fixture " + path.string());
}

}  // namespace

int main() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::temp_directory_path() / ("lazarum-tests-" + std::to_string(nonce));
    const auto image = root / "images" / "T-442-smith.laz";
    fs::create_directories(image);
    write(image / "metadata.json", R"({
      "format":"laz-dir", "format_version":2, "created_at":"2026-07-30T12:00:00-0400",
      "job":{"ticket_number":"T-442","customer_name":"Smith","technician":"Dana","purpose":"Backup Before Repair"},
      "source":{"model":"TEST SSD","serial_ending":"1234"},
      "imaging":{"bytes_written":1048576,"bytes_stored":524288,"compression":"zstd"}
    })");
    write(image / "disk.raw", "payload");
    write(image / "hashes.dat", "# lazarus-hashes-v2 compression=zstd\n");
    write(image / "FINALIZED", "done\n");
    write(image / "completion-report.txt", "Backup verified\n");
    write(image / "completion-report.html", "<html><body>Backup verified</body></html>\n");
    write(image / "not-a-report.txt", "not exposed\n");

    const auto scan = lazarum::scan_storage(root);
    require(scan.warnings.empty(), "fixture storage scans without warnings");
    require(scan.images.size() == 1, "one Lazarus image discovered");
    const auto& summary = scan.images.front();
    require(summary.ticket_number == "T-442", "ticket parsed");
    require(summary.customer_name == "Smith", "customer parsed");
    require(summary.source_model == "TEST SSD", "source model parsed");
    require(summary.logical_bytes == 1048576, "logical bytes parsed");
    require(summary.finalized && !summary.incomplete, "finalized state recognized");
    require(summary.reports.size() == 2, "only recognized reports listed");

    std::string error;
    require(lazarum::read_report(image, "completion-report.txt", error) == "Backup verified\n",
            "text report read");
    require(error.empty(), "report read has no error");
    (void)lazarum::read_report(image, "../metadata.json", error);
    require(!error.empty(), "report traversal rejected");

    const auto export_directory = root / "export";
    fs::create_directory(export_directory);
    fs::path written;
    require(lazarum::extract_report(image, "completion-report.txt", export_directory, written, error),
            "report extracted");
    require(written == export_directory / "completion-report.txt", "report destination resolved");
    require(!lazarum::extract_report(image, "completion-report.txt", export_directory, written, error),
            "existing report is not overwritten");

    const auto capability = lazarum::image_data_capability(image);
    require(capability.image_recognized, "data provider recognizes image");
    if (lazarum::host_platform() == lazarum::HostPlatform::Linux) {
        require(capability.raw_reconstruction_available, "Linux provider exposes authenticated logical-disk access");
        require(capability.filesystem_explorer_available, "Linux provider exposes read-only exploration");
        require(capability.file_extraction_available, "Linux provider exposes safe extraction");
    }
    auto provider = lazarum::make_image_data_provider();
    provider->set_progress_callback([](std::string) {});
    const auto extraction = provider->extract(image, "volume-1", {"Users/Smith/document.txt"}, export_directory);
    require(!extraction.completed && extraction.extracted_paths.empty() && !extraction.error.empty(),
            "invalid image data fails closed and reports no extracted paths");

    fs::remove_all(root);
    std::cout << "Lazarum viewer tests passed\n";
    return 0;
}
