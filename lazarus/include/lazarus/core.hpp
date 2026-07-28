#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lazarus {

enum class DeviceRole {
    Unknown,
    SourceOnly,
    DestinationOnly,
    ImageStorage,
    RemovableMedia,
    Ignored,
    SystemDisk,
};

enum class ImagingMode {
    Standard,
    Raw,
    Rescue,
};

enum class CompressionMode {
    None,
    Zstd,
};

enum class Severity {
    Info,
    Warning,
    Blocker,
};

enum class PartitionKind {
    Unknown,
    EfiSystem,
    MicrosoftReserved,
    WindowsBasicData,
    WindowsRecovery,
};

enum class FileSystemKind {
    Unknown,
    Ntfs,
    Fat12,
    Fat16,
    Fat32,
    Exfat,
};

struct JobInfo {
    std::string ticket_number;
    std::string customer_name;
    std::string technician;
    std::string purpose;
};

struct DeviceIdentity {
    std::string linux_path;
    std::string physical_path;
    std::string by_id_path;
    std::string by_path;
    std::string model;
    std::string serial;
    std::string serial_ending;
    std::string transport;
    std::uint64_t size_bytes = 0;
    std::uint32_t logical_block_size = 512;
    DeviceRole bench_role = DeviceRole::Unknown;
    bool is_system_disk = false;
    bool removable = false;
    bool rotational = false;
    std::vector<std::string> partitions;
    std::string port_path;
};

struct PortLabel {
    std::string identity;
    std::string label;
};

struct BrandingTheme {
    std::string name = "Lazarus Default Theme";
    std::string product_name = "Arcology Lazarus";
    std::string subtitle = "Offline Imaging | Recovery | Hardware Migration";
    std::string accent = "#f39a22";
    std::string background = "#10161b";
    std::string surface = "#171f25";
    std::string text = "#edf1f3";
    std::string icon_color = "#f39a22";
    std::string logo_path;
    std::string report_footer = "Generated locally by Arcology Lazarus. SMART results describe reported device facts.";
};

struct BenchProfile {
    std::string name;
    std::string image_storage_path;
    std::vector<std::string> image_storage_paths;
    std::vector<std::string> source_only_paths;
    std::vector<std::string> destination_only_paths;
    std::vector<std::string> ignored_paths;
    std::vector<std::string> removable_media_paths;
    std::vector<PortLabel> port_labels;
    std::string image_storage_device;
    std::string image_storage_volume;
    std::vector<std::string> image_storage_port_paths;
    BrandingTheme branding;
};

struct SafetyFinding {
    Severity severity = Severity::Info;
    std::string code;
    std::string observed;
    std::string action;
};

struct ImagePlan {
    JobInfo job;
    DeviceIdentity source;
    std::string image_storage_path;
    std::string image_extension = ".laz";
    ImagingMode mode = ImagingMode::Standard;
    bool source_open_read_only = true;
    std::vector<SafetyFinding> findings;
};

struct VerificationStage {
    std::string name;
    bool passed = false;
    std::vector<std::string> facts;
};

struct CompletionReport {
    JobInfo job;
    std::vector<VerificationStage> verification;
    std::vector<SafetyFinding> findings;
};

struct SupportBundleManifest {
    std::vector<std::string> include_files;
    std::vector<std::string> excluded_data_classes;
};

struct ProgressEvent {
    std::string operation;
    std::string phase;
    std::string message;
    std::uint64_t bytes_done = 0;
    std::uint64_t bytes_total = 0;
    std::uint64_t chunks_done = 0;
    std::uint64_t chunks_total = 0;
    bool indeterminate = false;
};

using ProgressCallback = std::function<void(const ProgressEvent&)>;

struct SourceRead {
    std::vector<std::byte> data;
    std::string error;
};

class SourceReadHandle {
public:
    SourceReadHandle() = default;
    explicit SourceReadHandle(int fd, DeviceIdentity device);
    ~SourceReadHandle();

    SourceReadHandle(const SourceReadHandle&) = delete;
    SourceReadHandle& operator=(const SourceReadHandle&) = delete;
    SourceReadHandle(SourceReadHandle&& other) noexcept;
    SourceReadHandle& operator=(SourceReadHandle&& other) noexcept;

    bool is_open() const;
    int native_handle() const;
    const DeviceIdentity& device() const;
    SourceRead read_at(std::uint64_t offset, std::size_t byte_count) const;
    void close();

private:
    int fd_ = -1;
    DeviceIdentity device_;
};

class DestinationWriteHandle {
public:
    DestinationWriteHandle() = default;
    explicit DestinationWriteHandle(int fd, DeviceIdentity device);
    ~DestinationWriteHandle();

