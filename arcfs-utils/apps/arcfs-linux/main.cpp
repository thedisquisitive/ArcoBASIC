#include "arco/arcfs/host.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef ARCO_HAVE_FUSE3
#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <cctype>
#include <csignal>
#include <cstdarg>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

struct ParsedArgs {
    std::string command;
    std::string subcommand;
    std::string source;
    std::string mountpoint;
    std::string path = "/";
    std::uint64_t offset = 0;
    std::uint32_t partition = 0;
    std::uint64_t size_bytes = 0;
    std::uint64_t volume_id = 0;
    std::uint64_t snapshot_id = 0;
    std::string label;
    bool force = false;
};

void usage(std::ostream& out) {
    out << "Usage:\n"
        << "  arcfs-linux inspect SOURCE [--partition N|--offset BYTES]\n"
        << "  arcfs-linux probe SOURCE [--partition N|--offset BYTES]\n"
        << "  arcfs-linux mkfs SOURCE [--offset BYTES] [--size BYTES] [--volume-id HEX] --force\n"
        << "  arcfs-linux ls SOURCE [PATH] [--partition N|--offset BYTES]\n"
        << "  arcfs-linux cat SOURCE PATH [--partition N|--offset BYTES]\n"
        << "  arcfs-linux mount SOURCE MOUNTPOINT [--partition N|--offset BYTES] [-- FUSE_OPTIONS...]\n"
        << "  arcfs-linux snapshot create SOURCE [--label TEXT] [--partition N|--offset BYTES]\n"
        << "  arcfs-linux snapshot list SOURCE [--partition N|--offset BYTES]\n"
        << "  arcfs-linux snapshot delete SOURCE ID [--partition N|--offset BYTES]\n"
        << "  arcfs-linux snapshot ls SOURCE ID [PATH] [--partition N|--offset BYTES]\n"
        << "  arcfs-linux snapshot cat SOURCE ID PATH [--partition N|--offset BYTES]\n"
        << "  arcfs-linux rollback SOURCE ID [--partition N|--offset BYTES]\n\n"
        << "SOURCE may be an ArcFS partition device, USB partition device, raw ArcFS image, or whole-disk image.\n"
        << "--partition N selects a GPT partition from a whole-disk image/device. Numbering starts at 1.\n"
        << "--offset BYTES mounts an ArcFS volume embedded at an explicit byte offset.\n";
}

ParsedArgs parse_args(int argc, char** argv) {
    if (argc < 2) {
        usage(std::cerr);
        throw std::runtime_error("missing command");
    }
    ParsedArgs args;
    args.command = argv[1];

    if (args.command == "snapshot") {
        if (argc < 4) throw std::runtime_error("snapshot requires a subcommand and SOURCE");
        args.subcommand = argv[2];
        args.source = argv[3];
        int index = 4;
        if (args.subcommand == "delete") {
            if (index >= argc) throw std::runtime_error("snapshot delete requires ID");
            args.snapshot_id = std::stoull(argv[index++]);
        } else if (args.subcommand == "ls") {
            if (index >= argc) throw std::runtime_error("snapshot ls requires ID");
            args.snapshot_id = std::stoull(argv[index++]);
            if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) args.path = argv[index++];
        } else if (args.subcommand == "cat") {
            if (index + 1 >= argc) throw std::runtime_error("snapshot cat requires ID and PATH");
            args.snapshot_id = std::stoull(argv[index++]);
            args.path = argv[index++];
        } else if (args.subcommand != "create" && args.subcommand != "list") {
            throw std::runtime_error("unknown snapshot subcommand: " + args.subcommand);
        }
        while (index < argc) {
            const std::string opt = argv[index++];
            if (opt == "--offset") {
                if (index >= argc) throw std::runtime_error("--offset requires a value");
                args.offset = std::stoull(argv[index++]);
            } else if (opt == "--partition") {
                if (index >= argc) throw std::runtime_error("--partition requires a value");
                args.partition = static_cast<std::uint32_t>(std::stoul(argv[index++]));
            } else if (opt == "--label") {
                if (index >= argc) throw std::runtime_error("--label requires a value");
                args.label = argv[index++];
            } else {
                throw std::runtime_error("unknown option: " + opt);
            }
        }
        if (args.offset != 0 && args.partition != 0) throw std::runtime_error("--offset and --partition are mutually exclusive");
        if (args.subcommand == "create" && args.label.empty()) args.label = "Snapshot";
        return args;
    }

    if (args.command == "rollback") {
        if (argc < 4) throw std::runtime_error("rollback requires SOURCE and ID");
        args.source = argv[2];
        args.snapshot_id = std::stoull(argv[3]);
        int index = 4;
        while (index < argc) {
            const std::string opt = argv[index++];
            if (opt == "--offset") {
                if (index >= argc) throw std::runtime_error("--offset requires a value");
                args.offset = std::stoull(argv[index++]);
            } else if (opt == "--partition") {
                if (index >= argc) throw std::runtime_error("--partition requires a value");
                args.partition = static_cast<std::uint32_t>(std::stoul(argv[index++]));
            } else {
                throw std::runtime_error("unknown option: " + opt);
            }
        }
        if (args.offset != 0 && args.partition != 0) throw std::runtime_error("--offset and --partition are mutually exclusive");
        return args;
    }

    if (argc < 3) {
        usage(std::cerr);
        throw std::runtime_error("missing command or source");
    }
    args.source = argv[2];
    int index = 3;
    if (args.command == "mount") {
        if (argc < 4) throw std::runtime_error("mount requires SOURCE and MOUNTPOINT");
        args.mountpoint = argv[3];
        index = 4;
    } else if (args.command == "ls") {
        if (index < argc && std::string(argv[index]).rfind("--", 0) != 0) {
            args.path = argv[index++];
        }
    } else if (args.command == "cat") {
        if (index >= argc) throw std::runtime_error("cat requires PATH");
        args.path = argv[index++];
    } else if (args.command != "inspect" && args.command != "probe" && args.command != "mkfs") {
        throw std::runtime_error("unknown command: " + args.command);
    }

    while (index < argc) {
        const std::string opt = argv[index++];
        if (opt == "--") break;
        if (opt == "--offset") {
            if (index >= argc) throw std::runtime_error("--offset requires a value");
            args.offset = std::stoull(argv[index++]);
        } else if (opt == "--partition") {
            if (index >= argc) throw std::runtime_error("--partition requires a value");
            args.partition = static_cast<std::uint32_t>(std::stoul(argv[index++]));
        } else if (opt == "--size") {
            if (index >= argc) throw std::runtime_error("--size requires a value");
            args.size_bytes = std::stoull(argv[index++]);
        } else if (opt == "--volume-id") {
            if (index >= argc) throw std::runtime_error("--volume-id requires a value");
            args.volume_id = std::stoull(argv[index++], nullptr, 16);
        } else if (opt == "--force") {
            args.force = true;
        } else {
            throw std::runtime_error("unknown option: " + opt);
        }
    }
    if (args.offset != 0 && args.partition != 0) throw std::runtime_error("--offset and --partition are mutually exclusive");
    if (args.command == "mkfs" && args.partition != 0) throw std::runtime_error("mkfs does not accept --partition; target the partition path directly");
    return args;
}

