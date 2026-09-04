#include "arco/arcfs/host.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace arco::arcfs {
namespace {

constexpr std::uint64_t kSectorSize = 512;
constexpr std::uint64_t kNodeSize = 4096;
constexpr std::uint64_t kNodeChecksumOffset = 4092;
constexpr std::uint64_t kTreeCapacity = 4;
constexpr std::uint64_t kMaxObjects = 2048;
constexpr std::uint64_t kMaxNamespaceRows = 4096;
constexpr std::uint64_t kMaxAttributes = 4096;
constexpr std::uint64_t kChunkSize = 4096;
constexpr std::uint64_t kIncompatMultiSectorBitmap = 1ULL << 16;
constexpr std::uint64_t kRecognizedIncompat = 1 | kIncompatMultiSectorBitmap;
constexpr std::uint64_t kRecognizedRoCompat = 0;

struct FormatLayout {
    std::uint64_t total_sectors = 0;
    std::uint64_t bitmap_sector_count = 0;
    std::uint64_t checkpoint_sector = 0;
    std::uint64_t health_record_sector = 0;
    std::uint64_t root_object_tree_sector = 0;
    std::uint64_t high_water_mark = 0;
};

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint16_t le16(const std::vector<std::uint8_t>& b, std::size_t o) {
    if (o + 2 > b.size()) throw std::runtime_error("short little-endian u16 read");
    return static_cast<std::uint16_t>(b[o]) | (static_cast<std::uint16_t>(b[o + 1]) << 8);
}

std::uint32_t le32(const std::vector<std::uint8_t>& b, std::size_t o) {
    if (o + 4 > b.size()) throw std::runtime_error("short little-endian u32 read");
    return static_cast<std::uint32_t>(b[o]) | (static_cast<std::uint32_t>(b[o + 1]) << 8) |
           (static_cast<std::uint32_t>(b[o + 2]) << 16) | (static_cast<std::uint32_t>(b[o + 3]) << 24);
}

std::uint64_t le64(const std::vector<std::uint8_t>& b, std::size_t o) {
    return static_cast<std::uint64_t>(le32(b, o)) | (static_cast<std::uint64_t>(le32(b, o + 4)) << 32);
}

void put32(std::vector<std::uint8_t>& b, std::size_t o, std::uint32_t value) {
    require(o + 4 <= b.size(), "short little-endian u32 write");
    b[o + 0] = static_cast<std::uint8_t>(value & 0xFFU);
    b[o + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    b[o + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    b[o + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void put64(std::vector<std::uint8_t>& b, std::size_t o, std::uint64_t value) {
    put32(b, o, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    put32(b, o + 4, static_cast<std::uint32_t>(value >> 32ULL));
}

std::uint64_t generate_volume_id() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto value = static_cast<std::uint64_t>(now) ^ 0xA9C0B4515A17CULL;
#ifndef _WIN32
    value ^= static_cast<std::uint64_t>(::getpid()) << 32U;
#endif
    if (value == 0) value = 42;
    return value;
}

FormatLayout layout_for_format(std::uint64_t total_sectors) {
    FormatLayout layout;
    layout.total_sectors = total_sectors;
    layout.bitmap_sector_count = (total_sectors + 507) / 508;
    layout.checkpoint_sector = 4 + layout.bitmap_sector_count;
    layout.health_record_sector = layout.checkpoint_sector + 1;
    layout.root_object_tree_sector = layout.health_record_sector + 1;
    layout.high_water_mark = layout.root_object_tree_sector + 8;
    return layout;
}

void write_all(int fd, const std::vector<std::uint8_t>& data, std::uint64_t offset, const std::string& path) {
#ifdef _WIN32
    (void)fd;
    (void)data;
    (void)offset;
    (void)path;
    throw std::runtime_error("ArcFS formatting is not implemented on Windows yet");
#else
    std::size_t done = 0;
    while (done < data.size()) {
        const auto n = ::pwrite(fd, data.data() + done, data.size() - done, static_cast<off_t>(offset + done));
        if (n < 0) throw std::runtime_error("write failed for " + path + ": " + std::strerror(errno));
        if (n == 0) throw std::runtime_error("short write to " + path);
        done += static_cast<std::size_t>(n);
    }
#endif
}

std::vector<std::uint8_t> object_leaf_node() {
    std::vector<std::uint8_t> node(kNodeSize, 0);
    put64(node, 0, 1);
    put64(node, 8, 1);
    put64(node, 24, 1);
    put64(node, 32, 2);
    put64(node, 40, 0);
    put64(node, 48, 0);
    put64(node, 56, 0);
    put32(node, kNodeChecksumOffset, crc32c(node.data(), kNodeChecksumOffset));
    return node;
}

std::vector<std::uint8_t> bitmap_record(const FormatLayout& layout, std::uint64_t bitmap_index) {
    std::vector<std::uint8_t> record(kSectorSize, 0);
    const auto base_sector = bitmap_index * 508;
    for (std::uint64_t i = 0; i < 508; ++i) {
        const auto sector = base_sector + i;
        if (sector < layout.high_water_mark && sector < layout.total_sectors) record[static_cast<std::size_t>(i)] = 1;
    }
    put32(record, 508, crc32c(record.data(), 508));
    return record;
}

std::vector<std::uint8_t> checkpoint_record(const FormatLayout& layout) {
    std::vector<std::uint8_t> record(kSectorSize, 0);
    put64(record, 0, 1);
    put64(record, 8, layout.root_object_tree_sector);
    put64(record, 16, 1);
    put64(record, 24, 0);
    put64(record, 32, 0);
    put64(record, 40, layout.high_water_mark);
    put64(record, 48, 0);
    put64(record, 56, 4);
    put64(record, 64, layout.high_water_mark);
    put64(record, 72, 0);
    put64(record, 80, 0);
    put64(record, 88, 0);
    put64(record, 96, 0);
    put64(record, 104, 112);
    put32(record, 112, crc32c(record.data(), 112));
    return record;
}

std::vector<std::uint8_t> superblock_record(const FormatLayout& layout, std::uint64_t volume_id) {
    std::vector<std::uint8_t> record(kSectorSize, 0);
    std::memcpy(record.data(), "ARCFSB02", 8);
    put32(record, 8, 2);
    put32(record, 12, 0);
    put64(record, 16, volume_id);
    put64(record, 24, 1);
    put32(record, 32, kSectorSize);
    put32(record, 36, 1);
    put64(record, 40, layout.total_sectors);
    put64(record, 48, kIncompatMultiSectorBitmap);
    put64(record, 56, layout.checkpoint_sector);
    put32(record, 64, crc32c(record.data(), 64));
    return record;
}

std::vector<std::string> split_posix_path(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && path[start] == '/') ++start;
        if (start >= path.size()) break;
        const auto next = path.find('/', start);
        parts.push_back(path.substr(start, next == std::string::npos ? std::string::npos : next - start));
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return parts;
}

std::vector<std::string> split_colon_path(const std::string& path) {
    require(!path.empty() && path[0] == ':', "ArcFS paths must start with ':'");
    std::vector<std::string> parts;
    std::size_t start = 1;
    while (start < path.size()) {
        const auto next = path.find(':', start);
        const auto len = next == std::string::npos ? path.size() - start : next - start;
        require(len != 0, "ArcFS path contains an empty component");
        parts.push_back(path.substr(start, len));
        if (next == std::string::npos) break;
        start = next + 1;
    }
    return parts;
}

void validate_name(const std::string& name) {
    require(!name.empty() && name.size() <= 32, "ArcFS name must be 1..32 bytes");
    for (const auto ch : name) require(ch != '/' && ch != ':' && ch != '\0', "ArcFS name contains an invalid byte");
}

std::uint64_t chunks_for_size(std::uint64_t size) {
    return (size + kChunkSize - 1) / kChunkSize;
}

std::size_t checked_size(std::uint64_t value, const char* what) {
    require(value <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()), what);
    return static_cast<std::size_t>(value);
}

std::string clean_name(const std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t length) {
    require(length <= 32, "ArcFS namespace name exceeds host reader limit");
    require(offset + length <= bytes.size(), "ArcFS namespace name exceeds source buffer");
    std::string name;
    name.reserve(static_cast<std::size_t>(length));
    for (std::uint64_t i = 0; i < length; ++i) {
        const auto ch = bytes[offset + static_cast<std::size_t>(i)];
        require(ch != 0 && ch != ':', "ArcFS namespace name contains an invalid byte");
        name.push_back(static_cast<char>(ch));
    }
    return name;
}

} // namespace

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const bool lsb = (crc & 1U) != 0;
            crc >>= 1U;
            if (lsb) crc ^= 0x82F63B78U;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

void BlockSource::write_at(std::uint64_t, const std::uint8_t*, std::size_t) {
    throw std::runtime_error("ArcFS block source is read-only");
}

void BlockSource::flush() {
}

FileBlockSource::FileBlockSource(std::string path) : path_(std::move(path)) {}

FileBlockSource::~FileBlockSource() {
#ifndef _WIN32
    if (fd_ >= 0) ::close(fd_);
#endif
}

void FileBlockSource::ensure_open(bool writable) const {
#ifdef _WIN32
    (void)writable;
    throw std::runtime_error("ArcFS file-backed block source is not implemented on Windows yet");
#else
    if (fd_ >= 0 && (!writable || writable_)) return;
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
        writable_ = false;
    }
    const int flags = (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC;
    fd_ = ::open(path_.c_str(), flags);
    if (fd_ < 0) throw std::runtime_error("open failed for " + path_ + ": " + std::strerror(errno));
    writable_ = writable;
#endif
}

std::vector<std::uint8_t> FileBlockSource::read_at(std::uint64_t offset, std::size_t size) const {
#ifdef _WIN32
    (void)offset;
    (void)size;
    throw std::runtime_error("ArcFS file-backed block source is not implemented on Windows yet");
#else
    std::vector<std::uint8_t> data(size);
    ensure_open(false);
    std::size_t done = 0;
    while (done < size) {
        const auto n = ::pread(fd_, data.data() + done, size - done, static_cast<off_t>(offset + done));
        if (n < 0) {
            const std::string error = std::strerror(errno);
            throw std::runtime_error("read failed for " + path_ + ": " + error);
        }
        if (n == 0) throw std::runtime_error("short read from " + path_);
        done += static_cast<std::size_t>(n);
    }
    return data;
#endif
}

void FileBlockSource::write_at(std::uint64_t offset, const std::uint8_t* data, std::size_t size) {
#ifdef _WIN32
    (void)offset;
    (void)data;
    (void)size;
    throw std::runtime_error("ArcFS file-backed block writes are not implemented on Windows yet");
#else
    ensure_open(true);
    std::size_t done = 0;
    while (done < size) {
        const auto n = ::pwrite(fd_, data + done, size - done, static_cast<off_t>(offset + done));
        if (n < 0) {
            const std::string error = std::strerror(errno);
            throw std::runtime_error("write failed for " + path_ + ": " + error);
        }
        if (n == 0) throw std::runtime_error("short write to " + path_);
        done += static_cast<std::size_t>(n);
    }
#endif
}

void FileBlockSource::flush() {
#ifndef _WIN32
    ensure_open(true);
    if (::fsync(fd_) != 0) {
        const std::string error = std::strerror(errno);
        throw std::runtime_error("fsync failed for " + path_ + ": " + error);
    }
#endif
}

std::uint64_t FileBlockSource::size_bytes() const {
    return file_size_bytes(path_);
}

std::uint64_t file_size_bytes(const std::string& path) {
#ifdef _WIN32
    (void)path;
    return 0;
#else
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) throw std::runtime_error("stat failed for " + path + ": " + std::strerror(errno));
    if (S_ISBLK(st.st_mode)) {
        std::uint64_t bytes = 0;
        const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) throw std::runtime_error("open failed for " + path + ": " + std::strerror(errno));
        if (::ioctl(fd, BLKGETSIZE64, &bytes) != 0) {
            const std::string error = std::strerror(errno);
            ::close(fd);
            throw std::runtime_error("block size query failed for " + path + ": " + error);
        }
        ::close(fd);
        return bytes;
    }
    return static_cast<std::uint64_t>(st.st_size);
#endif
}

