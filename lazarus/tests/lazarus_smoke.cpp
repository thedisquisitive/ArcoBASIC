#include "lazarus/core.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

bool has_code(const std::vector<lazarus::SafetyFinding>& findings, const std::string& code) {
    for (const auto& finding : findings) {
        if (finding.code == code) {
            return true;
        }
    }
    return false;
}

void put_le16(std::vector<unsigned char>& data, std::size_t offset, std::uint16_t value) {
    data[offset] = static_cast<unsigned char>(value & 0xFF);
    data[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xFF);
}

void put_le32(std::vector<unsigned char>& data, std::size_t offset, std::uint32_t value) {
    data[offset] = static_cast<unsigned char>(value & 0xFF);
    data[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    data[offset + 2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    data[offset + 3] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

void put_le64(std::vector<unsigned char>& data, std::size_t offset, std::uint64_t value) {
    put_le32(data, offset, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    put_le32(data, offset + 4, static_cast<std::uint32_t>(value >> 32));
}

void put_guid(std::vector<unsigned char>& data, std::size_t offset, const std::array<unsigned char, 16>& guid) {
    for (std::size_t i = 0; i < guid.size(); ++i) {
        data[offset + i] = guid[i];
    }
}

void put_ascii(std::vector<unsigned char>& data, std::size_t offset, const char* text) {
    for (std::size_t i = 0; text[i] != '\0'; ++i) {
        data[offset + i] = static_cast<unsigned char>(text[i]);
    }
}

void put_utf16le_name(std::vector<unsigned char>& data, std::size_t offset, const std::string& name) {
    for (std::size_t i = 0; i < name.size(); ++i) {
        data[offset + (i * 2)] = static_cast<unsigned char>(name[i]);
        data[offset + (i * 2) + 1] = 0;
    }
}

std::uint32_t crc32_bytes(const unsigned char* data, std::size_t length) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

void put_partition_entry(std::vector<unsigned char>& data, std::size_t offset, const std::array<unsigned char, 16>& type_guid, std::uint64_t first_lba, std::uint64_t last_lba, const std::string& name) {
    const std::array<unsigned char, 16> unique_guid{0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};
    put_guid(data, offset, type_guid);
    put_guid(data, offset + 16, unique_guid);
    put_le64(data, offset + 32, first_lba);
    put_le64(data, offset + 40, last_lba);
    put_utf16le_name(data, offset + 56, name);
}

void write_gpt_fixture(const std::filesystem::path& path) {
    constexpr std::size_t sector = 512;
    constexpr std::size_t sectors = 4096;
    std::vector<unsigned char> data(sector * sectors, 0);

    data[510] = 0x55;
    data[511] = 0xAA;
    data[446 + 4] = 0xEE;
    put_le32(data, 446 + 8, 1);
    put_le32(data, 446 + 12, sectors - 1);

    const std::size_t header = sector;
    const char signature[] = "EFI PART";
    for (std::size_t i = 0; i < 8; ++i) {
        data[header + i] = static_cast<unsigned char>(signature[i]);
    }
    put_le32(data, header + 8, 0x00010000);
    put_le32(data, header + 12, 92);
    put_le64(data, header + 24, 1);
    put_le64(data, header + 32, sectors - 1);
    put_le64(data, header + 40, 34);
    put_le64(data, header + 48, sectors - 34);
    put_le64(data, header + 72, 2);
    put_le32(data, header + 80, 128);
    put_le32(data, header + 84, 128);

    const std::array<unsigned char, 16> efi{0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11, 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
    const std::array<unsigned char, 16> msr{0x16, 0xe3, 0xc9, 0xe3, 0x5c, 0x0b, 0xb8, 0x4d, 0x81, 0x7d, 0xf9, 0x2d, 0xf0, 0x02, 0x15, 0xae};
    const std::array<unsigned char, 16> basic{0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44, 0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7};
    const std::array<unsigned char, 16> recovery{0xa4, 0xbb, 0x94, 0xde, 0xd1, 0x06, 0x40, 0x4d, 0xa1, 0x6a, 0xbf, 0xd5, 0x01, 0x79, 0xd6, 0xac};
    const std::size_t entries = sector * 2;
    put_partition_entry(data, entries, efi, 2048, 2303, "EFI");
    put_partition_entry(data, entries + 128, msr, 2304, 2559, "MSR");
    put_partition_entry(data, entries + 256, basic, 2560, 3583, "Windows");
    put_partition_entry(data, entries + 384, recovery, 3584, 3967, "Recovery");

    const std::size_t efi_boot = sector * 2048;
    data[efi_boot + 510] = 0x55;
    data[efi_boot + 511] = 0xAA;
    put_le16(data, efi_boot + 11, 512);
    data[efi_boot + 13] = 1;
    put_le16(data, efi_boot + 14, 32);
    data[efi_boot + 16] = 2;
    put_le32(data, efi_boot + 32, 256);
    put_ascii(data, efi_boot + 82, "FAT32   ");

    const std::size_t ntfs_boot = sector * 2560;
    data[ntfs_boot + 510] = 0x55;
    data[ntfs_boot + 511] = 0xAA;
    put_ascii(data, ntfs_boot + 3, "NTFS    ");
    put_le16(data, ntfs_boot + 11, 512);
    data[ntfs_boot + 13] = 8;
    put_le64(data, ntfs_boot + 40, 1024);
    put_le64(data, ntfs_boot + 48, 4);
    data[ntfs_boot + 64] = static_cast<unsigned char>(-10);
    put_ascii(data, ntfs_boot + (4 * 4096), "FILE");
    const auto entries_crc = crc32_bytes(data.data() + entries, 128 * 128);
    put_le32(data, header + 88, entries_crc);
    put_le32(data, header + 16, crc32_bytes(data.data() + header, 92));

    const std::size_t backup_entries = sector * (sectors - 33);
    std::copy_n(data.data() + entries, 128 * 128, data.data() + backup_entries);
    const std::size_t backup_header = sector * (sectors - 1);
    std::copy_n(signature, 8, data.data() + backup_header);
    put_le32(data, backup_header + 8, 0x00010000);
    put_le32(data, backup_header + 12, 92);
    put_le64(data, backup_header + 24, sectors - 1);
    put_le64(data, backup_header + 32, 1);
    put_le64(data, backup_header + 40, 34);
    put_le64(data, backup_header + 48, sectors - 34);
    put_le64(data, backup_header + 72, sectors - 33);
    put_le32(data, backup_header + 80, 128);
    put_le32(data, backup_header + 84, 128);
    put_le32(data, backup_header + 88, entries_crc);
    put_le32(data, backup_header + 16, crc32_bytes(data.data() + backup_header, 92));

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void write_mbr_ntfs_fixture(const std::filesystem::path& path) {
    constexpr std::size_t sector = 512;
    constexpr std::size_t sectors = 4096;
    std::vector<unsigned char> data(sector * sectors, 0);

    data[510] = 0x55;
    data[511] = 0xAA;
    const std::size_t entry = 446;
    data[entry] = 0x80;
    data[entry + 4] = 0x07;
    put_le32(data, entry + 8, 2048);
    put_le32(data, entry + 12, 1024);

    const std::size_t ntfs = sector * 2048;
    data[ntfs + 510] = 0x55;
    data[ntfs + 511] = 0xAA;
    put_ascii(data, ntfs + 3, "NTFS    ");
    put_le16(data, ntfs + 11, 512);
    data[ntfs + 13] = 8;
    put_le64(data, ntfs + 40, 1024);
    put_le64(data, ntfs + 48, 4);
    data[ntfs + 64] = static_cast<unsigned char>(-10);
    put_ascii(data, ntfs + (4 * 4096), "FILE");

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void write_mbr_fat32_fixture(const std::filesystem::path& path) {
    constexpr std::size_t sector = 512;
    constexpr std::size_t sectors = 4096;
    std::vector<unsigned char> data(sector * sectors, 0);

    data[510] = 0x55;
    data[511] = 0xAA;
    const std::size_t entry = 446;
    data[entry] = 0x80;
    data[entry + 4] = 0x0C;
    put_le32(data, entry + 8, 2048);
    put_le32(data, entry + 12, 1024);

    const std::size_t fat = sector * 2048;
    data[fat + 510] = 0x55;
    data[fat + 511] = 0xAA;
    put_ascii(data, fat + 3, "MSDOS5.0");
    put_le16(data, fat + 11, 512);
    data[fat + 13] = 8;
    put_le16(data, fat + 14, 32);
    data[fat + 16] = 2;
    put_le32(data, fat + 32, 1024);
    put_ascii(data, fat + 82, "FAT32   ");

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void write_whole_exfat_fixture(const std::filesystem::path& path) {
    constexpr std::size_t sector = 512;
    constexpr std::size_t sectors = 4096;
    std::vector<unsigned char> data(sector * sectors, 0);

    data[510] = 0x55;
    data[511] = 0xAA;
    put_ascii(data, 3, "EXFAT   ");

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void write_oversized_mbr_fixture(const std::filesystem::path& path) {
    constexpr std::size_t sector = 512;
    constexpr std::size_t sectors = 128;
    std::vector<unsigned char> data(sector * sectors, 0);

    data[510] = 0x55;
    data[511] = 0xAA;
    const std::size_t entry = 446;
    data[entry] = 0x80;
    data[entry + 4] = 0x07;
    put_le32(data, entry + 8, 2048);
    put_le32(data, entry + 12, 1024 * 1024);

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

}  // namespace

int main() {
    require(!lazarus::is_complete({}), "empty job must not be complete");
    require(has_code(lazarus::validate_job({}), "job.ticket_missing"), "missing ticket must block");

    const lazarus::JobInfo job{
        "45127",
        "Smith John",
        "bench-tech",
        "Backup Before Repair",
    };
    require(lazarus::is_complete(job), "complete job should pass required field check");
    require(lazarus::validate_job(job).empty(), "complete job should have no job findings");

    const lazarus::BenchProfile bench{
        "Bench Alpha",
        "/mnt/lazarus-storage",
        {"/mnt/lazarus-storage"},
        {"/port/source-left"},
        {"/port/destination-right"},
        {"/port/internal-system"},
    };
    require(lazarus::validate_bench_profile(bench).empty(), "valid bench profile should have no findings");

    const lazarus::DeviceIdentity good_source{
        "/dev/sdb",
        "/port/source-left",
        "/dev/disk/by-id/usb-Samsung_SSD_860_EVO_4K2A",
        "/dev/disk/by-path/pci-0000:00-usb-0:1:1.0-scsi-0:0:0:0",
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
    require(lazarus::role_for_device(bench, good_source) == lazarus::DeviceRole::SourceOnly, "bench should assign source role by physical path");
    require(lazarus::validate_source_device(bench, good_source).empty(), "source-only drive should be accepted as source");

    auto storage_bench = bench;
    storage_bench.image_storage_device = good_source.by_id_path;
    require(lazarus::role_for_device(storage_bench, good_source) == lazarus::DeviceRole::ImageStorage,
            "bench should assign image-storage role by stable device identity");

    auto removable_bench = bench;
    removable_bench.removable_media_paths.push_back("/port/recovery-media");
    auto recovery_media = good_source;
    recovery_media.physical_path = "/port/recovery-media";
    recovery_media.by_path.clear();
    require(lazarus::role_for_device(removable_bench, recovery_media) == lazarus::DeviceRole::RemovableMedia,
            "bench should assign removable-media role by physical path");
    removable_bench.destination_only_paths.push_back("/port/recovery-media");
    require(has_code(lazarus::validate_bench_profile(removable_bench), "bench.role_conflict"),
            "a physical port must not have removable-media and destination roles");

    auto by_id_source = good_source;
    by_id_source.physical_path = "/unmatched/physical";
    by_id_source.by_path = "/unmatched/by-path";
    const lazarus::BenchProfile by_id_bench{
        "Bench By ID",
        "/mnt/lazarus-storage",
        {"/mnt/lazarus-storage"},
        {by_id_source.by_id_path},
        {"/port/destination-right"},
        {},
    };
    require(lazarus::role_for_device(by_id_bench, by_id_source) == lazarus::DeviceRole::SourceOnly, "bench should assign source role by by-id path");
    auto labeled_bench = by_id_bench;
    labeled_bench.port_labels.push_back(lazarus::PortLabel{by_id_source.by_id_path, "Left USB3"});
    require(lazarus::label_for_device(labeled_bench, by_id_source).empty(),
            "a disk by-id selector must not behave as a physical port label");
    const auto normalized_usb_port = lazarus::physical_port_identity(good_source.by_path);
    require(normalized_usb_port == "port:pci-0000:00-usb-0:1",
            "USB block paths should normalize to the controller and downstream port");
    labeled_bench.port_labels = {{normalized_usb_port, "Left USB3"}};
    auto replacement_disk_same_port = good_source;
    replacement_disk_same_port.by_id_path = "/dev/disk/by-id/usb-Different_Disk_9999";
    replacement_disk_same_port.serial = "DIFFERENT9999";
    replacement_disk_same_port.physical_path = replacement_disk_same_port.by_path;
    require(lazarus::label_for_device(labeled_bench, replacement_disk_same_port) == "Left USB3",
            "a replacement disk in the same socket should inherit the physical port label");
    auto same_disk_other_port = replacement_disk_same_port;
    same_disk_other_port.by_path = "/dev/disk/by-path/pci-0000:00-usb-0:4:1.0-scsi-0:0:0:0";
    same_disk_other_port.physical_path = same_disk_other_port.by_path;
    require(lazarus::label_for_device(labeled_bench, same_disk_other_port).empty(),
            "moving a disk to another socket must not carry the old port label");

    auto destination_as_source = good_source;
    destination_as_source.physical_path = "/port/destination-right";
    destination_as_source.bench_role = lazarus::DeviceRole::DestinationOnly;
    auto destination_findings = lazarus::validate_source_device(bench, destination_as_source);
    require(has_code(destination_findings, "source.destination_port"), "destination port must be rejected as source");

    auto system_as_source = good_source;
    system_as_source.is_system_disk = true;
    auto system_findings = lazarus::validate_source_device(bench, system_as_source);
    require(has_code(system_findings, "source.system_disk"), "system disk must be rejected as source");

    const lazarus::BenchProfile invalid_bench{
        "",
        "",
        {},
        {"/port/conflict"},
        {"/port/conflict"},
        {},
    };
    auto invalid_bench_findings = lazarus::validate_bench_profile(invalid_bench);
    require(has_code(invalid_bench_findings, "bench.name_missing"), "bench without name must be rejected");
    require(has_code(invalid_bench_findings, "bench.role_conflict"), "conflicting bench roles must be rejected");

    auto plan = lazarus::create_backup_plan(job, bench, good_source, lazarus::ImagingMode::Standard);
    require(plan.findings.empty(), "valid backup plan should have no blockers");
    require(plan.source_open_read_only, "backup plan source must be read-only");
    require(plan.image_extension == ".laz", "backup plan must use .laz extension");
    require(plan.source.by_id_path.find("/dev/disk/by-id/") == 0, "device identity should carry persistent by-id path");
    require(plan.source.by_path.find("/dev/disk/by-path/") == 0, "device identity should carry persistent by-path");
    require(plan.source.logical_block_size == 512, "device identity should carry logical block size");
    require(plan.source.partitions.size() == 3, "device identity should carry partition list");

    const auto temp_path = std::filesystem::temp_directory_path() / "lazarus-source-handle-smoke.bin";
    {
        std::ofstream out(temp_path, std::ios::binary);
        out << "LAZARUS-READ-ONLY";
    }
    auto file_source = good_source;
    file_source.linux_path = temp_path.string();
    file_source.physical_path = "/port/source-left";
    file_source.by_id_path = "";
    file_source.by_path = "";
    auto open_result = lazarus::open_source_read_only(bench, file_source);
    require(open_result.handle.is_open(), "source handle should open valid source read-only");
    auto read = open_result.handle.read_at(0, 7);
    require(read.error.empty(), "source handle read should succeed");
    require(read.data.size() == 7, "source handle read should return requested bytes");
    require(std::to_integer<char>(read.data[0]) == 'L', "source handle should read file content");
    open_result.handle.close();
    std::filesystem::remove(temp_path);

    auto blocked_open = lazarus::open_source_read_only(bench, destination_as_source);
    require(!blocked_open.handle.is_open(), "destination device must not open as source");
    require(has_code(blocked_open.findings, "source.destination_port"), "blocked open should explain destination-port violation");

    const auto gpt_path = std::filesystem::temp_directory_path() / "lazarus-gpt-inspection-smoke.img";
    write_gpt_fixture(gpt_path);
    auto gpt_source = good_source;
    gpt_source.linux_path = gpt_path.string();
    gpt_source.physical_path = "/port/source-left";
    gpt_source.by_id_path = "";
    gpt_source.by_path = "";
    gpt_source.size_bytes = 512ULL * 4096ULL;
    gpt_source.logical_block_size = 512;
    auto gpt_open = lazarus::open_source_read_only(bench, gpt_source);
    require(gpt_open.handle.is_open(), "GPT fixture source should open read-only");
    const auto inspection = lazarus::inspect_source_disk(gpt_open.handle);
    require(inspection.first_sector_read, "inspector should read first sector");
    require(inspection.protective_mbr, "inspector should detect protective MBR");
    require(inspection.gpt_detected, "inspector should detect GPT");
    require(inspection.gpt_header_valid, "inspector should validate GPT header basics");
    require(inspection.partitions.size() == 4, "inspector should parse GPT partitions");
    require(inspection.partitions[0].kind == lazarus::PartitionKind::EfiSystem, "inspector should classify EFI partition");
    require(inspection.partitions[1].kind == lazarus::PartitionKind::MicrosoftReserved, "inspector should classify MSR partition");
    require(inspection.partitions[2].kind == lazarus::PartitionKind::WindowsBasicData, "inspector should classify Windows basic data partition");
    require(inspection.partitions[3].kind == lazarus::PartitionKind::WindowsRecovery, "inspector should classify Windows Recovery partition");
    require(inspection.partitions[2].name == "Windows", "inspector should decode GPT partition name");
    const auto image_dir = std::filesystem::temp_directory_path() / "lazarus-image-write-smoke.laz";
    std::filesystem::remove_all(image_dir);
    lazarus::ImageWriteOptions image_options{
        image_dir.string(),
        64 * 1024,
        0,
    };
    int image_progress_events = 0;
    bool image_completed_event = false;
    image_options.progress = [&](const lazarus::ProgressEvent& event) {
        ++image_progress_events;
        if (event.operation == "image" && event.phase == "complete") {
            image_completed_event = true;
        }
    };
    const auto image_result = lazarus::write_directory_image(job, gpt_open.handle, inspection, image_options);
    require(image_result.finalized, "image writer should finalize a complete source image");
    require(image_progress_events > 0, "image writer should emit progress events");
    require(image_completed_event, "image writer should emit a completion progress event");
    require(image_result.bytes_written == gpt_source.size_bytes, "image writer should write the full source size");
    require(image_result.chunks_written > 0, "image writer should record at least one chunk");
    require(std::filesystem::exists(image_dir / "FINALIZED"), "finalized image should have FINALIZED marker");
    require(!std::filesystem::exists(image_dir / "INCOMPLETE"), "finalized image should not keep INCOMPLETE marker");
    require(std::filesystem::exists(image_dir / "metadata.json"), "image writer should create metadata.json");
    require(std::filesystem::exists(image_dir / "job-journal.json"), "image writer should preserve a durable job journal for resume discovery");
    {
        std::ifstream journal(image_dir / "job-journal.json");
        const std::string contents((std::istreambuf_iterator<char>(journal)), std::istreambuf_iterator<char>());
        require(contents.find(job.ticket_number) != std::string::npos, "job journal should identify the ticket");
        require(contents.find(job.customer_name) != std::string::npos, "job journal should identify the customer");
        require(contents.find("interrupted-or-running") != std::string::npos, "job journal should describe its conservative state");
    }
    require(std::filesystem::exists(image_dir / "partition-table.bin"), "image writer should create partition-table.bin");
    require(std::filesystem::exists(image_dir / "disk.raw"), "image writer should create disk.raw");
    require(std::filesystem::exists(image_dir / "hashes.dat"), "image writer should create hashes.dat");
    require(std::filesystem::file_size(image_dir / "disk.raw") == gpt_source.size_bytes, "disk.raw should match source size");
    {
        std::ifstream hashes(image_dir / "hashes.dat");
        std::string first_line;
        std::getline(hashes, first_line);
        require(first_line == "algorithm=sha256", "hashes.dat should use SHA-256");
    }
    int verify_progress_events = 0;
    bool verify_completed_event = false;
    const auto verify_result = lazarus::verify_directory_image(image_dir.string(), [&](const lazarus::ProgressEvent& event) {
        ++verify_progress_events;
        if (event.operation == "verify" && event.phase == "complete") {
            verify_completed_event = true;
        }
    });
    require(verify_result.verified, "verifier should accept a freshly finalized image");
    require(verify_progress_events > 0, "verifier should emit progress events");
    require(verify_completed_event, "verifier should emit a completion progress event");
    require(verify_result.actual_bytes == gpt_source.size_bytes, "verifier should report disk.raw size");
    require(verify_result.chunks_verified == image_result.chunks_written, "verifier should check every chunk");
    require(std::filesystem::exists(image_dir / "verification.json"), "verifier should write verification.json");

    const auto browse_cache = std::filesystem::temp_directory_path() / "lazarus-browse-cache-smoke.raw";
    std::filesystem::remove(browse_cache);
    std::filesystem::remove(browse_cache.string() + ".ready");
    int browse_progress_events = 0;
    lazarus::ImageBrowseCacheOptions browse_options{image_dir.string(), browse_cache.string()};
    browse_options.progress = [&](const lazarus::ProgressEvent& event) {
        if (event.operation == "browse") ++browse_progress_events;
    };
    const auto browse_result = lazarus::prepare_image_browse_cache(browse_options);
    require(browse_result.prepared && browse_result.image_verified, "browse cache should require and preserve image verification");
    require(!browse_result.reused_existing_cache, "first browse-cache preparation should reconstruct the image");
    require(browse_result.logical_bytes == gpt_source.size_bytes, "browse cache should preserve logical disk size");
    require(browse_progress_events > 0, "browse-cache preparation should emit progress");
    require(std::filesystem::file_size(browse_cache) == gpt_source.size_bytes, "browse cache should expose the full logical disk");
    {
        std::ifstream source(gpt_path, std::ios::binary);
        std::ifstream cache(browse_cache, std::ios::binary);
        std::vector<char> source_bytes(static_cast<std::size_t>(gpt_source.size_bytes));
        std::vector<char> cache_bytes(source_bytes.size());
        source.read(source_bytes.data(), static_cast<std::streamsize>(source_bytes.size()));
        cache.read(cache_bytes.data(), static_cast<std::streamsize>(cache_bytes.size()));
        require(source_bytes == cache_bytes, "browse cache should match the logical source image exactly");
    }
    const auto reused_browse = lazarus::prepare_image_browse_cache({image_dir.string(), browse_cache.string()});
    require(reused_browse.prepared && reused_browse.reused_existing_cache,
            "a matching verified browse cache should be reused");
    std::filesystem::remove(browse_cache);
    std::filesystem::remove(browse_cache.string() + ".ready");

    std::vector<std::string> hash_lines;
    {
        std::ifstream hashes(image_dir / "hashes.dat");
        std::string line;
        while (std::getline(hashes, line)) {
            hash_lines.push_back(line);
        }
    }
    require(hash_lines.size() >= 3, "hashes.dat should contain at least one chunk record");
    {
        std::ofstream hashes(image_dir / "hashes.dat");
        std::size_t records_written = 0;
        for (const auto& line : hash_lines) {
            hashes << line << "\n";
            if (!line.empty() && line.find('=') == std::string::npos && line.rfind("columns=", 0) != 0) {
                ++records_written;
                if (records_written == 1) {
                    break;
                }
            }
        }
    }
    std::filesystem::resize_file(image_dir / "disk.raw", image_options.chunk_size);
    std::filesystem::remove(image_dir / "FINALIZED");
    {
        std::ofstream incomplete(image_dir / "INCOMPLETE");
        incomplete << "simulated interruption\n";
    }
    const auto wrong_resume_path = std::filesystem::temp_directory_path() / "lazarus-wrong-resume-source.img";
    std::filesystem::copy_file(gpt_path, wrong_resume_path, std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream wrong(wrong_resume_path, std::ios::binary | std::ios::in | std::ios::out);
        wrong.seekp(4096);
        wrong.put(static_cast<char>(0x7f));
    }
    auto wrong_resume_device = gpt_source;
    wrong_resume_device.linux_path = wrong_resume_path.string();
    const auto wrong_resume_open = lazarus::open_source_read_only(bench, wrong_resume_device);
    require(wrong_resume_open.handle.is_open(), "wrong resume fixture should open read-only");
    const auto rejected_resume = lazarus::write_directory_image(job, wrong_resume_open.handle, inspection, image_options);
    require(!rejected_resume.finalized && has_code(rejected_resume.findings, "image.resume_source_prefix_mismatch"),
            "resume must reject a different source even when its reported identity and size match");
    std::filesystem::remove(wrong_resume_path);
    const auto resumed_result = lazarus::write_directory_image(job, gpt_open.handle, inspection, image_options);
    require(resumed_result.finalized, "image writer should finalize after resuming an incomplete image");
    require(resumed_result.resumed, "image writer should report resume");
    require(resumed_result.resumed_bytes == image_options.chunk_size, "image writer should resume from the verified prefix");
    require(resumed_result.bytes_written == gpt_source.size_bytes, "resumed image should finish the full source size");
    const auto resumed_verify = lazarus::verify_directory_image(image_dir.string());
    require(resumed_verify.verified, "verifier should accept resumed finalized image");
    const auto restore_target = std::filesystem::temp_directory_path() / "lazarus-restore-target-smoke.bin";
    {
        std::ofstream target(restore_target, std::ios::binary);
        std::vector<char> zeros(static_cast<std::size_t>(gpt_source.size_bytes), 0);
        target.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    auto restore_device = good_source;
    restore_device.linux_path = restore_target.string();
    restore_device.physical_path = "/port/destination-right";
    restore_device.by_id_path = "";
    restore_device.by_path = "";
    restore_device.size_bytes = gpt_source.size_bytes;
    restore_device.bench_role = lazarus::DeviceRole::DestinationOnly;
    lazarus::ImageRestoreOptions restore_options{
        image_dir.string(),
        64 * 1024,
        "ERASE",
    };
    int restore_progress_events = 0;
    bool restore_completed_event = false;
    bool restore_write_started_at_zero = false;
    bool restore_write_had_partial_progress = false;
    restore_options.progress = [&](const lazarus::ProgressEvent& event) {
        ++restore_progress_events;
        if (event.operation == "restore" && event.phase == "write" && event.bytes_done == 0) {
            restore_write_started_at_zero = true;
        }
        if (event.operation == "restore" && event.phase == "write" && event.bytes_done > 0 && event.bytes_done < event.bytes_total) {
            restore_write_had_partial_progress = true;
        }
        if (event.operation == "restore" && event.phase == "complete") {
            restore_completed_event = true;
        }
    };
    const auto restore_result = lazarus::restore_directory_image(bench, restore_device, restore_options);
    require(restore_result.restored, "restore should write verified image to destination");
    require(restore_progress_events > 0, "restore should emit progress events");
    require(restore_write_started_at_zero, "restore should emit a zero-percent write progress event");
    require(restore_write_had_partial_progress, "restore should emit partial write progress before completion");
    require(restore_completed_event, "restore should emit a completion progress event");
    require(restore_result.bytes_written == gpt_source.size_bytes, "restore should write full image size");
    require(restore_result.bytes_verified == gpt_source.size_bytes, "restore should read back the full destination");
    require(restore_result.readback_verified, "restore should require full destination readback");
    require(restore_result.destination_layout_validated, "restore should reopen and inspect the destination layout");
    require(restore_result.image_verified_before_restore, "restore should verify image before writing");
    {
        std::ifstream source_image(image_dir / "disk.raw", std::ios::binary);
        std::ifstream restored(restore_target, std::ios::binary);
        std::vector<char> source_prefix(512);
        std::vector<char> restored_prefix(512);
        source_image.read(source_prefix.data(), static_cast<std::streamsize>(source_prefix.size()));
        restored.read(restored_prefix.data(), static_cast<std::streamsize>(restored_prefix.size()));
        require(source_prefix == restored_prefix, "restore destination should match image prefix");
    }
    std::filesystem::remove(restore_target);

    const auto compressed_image_dir = std::filesystem::temp_directory_path() / "lazarus-zstd-image-write-smoke.laz";
    std::filesystem::remove_all(compressed_image_dir);
    lazarus::ImageWriteOptions compressed_options{
        compressed_image_dir.string(),
        64 * 1024,
        0,
    };
    compressed_options.compression = lazarus::CompressionMode::Zstd;
    const auto compressed_result = lazarus::write_directory_image(job, gpt_open.handle, inspection, compressed_options);
    require(compressed_result.finalized, "zstd image writer should finalize a complete source image");
    require(compressed_result.bytes_written == gpt_source.size_bytes, "zstd image writer should report source bytes written");
    require(compressed_result.bytes_stored > 0, "zstd image writer should report stored bytes");
    require(compressed_result.bytes_stored < compressed_result.bytes_written, "zstd image should compress sparse fixture data");
    const auto compressed_verify = lazarus::verify_directory_image(compressed_image_dir.string());
    require(compressed_verify.verified, "verifier should accept zstd image");
    require(compressed_verify.actual_bytes == gpt_source.size_bytes, "zstd verifier should report logical source size");
    require(compressed_verify.stored_bytes == compressed_result.bytes_stored, "zstd verifier should report stored size");
    const auto compressed_restore_target = std::filesystem::temp_directory_path() / "lazarus-zstd-restore-target-smoke.bin";
    {
        std::ofstream target(compressed_restore_target, std::ios::binary);
        std::vector<char> zeros(static_cast<std::size_t>(gpt_source.size_bytes), 0);
        target.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
    auto compressed_restore_device = restore_device;
    compressed_restore_device.linux_path = compressed_restore_target.string();
    lazarus::ImageRestoreOptions compressed_restore_options{
        compressed_image_dir.string(),
        64 * 1024,
        "ERASE",
    };
    const auto compressed_restore_result = lazarus::restore_directory_image(bench, compressed_restore_device, compressed_restore_options);
    require(compressed_restore_result.restored, "restore should write verified zstd image to destination");
    {
        std::ifstream source_fixture(gpt_path, std::ios::binary);
        std::ifstream restored(compressed_restore_target, std::ios::binary);
        std::vector<char> source_prefix(512);
        std::vector<char> restored_prefix(512);
        source_fixture.read(source_prefix.data(), static_cast<std::streamsize>(source_prefix.size()));
        restored.read(restored_prefix.data(), static_cast<std::streamsize>(restored_prefix.size()));
        require(source_prefix == restored_prefix, "zstd restore destination should match source prefix");
    }
    std::filesystem::remove(compressed_restore_target);
    std::filesystem::remove_all(compressed_image_dir);

    const auto rescue_image_dir = std::filesystem::temp_directory_path() / "lazarus-rescue-image-smoke.laz";
    std::filesystem::remove_all(rescue_image_dir);
    lazarus::ImageWriteOptions rescue_options;
    rescue_options.output_directory = rescue_image_dir.string();
    rescue_options.chunk_size = 64 * 1024;
    rescue_options.max_bytes = gpt_source.size_bytes + 8192;
    rescue_options.mode = lazarus::ImagingMode::Rescue;
    rescue_options.rescue_minimum_read = 4096;
    rescue_options.rescue_retries = 0;
    const auto rescue_result = lazarus::write_directory_image(job, gpt_open.handle, inspection, rescue_options);
    require(rescue_result.finalized && rescue_result.completed_with_warnings, "rescue image should finalize with factual warnings");
    require(!rescue_result.unreadable_ranges.empty(), "rescue image should record unreadable ranges");
    require(std::filesystem::exists(rescue_image_dir / "bad-sector-map.dat"), "rescue image should persist its bad-sector map");
    std::filesystem::remove_all(rescue_image_dir);

    const auto corrupt_gpt_path = std::filesystem::temp_directory_path() / "lazarus-corrupt-gpt-smoke.img";
    std::filesystem::copy_file(gpt_path, corrupt_gpt_path, std::filesystem::copy_options::overwrite_existing);
    {
        std::fstream corrupt(corrupt_gpt_path, std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(512 + 40);
        corrupt.put(static_cast<char>(0x23));
    }
    auto corrupt_device = gpt_source;
    corrupt_device.linux_path = corrupt_gpt_path.string();
    auto corrupt_open = lazarus::open_source_read_only(bench, corrupt_device);
    const auto corrupt_inspection = lazarus::inspect_source_disk(corrupt_open.handle);
    require(has_code(corrupt_inspection.findings, "gpt.header_crc_invalid"), "GPT inspection should reject a corrupt primary header CRC");
    std::filesystem::remove(corrupt_gpt_path);
    gpt_open.handle.close();
    std::filesystem::remove_all(image_dir);
    std::filesystem::remove(gpt_path);

    const auto mbr_path = std::filesystem::temp_directory_path() / "lazarus-mbr-ntfs-inspection-smoke.img";
    write_mbr_ntfs_fixture(mbr_path);
    auto mbr_source = good_source;
    mbr_source.linux_path = mbr_path.string();
    mbr_source.physical_path = "/port/source-left";
    mbr_source.by_id_path = "";
    mbr_source.by_path = "";
    mbr_source.size_bytes = 512ULL * 4096ULL;
    mbr_source.logical_block_size = 512;
    auto mbr_open = lazarus::open_source_read_only(bench, mbr_source);
    require(mbr_open.handle.is_open(), "MBR fixture source should open read-only");
    const auto mbr_inspection = lazarus::inspect_source_disk(mbr_open.handle);
    require(mbr_inspection.first_sector_read, "MBR inspector should read first sector");
    require(mbr_inspection.mbr_signature_valid, "MBR inspector should detect MBR signature");
    require(mbr_inspection.mbr_detected, "MBR inspector should parse MBR layout");
    require(!mbr_inspection.gpt_detected, "MBR inspector should not require GPT");
    require(mbr_inspection.partitions.size() == 1, "MBR inspector should parse primary partition");
    require(mbr_inspection.partitions[0].kind == lazarus::PartitionKind::WindowsBasicData, "MBR inspector should classify NTFS partition as Windows data");
    require(mbr_inspection.partitions[0].filesystem == lazarus::FileSystemKind::Ntfs, "MBR inspector should classify NTFS filesystem");
    require(mbr_inspection.partitions[0].ntfs_detected, "MBR inspector should detect NTFS boot sector");
    const auto mbr_image_dir = std::filesystem::temp_directory_path() / "lazarus-mbr-image-write-smoke.laz";
    std::filesystem::remove_all(mbr_image_dir);
    const lazarus::ImageWriteOptions mbr_image_options{
        mbr_image_dir.string(),
        64 * 1024,
        0,
    };
    const auto mbr_image_result = lazarus::write_directory_image(job, mbr_open.handle, mbr_inspection, mbr_image_options);
    require(mbr_image_result.finalized, "image writer should finalize a complete MBR source image");
    const auto mbr_verify = lazarus::verify_directory_image(mbr_image_dir.string());
    require(mbr_verify.verified, "verifier should accept MBR source image");
    mbr_open.handle.close();
    std::filesystem::remove_all(mbr_image_dir);
    std::filesystem::remove(mbr_path);

    const auto fat32_path = std::filesystem::temp_directory_path() / "lazarus-mbr-fat32-inspection-smoke.img";
    write_mbr_fat32_fixture(fat32_path);
    auto fat32_source = good_source;
    fat32_source.linux_path = fat32_path.string();
    fat32_source.physical_path = "/port/source-left";
    fat32_source.by_id_path = "";
    fat32_source.by_path = "";
    fat32_source.size_bytes = 512ULL * 4096ULL;
    fat32_source.logical_block_size = 512;
    auto fat32_open = lazarus::open_source_read_only(bench, fat32_source);
    require(fat32_open.handle.is_open(), "FAT32 fixture source should open read-only");
    const auto fat32_inspection = lazarus::inspect_source_disk(fat32_open.handle);
    require(fat32_inspection.mbr_detected, "FAT32 fixture should parse MBR layout");
    require(fat32_inspection.partitions.size() == 1, "FAT32 fixture should expose one partition");
    require(fat32_inspection.partitions[0].filesystem == lazarus::FileSystemKind::Fat32, "inspector should classify FAT32 filesystem");
    require(!has_code(fat32_inspection.findings, "filesystem.not_detected"), "FAT32 fixture should not report missing filesystem support");
    fat32_open.handle.close();
    std::filesystem::remove(fat32_path);

    const auto exfat_path = std::filesystem::temp_directory_path() / "lazarus-whole-exfat-inspection-smoke.img";
    write_whole_exfat_fixture(exfat_path);
    auto exfat_source = good_source;
    exfat_source.linux_path = exfat_path.string();
    exfat_source.physical_path = "/port/source-left";
    exfat_source.by_id_path = "";
    exfat_source.by_path = "";
    exfat_source.size_bytes = 512ULL * 4096ULL;
    exfat_source.logical_block_size = 512;
    auto exfat_open = lazarus::open_source_read_only(bench, exfat_source);
    require(exfat_open.handle.is_open(), "whole-device exFAT fixture source should open read-only");
    const auto exfat_inspection = lazarus::inspect_source_disk(exfat_open.handle);
    require(!exfat_inspection.mbr_detected, "whole-device exFAT should not be treated as partitioned MBR");
    require(exfat_inspection.partitions.size() == 1, "whole-device exFAT should expose one filesystem extent");
    require(exfat_inspection.partitions[0].number == 0, "whole-device filesystem should use partition number zero");
    require(exfat_inspection.partitions[0].filesystem == lazarus::FileSystemKind::Exfat, "inspector should classify whole-device exFAT");
    const auto exfat_image_dir = std::filesystem::temp_directory_path() / "lazarus-whole-exfat-image-smoke.laz";
    std::filesystem::remove_all(exfat_image_dir);
    const lazarus::ImageWriteOptions exfat_image_options{
        exfat_image_dir.string(),
        64 * 1024,
        0,
    };
    const auto exfat_image_result = lazarus::write_directory_image(job, exfat_open.handle, exfat_inspection, exfat_image_options);
    require(exfat_image_result.finalized, "image writer should finalize a whole-device exFAT source image");
    const auto exfat_verify = lazarus::verify_directory_image(exfat_image_dir.string());
    require(exfat_verify.verified, "verifier should accept whole-device exFAT source image");
    exfat_open.handle.close();
    std::filesystem::remove_all(exfat_image_dir);
    std::filesystem::remove(exfat_path);

    const auto oversized_mbr_path = std::filesystem::temp_directory_path() / "lazarus-oversized-mbr-smoke.img";
    write_oversized_mbr_fixture(oversized_mbr_path);
    auto oversized_mbr_source = good_source;
    oversized_mbr_source.linux_path = oversized_mbr_path.string();
    oversized_mbr_source.physical_path = "/port/source-left";
    oversized_mbr_source.by_id_path = "";
    oversized_mbr_source.by_path = "";
    oversized_mbr_source.size_bytes = 512ULL * 128ULL;
    oversized_mbr_source.logical_block_size = 512;
    auto oversized_mbr_open = lazarus::open_source_read_only(bench, oversized_mbr_source);
    require(oversized_mbr_open.handle.is_open(), "oversized MBR fixture source should open read-only");
    const auto oversized_mbr_inspection = lazarus::inspect_source_disk(oversized_mbr_open.handle);
    require(oversized_mbr_inspection.mbr_detected, "oversized MBR fixture should still detect MBR signature");
    require(oversized_mbr_inspection.partitions.empty(), "inspector should not report partitions outside the source disk");
    require(has_code(oversized_mbr_inspection.findings, "mbr.partition_bounds_invalid"), "oversized MBR partition should be rejected");
    require(has_code(oversized_mbr_inspection.findings, "mbr.no_partitions"), "oversized MBR fixture should report no supported primary partitions");
    oversized_mbr_open.handle.close();
    std::filesystem::remove(oversized_mbr_path);

    const auto discovered = lazarus::discover_block_devices();
    for (const auto& device : discovered) {
        require(device.linux_path.find("/dev/") == 0, "discovered devices should expose /dev path");
        require(!device.physical_path.empty(), "discovered devices should have a persistent or sysfs physical path");
        require(device.size_bytes > 0 || device.removable, "non-removable discovered devices should report a nonzero size");
    }

    const std::string healthy_smart = R"json({
      "model_name": "Samsung SSD 860 EVO",
      "serial_number": "S3Z9NB0K123456A",
      "smart_status": { "passed": true },
      "power_on_time": { "hours": 1234 },
      "temperature": { "current": 33 },
      "ata_smart_attributes": {
        "table": [
          { "name": "Reallocated_Sector_Ct", "raw": { "value": 0 } },
          { "name": "Current_Pending_Sector", "raw": { "value": 0 } },
          { "name": "Offline_Uncorrectable", "raw": { "value": 0 } }
        ]
      }
    })json";
    const auto healthy_smart_result = lazarus::parse_smartctl_json(good_source, healthy_smart, 0);
    require(healthy_smart_result.health == "passed", "SMART parser should detect passing health");
    require(healthy_smart_result.model == "Samsung SSD 860 EVO", "SMART parser should capture model");
    require(healthy_smart_result.power_on_hours.present && healthy_smart_result.power_on_hours.value == 1234, "SMART parser should capture power-on hours");
    require(healthy_smart_result.temperature_celsius.present && healthy_smart_result.temperature_celsius.value == 33, "SMART parser should capture temperature");
    require(healthy_smart_result.reallocated_sectors.present && healthy_smart_result.reallocated_sectors.value == 0, "SMART parser should capture reallocated sectors");
    require(!has_code(healthy_smart_result.findings, "smart.health_failed"), "passing SMART should not report failed health");

    const std::string failing_smart = R"json({
      "model_name": "Failing Test Disk",
      "smart_status": { "passed": false },
      "power_on_time": { "hours": 45000 },
      "temperature": { "current": 63 },
      "ata_smart_attributes": {
        "table": [
          { "name": "Reallocated_Sector_Ct", "raw": { "value": 12 } },
          { "name": "Current_Pending_Sector", "raw": { "value": 3 } },
          { "name": "Offline_Uncorrectable", "raw": { "value": 2 } }
        ]
      }
    })json";
    const auto failing_smart_result = lazarus::parse_smartctl_json(good_source, failing_smart, 8);
    require(failing_smart_result.health == "failed", "SMART parser should detect failed health");
    require(has_code(failing_smart_result.findings, "smart.health_failed"), "failed SMART should produce blocker finding");
    require(has_code(failing_smart_result.findings, "smart.temperature_high"), "high SMART temperature should warn");
    require(has_code(failing_smart_result.findings, "smart.reallocated_sectors"), "reallocated sectors should warn");
    require(has_code(failing_smart_result.findings, "smart.pending_sectors"), "pending sectors should warn");
    require(has_code(failing_smart_result.findings, "smart.uncorrectable_errors"), "uncorrectable errors should warn");

    const auto driver_package = std::filesystem::temp_directory_path() / "lazarus-storage-driver-package";
    std::filesystem::remove_all(driver_package);
    std::filesystem::create_directories(driver_package);
    {
        std::ofstream inf(driver_package / "vmd.inf");
        inf << "[Version]\n"
            << "Signature=\"$Windows NT$\"\n"
            << "Class=SCSIAdapter\n"
            << "ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}\n"
            << "Provider=%Intel%\n"
            << "DriverVer=07/01/2026,20.1.0.1000\n"
            << "CatalogFile=vmd.cat\n"
            << "[Manufacturer]\n"
            << "%Intel%=Models,NTamd64\n"
            << "[Models.NTamd64]\n"
            << "%VMD%=Install, PCI\\VEN_8086&DEV_7D0B\n"
            << "[Install.NTamd64.Services]\n"
            << "AddService=iaStorVD,0x00000002,VmdService\n"
            << "[VmdService]\n"
            << "ServiceType=1\n"
            << "StartType=0\n"
            << "ErrorControl=1\n"
            << "LoadOrderGroup=SCSI Miniport\n"
            << "ServiceBinary=%12%\\iaStorVD.sys\n";
        std::ofstream(driver_package / "vmd.cat", std::ios::binary) << "CATALOG-FIXTURE";
        std::ofstream(driver_package / "iaStorVD.sys", std::ios::binary) << "SYS-FIXTURE";
    }
    const auto driver_inspection = lazarus::inspect_driver_package(driver_package.string());
    require(driver_inspection.inf_files.size() == 1, "driver inspector should find the storage INF");
    require(driver_inspection.contains_storage_controller_driver, "driver inspector should classify SCSIAdapter packages");
    require(driver_inspection.ready_for_windows_signature_verification, "complete package should be ready for offline bootstrap injection");
    require(driver_inspection.hardware_ids.size() == 1, "driver inspector should index hardware IDs");
    require(driver_inspection.inf_files[0].catalog_present, "driver inspector should require the declared catalog");
    require(driver_inspection.inf_files[0].boot_service_present, "driver inspector should resolve the boot-start service");
    require(driver_inspection.inf_files[0].service_name == "iaStorVD", "driver inspector should retain the service name");
    require(driver_inspection.inf_files[0].service_binary == "iaStorVD.sys", "driver inspector should resolve the service binary");

    lazarus::DriverMigrationPlanOptions migration_options;
    migration_options.target_hardware_ids = {"PCI\\VEN_8086&DEV_7D0B&SUBSYS_12345678"};
    const auto migration_plan = lazarus::create_driver_migration_plan(migration_options, {driver_inspection});
    require(migration_plan.matching_storage_driver_found, "migration planner should match a compatible storage controller ID");
    require(migration_plan.ready_for_servicing, "matching complete package should produce a serviceable plan");
    require(!migration_plan.actions.empty() && migration_plan.actions.front().action == "add", "migration plan should add matching drivers first");
    require(!migration_plan.requires_windows_pe, "driver servicing plan should remain inside Lazarus OS");
    require(migration_plan.actions.front().service_name == "iaStorVD", "migration plan should carry boot service metadata");

    migration_options.requested_removals = {"oem42.inf"};
    const auto unacknowledged_removal = lazarus::create_driver_migration_plan(migration_options, {driver_inspection});
    require(!unacknowledged_removal.ready_for_servicing, "unacknowledged driver removal must block servicing");
    require(has_code(unacknowledged_removal.findings, "migration.removal_not_acknowledged"), "blocked removal should explain acknowledgement requirement");
    migration_options.removal_risk_acknowledged = true;
    const auto acknowledged_removal = lazarus::create_driver_migration_plan(migration_options, {driver_inspection});
    require(acknowledged_removal.ready_for_servicing, "explicitly acknowledged OEM removal should produce a serviceable plan");
    require(acknowledged_removal.actions.back().action == "remove", "driver removals must be ordered after additions");
    std::filesystem::remove_all(driver_package);

    const auto bundle = lazarus::create_support_bundle_manifest();
    require(!bundle.include_files.empty(), "support bundle should include diagnostic files");
    require(!bundle.excluded_data_classes.empty(), "support bundle should define excluded data classes");

    std::cout << "lazarus smoke tests passed\n";
    return 0;
}