arco::arcfs::Volume mount_volume(const ParsedArgs& args) {
    auto source = std::make_shared<arco::arcfs::FileBlockSource>(args.source);
    arco::arcfs::MountOptions options;
    if (args.partition != 0) {
        const auto offset = arco::arcfs::gpt_partition_offset(*source, args.partition);
        if (!offset) throw std::runtime_error("could not resolve GPT partition " + std::to_string(args.partition));
        options.byte_offset = *offset;
    } else {
        options.byte_offset = args.offset;
    }
    arco::arcfs::Volume volume;
    volume.mount(std::move(source), options);
    return volume;
}

std::string type_name(std::uint64_t type) {
    if (type == 1) return "file";
    if (type == 2) return "directory";
    return "unknown";
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

void inspect(const arco::arcfs::Volume& volume) {
    const auto& cp = volume.checkpoint();
    const auto& sb = volume.superblock();
    std::uint64_t files = 0;
    std::uint64_t dirs = 0;
    for (const auto& [_, obj] : volume.objects()) {
        if (obj.type == 1) ++files;
        if (obj.type == 2) ++dirs;
    }
    std::cout << "ArcFS FMV2 volume\n"
              << "volume_id: " << hex64(sb.volume_id) << "\n"
              << "generation: " << cp.generation << "\n"
              << "superblock_sector: " << sb.sector << "\n"
              << "checkpoint_sector: " << cp.sector << "\n"
              << "objects: " << volume.objects().size() << " (" << files << " files, " << dirs << " directories)\n"
              << "namespace_entries: " << volume.namespace_entries().size() << "\n"
              << "attributes: " << volume.attributes().size() << "\n"
              << "extents: " << cp.extent_tree_count << "\n"
              << "high_water_mark: " << cp.high_water_mark << "\n"
              << "snapshots: " << volume.snapshots().size() << "\n";
}

void probe(const arco::arcfs::Volume& volume) {
    const auto& sb = volume.superblock();
    std::cout << "ID_FS_TYPE=arcfs\n"
              << "ID_FS_USAGE=filesystem\n"
              << "ID_FS_VERSION=FMV2\n"
              << "ID_FS_LABEL=ArcFS\n"
              << "ID_FS_LABEL_ENC=ArcFS\n"
              << "ID_FS_UUID=" << hex64(sb.volume_id) << "\n"
              << "ID_FS_UUID_ENC=" << hex64(sb.volume_id) << "\n";
}

void mkfs(const ParsedArgs& args) {
    if (!args.force) throw std::runtime_error("mkfs requires --force because it overwrites the target");
    const auto source_size = arco::arcfs::file_size_bytes(args.source);
    if (source_size == 0) throw std::runtime_error("target has no readable size: " + args.source);
    arco::arcfs::FormatOptions options;
    options.byte_offset = args.offset;
    options.size_bytes = args.size_bytes;
    options.volume_id = args.volume_id;
    arco::arcfs::format_file(args.source, options);

    arco::arcfs::MountOptions mount_options;
    mount_options.byte_offset = args.offset;
    auto source = std::make_shared<arco::arcfs::FileBlockSource>(args.source);
    arco::arcfs::Volume volume;
    volume.mount(std::move(source), mount_options);
    std::cout << "formatted ArcFS FMV2 volume\n"
              << "source: " << args.source << "\n"
              << "size_bytes: " << (args.size_bytes == 0 ? source_size - args.offset : args.size_bytes) << "\n"
              << "volume_id: " << hex64(volume.superblock().volume_id) << "\n";
}

void list_path(const arco::arcfs::Volume& volume, const std::string& path) {
    const auto oid = volume.resolve_posix_path(path);
    if (!oid) throw std::runtime_error("path not found: " + path);
    const auto* obj = volume.object(*oid);
    if (obj == nullptr) throw std::runtime_error("resolved object is missing");
    if (obj->type == 1) {
        std::cout << obj->size << " " << path << "\n";
        return;
    }
    if (obj->type != 2) throw std::runtime_error("object is not listable");
    for (const auto& entry : volume.children(*oid)) {
        const auto* child = volume.object(entry.child_oid);
        std::cout << (child ? type_name(child->type) : "missing") << "\t" << (child ? child->size : 0) << "\t" << entry.name << "\n";
    }
}

void cat_path(const arco::arcfs::Volume& volume, const std::string& path) {
    const auto oid = volume.resolve_posix_path(path);
    if (!oid) throw std::runtime_error("path not found: " + path);
    const auto* obj = volume.object(*oid);
    if (obj == nullptr || obj->type != 1) throw std::runtime_error("path is not a file: " + path);
    const auto bytes = volume.read_file(*oid, 0, static_cast<std::size_t>(obj->size));
    std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void snapshot_create(arco::arcfs::Volume& volume, const std::string& label) {
    const auto id = volume.create_snapshot(label);
    std::cout << "created snapshot " << id << " (\"" << label << "\") at generation "
              << volume.checkpoint().generation << "\n";
}

void snapshot_list(const arco::arcfs::Volume& volume) {
    if (volume.snapshots().empty()) {
        std::cout << "no snapshots\n";
        return;
    }
    std::cout << "id\tgeneration\tcreated_at\tlabel\n";
    for (const auto& snap : volume.snapshots()) {
        std::cout << snap.snapshot_id << "\t" << snap.source_generation << "\t" << snap.created_at << "\t" << snap.label << "\n";
    }
}

void snapshot_delete(arco::arcfs::Volume& volume, std::uint64_t id) {
    if (!volume.delete_snapshot(id)) throw std::runtime_error("snapshot does not exist: " + std::to_string(id));
    std::cout << "deleted snapshot " << id << "\n";
}

void snapshot_list_path(const arco::arcfs::Volume& volume, std::uint64_t snapshot_id, const std::string& path) {
    const auto oid = volume.resolve_posix_path_in_snapshot(snapshot_id, path);
    if (!oid) throw std::runtime_error("path not found in snapshot: " + path);
    const auto obj = volume.object_in_snapshot(snapshot_id, *oid);
    if (!obj) throw std::runtime_error("resolved object is missing from snapshot");
    if (obj->type == 1) {
        std::cout << obj->size << " " << path << "\n";
        return;
    }
    if (obj->type != 2) throw std::runtime_error("object is not listable");
    for (const auto& entry : volume.children_in_snapshot(snapshot_id, *oid)) {
        const auto child = volume.object_in_snapshot(snapshot_id, entry.child_oid);
        std::cout << (child ? type_name(child->type) : "missing") << "\t" << (child ? child->size : 0) << "\t" << entry.name << "\n";
    }
}

void snapshot_cat_path(const arco::arcfs::Volume& volume, std::uint64_t snapshot_id, const std::string& path) {
    const auto oid = volume.resolve_posix_path_in_snapshot(snapshot_id, path);
    if (!oid) throw std::runtime_error("path not found in snapshot: " + path);
    const auto obj = volume.object_in_snapshot(snapshot_id, *oid);
    if (!obj || obj->type != 1) throw std::runtime_error("path is not a file: " + path);
    const auto bytes = volume.read_file_in_snapshot(snapshot_id, *oid, 0, static_cast<std::size_t>(obj->size));
    std::cout.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void rollback(arco::arcfs::Volume& volume, std::uint64_t id) {
    volume.rollback_to_snapshot(id);
    std::cout << "rolled back to snapshot " << id << " (now generation " << volume.checkpoint().generation << ")\n";
}

#ifdef ARCO_HAVE_FUSE3
std::unique_ptr<arco::arcfs::Volume> g_volume;
std::mutex g_volume_mutex;
uid_t g_mount_uid = 0;
gid_t g_mount_gid = 0;

std::pair<std::string, std::string> parent_and_name(const char* raw_path) {
    std::string path(raw_path);
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    const auto slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0) return {"/", path.substr(slash == 0 ? 1 : 0)};
    return {path.substr(0, slash), path.substr(slash + 1)};
}

std::uint64_t parent_oid_or_throw(const std::string& parent_path) {
    const auto parent = g_volume->resolve_posix_path(parent_path);
    if (!parent) throw std::runtime_error("parent path not found");
    return *parent;
}

int arcfs_getattr(const char* path, struct stat* st, struct fuse_file_info*) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    std::memset(st, 0, sizeof(*st));
    try {
        const auto oid = g_volume->resolve_posix_path(path);
        if (!oid) return -ENOENT;
        const auto* obj = g_volume->object(*oid);
        if (obj == nullptr) return -ENOENT;
        if (obj->type == 2) {
            st->st_mode = S_IFDIR | 0755;
            st->st_nlink = 2;
        } else if (obj->type == 1) {
            st->st_mode = S_IFREG | 0644;
            st->st_nlink = 1;
            st->st_size = static_cast<off_t>(obj->size);
        } else {
            return -ENOENT;
        }
        st->st_ino = static_cast<ino_t>(*oid);
        st->st_uid = g_mount_uid;
        st->st_gid = g_mount_gid;
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_readdir(const char* path, void* buf, fuse_fill_dir_t filler, off_t, struct fuse_file_info*, enum fuse_readdir_flags) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto oid = g_volume->resolve_posix_path(path);
        if (!oid) return -ENOENT;
        const auto* obj = g_volume->object(*oid);
        if (obj == nullptr || obj->type != 2) return -ENOTDIR;
        filler(buf, ".", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        filler(buf, "..", nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        for (const auto& entry : g_volume->children(*oid)) {
            filler(buf, entry.name.c_str(), nullptr, 0, static_cast<fuse_fill_dir_flags>(0));
        }
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_open(const char* path, struct fuse_file_info* fi) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    const auto oid = g_volume->resolve_posix_path(path);
    if (!oid) return -ENOENT;
    const auto* obj = g_volume->object(*oid);
    return obj != nullptr && obj->type == 1 ? 0 : -EISDIR;
}

int arcfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info*) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto oid = g_volume->resolve_posix_path(path);
        if (!oid) return -ENOENT;
        const auto data = g_volume->read_file(*oid, static_cast<std::uint64_t>(offset), size);
        std::copy(data.begin(), data.end(), reinterpret_cast<std::uint8_t*>(buf));
        return static_cast<int>(data.size());
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_create(const char* path, mode_t, struct fuse_file_info*) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto [parent_path, name] = parent_and_name(path);
        const auto parent = parent_oid_or_throw(parent_path);
        g_volume->create_file(parent, name);
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_mkdir(const char* path, mode_t) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto [parent_path, name] = parent_and_name(path);
        const auto parent = parent_oid_or_throw(parent_path);
        g_volume->create_directory(parent, name);
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_unlink(const char* path) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto [parent_path, name] = parent_and_name(path);
        g_volume->remove_child(parent_oid_or_throw(parent_path), name);
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_rmdir(const char* path) {
    return arcfs_unlink(path);
}

int arcfs_rename(const char* from, const char* to, unsigned int flags) {
    if (flags != 0) return -EINVAL;
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto [old_parent_path, old_name] = parent_and_name(from);
        const auto [new_parent_path, new_name] = parent_and_name(to);
        g_volume->rename_child(parent_oid_or_throw(old_parent_path), old_name, parent_oid_or_throw(new_parent_path), new_name);
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_truncate(const char* path, off_t size, struct fuse_file_info*) {
    if (size < 0) return -EINVAL;
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto oid = g_volume->resolve_posix_path(path);
        if (!oid) return -ENOENT;
        g_volume->resize_file(*oid, static_cast<std::uint64_t>(size));
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info*) {
    if (offset < 0) return -EINVAL;
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        const auto oid = g_volume->resolve_posix_path(path);
        if (!oid) return -ENOENT;
        g_volume->write_file(*oid, static_cast<std::uint64_t>(offset), reinterpret_cast<const std::uint8_t*>(buf), size);
        return static_cast<int>(size);
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_flush(const char*, struct fuse_file_info*) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        g_volume->commit(false);
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

int arcfs_release(const char* path, struct fuse_file_info* fi) {
    return arcfs_flush(path, fi);
}

int arcfs_fsync(const char* path, int, struct fuse_file_info* fi) {
    (void)path;
    (void)fi;
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    try {
        g_volume->commit(true);
        return 0;
    } catch (const std::exception&) {
        return -EIO;
    }
}

void arcfs_destroy(void*) {
    std::lock_guard<std::mutex> lock(g_volume_mutex);
    if (g_volume) {
        try {
            g_volume->commit();
        } catch (const std::exception&) {
        }
    }
}

// TEMPORARY diagnostic instrumentation -- to be removed once the live-field mount/unmount-flash
// issue is root-caused. Writes to a persistent, real log path (not /tmp) so it survives across
// mount/unmount cycles and reboots for inspection.
void diag_log(const char* fmt, ...) {
    FILE* f = std::fopen("/var/log/arcfs-mount-debug.log", "a");
    if (!f) return;
    std::va_list args;
    va_start(args, fmt);
    std::fprintf(f, "[pid %d] ", ::getpid());
    std::vfprintf(f, fmt, args);
    va_end(args);
    std::fprintf(f, "\n");
    std::fclose(f);
}

// Standard "is this path a mount point" test (what mountpoint(1) itself does): a directory and its
// own parent live on the same device unless something is mounted on the directory. Used by the
// watchdog below to notice its mount ending by any means, not just the removal it watches for.
bool is_mount_active(const std::string& path) {
    struct stat mount_stat {};
    if (::stat(path.c_str(), &mount_stat) != 0) return false;
    struct stat parent_stat {};
    if (::stat((path + "/..").c_str(), &parent_stat) != 0) return false;
    return mount_stat.st_dev != parent_stat.st_dev;
}

// A real kernel filesystem driver gets told by the kernel when its backing device goes away; a
// FUSE daemon backed by a plain open file descriptor on a device node or image path gets no such
// signal, and would otherwise just sit mounted forever after the USB stick is pulled, returning
// I/O errors on every access until a human notices and runs `fusermount3 -u` by hand -- exactly
// how repeated insert/remove cycles piled up seven orphaned root-owned mounts in the field (a real
// incident: several `arcfs-linux mount` processes for a since-vanished /dev/sda were still mounted
// and running as root, confusing udisks2/Dolphin's view of a later, different insertion).
//
// This spawns a completely separate, detached watchdog PROCESS rather than a thread inside this
// one -- deliberately, after an in-process std::thread version was actually tried and caught
// failing here: fuse_main() daemonizes by forking internally whenever the caller doesn't pass
// -f/-d itself (the normal case for every real mount via mount.arcfs/udisksctl/Arconaut today),
// and only the forked child continues as the real long-running server -- a thread started before
// that call simply does not exist in the child, so it silently did nothing in exactly the
// deployment this exists to fix. A separate process forked before fuse_main() is unaffected by
// whatever fuse_main() does internally afterward: it just polls `source` externally and, if it
// disappears (covers a removed USB device node, and as a free bonus a deleted backing image
// file), lazy-unmounts `mountpoint` via the same `fusermount3 -u -z` a human would run by hand --
// which works regardless of which PID currently owns that mount.
//
// Also polls `is_mount_active(mountpoint)` and quietly exits the moment it goes false, whatever
// the reason -- a real, caught-live bug in an earlier version of this same watchdog: it only ever
// checked for device removal, so cleanly unmounting via other means (a normal `fusermount3 -u`, an
// Arconaut/Dolphin "Eject" while the drive stayed plugged in) left it running forever, still
// polling a device that never disappeared. Two of these orphans were found still running,
// discovered while diagnosing a *different* pileup (three redundant automount attempts for one
// insertion, all within the same second -- see acquire_source_lock() below for that one). An
// orphaned watchdog is not just a wasted process: it also keeps the source lock held forever,
// which would have made that fix actively block every future legitimate mount of the same device.
void run_removal_watchdog(const std::string& source, const std::string& mountpoint) {
    diag_log("removal-watchdog started source=%s mountpoint=%s", source.c_str(), mountpoint.c_str());
    ::setsid();
    ::signal(SIGHUP, SIG_IGN);
    const int null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        ::dup2(null_fd, STDIN_FILENO);
        ::dup2(null_fd, STDOUT_FILENO);
        ::dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) ::close(null_fd);
    }
    // This process is forked BEFORE the parent calls fuse_main() -- the actual mount(2)/FUSE
    // session establishment happens inside that call, which hasn't run yet at this point, so
    // is_mount_active(mountpoint) legitimately reads "not mounted" for a brief startup window
    // that has nothing to do with anyone unmounting anything. A real, caught-live bug in an
    // earlier version of this loop: it treated that startup race as "already unmounted, done"
    // and exited immediately, before the mount it was supposed to be protecting even existed.
    // Fixed by only treating "not active" as a real signal to stop once the mount has been
    // observed active at least once; `startup_grace_ticks` bounds how long to wait for that first
    // observation, so a mount attempt that never actually succeeds doesn't leave this looping
    // forever either.
    bool ever_active = false;
    int startup_grace_ticks = 15;
    while (::access(source.c_str(), F_OK) == 0) {
        const bool active = is_mount_active(mountpoint);
        if (active) {
            ever_active = true;
        } else if (ever_active || startup_grace_ticks-- <= 0) {
            diag_log("removal-watchdog exiting without unmount: ever_active=%d grace_left=%d", ever_active, startup_grace_ticks);
            ::_exit(0); // either cleanly unmounted elsewhere, or the mount never came up at all
        }
        ::sleep(2);
    }
    diag_log("removal-watchdog: source gone, force-unmounting %s", mountpoint.c_str());
    const pid_t unmount_pid = ::fork();
    if (unmount_pid == 0) {
        ::execlp("fusermount3", "fusermount3", "-u", "-z", mountpoint.c_str(), static_cast<char*>(nullptr));
        ::_exit(127); // execlp only returns on failure
    }
    if (unmount_pid > 0) {
        int status = 0;
        ::waitpid(unmount_pid, &status, 0);
        diag_log("removal-watchdog: fusermount3 exit status=%d", status);
    }
    ::_exit(0);
}

void spawn_removal_watchdog(const std::string& source, const std::string& mountpoint) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        std::fprintf(stderr, "arcfs-linux: warning: could not start removal watchdog: %s\n", std::strerror(errno));
        return;
    }
    if (pid > 0) return; // parent: proceeds into fuse_main() completely unaffected
    run_removal_watchdog(source, mountpoint); // child: never returns
}

std::string sanitize_for_filename(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        result.push_back(std::isalnum(static_cast<unsigned char>(ch)) ? ch : '_');
    }
    return result;
}