    DestinationWriteHandle(const DestinationWriteHandle&) = delete;
    DestinationWriteHandle& operator=(const DestinationWriteHandle&) = delete;
    DestinationWriteHandle(DestinationWriteHandle&& other) noexcept;
    DestinationWriteHandle& operator=(DestinationWriteHandle&& other) noexcept;

    bool is_open() const;
    int native_handle() const;
    const DeviceIdentity& device() const;
    bool write_at(std::uint64_t offset, const std::vector<std::byte>& data, std::string& error) const;
    bool flush(std::string& error) const;
    void close();

private:
    int fd_ = -1;
    DeviceIdentity device_;
};

struct SourceOpenResult {
    SourceReadHandle handle;
    std::vector<SafetyFinding> findings;
};

struct DestinationOpenResult {
    DestinationWriteHandle handle;
    std::vector<SafetyFinding> findings;
};

struct PartitionInfo {
    std::uint32_t number = 0;
    PartitionKind kind = PartitionKind::Unknown;
    std::string type_guid;
    std::string unique_guid;
    std::string name;
    std::uint64_t first_lba = 0;
    std::uint64_t last_lba = 0;
    std::uint64_t size_bytes = 0;
    FileSystemKind filesystem = FileSystemKind::Unknown;
    bool ntfs_detected = false;
};

