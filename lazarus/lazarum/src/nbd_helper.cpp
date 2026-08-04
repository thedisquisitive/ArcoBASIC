#include "lazarus/core.hpp"

#include <arpa/inet.h>
#include <endian.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/nbd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

bool transfer_all(int fd, void* buffer, std::size_t length, bool write_data) {
    auto* bytes = static_cast<unsigned char*>(buffer);
    std::size_t completed = 0;
    while (completed < length) {
        const auto count = write_data
            ? ::write(fd, bytes + completed, length - completed)
            : ::read(fd, bytes + completed, length - completed);
        if (count == 0) return false;
        if (count < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        completed += static_cast<std::size_t>(count);
    }
    return true;
}

bool write_all(int fd, const void* buffer, std::size_t length) {
    return transfer_all(fd, const_cast<void*>(buffer), length, true);
}

bool read_all(int fd, void* buffer, std::size_t length) {
    return transfer_all(fd, buffer, length, false);
}

void drop_to_invoking_user() {
    const char* uid_text = std::getenv("SUDO_UID");
    const char* gid_text = std::getenv("SUDO_GID");
    if (uid_text == nullptr || gid_text == nullptr) return;
    try {
        const auto uid = static_cast<uid_t>(std::stoul(uid_text));
        const auto gid = static_cast<gid_t>(std::stoul(gid_text));
        (void)::setgroups(0, nullptr);
        (void)::setgid(gid);
        (void)::setuid(uid);
    } catch (...) {
        // The image is already open read-only; retaining root here does not
        // broaden the NBD protocol, which implements reads and disconnect only.
    }
}

int serve_requests(int socket, const lazarus::LogicalImageReader& reader) {
    drop_to_invoking_user();
    for (;;) {
        nbd_request request{};
        if (!read_all(socket, &request, sizeof(request))) return 0;
        if (ntohl(request.magic) != NBD_REQUEST_MAGIC) return 1;
        const auto command = ntohl(request.type) & 0xffffU;
        if (command == NBD_CMD_DISC) return 0;

        nbd_reply reply{};
        reply.magic = htonl(NBD_REPLY_MAGIC);
        std::memcpy(reply.handle, request.handle, sizeof(reply.handle));
        std::vector<std::byte> data;
        std::string error;
        if (command == NBD_CMD_READ) {
            const auto offset = be64toh(request.from);
            const auto length = static_cast<std::size_t>(ntohl(request.len));
            constexpr std::size_t maximum_request = 16 * 1024 * 1024;
            if (length > maximum_request) reply.error = htonl(EINVAL);
            else if (!reader.read_at(offset, length, data, error)) reply.error = htonl(EIO);
        } else if (command == NBD_CMD_FLUSH) {
            reply.error = 0;
        } else {
            reply.error = htonl(EROFS);
        }
        if (!write_all(socket, &reply, sizeof(reply))) return 1;
        if (command == NBD_CMD_READ && reply.error == 0 &&
            !write_all(socket, data.data(), data.size())) return 1;
    }
}

bool device_is_free(const fs::path& device) {
    const auto name = device.filename();
    return !fs::exists(fs::path("/sys/block") / name / "pid");
}

int connect_device(const lazarus::LogicalImageReader& reader, int socket,
                   std::string& selected_device) {
    for (unsigned index = 0; index < 64; ++index) {
        const auto device = fs::path("/dev") / ("nbd" + std::to_string(index));
        if (!fs::exists(device) || !device_is_free(device)) continue;
        const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        if (::ioctl(fd, NBD_SET_SOCK, socket) != 0) {
            ::close(fd);
            continue;
        }
        if (::ioctl(fd, NBD_SET_BLKSIZE, 512UL) != 0 ||
            ::ioctl(fd, NBD_SET_SIZE_BLOCKS,
                    static_cast<unsigned long>(reader.size_bytes() / 512ULL)) != 0 ||
            ::ioctl(fd, NBD_SET_TIMEOUT, 30UL) != 0 ||
            ::ioctl(fd, NBD_SET_FLAGS,
                    static_cast<unsigned long>(NBD_FLAG_HAS_FLAGS | NBD_FLAG_READ_ONLY | NBD_FLAG_SEND_FLUSH)) != 0) {
            (void)::ioctl(fd, NBD_CLEAR_SOCK);
            ::close(fd);
            continue;
        }
        selected_device = device.string();
        return fd;
    }
    return -1;
}

struct ReadyMessage {
    int error_number = 0;
    std::array<char, 64> device{};
};

int attach(const std::string& image_directory) {
    if (::geteuid() != 0) {
        std::cerr << "lazarum-nbd attach must run as root\n";
        return 1;
    }
    std::string open_error;
    auto reader = lazarus::LogicalImageReader::open(image_directory, open_error);
    if (!reader) {
        std::cerr << open_error << '\n';
        return 1;
    }
    if (reader->size_bytes() % 512 != 0) {
        std::cerr << "The logical image size is not sector aligned.\n";
        return 1;
    }

    int ready_pipe[2]{};
    if (::pipe2(ready_pipe, O_CLOEXEC) != 0) {
        std::cerr << "Could not create readiness pipe: " << std::strerror(errno) << '\n';
        return 1;
    }
    const pid_t daemon = ::fork();
    if (daemon < 0) {
        std::cerr << "Could not start NBD process: " << std::strerror(errno) << '\n';
        return 1;
    }
    if (daemon != 0) {
        ::close(ready_pipe[1]);
        ReadyMessage message{};
        const bool received = read_all(ready_pipe[0], &message, sizeof(message));
        ::close(ready_pipe[0]);
        if (!received || message.error_number != 0 || message.device[0] == '\0') {
            std::cerr << "Could not attach an available NBD device: "
                      << std::strerror(received ? message.error_number : EIO) << '\n';
            (void)::waitpid(daemon, nullptr, 0);
            return 1;
        }
        std::cout << "device=" << message.device.data() << "\n";
        return 0;
    }

    ::close(ready_pipe[0]);
    (void)::setsid();
    const int null_fd = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd >= 0) {
        (void)::dup2(null_fd, STDIN_FILENO);
        (void)::dup2(null_fd, STDOUT_FILENO);
        (void)::dup2(null_fd, STDERR_FILENO);
        if (null_fd > STDERR_FILENO) ::close(null_fd);
    }
    int sockets[2]{};
    ReadyMessage message{};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets) != 0) {
        message.error_number = errno;
        (void)write_all(ready_pipe[1], &message, sizeof(message));
        _exit(1);
    }
    std::string device;
    const int nbd_fd = connect_device(*reader, sockets[0], device);
    if (nbd_fd < 0) {
        message.error_number = EBUSY;
        (void)write_all(ready_pipe[1], &message, sizeof(message));
        _exit(1);
    }
    const pid_t server = ::fork();
    if (server < 0) {
        message.error_number = errno;
        (void)::ioctl(nbd_fd, NBD_CLEAR_SOCK);
        (void)write_all(ready_pipe[1], &message, sizeof(message));
        _exit(1);
    }
    if (server == 0) {
        ::close(ready_pipe[1]);
        ::close(sockets[0]);
        ::close(nbd_fd);
        const int result = serve_requests(sockets[1], *reader);
        ::close(sockets[1]);
        _exit(result);
    }
    ::close(sockets[1]);
    std::strncpy(message.device.data(), device.c_str(), message.device.size() - 1);
    (void)write_all(ready_pipe[1], &message, sizeof(message));
    ::close(ready_pipe[1]);

    const int do_it_result = ::ioctl(nbd_fd, NBD_DO_IT);
    const int saved_errno = errno;
    (void)::ioctl(nbd_fd, NBD_CLEAR_QUE);
    (void)::ioctl(nbd_fd, NBD_CLEAR_SOCK);
    ::close(nbd_fd);
    ::close(sockets[0]);
    (void)::kill(server, SIGTERM);
    (void)::waitpid(server, nullptr, 0);
    _exit(do_it_result == 0 || saved_errno == EPIPE ? 0 : 1);
}

int detach(const std::string& device) {
    if (::geteuid() != 0 || device.rfind("/dev/nbd", 0) != 0 ||
        device.find('/', 5) != std::string::npos) {
        std::cerr << "Refusing invalid NBD detach request.\n";
        return 1;
    }
    const int fd = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        std::cerr << "Could not open " << device << ": " << std::strerror(errno) << '\n';
        return 1;
    }
    const int result = ::ioctl(fd, NBD_DISCONNECT);
    const int saved_errno = errno;
    ::close(fd);
    if (result != 0 && saved_errno != ENOTCONN && saved_errno != EINVAL) {
        std::cerr << "Could not disconnect " << device << ": " << std::strerror(saved_errno) << '\n';
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "attach") return attach(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "detach") return detach(argv[2]);
    std::cerr << "Usage: lazarum-nbd attach IMAGE_DIRECTORY | detach /dev/nbdN\n";
    return 2;
}