std::string source_lock_path(const ParsedArgs& args) {
    const std::string key = args.source + "|partition=" + std::to_string(args.partition) +
                             "|offset=" + std::to_string(args.offset);
    return "/run/arcfs/locks/" + sanitize_for_filename(key) + ".lock";
}

// udisks2/GVFS decide whether a block device is "already mounted" by looking up its major:minor
// device number in the kernel's own mount table -- which works for real filesystem types, but a
// FUSE mount's kernel-visible device is always FUSE's own synthetic pseudo-device, never the
// backing block device's. So neither of them can ever see that a given source already has an
// active `arcfs-linux mount` -- confirmed live: a single physical insertion produced three fully
// independent, simultaneous automount attempts (all launched within the same second), each
// blindly creating its own redundant mount rather than recognizing the first had already
// succeeded, and (separately confirmed) Dolphin/udisks2 surfacing a hard mount error the moment a
// duplicate attempt is correctly refused instead of silently duplicated, since neither of them
// realize the device is already perfectly accessible elsewhere.
//
// This is the real fix for both: an flock() on a small lock file keyed by the exact source +
// partition/offset selector (so distinct partitions of the same physical disk can still mount
// concurrently), held for the entire process lifetime -- across fuse_main()'s own internal
// daemonizing fork, since a forked child inherits the parent's open file descriptions, and the
// flock along with them. Released automatically the moment every process referencing it has
// exited, by any means including a crash -- no separate stale-lock bookkeeping needed. The lock
// winner also records its own mountpoint in the file's contents, so a loser doesn't just have to
// fail -- see run_fuse()'s use of read_lock_holder_mountpoint() to instead bind-mount onto the
// winner's already-live mount, giving whoever asked (Dolphin, in the field) a real, working
// directory at the exact path it requested. Returns an intentionally-leaked fd (kept for the
// process's entire life) on success; -1 with errno EWOULDBLOCK if another arcfs-linux mount
// already holds this exact source; -1 with any other errno if the lock infrastructure itself
// couldn't be used, in which case the caller should proceed anyway rather than let a /run
// permissions hiccup block a real, otherwise-valid mount.
// Root (via udisks2/pkexec) and a plain user (via a direct, unprivileged `arcfs-linux mount` --
// FUSE's own normal unprivileged-mount path, using the setuid fusermount3 helper, no sudo/pkexec
// needed once the device node itself is group-readable; see the 61-arcfs.rules udev rule) both
// need to coordinate through this SAME lock file, so it can't be root-owned/root-only the way a
// plain 0755/0644 default would leave it. Mirrors /tmp's own model exactly: 01777 (sticky bit) on
// the directories -- anyone can create a lock file, but only its own owner can delete/rename it --
// and 0666 on the lock files themselves, since the directory's sticky bit is what actually
// prevents cross-user tampering, not the file mode.
void ensure_shared_dir(const char* path) {
    ::mkdir(path, 0777);
    ::chmod(path, 01777);
}

