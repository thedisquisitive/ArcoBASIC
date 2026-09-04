#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace arco::arcfs {

struct Object {
    std::uint64_t oid = 0;
    std::uint64_t oid_high = 0;
    std::uint64_t type = 0;
    std::uint64_t size = 0;
    std::uint64_t data_slot = 0;
};

struct DirectoryEntry {
    std::uint64_t parent_oid = 0;
    std::string name;
    std::uint64_t child_oid = 0;
};

struct Attribute {
    std::uint64_t oid = 0;
    std::uint64_t oid_high = 0;
    std::uint64_t id = 0;
    std::uint64_t type = 0;
    std::uint64_t value_low = 0;
    std::uint64_t value_high = 0;
};

struct Checkpoint {
    std::uint64_t sector = 0;
    std::uint64_t generation = 0;
    std::uint64_t object_root = 0;
    std::uint64_t object_count = 0;
    std::uint64_t namespace_root = 0;
    std::uint64_t namespace_count = 0;
    std::uint64_t allocation_bitmap_sector = 0;
    std::uint64_t high_water_mark = 0;
    std::uint64_t attribute_tree_root = 0;
    std::uint64_t attribute_tree_count = 0;
    std::uint64_t extent_tree_root = 0;
    std::uint64_t extent_tree_count = 0;
};

struct Superblock {
    std::uint64_t sector = 0;
    std::uint64_t volume_id = 0;
    std::uint64_t sequence = 0;
    std::uint64_t total_sectors = 0;
    std::uint64_t checkpoint_sector = 0;
    std::uint64_t feature_flags = 0;
};

struct MountOptions {
    std::uint64_t byte_offset = 0;
};

struct FormatOptions {
    std::uint64_t byte_offset = 0;
    std::uint64_t size_bytes = 0;
    std::uint64_t volume_id = 0;
};

class BlockSource {
public:
    virtual ~BlockSource() = default;
    virtual std::vector<std::uint8_t> read_at(std::uint64_t offset, std::size_t size) const = 0;
    virtual void write_at(std::uint64_t offset, const std::uint8_t* data, std::size_t size);
    virtual void flush();
    virtual std::uint64_t size_bytes() const = 0;
};

class FileBlockSource final : public BlockSource {
public:
    explicit FileBlockSource(std::string path);
    ~FileBlockSource() override;
    std::vector<std::uint8_t> read_at(std::uint64_t offset, std::size_t size) const override;
    void write_at(std::uint64_t offset, const std::uint8_t* data, std::size_t size) override;
    void flush() override;
    std::uint64_t size_bytes() const override;
    const std::string& path() const { return path_; }

private:
    void ensure_open(bool writable) const;

    std::string path_;
    mutable int fd_ = -1;
    mutable bool writable_ = false;
};

class Volume {
public:
    void mount(std::shared_ptr<BlockSource> source, MountOptions options = {});

    const Superblock& superblock() const { return superblock_; }
    const Checkpoint& checkpoint() const { return checkpoint_; }
    const std::map<std::uint64_t, Object>& objects() const { return objects_; }
    const std::vector<DirectoryEntry>& namespace_entries() const { return namespace_entries_; }
    const std::vector<Attribute>& attributes() const { return attributes_; }

    const Object* object(std::uint64_t oid) const;
    std::optional<std::uint64_t> resolve_colon_path(const std::string& path) const;
    std::optional<std::uint64_t> resolve_posix_path(const std::string& path) const;
    std::vector<DirectoryEntry> children(std::uint64_t parent_oid) const;
    std::vector<std::uint8_t> read_file(std::uint64_t oid, std::uint64_t offset, std::size_t size) const;
    std::uint64_t create_file(std::uint64_t parent_oid, const std::string& name);
    std::uint64_t create_directory(std::uint64_t parent_oid, const std::string& name);
    void remove_child(std::uint64_t parent_oid, const std::string& name);
    void rename_child(std::uint64_t old_parent_oid, const std::string& old_name,
                      std::uint64_t new_parent_oid, const std::string& new_name);
    void resize_file(std::uint64_t oid, std::uint64_t size);
    void write_file(std::uint64_t oid, std::uint64_t offset, const std::uint8_t* data, std::size_t size);
    void commit(bool sync = true);

private:
    std::vector<std::uint8_t> read_sector(std::uint64_t sector, std::size_t count = 1) const;
    std::vector<std::uint8_t> collect_tree(std::uint64_t root_sector, std::uint64_t expected_count,
                                           std::size_t entry_size, std::uint64_t max_count) const;
    void collect_tree_node(std::uint64_t sector, std::size_t entry_size, std::uint64_t max_count,
                           std::vector<std::uint8_t>& entries) const;
    Checkpoint read_checkpoint(std::uint64_t sector) const;
    Superblock select_superblock() const;

    std::shared_ptr<BlockSource> source_;
    std::uint64_t byte_offset_ = 0;
    Superblock superblock_;
    Checkpoint checkpoint_;
    std::map<std::uint64_t, Object> objects_;
    std::vector<DirectoryEntry> namespace_entries_;
    std::vector<Attribute> attributes_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::uint64_t> extents_;
    std::map<std::uint64_t, std::vector<std::uint8_t>> dirty_files_;
    std::uint64_t next_oid_ = 2;
    bool metadata_dirty_ = false;
};

std::uint32_t crc32c(const std::uint8_t* data, std::size_t size);
std::optional<std::uint64_t> gpt_partition_offset(const BlockSource& source, std::uint32_t partition_number);
std::uint64_t file_size_bytes(const std::string& path);
void format_file(const std::string& path, const FormatOptions& options = {});

} // namespace arco::arcfs