void format_file(const std::string& path, const FormatOptions& options) {
#ifdef _WIN32
    (void)path;
    (void)options;
    throw std::runtime_error("ArcFS formatting is not implemented on Windows yet");
#else
    const auto source_size = file_size_bytes(path);
    require(source_size != 0, "target has no readable size");
    require(options.byte_offset <= source_size, "format offset is beyond target size");
    const auto usable_size = options.size_bytes == 0 ? source_size - options.byte_offset : options.size_bytes;
    require(usable_size % kSectorSize == 0, "ArcFS target size must be a multiple of 512 bytes");

    const auto total_sectors = usable_size / kSectorSize;
    const auto layout = layout_for_format(total_sectors);
    require(total_sectors >= layout.high_water_mark, "target is too small for an ArcFS FMV2 volume");
    const auto volume_id = options.volume_id == 0 ? generate_volume_id() : options.volume_id;

    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) throw std::runtime_error("open failed for " + path + ": " + std::strerror(errno));
    try {
        const auto zero = std::vector<std::uint8_t>(kSectorSize, 0);
        write_all(fd, zero, options.byte_offset, path);
        write_all(fd, zero, options.byte_offset + usable_size - kSectorSize, path);

        const auto bitmap = [&]() {
            for (std::uint64_t i = 0; i < layout.bitmap_sector_count; ++i) {
                write_all(fd, bitmap_record(layout, i), options.byte_offset + (4 + i) * kSectorSize, path);
            }
        };
        bitmap();
        write_all(fd, checkpoint_record(layout), options.byte_offset + layout.checkpoint_sector * kSectorSize, path);
        write_all(fd, std::vector<std::uint8_t>(kSectorSize, 0), options.byte_offset + layout.health_record_sector * kSectorSize, path);
        write_all(fd, object_leaf_node(), options.byte_offset + layout.root_object_tree_sector * kSectorSize, path);

        const auto super = superblock_record(layout, volume_id);
        for (std::uint64_t ring_slot = 0; ring_slot < 4; ++ring_slot) {
            write_all(fd, super, options.byte_offset + ring_slot * kSectorSize, path);
        }
        if (::fsync(fd) != 0) throw std::runtime_error("fsync failed for " + path + ": " + std::strerror(errno));
        ::close(fd);
    } catch (...) {
        ::close(fd);
        throw;
    }