int acquire_source_lock(const ParsedArgs& args) {
    ensure_shared_dir("/run/arcfs");
    ensure_shared_dir("/run/arcfs/locks");
    const std::string path = source_lock_path(args);
    const int fd = ::open(path.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0) return -1;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int saved_errno = errno;
        ::close(fd);
        errno = saved_errno;
        return -1;
    }
    if (::ftruncate(fd, 0) == 0) {
        ::lseek(fd, 0, SEEK_SET);
        const std::string content = args.mountpoint + "\n";
        if (::write(fd, content.data(), content.size()) < 0) { /* best-effort; a losing contender just retries/times out below */ }
    }
    return fd;
}

std::optional<std::string> read_lock_holder_mountpoint(const ParsedArgs& args) {
    const int fd = ::open(source_lock_path(args).c_str(), O_RDONLY);
    if (fd < 0) return std::nullopt;
    char buffer[4096];
    const auto n = ::read(fd, buffer, sizeof(buffer) - 1);
    ::close(fd);
    if (n <= 0) return std::nullopt;
    buffer[n] = '\0';
    std::string content(buffer);
    while (!content.empty() && (content.back() == '\n' || content.back() == '\r')) content.pop_back();
    if (content.empty()) return std::nullopt;
    return content;
}

// Two narrow startup races the lock winner and a losing contender can hit against each other
// (both real, both closed here rather than assumed away): the winner's mountpoint may not be
// written into the lock file yet the instant a loser's flock() call returns EWOULDBLOCK, and even
// once it is, the winner's actual FUSE mount (established later, inside its own fuse_main() call)
// may not have come up yet either -- bind-mounting from a directory nothing is mounted on yet
// would just silently create a view of that *empty* directory, reporting success while showing
// nothing. This polls both conditions together, bounded, rather than reading the file once.
//
// Bounded to ~2s, not the ~5s this originally used: a real bind-mount establishes in well under
// 100ms in every observed case (it's a single mount(2) syscall), and this whole function runs
// synchronously inside whatever invoked `arcfs-linux mount` -- for Arconaut specifically, that's
// a blocking Process.Run() call on its own single-threaded GUI loop, so every millisecond spent
// here is a millisecond the whole window stops responding and the desktop queues up input behind
// it. 5s was long enough to be genuinely felt as "the GUI hung"; nothing about a real, healthy
// mount needs anywhere near that, and a case that's actually stuck should fail fast into the
// clean error message below, not sit blocking the caller for seconds first.
std::optional<std::string> wait_for_lock_holder_mount(const ParsedArgs& args) {
    for (int attempt = 0; attempt < 20; ++attempt) { // up to ~2s
        const auto existing = read_lock_holder_mountpoint(args);
        if (existing && is_mount_active(*existing)) return existing;
        ::usleep(100000);
    }
    return std::nullopt;
}