struct DiskInspection {
    DeviceIdentity source;
    std::uint32_t logical_block_size = 512;
    bool first_sector_read = false;
    bool mbr_signature_valid = false;
    bool protective_mbr = false;
    bool mbr_detected = false;
    bool gpt_detected = false;
    bool gpt_header_valid = false;
    std::uint64_t first_usable_lba = 0;
    std::uint64_t last_usable_lba = 0;
    std::vector<PartitionInfo> partitions;
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

struct ImageWriteOptions {
    std::string output_directory;
    std::size_t chunk_size = 4 * 1024 * 1024;
    std::uint64_t max_bytes = 0;
    CompressionMode compression = CompressionMode::None;
    ImagingMode mode = ImagingMode::Raw;
    std::size_t rescue_minimum_read = 4096;
    std::uint32_t rescue_retries = 2;
    ProgressCallback progress;
};

struct UnreadableRange {
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct ImageWriteResult {
    std::string output_directory;
    std::uint64_t bytes_read = 0;
    std::uint64_t bytes_written = 0;
    std::uint64_t bytes_stored = 0;
    std::uint64_t chunks_written = 0;
    std::uint64_t resumed_bytes = 0;
    std::uint64_t resumed_chunks = 0;
    bool incomplete_marker_created = false;
    bool resumed = false;
    bool finalized = false;
    bool completed_with_warnings = false;
    std::vector<UnreadableRange> unreadable_ranges;
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

struct ImageVerificationResult {
    std::string image_directory;
    std::uint64_t expected_bytes = 0;
    std::uint64_t actual_bytes = 0;
    std::uint64_t stored_bytes = 0;
    std::uint64_t chunks_verified = 0;
    bool metadata_read = false;
    bool finalized_marker_present = false;
    bool incomplete_marker_present = false;
    bool raw_stream_length_valid = false;
    bool hashes_valid = false;
    bool partition_table_valid = false;
    bool filesystem_readable = false;
    bool ntfs_mft_readable = false;
    bool bitlocker_detected = false;
    bool verified = false;
    std::vector<UnreadableRange> unreadable_ranges;
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

struct ImageRestoreOptions {
    std::string image_directory;
    std::size_t chunk_size = 4 * 1024 * 1024;
    std::string confirmation;
    ProgressCallback progress;
};

struct ImageRestoreResult {
    std::string image_directory;
    DeviceIdentity destination;
    std::uint64_t bytes_written = 0;
    std::uint64_t bytes_verified = 0;
    std::uint64_t chunks_written = 0;
    bool image_verified_before_restore = false;
    bool flushed = false;
    bool readback_verified = false;
    bool destination_layout_validated = false;
    bool restored = false;
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

struct ImageBrowseCacheOptions {
    std::string image_directory;
    std::string output_path;
    ProgressCallback progress;
};

struct ImageBrowseCacheResult {
    std::string image_directory;
    std::string output_path;
    std::uint64_t logical_bytes = 0;
    std::uint64_t allocated_bytes_written = 0;
    std::uint64_t chunks_reconstructed = 0;
    bool image_verified = false;
    bool reused_existing_cache = false;
    bool prepared = false;
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

struct SmartAttribute {
    std::string name;
    std::int64_t value = 0;
    bool present = false;
};

struct SmartDiagnosticResult {
    DeviceIdentity device;
    bool smartctl_available = false;
    bool command_completed = false;
    int exit_code = -1;
    std::string raw_json;
    std::string model;
    std::string serial;
    std::string health = "unknown";
    SmartAttribute power_on_hours{"power_on_hours", 0, false};
    SmartAttribute temperature_celsius{"temperature_celsius", 0, false};
    SmartAttribute reallocated_sectors{"reallocated_sectors", 0, false};
    SmartAttribute pending_sectors{"pending_sectors", 0, false};
    SmartAttribute uncorrectable_errors{"uncorrectable_errors", 0, false};
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

struct DriverInfMetadata {
    std::string path;
    std::string provider;
    std::string class_name;
    std::string class_guid;
    std::string driver_version;
    std::string catalog_file;
    std::vector<std::string> hardware_ids;
    std::string service_name;
    std::string service_binary;
    std::string service_binary_path;
    std::string load_order_group;
    std::uint32_t service_type = 0;
    std::uint32_t start_type = 0;
    std::uint32_t error_control = 0;
    bool windows_nt_signature = false;
    bool amd64_decorated = false;
    bool catalog_present = false;
    bool storage_controller_driver = false;
    bool boot_service_present = false;
};

struct DriverPackageInspection {
    std::string package_root;
    std::vector<DriverInfMetadata> inf_files;
    std::vector<std::string> hardware_ids;
    bool symlink_detected = false;
    bool contains_storage_controller_driver = false;
    bool ready_for_windows_signature_verification = false;
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

struct DriverMigrationPlanOptions {
    std::vector<std::string> target_hardware_ids;
    std::vector<std::string> requested_removals;
    bool removal_risk_acknowledged = false;
};

struct DriverPlanItem {
    std::string action;
    std::string inf_path;
    std::string published_name;
    std::vector<std::string> matching_hardware_ids;
    std::string service_name;
    std::string service_binary;
    std::string service_binary_path;
    std::string load_order_group;
    std::uint32_t service_type = 0;
    std::uint32_t start_type = 0;
    std::uint32_t error_control = 0;
    std::string reason;
};

struct DriverMigrationPlan {
    std::vector<std::string> target_hardware_ids;
    std::vector<DriverPlanItem> actions;
    bool matching_storage_driver_found = false;
    bool additions_precede_removals = true;
    bool requires_windows_pe = false;
    bool ready_for_servicing = false;
    std::vector<std::string> facts;
    std::vector<SafetyFinding> findings;
};

std::string version();
std::string to_string(DeviceRole role);
std::string to_string(ImagingMode mode);
std::string to_string(CompressionMode mode);
std::string to_string(Severity severity);
std::string to_string(PartitionKind kind);
std::string to_string(FileSystemKind kind);

bool is_complete(const JobInfo& job);
std::vector<SafetyFinding> validate_job(const JobInfo& job);
std::vector<SafetyFinding> validate_bench_profile(const BenchProfile& bench);
std::vector<SafetyFinding> validate_source_device(const BenchProfile& bench, const DeviceIdentity& device);
std::vector<SafetyFinding> validate_destination_device(const BenchProfile& bench, const DeviceIdentity& device);
DeviceRole role_for_device(const BenchProfile& bench, const DeviceIdentity& device);
std::string label_for_device(const BenchProfile& bench, const DeviceIdentity& device);
std::string physical_port_identity(const std::string& by_path);
DeviceIdentity apply_bench_policy(const BenchProfile& bench, DeviceIdentity device);
std::vector<DeviceIdentity> apply_bench_policy(const BenchProfile& bench, const std::vector<DeviceIdentity>& devices);
BenchProfile load_bench_profile(const std::string& path);
ImagePlan create_backup_plan(const JobInfo& job, const BenchProfile& bench, const DeviceIdentity& source, ImagingMode mode);
SourceOpenResult open_source_read_only(const BenchProfile& bench, DeviceIdentity source);
DestinationOpenResult open_destination_write_only(const BenchProfile& bench, DeviceIdentity destination);
DiskInspection inspect_source_disk(const SourceReadHandle& source);
ImageWriteResult write_directory_image(const JobInfo& job, const SourceReadHandle& source, const DiskInspection& inspection, const ImageWriteOptions& options);
ImageVerificationResult verify_directory_image(const std::string& image_directory);
ImageVerificationResult verify_directory_image(const std::string& image_directory, ProgressCallback progress);
ImageBrowseCacheResult prepare_image_browse_cache(const ImageBrowseCacheOptions& options);
ImageRestoreResult restore_directory_image(const BenchProfile& bench, DeviceIdentity destination, const ImageRestoreOptions& options);
SmartDiagnosticResult parse_smartctl_json(const DeviceIdentity& device, const std::string& json, int exit_code);
SmartDiagnosticResult collect_smart_diagnostics(DeviceIdentity device);
DriverPackageInspection inspect_driver_package(const std::string& package_root);
DriverMigrationPlan create_driver_migration_plan(
    const DriverMigrationPlanOptions& options,
    const std::vector<DriverPackageInspection>& packages);
SupportBundleManifest create_support_bundle_manifest();
std::vector<DeviceIdentity> discover_block_devices();

}  // namespace lazarus