#endif
}

std::optional<std::uint64_t> gpt_partition_offset(const BlockSource& source, std::uint32_t partition_number) {
    if (partition_number == 0) return std::uint64_t{0};
    const auto header = source.read_at(kSectorSize, kSectorSize);
    if (std::string(reinterpret_cast<const char*>(header.data()), 8) != "EFI PART") return std::nullopt;
    const auto entry_lba = le64(header, 72);
    const auto entry_count = le32(header, 80);
    const auto entry_size = le32(header, 84);
    if (entry_size < 56 || entry_size > 4096 || entry_count == 0) return std::nullopt;
    if (partition_number > entry_count) return std::nullopt;
    const auto entry_offset = entry_lba * kSectorSize + (static_cast<std::uint64_t>(partition_number - 1) * entry_size);
    const auto entry = source.read_at(entry_offset, entry_size);
    const bool empty = std::all_of(entry.begin(), entry.begin() + 16, [](std::uint8_t b) { return b == 0; });
    if (empty) return std::nullopt;
    return le64(entry, 32) * kSectorSize;
}

std::vector<std::uint8_t> Volume::read_sector(std::uint64_t sector, std::size_t count) const {
    require(source_ != nullptr, "ArcFS volume has no block source");
    return source_->read_at(byte_offset_ + sector * kSectorSize, count * static_cast<std::size_t>(kSectorSize));
}