// Cleans up a bind-mounted secondary view (see run_fuse()) once either side of it goes away: the
// real mount it points at ending (device removal -- handled by that mount's own removal watchdog
// -- or any other clean unmount) or someone unmounting this bind view directly. Mirrors
// run_removal_watchdog()'s own separate-process design for exactly the same reason: started
// before fuse_main()... except this path never calls fuse_main() at all (no FUSE session of its
// own is created for a bind-mounted view), so there's no daemonizing-fork hazard here -- it's
// still a separate detached process purely so it can run its own independent poll loop without
// blocking whatever is waiting on this invocation's own exit code.
void run_bind_mount_watchdog(std::string existing_mountpoint, std::string bind_mountpoint) {
    ::setsid();
    ::signal(SIGHUP, SIG_IGN);
    const int null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        ::dup2(null_fd, STDIN_FILENO);
        ::dup2(null_fd, STDOUT_FILENO);
        ::dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) ::close(null_fd);
    }
    while (is_mount_active(existing_mountpoint) && is_mount_active(bind_mountpoint)) {
        ::sleep(2);
    }
    if (is_mount_active(bind_mountpoint)) {
        const pid_t unmount_pid = ::fork();
        if (unmount_pid == 0) {
            ::execlp("umount", "umount", "-l", bind_mountpoint.c_str(), static_cast<char*>(nullptr));
            ::_exit(127);
        }
        if (unmount_pid > 0) {
            int status = 0;
            ::waitpid(unmount_pid, &status, 0);
        }
    }
    ::_exit(0);
}

