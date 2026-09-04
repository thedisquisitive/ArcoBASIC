#include "arco/arcfs/host.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef ARCO_HAVE_FUSE3
#define FUSE_USE_VERSION 35
#include <fuse3/fuse.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

struct ParsedArgs {
    std::string command;
    std::string source;
    std::string mountpoint;
    std::string path = "/";
    std::uint64_t offset = 0;
    std::uint32_t partition = 0;
    std::uint64_t size_bytes = 0;
    std::uint64_t volume_id = 0;
    bool force = false;
};

void usage(std::ostream& out) {
    out << "Usage:\n"
        << "  arcfs-linux inspect SOURCE [--partition N|--offset BYTES]\n"
        << "  arcfs-linux probe SOURCE [--partition N|--offset BYTES]\n"
        << "  arcfs-linux mkfs SOURCE [--offset BYTES] [--size BYTES] [--volume-id HEX] --force\n"
        << "  arcfs-linux ls SOURCE [PATH] [--partition N|--offset BYTES]\n"
        << "  arcfs-linux cat SOURCE PATH [--partition N|--offset BYTES]\n"
        << "  arcfs-linux mount SOURCE MOUNTPOINT [--partition N|--offset BYTES] [-- FUSE_OPTIONS...]\n\n"
        << "SOURCE may be an ArcFS partition device, USB partition device, raw ArcFS image, or whole-disk image.\n"
        << "--partition N selects a GPT partition from a whole-disk image/device. Numbering starts at 1.\n"
        << "--offset BYTES mounts an ArcFS volume embedded at an explicit byte offset.\n";
}

ParsedArgs parse_args(int argc, char** argv) {
    if (argc < 3) {
        usage(std::cerr);
        throw std::runtime_error("missing command or source");
    }
    ParsedArgs args;
    args.command = argv[1];
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
              << "high_water_mark: " << cp.high_water_mark << "\n";
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

int run_fuse(const ParsedArgs& args, int argc, char** argv) {
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