Superblock Volume::select_superblock() const {
    std::uint64_t best_generation = 0;
    Superblock best;
    for (std::uint64_t slot = 0; slot < 4; ++slot) {
        const auto super = read_sector(slot);
        if (std::string(reinterpret_cast<const char*>(super.data()), 8) != "ARCFSB02") continue;
        if (le32(super, 64) != crc32c(super.data(), 64)) continue;
        if (le32(super, 8) != 2 || le32(super, 32) != kSectorSize || le32(super, 36) != 1) continue;
        const auto raw_flags = le64(super, 48);
        const auto incompat = (raw_flags >> 16U) & 0xFFFFU;
        const auto ro_compat = (raw_flags >> 32U) & 0xFFFFU;
        if ((incompat & ~kRecognizedIncompat) != 0) continue;
        if ((ro_compat & ~kRecognizedRoCompat) != 0) continue;
        if ((raw_flags & kIncompatMultiSectorBitmap) == 0) continue;
        try {
            const auto checkpoint_sector = le64(super, 56);
            const auto checkpoint = read_checkpoint(checkpoint_sector);
            if (checkpoint.generation > best_generation) {
                best_generation = checkpoint.generation;
                best.sector = slot;
                best.volume_id = le64(super, 16);
                best.sequence = le64(super, 24);
                best.total_sectors = le64(super, 40);
                best.checkpoint_sector = checkpoint_sector;
                best.feature_flags = raw_flags;
            }
        } catch (const std::exception&) {
        }
    }
    require(best_generation != 0, "no valid ArcFS FMV2 superblock ring entry found");
    return best;
}

Checkpoint Volume::read_checkpoint(std::uint64_t sector) const {
    const auto data = read_sector(sector);
    std::uint64_t payload_len = 0;
    if (le32(data, 104) == crc32c(data.data(), 104)) {
        payload_len = 104;
    } else {
        const auto candidate = le64(data, 104);
        require(candidate >= 112 && candidate <= 508, "invalid ArcFS checkpoint record length");
        require(le32(data, static_cast<std::size_t>(candidate)) == crc32c(data.data(), static_cast<std::size_t>(candidate)),
                "ArcFS checkpoint checksum mismatch");
        payload_len = candidate;
    }
    require(payload_len >= 104, "ArcFS checkpoint is too old for the host reader");
    Checkpoint cp;
    cp.sector = sector;
    cp.generation = le64(data, 0);
    cp.object_root = le64(data, 8);
    cp.object_count = le64(data, 16);
    cp.namespace_root = le64(data, 24);
    cp.namespace_count = le64(data, 32);
    cp.allocation_bitmap_sector = le64(data, 56);
    cp.high_water_mark = le64(data, 64);
    cp.attribute_tree_root = le64(data, 72);
    cp.attribute_tree_count = le64(data, 80);
    cp.extent_tree_root = le64(data, 88);
    cp.extent_tree_count = le64(data, 96);
    require(cp.object_count <= kMaxObjects, "ArcFS object count exceeds host reader limit");
    require(cp.namespace_count <= kMaxNamespaceRows, "ArcFS namespace count exceeds host reader limit");
    require(cp.attribute_tree_count <= kMaxAttributes, "ArcFS attribute count exceeds host reader limit");
    return cp;
}

void Volume::collect_tree_node(std::uint64_t sector, std::size_t entry_size, std::uint64_t max_count,
                               std::vector<std::uint8_t>& entries) const {
    const auto node = read_sector(sector, 8);
    require(le32(node, kNodeChecksumOffset) == crc32c(node.data(), kNodeChecksumOffset), "ArcFS tree node checksum mismatch");
    const auto node_type = le64(node, 0);
    const auto key_count = le64(node, 8);
    require(key_count <= kTreeCapacity, "ArcFS tree node key count exceeds host reader fanout");
    if (node_type == 1) {
        require(entries.size() / entry_size + key_count <= max_count, "ArcFS tree contains too many entries");
        for (std::uint64_t i = 0; i < key_count; ++i) {
            const auto offset = 24 + static_cast<std::size_t>(i) * entry_size;
            require(offset + entry_size <= kNodeChecksumOffset, "ArcFS tree leaf entry exceeds node payload");
            entries.insert(entries.end(), node.begin() + static_cast<std::ptrdiff_t>(offset),
                           node.begin() + static_cast<std::ptrdiff_t>(offset + entry_size));
        }
        return;
    }
    require(node_type == 2, "ArcFS tree node type is invalid");
    const auto children_base = 24 + kTreeCapacity * 8;
    for (std::uint64_t i = 0; i < key_count; ++i) {
        const auto child = le64(node, static_cast<std::size_t>(children_base + i * 8));
        require(child != 0, "ArcFS internal tree node has a null child");
        collect_tree_node(child, entry_size, max_count, entries);
    }
}