// Same idea as run_bind_mount_watchdog() above, for the symlink fallback (see run_fuse()): waits
// for the real mount it points at to end, then just unlinks the symlink -- no umount involved,
// since nothing was ever mounted at `symlink_path` in the first place.
void run_symlink_watchdog(std::string existing_mountpoint, std::string symlink_path) {
    ::setsid();
    ::signal(SIGHUP, SIG_IGN);
    const int null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd >= 0) {
        ::dup2(null_fd, STDIN_FILENO);
        ::dup2(null_fd, STDOUT_FILENO);
        ::dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) ::close(null_fd);
    }
    while (is_mount_active(existing_mountpoint)) {
        ::sleep(2);
    }
    ::unlink(symlink_path.c_str());
    ::_exit(0);
}

void spawn_symlink_watchdog(const std::string& existing_mountpoint, const std::string& symlink_path) {
    const pid_t pid = ::fork();
    if (pid < 0) return;
    if (pid > 0) return;
    run_symlink_watchdog(existing_mountpoint, symlink_path);
}

void spawn_bind_mount_watchdog(const std::string& existing_mountpoint, const std::string& bind_mountpoint) {
    const pid_t pid = ::fork();
    if (pid < 0) return;
    if (pid > 0) return;
    run_bind_mount_watchdog(existing_mountpoint, bind_mountpoint);
}

