#include "lazarus/core.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: lazarum_provider_fixture RAW_SOURCE OUTPUT_IMAGE_DIRECTORY\n";
        return 2;
    }
    std::error_code error;
    const auto source_path = fs::canonical(argv[1], error);
    if (error || !fs::is_regular_file(source_path, error)) {
        std::cerr << "The fixture source is not a regular file.\n";
        return 1;
    }
    const auto output_path = fs::absolute(argv[2]);
    lazarus::BenchProfile bench;
    bench.name = "Lazarum provider integration";
    bench.image_storage_path = output_path.parent_path().string();
    bench.image_storage_paths = {bench.image_storage_path};
    bench.source_only_paths = {"/fixture/source"};

    lazarus::DeviceIdentity source;
    source.linux_path = source_path.string();
    source.physical_path = "/fixture/source";
    source.model = "Lazarum integration fixture";
    source.serial = "FIXTURE-0001";
    source.serial_ending = "0001";
    source.size_bytes = fs::file_size(source_path, error);
    source.logical_block_size = 512;

    auto opened = lazarus::open_source_read_only(bench, source);
    if (!opened.handle.is_open()) {
        for (const auto& finding : opened.findings) std::cerr << finding.observed << "\n";
        return 1;
    }
    const auto inspection = lazarus::inspect_source_disk(opened.handle);
    lazarus::ImageWriteOptions options;
    options.output_directory = output_path.string();
    options.chunk_size = 1024 * 1024;
    options.compression = lazarus::CompressionMode::Zstd;
    const lazarus::JobInfo job{
        "PROVIDER-TEST", "Lazarum Test", "Automated", "Read-only provider integration",
    };
    const auto written = lazarus::write_directory_image(job, opened.handle, inspection, options);
    if (!written.finalized) {
        for (const auto& finding : written.findings) std::cerr << finding.observed << "\n";
        return 1;
    }
    std::cout << output_path.string() << "\n";
    return 0;
}