std::vector<std::uint8_t> Volume::collect_tree(std::uint64_t root_sector, std::uint64_t expected_count,
                                               std::size_t entry_size, std::uint64_t max_count) const {
    std::vector<std::uint8_t> entries;
    if (expected_count == 0) {
        require(root_sector == 0, "ArcFS empty tree has a nonzero root");
        return entries;
    }
    require(root_sector != 0, "ArcFS nonempty tree has a zero root");
    collect_tree_node(root_sector, entry_size, max_count, entries);
    require(entries.size() / entry_size == expected_count, "ArcFS tree entry count mismatch");
    return entries;
}

void Volume::mount(std::shared_ptr<BlockSource> source, MountOptions options) {
    source_ = std::move(source);
    byte_offset_ = options.byte_offset;
    superblock_ = select_superblock();
    checkpoint_ = read_checkpoint(superblock_.checkpoint_sector);
    objects_.clear();
    namespace_entries_.clear();
    attributes_.clear();
    extents_.clear();
    dirty_files_.clear();
    metadata_dirty_ = false;
    next_oid_ = 2;

    const auto object_entries = collect_tree(checkpoint_.object_root, checkpoint_.object_count, 40, kMaxObjects);
    for (std::size_t o = 0; o < object_entries.size(); o += 40) {
        Object obj;
        obj.oid = le64(object_entries, o);
        obj.type = le64(object_entries, o + 8);
        obj.size = le64(object_entries, o + 16);
        obj.data_slot = le64(object_entries, o + 24);
        obj.oid_high = le64(object_entries, o + 32);
        require(obj.oid != 0 && (obj.type == 1 || obj.type == 2), "ArcFS object entry is invalid");
        objects_[obj.oid] = obj;
        if (obj.oid >= next_oid_) next_oid_ = obj.oid + 1;
    }
    require(objects_.count(1) != 0 && objects_.at(1).type == 2, "ArcFS root directory is missing");

    const auto ns_entries = collect_tree(checkpoint_.namespace_root, checkpoint_.namespace_count, 56, kMaxNamespaceRows);
    for (std::size_t o = 0; o < ns_entries.size(); o += 56) {
        DirectoryEntry entry;
        entry.parent_oid = le64(ns_entries, o);
        entry.name = clean_name(ns_entries, o + 8, le64(ns_entries, o + 40));
        entry.child_oid = le64(ns_entries, o + 48);
        require(objects_.count(entry.parent_oid) != 0 && objects_.count(entry.child_oid) != 0, "ArcFS namespace entry references a missing object");
        namespace_entries_.push_back(std::move(entry));
    }

    std::uint64_t max_referenced_extents = 0;
    for (const auto& [_, obj] : objects_) {
        if (obj.type != 1) continue;
        const auto chunks = chunks_for_size(obj.size);
        require(max_referenced_extents <= std::numeric_limits<std::uint64_t>::max() - chunks, "ArcFS file extents overflow host reader limit");
        max_referenced_extents += chunks;
    }
    const auto max_addressable_extents = superblock_.total_sectors / 8;
    require(checkpoint_.extent_tree_count <= max_referenced_extents, "ArcFS extent count exceeds referenced file data");
    require(checkpoint_.extent_tree_count <= max_addressable_extents, "ArcFS extent count exceeds volume capacity");
    const auto extent_entries = collect_tree(checkpoint_.extent_tree_root, checkpoint_.extent_tree_count, 32, max_referenced_extents);
    for (std::size_t o = 0; o < extent_entries.size(); o += 32) {
        const auto data_slot = le64(extent_entries, o);
        const auto chunk = le64(extent_entries, o + 8);
        const auto sector = le64(extent_entries, o + 16);
        require(sector != 0 && sector + 8 <= superblock_.total_sectors, "ArcFS extent entry is invalid");
        extents_[{data_slot, chunk}] = sector;
    }

    const auto attr_entries = collect_tree(checkpoint_.attribute_tree_root, checkpoint_.attribute_tree_count, 48, kMaxAttributes);
    for (std::size_t o = 0; o < attr_entries.size(); o += 48) {
        Attribute attr;
        attr.oid = le64(attr_entries, o);
        attr.oid_high = le64(attr_entries, o + 8);
        attr.id = le64(attr_entries, o + 16);
        attr.type = le64(attr_entries, o + 24);
        attr.value_low = le64(attr_entries, o + 32);
        attr.value_high = le64(attr_entries, o + 40);
        require(objects_.count(attr.oid) != 0, "ArcFS attribute references a missing object");
        attributes_.push_back(attr);
    }
}

const Object* Volume::object(std::uint64_t oid) const {
    const auto it = objects_.find(oid);
    return it == objects_.end() ? nullptr : &it->second;
}

