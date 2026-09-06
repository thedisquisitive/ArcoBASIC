#include "arco/arcfs/host.hpp"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kSectorSize = 512;
constexpr std::uint64_t kNodeSize = 4096;

class MemoryBlockSource final : public arco::arcfs::BlockSource {
public:
    explicit MemoryBlockSource(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    std::vector<std::uint8_t> read_at(std::uint64_t offset, std::size_t size) const override {
        if (offset + size > bytes_.size()) throw std::runtime_error("test image short read");
        return std::vector<std::uint8_t>(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                                         bytes_.begin() + static_cast<std::ptrdiff_t>(offset + size));
    }
    std::uint64_t size_bytes() const override { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset + 0] = static_cast<std::uint8_t>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    bytes[offset + 2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    bytes[offset + 3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void put64(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint64_t value) {
    put32(bytes, offset, static_cast<std::uint32_t>(value & 0xFFFFFFFFULL));
    put32(bytes, offset + 4, static_cast<std::uint32_t>(value >> 32ULL));
}

void write_at(std::vector<std::uint8_t>& image, std::uint64_t sector, const std::vector<std::uint8_t>& bytes) {
    const auto offset = static_cast<std::size_t>(sector * kSectorSize);
    std::copy(bytes.begin(), bytes.end(), image.begin() + static_cast<std::ptrdiff_t>(offset));
}

std::vector<std::uint8_t> leaf_node(std::size_t entry_size, const std::vector<std::vector<std::uint8_t>>& entries) {
    std::vector<std::uint8_t> node(kNodeSize, 0);
    put64(node, 0, 1);
    put64(node, 8, entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::copy(entries[i].begin(), entries[i].end(), node.begin() + static_cast<std::ptrdiff_t>(24 + i * entry_size));
    }
    put32(node, 4092, arco::arcfs::crc32c(node.data(), 4092));
    return node;
}

std::vector<std::uint8_t> object_entry(std::uint64_t oid, std::uint64_t type, std::uint64_t size, std::uint64_t data_slot) {
    std::vector<std::uint8_t> entry(40, 0);
    put64(entry, 0, oid);
    put64(entry, 8, type);
    put64(entry, 16, size);
    put64(entry, 24, data_slot);
    put64(entry, 32, 0);
    return entry;
}

std::vector<std::uint8_t> namespace_entry(std::uint64_t parent, const char* name, std::uint64_t child) {
    std::vector<std::uint8_t> entry(56, 0);
    const auto len = std::strlen(name);
    put64(entry, 0, parent);
    std::memcpy(entry.data() + 8, name, len);
    put64(entry, 40, len);
    put64(entry, 48, child);
    return entry;
}

std::vector<std::uint8_t> extent_entry(std::uint64_t data_slot, std::uint64_t chunk, std::uint64_t sector) {
    std::vector<std::uint8_t> entry(32, 0);
    put64(entry, 0, data_slot);
    put64(entry, 8, chunk);
    put64(entry, 16, sector);
    return entry;
}

std::vector<std::uint8_t> build_image() {
    constexpr std::uint64_t checkpoint_sector = 398;
    constexpr std::uint64_t object_root = 400;
    constexpr std::uint64_t namespace_root = 408;
    constexpr std::uint64_t extent_root = 416;
    constexpr std::uint64_t data_sector = 424;
    std::vector<std::uint8_t> image(512 * kSectorSize, 0);

    auto objects = leaf_node(40, {
        object_entry(1, 2, 0, 0),
        object_entry(2, 2, 0, 1),
        object_entry(3, 1, 13, 2),
    });
    write_at(image, object_root, objects);

    auto ns = leaf_node(56, {
        namespace_entry(1, "home", 2),
        namespace_entry(2, "hello.txt", 3),
    });
    write_at(image, namespace_root, ns);

    auto extents = leaf_node(32, {extent_entry(2, 0, data_sector)});
    write_at(image, extent_root, extents);

    std::vector<std::uint8_t> data(kNodeSize, 0);
    const char* text = "hello ArcFS!\n";
    std::memcpy(data.data(), text, std::strlen(text));
    write_at(image, data_sector, data);

    std::vector<std::uint8_t> checkpoint(kSectorSize, 0);
    put64(checkpoint, 0, 2);
    put64(checkpoint, 8, object_root);
    put64(checkpoint, 16, 3);
    put64(checkpoint, 24, namespace_root);
    put64(checkpoint, 32, 2);
    put64(checkpoint, 40, 432);
    put64(checkpoint, 48, 0);
    put64(checkpoint, 56, 4);
    put64(checkpoint, 64, 432);
    put64(checkpoint, 72, 0);
    put64(checkpoint, 80, 0);
    put64(checkpoint, 88, extent_root);
    put64(checkpoint, 96, 1);
    put64(checkpoint, 104, 112);
    put32(checkpoint, 112, arco::arcfs::crc32c(checkpoint.data(), 112));
    write_at(image, checkpoint_sector, checkpoint);

    std::vector<std::uint8_t> super(kSectorSize, 0);
    std::memcpy(super.data(), "ARCFSB02", 8);
    put32(super, 8, 2);
    put32(super, 12, 0);
    put64(super, 16, 42);
    put64(super, 24, 1);
    put32(super, 32, 512);
    put32(super, 36, 1);
    put64(super, 40, 512);
    put64(super, 48, 65536);
    put64(super, 56, checkpoint_sector);
    put32(super, 64, arco::arcfs::crc32c(super.data(), 64));
    for (std::uint64_t sector = 0; sector < 4; ++sector) write_at(image, sector, super);
    return image;
}

std::vector<std::uint8_t> build_gpt_image() {
    std::vector<std::uint8_t> image(64 * kSectorSize, 0);
    std::memcpy(image.data() + kSectorSize, "EFI PART", 8);
    put64(image, kSectorSize + 72, 2);
    put32(image, kSectorSize + 80, 4);
    put32(image, kSectorSize + 84, 128);
    const auto entry_offset = static_cast<std::size_t>(2 * kSectorSize + 128);
    image[entry_offset] = 0xAF;
    put64(image, entry_offset + 32, 24);
    put64(image, entry_offset + 40, 47);
    return image;
}

void test_mount_and_read() {
    auto source = std::make_shared<MemoryBlockSource>(build_image());
    arco::arcfs::Volume volume;
    volume.mount(source);
    assert(volume.superblock().volume_id == 42);
    assert(volume.superblock().checkpoint_sector == 398);
    assert(volume.checkpoint().generation == 2);
    assert(volume.objects().size() == 3);
    const auto oid = volume.resolve_posix_path("/home/hello.txt");
    assert(oid && *oid == 3);
    const auto data = volume.read_file(*oid, 0, 64);
    const std::string text(data.begin(), data.end());
    assert(text == "hello ArcFS!\n");
    const auto children = volume.children(1);
    assert(children.size() == 1);
    assert(children[0].name == "home");
    // build_image() is a hand-built OLD-format (112-byte checkpoint, no snapshot bit) fixture --
    // this is the real backward-compatibility guarantee: it must mount cleanly with zero snapshots,
    // no incompat-bit rejection, exactly as if the Snapshot Tree feature didn't exist yet.
    assert(volume.checkpoint().snapshot_tree_root == 0);
    assert(volume.checkpoint().snapshot_tree_count == 0);
    assert(volume.snapshots().empty());
}

void test_rejects_bad_checksum() {
    auto image = build_image();
    image[400 * kSectorSize + 24] ^= 0x40;
    auto source = std::make_shared<MemoryBlockSource>(std::move(image));
    arco::arcfs::Volume volume;
    bool rejected = false;
    try {
        volume.mount(source);
    } catch (const std::exception&) {
        rejected = true;
    }
    assert(rejected);
}

void test_gpt_partition_offset() {
    auto source = MemoryBlockSource(build_gpt_image());
    const auto missing = arco::arcfs::gpt_partition_offset(source, 1);
    assert(!missing);
    const auto offset = arco::arcfs::gpt_partition_offset(source, 2);
    assert(offset && *offset == 24 * kSectorSize);
}

void test_large_host_write() {
    const auto path = std::filesystem::temp_directory_path() / "arcfs_host_large_write.img";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.seekp(4 * 1024 * 1024 - 1);
        out.put('\0');
    }
    arco::arcfs::format_file(path.string(), {});
    auto source = std::make_shared<arco::arcfs::FileBlockSource>(path.string());
    arco::arcfs::Volume volume;
    volume.mount(source);
    const auto oid = volume.create_file(1, "large.bin");
    std::vector<std::uint8_t> data(512 * 1024, 0);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>((i * 31) & 0xFFU);
    volume.write_file(oid, 0, data.data(), data.size());
    volume.commit();

    arco::arcfs::Volume remounted;
    remounted.mount(source);
    const auto remounted_oid = remounted.resolve_posix_path("/large.bin");
    assert(remounted_oid);
    const auto readback = remounted.read_file(*remounted_oid, 0, data.size());
    assert(readback == data);
    std::filesystem::remove(path);
}

// The core "time regression" guarantee: a snapshot keeps reading the content that existed at the
// moment it was taken, even after the live volume has been overwritten and re-committed on top of
// it -- checked both in-memory and after a completely fresh mount (durability, not just a cache).
void test_snapshot_preserves_old_generation() {
    const auto path = std::filesystem::temp_directory_path() / "arcfs_host_snapshot.img";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.seekp(4 * 1024 * 1024 - 1);
        out.put('\0');
    }
    arco::arcfs::format_file(path.string(), {});
    auto source = std::make_shared<arco::arcfs::FileBlockSource>(path.string());
    arco::arcfs::Volume volume;
    volume.mount(source);

    const auto oid = volume.create_file(1, "note.txt");
    const std::string original = "original content";
    volume.write_file(oid, 0, reinterpret_cast<const std::uint8_t*>(original.data()), original.size());
    volume.commit();

    const auto snapshot_id = volume.create_snapshot("before-edit");
    assert(volume.snapshots().size() == 1);
    assert(volume.snapshots()[0].label == "before-edit");
    assert(volume.snapshots()[0].source_generation == volume.checkpoint().generation);

    const std::string updated = "updated content, longer than the original";
    volume.write_file(oid, 0, reinterpret_cast<const std::uint8_t*>(updated.data()), updated.size());
    volume.commit();

    const auto live = volume.read_file(oid, 0, updated.size());
    assert(std::string(live.begin(), live.end()) == updated);

    const auto snap_oid = volume.resolve_posix_path_in_snapshot(snapshot_id, "/note.txt");
    assert(snap_oid && *snap_oid == oid);
    const auto snap_data = volume.read_file_in_snapshot(snapshot_id, *snap_oid, 0, original.size());
    assert(std::string(snap_data.begin(), snap_data.end()) == original);

    const auto snap_children = volume.children_in_snapshot(snapshot_id, 1);
    assert(snap_children.size() == 1 && snap_children[0].name == "note.txt");

    arco::arcfs::Volume remounted;
    remounted.mount(source);
    assert(remounted.snapshots().size() == 1);
    const auto remounted_snap_data = remounted.read_file_in_snapshot(snapshot_id, oid, 0, original.size());
    assert(std::string(remounted_snap_data.begin(), remounted_snap_data.end()) == original);
    const auto remounted_live = remounted.read_file(oid, 0, updated.size());
    assert(std::string(remounted_live.begin(), remounted_live.end()) == updated);

    std::filesystem::remove(path);
}

// Restores the live volume's content by committing a NEW generation whose roots equal the
// snapshot's own -- not by rewinding the generation counter -- and leaves the Snapshot Tree
// (including the snapshot just restored from) untouched.
void test_rollback_round_trip() {
    const auto path = std::filesystem::temp_directory_path() / "arcfs_host_rollback.img";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.seekp(4 * 1024 * 1024 - 1);
        out.put('\0');
    }
    arco::arcfs::format_file(path.string(), {});
    auto source = std::make_shared<arco::arcfs::FileBlockSource>(path.string());
    arco::arcfs::Volume volume;
    volume.mount(source);

    const auto oid = volume.create_file(1, "state.txt");
    const std::string original = "state v1";
    volume.write_file(oid, 0, reinterpret_cast<const std::uint8_t*>(original.data()), original.size());
    volume.commit();

    const auto snapshot_id = volume.create_snapshot("v1");
    const auto generation_at_snapshot = volume.checkpoint().generation;

    const std::string updated = "state v2, a different length entirely";
    volume.write_file(oid, 0, reinterpret_cast<const std::uint8_t*>(updated.data()), updated.size());
    volume.commit();
    const auto before_rollback = volume.read_file(oid, 0, updated.size());
    assert(std::string(before_rollback.begin(), before_rollback.end()) == updated);

    volume.rollback_to_snapshot(snapshot_id);

    // Rollback moves forward: a strictly newer generation, not the snapshot's own generation number.
    assert(volume.checkpoint().generation > generation_at_snapshot);
    const auto restored = volume.read_file(oid, 0, original.size());
    assert(std::string(restored.begin(), restored.end()) == original);
    assert(volume.snapshots().size() == 1);
    assert(volume.snapshots()[0].snapshot_id == snapshot_id);

    arco::arcfs::Volume remounted;
    remounted.mount(source);
    const auto remounted_oid = remounted.resolve_posix_path("/state.txt");
    assert(remounted_oid);
    const auto remounted_data = remounted.read_file(*remounted_oid, 0, original.size());
    assert(std::string(remounted_data.begin(), remounted_data.end()) == original);
    assert(remounted.snapshots().size() == 1);

    std::filesystem::remove(path);
}

void test_delete_snapshot() {
    const auto path = std::filesystem::temp_directory_path() / "arcfs_host_delete_snapshot.img";
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.seekp(4 * 1024 * 1024 - 1);
        out.put('\0');
    }
    arco::arcfs::format_file(path.string(), {});
    auto source = std::make_shared<arco::arcfs::FileBlockSource>(path.string());
    arco::arcfs::Volume volume;
    volume.mount(source);

    volume.create_file(1, "a.txt");
    volume.commit();
    const auto first = volume.create_snapshot("first");
    const auto second = volume.create_snapshot("second");
    assert(volume.snapshots().size() == 2);

    assert(volume.delete_snapshot(first));
    assert(volume.snapshots().size() == 1);
    assert(volume.snapshots()[0].snapshot_id == second);
    // Deleting an already-gone id is a clean no-op, not an error.
    assert(!volume.delete_snapshot(first));

    arco::arcfs::Volume remounted;
    remounted.mount(source);
    assert(remounted.snapshots().size() == 1);
    assert(remounted.snapshots()[0].snapshot_id == second);
    assert(remounted.snapshots()[0].label == "second");

    std::filesystem::remove(path);
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--write-image") {
        std::ofstream out(argv[2], std::ios::binary);
        const auto image = build_image();
        out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
        return out ? 0 : 1;
    }

    test_mount_and_read();
    test_rejects_bad_checksum();
    test_gpt_partition_offset();
    test_large_host_write();
    test_snapshot_preserves_old_generation();
    test_rollback_round_trip();
    test_delete_snapshot();
    std::cout << "ArcFS host tests passed\n";
    return 0;
}