int run_fuse(const ParsedArgs& args, int argc, char** argv) {
    if (acquire_source_lock(args) < 0) {
        if (errno == EWOULDBLOCK) {
            const auto existing = wait_for_lock_holder_mount(args);
            if (existing && *existing != args.mountpoint) {
                // Give whoever asked to mount here (Dolphin/udisks2, unaware the source was
                // already served elsewhere -- see acquire_source_lock()'s own comment) a real,
                // live, working directory instead of an error, and make sure it doesn't outlive
                // the mount it's a view of.
                if (::mount(existing->c_str(), args.mountpoint.c_str(), nullptr, MS_BIND, nullptr) == 0) {
                    spawn_bind_mount_watchdog(*existing, args.mountpoint);
                    return 0;
                }
                // MS_BIND needs CAP_SYS_ADMIN, which a real, otherwise-working unprivileged mount
                // (the plugdev udev grant's whole point -- see acquire_source_lock()'s comment)
                // doesn't have -- confirmed live: it failed silently here the moment Arconaut's
                // own mount path stopped going through root/udisksctl. A symlink needs no
                // privilege at all and is just as usable for ordinary file access; it simply
                // isn't a real second mountpoint (mountpoint(1)/df won't count it as one), an
                // acceptable trade for "give the caller something real" over failing outright.
                if (::rmdir(args.mountpoint.c_str()) == 0 &&
                    ::symlink(existing->c_str(), args.mountpoint.c_str()) == 0) {
                    spawn_symlink_watchdog(*existing, args.mountpoint);
                    return 0;
                }
            }
            throw std::runtime_error("ArcFS source " + args.source + " is already mounted by another arcfs-linux process");
        }
        std::fprintf(stderr, "arcfs-linux: warning: could not use the source lock (%s); proceeding without duplicate-mount protection\n",
                     std::strerror(errno));
    }
    if (const char* uid = std::getenv("ARCFS_MOUNT_UID")) {
        g_mount_uid = static_cast<uid_t>(std::stoul(uid));
    } else if (const char* uid = std::getenv("SUDO_UID")) {
        g_mount_uid = static_cast<uid_t>(std::stoul(uid));
    } else {
        g_mount_uid = getuid();
    }
    if (const char* gid = std::getenv("ARCFS_MOUNT_GID")) {
        g_mount_gid = static_cast<gid_t>(std::stoul(gid));
    } else if (const char* gid = std::getenv("SUDO_GID")) {
        g_mount_gid = static_cast<gid_t>(std::stoul(gid));
    } else {
        g_mount_gid = getgid();
    }
    g_volume = std::make_unique<arco::arcfs::Volume>(mount_volume(args));
    static fuse_operations ops {};
    ops.getattr = arcfs_getattr;
    ops.readdir = arcfs_readdir;
    ops.open = arcfs_open;
    ops.read = arcfs_read;
    ops.create = arcfs_create;
    ops.mkdir = arcfs_mkdir;
    ops.unlink = arcfs_unlink;
    ops.rmdir = arcfs_rmdir;
    ops.rename = arcfs_rename;
    ops.truncate = arcfs_truncate;
    ops.write = arcfs_write;
    ops.flush = arcfs_flush;
    ops.release = arcfs_release;
    ops.fsync = arcfs_fsync;
    ops.destroy = arcfs_destroy;
    std::vector<char*> fuse_argv;
    fuse_argv.push_back(argv[0]);
    fuse_argv.push_back(const_cast<char*>(args.mountpoint.c_str()));
    bool passthrough = false;
    for (int i = 1; i < argc; ++i) {
        if (passthrough) fuse_argv.push_back(argv[i]);
        if (std::string(argv[i]) == "--") passthrough = true;
    }
    spawn_removal_watchdog(args.source, args.mountpoint);
    return fuse_main(static_cast<int>(fuse_argv.size()), fuse_argv.data(), &ops, nullptr);
}
#endif

} // namespace