std::vector<DirectoryEntry> Volume::children(std::uint64_t parent_oid) const {
    std::vector<DirectoryEntry> result;
    for (const auto& entry : namespace_entries_) {
        if (entry.parent_oid == parent_oid) result.push_back(entry);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return result;
}

std::optional<std::uint64_t> Volume::resolve_colon_path(const std::string& path) const {
    std::uint64_t current = 1;
    for (const auto& part : split_colon_path(path)) {
        bool found = false;
        for (const auto& entry : namespace_entries_) {
            if (entry.parent_oid == current && entry.name == part) {
                current = entry.child_oid;
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return current;
}

std::optional<std::uint64_t> Volume::resolve_posix_path(const std::string& path) const {
    std::uint64_t current = 1;
    for (const auto& part : split_posix_path(path)) {
        bool found = false;
        for (const auto& entry : namespace_entries_) {
            if (entry.parent_oid == current && entry.name == part) {
                current = entry.child_oid;
                found = true;
                break;
            }
        }
        if (!found) return std::nullopt;
    }
    return current;
}

std::vector<std::uint8_t> Volume::read_file(std::uint64_t oid, std::uint64_t offset, std::size_t size) const {
    const auto* obj = object(oid);
    require(obj != nullptr && obj->type == 1, "ArcFS object is not a file");
    const auto dirty = dirty_files_.find(oid);
    if (dirty != dirty_files_.end()) {
        if (offset >= dirty->second.size() || size == 0) return {};
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(dirty->second.size() - offset, size));
        return std::vector<std::uint8_t>(dirty->second.begin() + static_cast<std::ptrdiff_t>(offset),
                                         dirty->second.begin() + static_cast<std::ptrdiff_t>(offset + take));
    }
    if (offset >= obj->size || size == 0) return {};
    const auto available = obj->size - offset;
    const auto to_read = static_cast<std::size_t>(std::min<std::uint64_t>(available, size));
    std::vector<std::uint8_t> out(to_read, 0);
    std::uint64_t logical = offset;
    std::size_t written = 0;
    while (written < to_read) {
        const auto chunk = logical / kChunkSize;
        const auto in_chunk = logical % kChunkSize;
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(kChunkSize - in_chunk, to_read - written));
        const auto extent = extents_.find({obj->data_slot, chunk});
        if (extent != extents_.end()) {
            const auto data = source_->read_at(byte_offset_ + extent->second * kSectorSize + in_chunk, take);
            std::copy(data.begin(), data.end(), out.begin() + static_cast<std::ptrdiff_t>(written));
        }
        logical += take;
        written += take;
    }
    return out;
}

std::uint64_t Volume::create_file(std::uint64_t parent_oid, const std::string& name) {
    validate_name(name);
    const auto* parent = object(parent_oid);
    require(parent != nullptr && parent->type == 2, "ArcFS parent is not a directory");
    require(!resolve_posix_path("/" + name) || parent_oid != 1, "ArcFS name already exists");
    for (const auto& entry : namespace_entries_) {
        require(!(entry.parent_oid == parent_oid && entry.name == name), "ArcFS name already exists");
    }
    const auto oid = next_oid_++;
    Object obj;
    obj.oid = oid;
    obj.type = 1;
    obj.size = 0;
    obj.data_slot = oid;
    objects_[oid] = obj;
    namespace_entries_.push_back({parent_oid, name, oid});
    dirty_files_[oid] = {};
    metadata_dirty_ = true;
    return oid;
}

std::uint64_t Volume::create_directory(std::uint64_t parent_oid, const std::string& name) {
    validate_name(name);
    const auto* parent = object(parent_oid);
    require(parent != nullptr && parent->type == 2, "ArcFS parent is not a directory");
    for (const auto& entry : namespace_entries_) {
        require(!(entry.parent_oid == parent_oid && entry.name == name), "ArcFS name already exists");
    }
    const auto oid = next_oid_++;
    Object obj;
    obj.oid = oid;
    obj.type = 2;
    obj.size = 0;
    obj.data_slot = 0;
    objects_[oid] = obj;
    namespace_entries_.push_back({parent_oid, name, oid});
    metadata_dirty_ = true;
    return oid;
}

void Volume::remove_child(std::uint64_t parent_oid, const std::string& name) {
    const auto it = std::find_if(namespace_entries_.begin(), namespace_entries_.end(),
                                 [&](const auto& entry) { return entry.parent_oid == parent_oid && entry.name == name; });
    require(it != namespace_entries_.end(), "ArcFS child does not exist");
    const auto oid = it->child_oid;
    const auto* obj = object(oid);
    require(obj != nullptr, "ArcFS child object is missing");
    if (obj->type == 2) {
        for (const auto& entry : namespace_entries_) require(entry.parent_oid != oid, "ArcFS directory is not empty");
    }
    namespace_entries_.erase(it);
    objects_.erase(oid);
    dirty_files_.erase(oid);
    metadata_dirty_ = true;
}

void Volume::rename_child(std::uint64_t old_parent_oid, const std::string& old_name,
                          std::uint64_t new_parent_oid, const std::string& new_name) {
    validate_name(new_name);
    const auto* new_parent = object(new_parent_oid);
    require(new_parent != nullptr && new_parent->type == 2, "ArcFS new parent is not a directory");
    auto it = std::find_if(namespace_entries_.begin(), namespace_entries_.end(),
                           [&](const auto& entry) { return entry.parent_oid == old_parent_oid && entry.name == old_name; });
    require(it != namespace_entries_.end(), "ArcFS source child does not exist");
    for (const auto& entry : namespace_entries_) {
        require(!(entry.parent_oid == new_parent_oid && entry.name == new_name), "ArcFS destination already exists");
    }
    it->parent_oid = new_parent_oid;
    it->name = new_name;
    metadata_dirty_ = true;
}

void Volume::resize_file(std::uint64_t oid, std::uint64_t size) {
    auto obj_it = objects_.find(oid);
    require(obj_it != objects_.end() && obj_it->second.type == 1, "ArcFS object is not a file");
    require(chunks_for_size(size) <= superblock_.total_sectors / 8, "ArcFS file exceeds volume capacity");
    const auto host_size = checked_size(size, "ArcFS file exceeds host addressable size");
    auto& bytes = dirty_files_[oid];
    if (bytes.empty() && obj_it->second.size != 0) bytes = read_file(oid, 0, checked_size(obj_it->second.size, "ArcFS file exceeds host addressable size"));
    bytes.resize(host_size, 0);
    obj_it->second.size = size;
    metadata_dirty_ = true;
}

void Volume::write_file(std::uint64_t oid, std::uint64_t offset, const std::uint8_t* data, std::size_t size) {
    auto obj_it = objects_.find(oid);
    require(obj_it != objects_.end() && obj_it->second.type == 1, "ArcFS object is not a file");
    require(offset <= std::numeric_limits<std::uint64_t>::max() - size, "ArcFS write offset overflows");
    const auto end = offset + size;
    require(chunks_for_size(end) <= superblock_.total_sectors / 8, "ArcFS file exceeds volume capacity");
    if (end > obj_it->second.size) resize_file(oid, end);
    auto& bytes = dirty_files_[oid];
    if (bytes.empty() && obj_it->second.size != 0) bytes = read_file(oid, 0, checked_size(obj_it->second.size, "ArcFS file exceeds host addressable size"));
    if (bytes.size() < end) bytes.resize(checked_size(end, "ArcFS file exceeds host addressable size"), 0);
    std::copy(data, data + size, bytes.begin() + static_cast<std::ptrdiff_t>(checked_size(offset, "ArcFS write offset exceeds host addressable size")));
}

void Volume::commit(bool sync) {
    require(source_ != nullptr, "ArcFS volume has no block source");
    if (!metadata_dirty_ && dirty_files_.empty()) return;
    std::uint64_t cursor = checkpoint_.high_water_mark;
    std::vector<std::vector<std::uint8_t>> extent_entries;
    std::map<std::uint64_t, std::uint64_t> new_data_slots;

    auto write_sector_run = [&](std::uint64_t sector, const std::vector<std::uint8_t>& data) {
        source_->write_at(byte_offset_ + sector * kSectorSize, data.data(), data.size());
    };
    auto allocate_run = [&](std::uint64_t sectors) {
        const auto sector = cursor;
        cursor += sectors;
        require(cursor <= superblock_.total_sectors, "ArcFS volume is full");
        return sector;
    };
    auto append_extent = [&](std::uint64_t data_slot, std::uint64_t chunk, std::uint64_t sector) {
        std::vector<std::uint8_t> entry(32, 0);
        put64(entry, 0, data_slot);
        put64(entry, 8, chunk);
        put64(entry, 16, sector);
        extent_entries.push_back(std::move(entry));
    };

    for (auto& [oid, obj] : objects_) {
        if (obj.type != 1) continue;
        const auto dirty = dirty_files_.find(oid);
        const auto old_slot = obj.data_slot;
        const auto slot = oid;
        new_data_slots[oid] = slot;
        obj.data_slot = slot;
        if (dirty == dirty_files_.end()) {
            const auto chunks = chunks_for_size(obj.size);
            for (std::uint64_t chunk = 0; chunk < chunks; ++chunk) {
                const auto extent = extents_.find({old_slot, chunk});
                require(extent != extents_.end(), "ArcFS file is missing an extent");
                append_extent(slot, chunk, extent->second);
            }
            continue;
        }
        const auto& data = dirty->second;
        obj.size = data.size();
        for (std::uint64_t chunk = 0; chunk * kChunkSize < data.size(); ++chunk) {
            std::vector<std::uint8_t> block(kNodeSize, 0);
            const auto begin = static_cast<std::size_t>(chunk * kChunkSize);
            const auto take = std::min<std::size_t>(kChunkSize, data.size() - begin);
            std::copy(data.begin() + static_cast<std::ptrdiff_t>(begin),
                      data.begin() + static_cast<std::ptrdiff_t>(begin + take), block.begin());
            const auto sector = allocate_run(8);
            write_sector_run(sector, block);
            append_extent(slot, chunk, sector);
        }
    }

    auto write_tree = [&](const std::vector<std::vector<std::uint8_t>>& rows, std::size_t entry_size) -> std::uint64_t {
        if (rows.empty()) return 0;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> level;
        for (std::size_t offset = 0; offset < rows.size(); offset += kTreeCapacity) {
            const auto count = std::min<std::size_t>(kTreeCapacity, rows.size() - offset);
            std::vector<std::uint8_t> node(kNodeSize, 0);
            put64(node, 0, 1);
            put64(node, 8, count);
            for (std::size_t i = 0; i < count; ++i) {
                std::copy(rows[offset + i].begin(), rows[offset + i].end(),
                          node.begin() + static_cast<std::ptrdiff_t>(24 + i * entry_size));
            }
            put32(node, kNodeChecksumOffset, crc32c(node.data(), kNodeChecksumOffset));
            const auto sector = allocate_run(8);
            write_sector_run(sector, node);
            level.push_back({le64(rows[offset], 0), sector});
        }
        while (level.size() > 1) {
            std::vector<std::pair<std::uint64_t, std::uint64_t>> next;
            for (std::size_t offset = 0; offset < level.size(); offset += kTreeCapacity) {
                const auto count = std::min<std::size_t>(kTreeCapacity, level.size() - offset);
                std::vector<std::uint8_t> node(kNodeSize, 0);
                put64(node, 0, 2);
                put64(node, 8, count);
                for (std::size_t i = 0; i < count; ++i) {
                    put64(node, 24 + i * 8, level[offset + i].first);
                    put64(node, 24 + kTreeCapacity * 8 + i * 8, level[offset + i].second);
                }
                put32(node, kNodeChecksumOffset, crc32c(node.data(), kNodeChecksumOffset));
                const auto sector = allocate_run(8);
                write_sector_run(sector, node);
                next.push_back({level[offset].first, sector});
            }
            level = std::move(next);
        }
        return level.front().second;
    };

    std::vector<std::vector<std::uint8_t>> object_rows;
    for (const auto& [_, obj] : objects_) {
        std::vector<std::uint8_t> row(40, 0);
        put64(row, 0, obj.oid);
        put64(row, 8, obj.type);
        put64(row, 16, obj.size);
        put64(row, 24, obj.data_slot);
        put64(row, 32, obj.oid_high);
        object_rows.push_back(std::move(row));
    }
    std::sort(object_rows.begin(), object_rows.end(), [](const auto& a, const auto& b) { return le64(a, 0) < le64(b, 0); });

    std::vector<std::vector<std::uint8_t>> namespace_rows;
    for (const auto& entry : namespace_entries_) {
        std::vector<std::uint8_t> row(56, 0);
        put64(row, 0, entry.parent_oid);
        std::copy(entry.name.begin(), entry.name.end(), row.begin() + 8);
        put64(row, 40, entry.name.size());
        put64(row, 48, entry.child_oid);
        namespace_rows.push_back(std::move(row));
    }
    std::sort(namespace_rows.begin(), namespace_rows.end(), [](const auto& a, const auto& b) {
        if (le64(a, 0) != le64(b, 0)) return le64(a, 0) < le64(b, 0);
        return std::lexicographical_compare(a.begin() + 8, a.begin() + 40, b.begin() + 8, b.begin() + 40);
    });
    std::sort(extent_entries.begin(), extent_entries.end(), [](const auto& a, const auto& b) {
        if (le64(a, 0) != le64(b, 0)) return le64(a, 0) < le64(b, 0);
        return le64(a, 8) < le64(b, 8);
    });

    const auto extent_root = write_tree(extent_entries, 32);
    const auto object_root = write_tree(object_rows, 40);
    const auto namespace_root = write_tree(namespace_rows, 56);
    const auto attr_root = std::uint64_t{0};
    const auto bitmap_sector = cursor;
    std::uint64_t bitmap_count = 1;
    while (true) {
        const auto final_high_water = cursor + bitmap_count + 1;
        const auto required = (final_high_water + 507) / 508;
        if (required == bitmap_count) break;
        bitmap_count = required;
    }
    cursor += bitmap_count;
    require(cursor < superblock_.total_sectors, "ArcFS volume is full");
    const auto checkpoint_sector = cursor++;
    const auto final_high_water = cursor;
    FormatLayout layout;
    layout.total_sectors = superblock_.total_sectors;
    layout.high_water_mark = final_high_water;
    for (std::uint64_t i = 0; i < bitmap_count; ++i) write_sector_run(bitmap_sector + i, bitmap_record(layout, i));
    auto checkpoint = checkpoint_record(layout);
    put64(checkpoint, 0, checkpoint_.generation + 1);
    put64(checkpoint, 8, object_root);
    put64(checkpoint, 16, object_rows.size());
    put64(checkpoint, 24, namespace_root);
    put64(checkpoint, 32, namespace_rows.size());
    put64(checkpoint, 40, final_high_water);
    put64(checkpoint, 48, checkpoint_.sector);
    put64(checkpoint, 56, bitmap_sector);
    put64(checkpoint, 64, final_high_water);
    put64(checkpoint, 72, attr_root);
    put64(checkpoint, 80, 0);
    put64(checkpoint, 88, extent_root);
    put64(checkpoint, 96, extent_entries.size());
    put64(checkpoint, 104, 112);
    put32(checkpoint, 112, crc32c(checkpoint.data(), 112));
    write_sector_run(checkpoint_sector, checkpoint);

    FormatLayout super_layout;
    super_layout.total_sectors = superblock_.total_sectors;
    super_layout.checkpoint_sector = checkpoint_sector;
    const auto super = superblock_record(super_layout, superblock_.volume_id);
    for (std::uint64_t ring_slot = 0; ring_slot < 4; ++ring_slot) write_sector_run(ring_slot, super);
    if (sync) source_->flush();

    MountOptions options;
    options.byte_offset = byte_offset_;
    mount(source_, options);
}

} // namespace arco::arcfs