int main(int argc, char** argv) {
    try {
        const auto args = parse_args(argc, argv);
        if (args.command == "mount") {
#ifdef ARCO_HAVE_FUSE3
            return run_fuse(args, argc, argv);
#else
            throw std::runtime_error("this build was compiled without FUSE 3 support; install libfuse3-dev and rebuild");
#endif
        }
        if (args.command == "mkfs") {
            mkfs(args);
            return 0;
        }
        if (args.command == "snapshot") {
            auto volume = mount_volume(args);
            if (args.subcommand == "create") {
                snapshot_create(volume, args.label);
            } else if (args.subcommand == "list") {
                snapshot_list(volume);
            } else if (args.subcommand == "delete") {
                snapshot_delete(volume, args.snapshot_id);
            } else if (args.subcommand == "ls") {
                snapshot_list_path(volume, args.snapshot_id, args.path);
            } else if (args.subcommand == "cat") {
                snapshot_cat_path(volume, args.snapshot_id, args.path);
            }
            return 0;
        }
        if (args.command == "rollback") {
            auto volume = mount_volume(args);
            rollback(volume, args.snapshot_id);
            return 0;
        }
        const auto volume = mount_volume(args);
        if (args.command == "inspect") {
            inspect(volume);
        } else if (args.command == "probe") {
            probe(volume);
        } else if (args.command == "ls") {
            list_path(volume, args.path);
        } else if (args.command == "cat") {
            cat_path(volume, args.path);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "arcfs-linux: " << error.what() << "\n";
        return 1;
    }
}
