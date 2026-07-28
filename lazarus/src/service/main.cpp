#include "lazarus/core.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <grp.h>
#include <ifaddrs.h>
#include <linux/fs.h>
#include <net/if.h>
#include <hivex.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <streambuf>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <sys/wait.h>

namespace {

struct ServiceConfig {
    std::string bench_path = "/etc/arcology-lazarus/bench.profile";
    std::string socket_path = "/run/arcology-lazarus/service.sock";
    std::string security_path = "/var/lib/arcology-lazarus/admin.auth";
    std::string security_backup_path = "/mnt/lazarus-storage/admin.auth";
    std::string network_path = "/etc/arcology-lazarus/network.conf";
    std::string network_helper = "/usr/local/sbin/lazarus-network-up";
    bool stdio = false;
};

struct NetworkSettings {
    std::string mode = "dhcp";
    std::string interface = "auto";
    std::string address;
    std::string prefix = "24";
    std::string gateway;
    std::string dns;
};

constexpr int kPasswordIterations = 310000;
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;
constexpr auto kAdminSessionLifetime = std::chrono::minutes(15);

struct AdminRecord {
    int iterations = kPasswordIterations;
    std::string password_salt;
    std::string password_hash;
    std::string recovery_salt;
    std::string recovery_hash;
};

struct AdminSession {
    std::chrono::steady_clock::time_point expires;
    bool recovery_login = false;
};

std::unordered_map<std::string, AdminSession> admin_sessions;
unsigned int failed_admin_logins = 0;
std::chrono::steady_clock::time_point admin_login_blocked_until{};
std::recursive_mutex admin_mutex;
std::mutex profile_save_mutex;

struct DeviceActivity {
    lazarus::DeviceIdentity device;
    std::string operation;
    std::string phase;
};

std::mutex activity_mutex;
std::unordered_map<std::string, DeviceActivity> active_devices;

struct BrowseVolume {
    std::string token;
    std::string device_path;
    std::string filesystem;
    std::string label;
    std::uint64_t size_bytes = 0;
    std::string mount_path;
};

struct BrowseSession {
    std::string id;
    std::string image_directory;
    std::string cache_path;
    std::string loop_device;
    std::vector<BrowseVolume> volumes;
};

std::mutex browse_mutex;
std::mutex browse_prepare_mutex;
std::unordered_map<std::string, BrowseSession> browse_sessions;

std::string activity_key(const lazarus::DeviceIdentity& device) {
    if (!device.serial.empty()) return "serial:" + device.serial;
    if (!device.by_id_path.empty()) return "id:" + device.by_id_path;
    if (!device.by_path.empty()) return "path:" + device.by_path;
    if (!device.physical_path.empty()) return "physical:" + device.physical_path;
    return "linux:" + device.linux_path;
}

class DeviceActivityGuard final {
public:
    DeviceActivityGuard(const lazarus::DeviceIdentity& device, std::string operation)
        : key_(activity_key(device)) {
        std::lock_guard lock(activity_mutex);
        if (active_devices.contains(key_)) return;
        active_devices.emplace(key_, DeviceActivity{device, std::move(operation), "starting"});
        acquired_ = true;
    }

    DeviceActivityGuard(const DeviceActivityGuard&) = delete;
    DeviceActivityGuard& operator=(const DeviceActivityGuard&) = delete;

    ~DeviceActivityGuard() {
        if (!acquired_) return;
        std::lock_guard lock(activity_mutex);
        active_devices.erase(key_);
    }

    bool acquired() const { return acquired_; }

    void phase(const std::string& value) {
        if (!acquired_) return;
        std::lock_guard lock(activity_mutex);
        const auto found = active_devices.find(key_);
        if (found != active_devices.end()) found->second.phase = value;
    }

private:
    std::string key_;
    bool acquired_ = false;
};

std::optional<DeviceActivity> device_activity(const lazarus::DeviceIdentity& device) {
    std::lock_guard lock(activity_mutex);
    const auto found = active_devices.find(activity_key(device));
    if (found == active_devices.end()) return std::nullopt;
    return found->second;
}

bool operation_in_progress() {
    std::lock_guard lock(activity_mutex);
    return !active_devices.empty();
}

struct PrinterInfo {
    std::string name;
    std::string state;
    std::string uri;
    bool is_default = false;
};

struct DiscoveredPrinter {
    std::string name;
    std::string uri;
};

constexpr const char* kCupsServer = "/run/cups/cups.sock";

std::string trim_copy(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool run_program(const std::vector<std::string>& arguments, std::string& output, std::string& error) {
    if (arguments.empty()) {
        error = "No command was provided.";
        return false;
    }
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) {
        error = std::strerror(errno);
        return false;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        error = std::strerror(errno);
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        return false;
    }
    if (child == 0) {
        ::dup2(descriptors[1], STDOUT_FILENO);
        ::dup2(descriptors[1], STDERR_FILENO);
        ::close(descriptors[0]);
        ::close(descriptors[1]);
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());
        _exit(127);
    }

    ::close(descriptors[1]);
    char buffer[4096];
    while (output.size() < 1024 * 1024) {
        const ssize_t count = ::read(descriptors[0], buffer, sizeof(buffer));
        if (count <= 0) break;
        output.append(buffer, static_cast<std::size_t>(count));
    }
    ::close(descriptors[0]);
    int status = 0;
    if (::waitpid(child, &status, 0) < 0) {
        error = std::strerror(errno);
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = trim_copy(output);
        if (error.empty()) {
            error = WIFEXITED(status)
                ? arguments.front() + " exited with status " + std::to_string(WEXITSTATUS(status)) + "."
                : arguments.front() + " did not exit cleanly.";
        }
        return false;
    }
    return true;
}

bool valid_printer_name(const std::string& name) {
    return !name.empty() && name.size() <= 127 &&
           std::all_of(name.begin(), name.end(), [](unsigned char character) {
               return std::isalnum(character) || character == '-' || character == '_';
           });
}

bool valid_printer_uri(const std::string& uri) {
    if (uri.empty() || uri.size() > 512) return false;
    static constexpr const char* schemes[] = {"ipp://", "ipps://", "dnssd://", "usb://", "socket://", "lpd://"};
    return std::any_of(std::begin(schemes), std::end(schemes), [&uri](const char* scheme) {
        return uri.rfind(scheme, 0) == 0;
    });
}

std::string printer_queue_name(const std::string& display_name) {
    std::string result;
    result.reserve(std::min<std::size_t>(display_name.size(), 127));
    bool separator = false;
    for (const unsigned char character : display_name) {
        if (std::isalnum(character)) {
            result.push_back(static_cast<char>(character));
            separator = false;
        } else if (!result.empty() && !separator) {
            result.push_back('_');
            separator = true;
        }
        if (result.size() == 127) break;
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? "Network_Printer" : result;
}

bool ipv4_literal(const std::string& value) {
    in_addr address{};
    return ::inet_pton(AF_INET, value.c_str(), &address) == 1;
}

std::optional<std::string> resolve_printer_ipv4(const std::string& hostname) {
    if (ipv4_literal(hostname)) return hostname;

    std::string output;
    std::string ignored_error;
    if (run_program({"timeout", "5", "avahi-resolve-host-name", "-4", hostname}, output, ignored_error)) {
        const auto separator = output.find('\t');
        const auto address = trim_copy(separator == std::string::npos ? output : output.substr(separator + 1));
        if (ipv4_literal(address)) return address;
    }

    output.clear();
    if (run_program({"timeout", "5", "getent", "ahostsv4", hostname}, output, ignored_error)) {
        const auto separator = output.find_first_of(" \t\r\n");
        const auto address = trim_copy(output.substr(0, separator));
        if (ipv4_literal(address)) return address;
    }
    return std::nullopt;
}

std::optional<std::string> printer_uri_with_ipv4(const std::string& uri, const std::string& hostname) {
    const auto address = resolve_printer_ipv4(hostname);
    if (!address) return std::nullopt;
    const auto scheme_end = uri.find("://");
    if (scheme_end == std::string::npos) return std::nullopt;
    const auto authority_start = scheme_end + 3;
    const auto authority_end = uri.find('/', authority_start);
    const auto end = authority_end == std::string::npos ? uri.size() : authority_end;
    auto authority = uri.substr(authority_start, end - authority_start);
    const auto hostname_position = authority.find(hostname);
    if (hostname_position != std::string::npos) {
        authority.replace(hostname_position, hostname.size(), *address);
    } else {
        const auto port = authority.rfind(':');
        authority = *address + (port == std::string::npos ? "" : authority.substr(port));
    }
    return uri.substr(0, authority_start) + authority + uri.substr(end);
}

std::string printer_uri_hostname(const std::string& uri) {
    const auto scheme_end = uri.find("://");
    if (scheme_end == std::string::npos) return {};
    const auto authority_start = scheme_end + 3;
    const auto authority_end = uri.find('/', authority_start);
    auto authority = uri.substr(authority_start,
        (authority_end == std::string::npos ? uri.size() : authority_end) - authority_start);
    if (authority.empty()) return {};
    if (authority.front() == '[') {
        const auto close = authority.find(']');
        return close == std::string::npos ? std::string{} : authority.substr(1, close - 1);
    }
    const auto port = authority.rfind(':');
    if (port != std::string::npos) authority.resize(port);
    return authority;
}

bool discover_printers(std::vector<DiscoveredPrinter>& printers, std::string& error) {
    std::string output;
    const bool found = run_program({
        "timeout", "8", "ippfind", "-T", "5", "_ipp._tcp", "_ipps._tcp",
        "-x", "/usr/bin/printf", "%s\\t%s\\t%s\\n",
        "{service_name}", "{}", "{service_hostname}", ";"
    }, output, error);

    // ippfind returns 1 when browsing completed without a matching service.
    if (!found && output.empty() && error.find("exited with status 1") != std::string::npos) {
        error.clear();
        return true;
    }
    if (!found) {
        error = "Network printer discovery failed. Verify that Avahi is running and that the printer and Lazarus are on the same network. " + error;
        return false;
    }

    std::set<std::string> seen;
    std::size_t announced = 0;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim_copy(line);
        if (line.empty()) continue;
        const auto separator = line.find('\t');
        if (separator == std::string::npos) continue;
        const auto second_separator = line.find('\t', separator + 1);
        if (second_separator == std::string::npos) continue;
        ++announced;
        auto name = trim_copy(line.substr(0, separator));
        auto uri = trim_copy(line.substr(separator + 1, second_separator - separator - 1));
        const auto hostname = trim_copy(line.substr(second_separator + 1));
        const auto numeric_uri = printer_uri_with_ipv4(uri, hostname);
        if (!numeric_uri) continue;
        uri = *numeric_uri;
        if (!valid_printer_uri(uri) || !seen.insert(uri).second) continue;
        if (name.empty()) name = uri;
        const auto duplicate_name = std::find_if(printers.begin(), printers.end(), [&name](const DiscoveredPrinter& printer) {
            return printer.name == name;
        });
        if (duplicate_name != printers.end()) {
            if (uri.rfind("ipp://", 0) == 0 && duplicate_name->uri.rfind("ipp://", 0) != 0) {
                duplicate_name->uri = std::move(uri);
            }
            continue;
        }
        printers.push_back({std::move(name), std::move(uri)});
    }
    if (printers.empty() && announced > 0) {
        error = "Printers were announced on the network, but Lazarus could not resolve their numeric IPv4 addresses.";
        return false;
    }
    return true;
}

bool valid_printer_address(const std::string& address) {
    if (address.empty() || address.size() > 253) return false;
    return std::all_of(address.begin(), address.end(), [](unsigned char character) {
        return std::isalnum(character) || character == '.' || character == '-' ||
               character == ':' || character == '[' || character == ']' ||
               character == '%' || character == '_';
    });
}

std::string printer_host_for_uri(const std::string& address) {
    if (address.find(':') == std::string::npos ||
        (address.front() == '[' && address.back() == ']')) {
        return address;
    }
    return "[" + address + "]";
}

bool configure_printer(const std::string& name, const std::string& uri,
                       const std::string& model, std::string& output, std::string& error) {
    return run_program({"timeout", "30", "lpadmin", "-h", kCupsServer,
                        "-p", name, "-E", "-v", uri, "-m", model},
                       output, error);
}

bool load_printers(std::vector<PrinterInfo>& printers, std::string& default_printer, std::string& error) {
    std::string scheduler;
    if (!run_program({"lpstat", "-h", kCupsServer, "-r"}, scheduler, error)) {
        error = "The CUPS print service is not running. " + error;
        return false;
    }

    std::string status;
    std::string status_error;
    const bool status_ok = run_program({"lpstat", "-h", kCupsServer, "-p", "-d"}, status, status_error);
    if (!status_ok && status.find("No destinations added") == std::string::npos) {
        error = status_error;
        return false;
    }
    std::istringstream lines(status);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.rfind("printer ", 0) == 0) {
            const auto name_start = std::string{"printer "}.size();
            const auto name_end = line.find(' ', name_start);
            PrinterInfo printer;
            printer.name = line.substr(name_start, name_end - name_start);
            printer.state = name_end == std::string::npos ? "configured" : trim_copy(line.substr(name_end + 1));
            printers.push_back(std::move(printer));
        } else if (line.rfind("system default destination: ", 0) == 0) {
            default_printer = trim_copy(line.substr(std::string{"system default destination: "}.size()));
        }
    }

    std::string devices;
    std::string ignored_error;
    run_program({"lpstat", "-h", kCupsServer, "-v"}, devices, ignored_error);
    std::istringstream device_lines(devices);
    while (std::getline(device_lines, line)) {
        if (line.rfind("device for ", 0) != 0) continue;
        const auto separator = line.find(':', std::string{"device for "}.size());
        if (separator == std::string::npos) continue;
        const auto name = line.substr(std::string{"device for "}.size(), separator - std::string{"device for "}.size());
        for (auto& printer : printers) {
            if (printer.name == name) printer.uri = trim_copy(line.substr(separator + 1));
        }
    }
    for (auto& printer : printers) printer.is_default = printer.name == default_printer;
    return true;
}

bool persist_printer_state(std::string& error) {
    const std::filesystem::path source_root = "/etc/cups";
    const std::filesystem::path storage_root = "/mnt/lazarus-storage";
    if (!std::filesystem::exists(source_root / "printers.conf") || !std::filesystem::is_directory(storage_root)) {
        return true;
    }

    const auto destination_root = storage_root / "cups";
    std::error_code filesystem_error;
    std::filesystem::create_directories(destination_root / "ppd", filesystem_error);
    if (filesystem_error) {
        error = "Could not create persistent printer storage: " + filesystem_error.message();
        return false;
    }
    for (const char* name : {"printers.conf", "printers.conf.O", "classes.conf", "classes.conf.O"}) {
        const auto source = source_root / name;
        if (!std::filesystem::exists(source)) continue;
        std::filesystem::copy_file(source, destination_root / name,
                                   std::filesystem::copy_options::overwrite_existing, filesystem_error);
        if (filesystem_error) {
            error = "Could not persist " + source.string() + ": " + filesystem_error.message();
            return false;
        }
    }
    if (std::filesystem::is_directory(source_root / "ppd")) {
        for (const auto& entry : std::filesystem::directory_iterator(source_root / "ppd", filesystem_error)) {
            if (filesystem_error) break;
            if (!entry.is_regular_file()) continue;
            std::filesystem::copy_file(entry.path(), destination_root / "ppd" / entry.path().filename(),
                                       std::filesystem::copy_options::overwrite_existing, filesystem_error);
            if (filesystem_error) break;
        }
    }
    if (filesystem_error) {
        error = "Could not persist CUPS driver data: " + filesystem_error.message();
        return false;
    }
    return true;
}

std::string hex_encode(const unsigned char* data, std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2, '0');
    for (std::size_t index = 0; index < size; ++index) {
        result[index * 2] = digits[data[index] >> 4];
        result[index * 2 + 1] = digits[data[index] & 0x0f];
    }
    return result;
}

bool read_exact_at(int descriptor, void* buffer, std::size_t size, std::uint64_t offset) {
    auto* output = static_cast<unsigned char*>(buffer);
    std::size_t completed = 0;
    while (completed < size) {
        const auto count = ::pread(descriptor, output + completed, size - completed,
                                   static_cast<off_t>(offset + completed));
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        completed += static_cast<std::size_t>(count);
    }
    return true;
}

std::string partition_table_identifier(const std::string& device_path) {
    const int descriptor = ::open(device_path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) return {};

    std::array<unsigned char, 512> mbr{};
    if (!read_exact_at(descriptor, mbr.data(), mbr.size(), 0) ||
        mbr[510] != 0x55 || mbr[511] != 0xaa) {
        ::close(descriptor);
        return {};
    }

    unsigned int logical_sector_size = 512;
    if (::ioctl(descriptor, BLKSSZGET, &logical_sector_size) != 0 || logical_sector_size < 512) {
        logical_sector_size = 512;
    }
    std::vector<unsigned char> gpt_header(logical_sector_size);
    const bool has_gpt = read_exact_at(descriptor, gpt_header.data(), gpt_header.size(), logical_sector_size) &&
                         std::equal(gpt_header.begin(), gpt_header.begin() + 8,
                                    std::array<unsigned char, 8>{'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'}.begin());
    if (has_gpt) {
        const auto* guid = gpt_header.data() + 56;
        if (std::any_of(guid, guid + 16, [](unsigned char byte) { return byte != 0; })) {
            char formatted[37]{};
            std::snprintf(formatted, sizeof(formatted),
                          "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                          guid[3], guid[2], guid[1], guid[0], guid[5], guid[4], guid[7], guid[6],
                          guid[8], guid[9], guid[10], guid[11], guid[12], guid[13], guid[14], guid[15]);
            ::close(descriptor);
            return formatted;
        }
    }

    ::close(descriptor);
    const std::uint32_t mbr_identifier = static_cast<std::uint32_t>(mbr[440]) |
                                         (static_cast<std::uint32_t>(mbr[441]) << 8U) |
                                         (static_cast<std::uint32_t>(mbr[442]) << 16U) |
                                         (static_cast<std::uint32_t>(mbr[443]) << 24U);
    if (mbr_identifier == 0) return {};
    char formatted[9]{};
    std::snprintf(formatted, sizeof(formatted), "%08x", mbr_identifier);
    return formatted;
}

std::vector<unsigned char> hex_decode(const std::string& value) {
    if (value.size() % 2 != 0) return {};
    const auto nibble = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
    };
    std::vector<unsigned char> result(value.size() / 2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        const int high = nibble(value[index * 2]);
        const int low = nibble(value[index * 2 + 1]);
        if (high < 0 || low < 0) return {};
        result[index] = static_cast<unsigned char>((high << 4) | low);
    }
    return result;
}

std::string random_hex(std::size_t bytes) {
    std::vector<unsigned char> random(bytes);
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) return {};
    return hex_encode(random.data(), random.size());
}

std::string recovery_key() {
    static constexpr char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::vector<unsigned char> random(20);
    if (RAND_bytes(random.data(), static_cast<int>(random.size())) != 1) return {};
    std::string result = "LAZ-";
    for (std::size_t index = 0; index < random.size(); ++index) {
        if (index > 0 && index % 5 == 0) result.push_back('-');
        result.push_back(alphabet[random[index] & 31]);
    }
    return result;
}

std::string derive_secret(const std::string& secret, const std::string& salt_hex, int iterations) {
    const auto salt = hex_decode(salt_hex);
    if (secret.empty() || salt.empty() || iterations < 100000) return {};
    std::vector<unsigned char> hash(kHashBytes);
    if (PKCS5_PBKDF2_HMAC(secret.data(), static_cast<int>(secret.size()), salt.data(),
                          static_cast<int>(salt.size()), iterations, EVP_sha256(),
                          static_cast<int>(hash.size()), hash.data()) != 1) {
        return {};
    }
    return hex_encode(hash.data(), hash.size());
}

bool constant_time_equal(const std::string& left, const std::string& right) {
    return left.size() == right.size() && !left.empty() &&
           CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::optional<AdminRecord> load_admin_record(const ServiceConfig& config) {
    std::ifstream input(config.security_path);
    if (!input) return std::nullopt;
    AdminRecord record;
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) continue;
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1);
        if (key == "iterations") {
            try { record.iterations = std::stoi(value); } catch (...) { return std::nullopt; }
        } else if (key == "password_salt") record.password_salt = value;
        else if (key == "password_hash") record.password_hash = value;
        else if (key == "recovery_salt") record.recovery_salt = value;
        else if (key == "recovery_hash") record.recovery_hash = value;
    }
    if (record.iterations < 100000 || record.password_salt.empty() || record.password_hash.empty() ||
        record.recovery_salt.empty() || record.recovery_hash.empty()) return std::nullopt;
    return record;
}

bool write_admin_record_file(const std::filesystem::path& path, const AdminRecord& record, std::string& error) {
    std::error_code filesystem_error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error) { error = filesystem_error.message(); return false; }
    const auto temporary = path.string() + ".tmp." + std::to_string(::getpid());
    const std::string contents =
        "version=1\n"
        "iterations=" + std::to_string(record.iterations) + "\n" +
        "password_salt=" + record.password_salt + "\n" +
        "password_hash=" + record.password_hash + "\n" +
        "recovery_salt=" + record.recovery_salt + "\n" +
        "recovery_hash=" + record.recovery_hash + "\n";

    const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0) {
        error = "Could not create admin credential file: " + std::string(std::strerror(errno));
        return false;
    }

    std::size_t written = 0;
    while (written < contents.size()) {
        const ssize_t count = ::write(descriptor, contents.data() + written, contents.size() - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            error = "Could not write admin credential file: " + std::string(std::strerror(errno));
            ::close(descriptor);
            std::filesystem::remove(temporary);
            return false;
        }
        written += static_cast<std::size_t>(count);
    }
    if (::fsync(descriptor) != 0) {
        error = "Could not commit admin credential file: " + std::string(std::strerror(errno));
        ::close(descriptor);
        std::filesystem::remove(temporary);
        return false;
    }
    if (::close(descriptor) != 0) {
        error = "Could not close admin credential file: " + std::string(std::strerror(errno));
        std::filesystem::remove(temporary);
        return false;
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
        error = "Could not replace admin credential file: " + std::string(std::strerror(errno));
        std::filesystem::remove(temporary);
        return false;
    }
    if (::chmod(path.c_str(), 0600) != 0) {
        error = "Could not protect admin credential file: " + std::string(std::strerror(errno));
        return false;
    }

    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
    const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory < 0) {
        error = "Could not open credential directory for synchronization: " + std::string(std::strerror(errno));
        return false;
    }
    const bool synchronized = ::fsync(directory) == 0;
    const int sync_error = errno;
    ::close(directory);
    if (!synchronized) {
        error = "Could not commit credential directory: " + std::string(std::strerror(sync_error));
        return false;
    }
    return true;
}

bool save_admin_record(const ServiceConfig& config, const AdminRecord& record, std::string& error) {
    if (!write_admin_record_file(config.security_path, record, error)) return false;
    const std::filesystem::path backup(config.security_backup_path);
    std::error_code filesystem_error;
    if (backup.has_parent_path() && std::filesystem::is_directory(backup.parent_path(), filesystem_error)) {
        std::string backup_error;
        if (!write_admin_record_file(backup, record, backup_error)) {
            error = "Credentials were saved locally but could not be mirrored to persistent storage: " + backup_error;
            return false;
        }
    }
    return true;
}

bool valid_new_password(const std::string& password) {
    return !password.empty() && password.size() <= 256;
}

bool schedule_power_action(const std::string& executable, std::string& error) {
    if (::access(executable.c_str(), X_OK) != 0) {
        error = "System power command is unavailable: " + executable;
        return false;
    }
    const pid_t child = ::fork();
    if (child < 0) {
        error = "Could not schedule system power action: " + std::string(std::strerror(errno));
        return false;
    }
    if (child == 0) {
        ::sleep(1);
        ::sync();
        ::execl(executable.c_str(), executable.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return true;
}

std::string create_admin_session(bool recovery_login) {
    std::lock_guard lock(admin_mutex);
    const auto token = random_hex(32);
    if (!token.empty()) admin_sessions[token] = {std::chrono::steady_clock::now() + kAdminSessionLifetime, recovery_login};
    return token;
}

bool validate_admin_session(const std::string& token) {
    std::lock_guard lock(admin_mutex);
    const auto now = std::chrono::steady_clock::now();
    for (auto iterator = admin_sessions.begin(); iterator != admin_sessions.end();) {
        if (iterator->second.expires <= now) iterator = admin_sessions.erase(iterator);
        else ++iterator;
    }
    const auto session = admin_sessions.find(token);
    if (session == admin_sessions.end()) return false;
    session->second.expires = now + kAdminSessionLifetime;
    return true;
}

struct BackupSummary {
    std::string image_directory;
    std::string ticket_number;
    std::string customer_name;
    std::string technician;
    std::string purpose;
    std::string created_date;
    bool finalized = false;
    bool incomplete = false;
};

class SocketStreamBuffer final : public std::streambuf {
public:
    explicit SocketStreamBuffer(int fd) : fd_(fd) {}

protected:
    std::streamsize xsputn(const char* data, std::streamsize size) override {
        std::streamsize written = 0;
        while (written < size) {
            const ssize_t count = ::send(fd_, data + written, static_cast<std::size_t>(size - written), MSG_NOSIGNAL);
            if (count > 0) {
                written += count;
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            break;
        }
        return written;
    }

    int overflow(int character) override {
        if (character == traits_type::eof()) return traits_type::not_eof(character);
        const char byte = static_cast<char>(character);
        return xsputn(&byte, 1) == 1 ? character : traits_type::eof();
    }

    int sync() override { return 0; }

private:
    int fd_;
};

std::string json_escape(const std::string& value) {
    std::string out;
    for (const unsigned char ch : value) {
        switch (ch) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    char buffer[8] = {};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return out;
}

std::string quote(const std::string& value) {
    return "\"" + json_escape(value) + "\"";
}

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (const char ch : value) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out += "'";
    return out;
}

std::string extract_json_string(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) {
        return "";
    }
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) {
        return "";
    }
    auto pos = text.find_first_not_of(" \t\r\n", colon + 1);
    if (pos == std::string::npos || text[pos] != '"') {
        return "";
    }
    ++pos;
    std::string out;
    bool escape = false;
    for (; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (escape) {
            switch (ch) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    out.push_back(ch);
                    break;
            }
            escape = false;
            continue;
        }
        if (ch == '\\') {
            escape = true;
            continue;
        }
        if (ch == '"') {
            break;
        }
        out.push_back(ch);
    }
    return out;
}

std::uint64_t extract_json_u64(const std::string& text, const std::string& key) {
    const auto key_pos = text.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return 0;
    const auto colon = text.find(':', key_pos);
    if (colon == std::string::npos) return 0;
    const auto first = text.find_first_of("0123456789", colon + 1);
    if (first == std::string::npos) return 0;
    const auto last = text.find_first_not_of("0123456789", first);
    try {
        return std::stoull(text.substr(first, last - first));
    } catch (...) {
        return 0;
    }
}

std::string trim(std::string value) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) {
        text += line + "\n";
    }
    return text;
}

std::string labels_to_text(const std::vector<lazarus::PortLabel>& labels) {
    std::string text;
    for (const auto& label : labels) {
        text += label.identity + "|" + label.label + "\n";
    }
    return text;
}

std::vector<lazarus::PortLabel> labels_from_text(const std::string& text) {
    std::vector<lazarus::PortLabel> labels;
    for (const auto& line : split_lines(text)) {
        const auto separator = line.find('|');
        if (separator == std::string::npos) {
            continue;
        }
        lazarus::PortLabel label;
        label.identity = trim(line.substr(0, separator));
        label.label = trim(line.substr(separator + 1));
        if (!label.identity.empty() && !label.label.empty()) {
            labels.push_back(label);
        }
    }
    return labels;
}

bool ensure_writable_directory(const std::string& path, std::string& error) {
    if (path.empty()) {
        error = "Path is empty.";
        return false;
    }
    std::error_code fs_error;
    std::filesystem::create_directories(path, fs_error);
    if (fs_error) {
        error = "Could not create directory: " + fs_error.message();
        return false;
    }
    if (!std::filesystem::is_directory(path, fs_error)) {
        error = "Path is not a directory.";
        return false;
    }
    const auto test_path = std::filesystem::path(path) / ".lazarus-write-test";
    {
        std::ofstream out(test_path);
        if (!out) {
            error = "Could not create a test file in that directory.";
            return false;
        }
        out << "ok\n";
    }
    std::filesystem::remove(test_path, fs_error);
    error.clear();
    return true;
}

bool path_is_beneath(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error || canonical_root.empty()) return false;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, error);
    if (error || canonical_candidate.empty() || canonical_candidate == canonical_root) return false;
    const auto relative = canonical_candidate.lexically_relative(canonical_root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    return true;
}

bool image_path_allowed(const lazarus::BenchProfile& bench, const std::string& value, bool must_exist, std::string& error) {
    if (value.empty()) {
        error = "No image path was provided.";
        return false;
    }
    const std::filesystem::path candidate(value);
    std::error_code filesystem_error;
    if (must_exist && !std::filesystem::is_directory(candidate, filesystem_error)) {
        error = "The selected image directory does not exist.";
        return false;
    }
    for (const auto& root : bench.image_storage_paths) {
        if (path_is_beneath(candidate, root)) return true;
    }
    error = "The image path is outside every configured image-storage directory.";
    return false;
}

bool image_path_configured(const lazarus::BenchProfile& bench, const std::string& value, std::string& error) {
    if (value.empty()) {
        error = "No image path was provided.";
        return false;
    }
    const auto candidate = std::filesystem::absolute(value).lexically_normal();
    for (const auto& configured : bench.image_storage_paths) {
        const auto root = std::filesystem::absolute(configured).lexically_normal();
        if (candidate == root) continue;
        const auto relative = candidate.lexically_relative(root);
        if (relative.empty() || relative.is_absolute()) continue;
        const bool escapes = std::any_of(relative.begin(), relative.end(), [](const auto& component) {
            return component == "..";
        });
        if (!escapes) return true;
    }
    error = "The image path is outside every configured image-storage directory.";
    return false;
}

bool network_link_online() {
    const std::filesystem::path network_root("/sys/class/net");
    std::error_code error;
    if (!std::filesystem::is_directory(network_root, error)) return false;
    for (const auto& entry : std::filesystem::directory_iterator(network_root, error)) {
        if (error || entry.path().filename() == "lo") continue;
        std::ifstream state(entry.path() / "operstate");
        std::string value;
        state >> value;
        if (value == "up") return true;
    }
    return false;
}

std::string network_ipv4_address() {
    ifaddrs* interfaces = nullptr;
    if (::getifaddrs(&interfaces) != 0) return {};

    std::string fallback;
    for (auto* current = interfaces; current != nullptr; current = current->ifa_next) {
        if (current->ifa_addr == nullptr || current->ifa_addr->sa_family != AF_INET ||
            (current->ifa_flags & IFF_LOOPBACK) != 0 || (current->ifa_flags & IFF_UP) == 0) {
            continue;
        }
        char address[INET_ADDRSTRLEN]{};
        const auto* socket_address = reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
        if (::inet_ntop(AF_INET, &socket_address->sin_addr, address, sizeof(address)) == nullptr) continue;
        const std::string value(address);
        if (value.rfind("169.254.", 0) != 0) {
            ::freeifaddrs(interfaces);
            return value;
        }
        if (fallback.empty()) fallback = value;
    }
    ::freeifaddrs(interfaces);
    return fallback;
}

NetworkSettings load_network_settings(const std::string& path) {
    NetworkSettings settings;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos || line.empty() || line.front() == '#') continue;
        const auto key = trim_copy(line.substr(0, separator));
        const auto value = trim_copy(line.substr(separator + 1));
        if (key == "mode") settings.mode = value;
        else if (key == "interface") settings.interface = value;
        else if (key == "address") settings.address = value;
        else if (key == "prefix") settings.prefix = value;
        else if (key == "gateway") settings.gateway = value;
        else if (key == "dns") settings.dns = value;
    }
    if (settings.mode != "static") settings.mode = "dhcp";
    if (settings.interface.empty()) settings.interface = "auto";
    if (settings.prefix.empty()) settings.prefix = "24";
    return settings;
}

bool valid_ipv4_address(const std::string& value) {
    in_addr address{};
    return !value.empty() && ::inet_pton(AF_INET, value.c_str(), &address) == 1;
}

bool valid_network_interface(const std::string& value) {
    if (value == "auto") return true;
    if (value.empty() || value == "lo" || value.find('/') != std::string::npos) return false;
    return std::filesystem::is_directory(std::filesystem::path("/sys/class/net") / value);
}

std::string network_interfaces_rows() {
    std::unordered_map<std::string, std::string> addresses;
    ifaddrs* interfaces = nullptr;
    if (::getifaddrs(&interfaces) == 0) {
        for (auto* current = interfaces; current != nullptr; current = current->ifa_next) {
            if (current->ifa_addr == nullptr || current->ifa_addr->sa_family != AF_INET) continue;
            char address[INET_ADDRSTRLEN]{};
            const auto* socket_address = reinterpret_cast<const sockaddr_in*>(current->ifa_addr);
            if (::inet_ntop(AF_INET, &socket_address->sin_addr, address, sizeof(address)) != nullptr) {
                addresses[current->ifa_name] = address;
            }
        }
        ::freeifaddrs(interfaces);
    }

    std::vector<std::string> rows;
    const std::filesystem::path root("/sys/class/net");
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (error) break;
        const auto name = entry.path().filename().string();
        if (name == "lo") continue;
        std::ifstream state_file(entry.path() / "operstate");
        std::ifstream carrier_file(entry.path() / "carrier");
        std::ifstream address_file(entry.path() / "address");
        std::string state = "unknown";
        std::string carrier = "unknown";
        std::string mac;
        state_file >> state;
        carrier_file >> carrier;
        address_file >> mac;
        const bool wireless = std::filesystem::is_directory(entry.path() / "wireless") ||
                              std::filesystem::exists(entry.path() / "phy80211");
        rows.push_back(name + "\t" + (wireless ? "wireless" : "wired") + "\t" + state + "\t" +
                       (carrier == "1" ? "connected" : "disconnected") + "\t" + addresses[name] + "\t" + mac);
    }
    std::sort(rows.begin(), rows.end());
    std::string output;
    for (const auto& row : rows) output += row + "\n";
    return output;
}

std::string recent_network_log() {
    std::ifstream input("/var/log/arcology-lazarus/network.log");
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!trim_copy(line).empty()) lines.push_back(line);
        if (lines.size() > 12) lines.erase(lines.begin());
    }
    std::string output;
    for (const auto& value : lines) output += value + "\n";
    return output;
}

bool save_network_settings(const ServiceConfig& config, const NetworkSettings& settings, std::string& error) {
    const std::filesystem::path path(config.network_path);
    std::error_code filesystem_error;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), filesystem_error);
    if (filesystem_error) {
        error = "Could not create the network configuration directory: " + filesystem_error.message();
        return false;
    }
    const auto temporary = path.string() + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            error = "Could not open the temporary network configuration.";
            return false;
        }
        output << "# Arcology Lazarus wired network configuration.\n"
               << "mode=" << settings.mode << "\n"
               << "interface=" << settings.interface << "\n"
               << "address=" << settings.address << "\n"
               << "prefix=" << settings.prefix << "\n"
               << "gateway=" << settings.gateway << "\n"
               << "dns=" << settings.dns << "\n";
        if (!output) {
            error = "Could not write the temporary network configuration.";
            std::filesystem::remove(temporary, filesystem_error);
            return false;
        }
    }
    std::filesystem::permissions(temporary, std::filesystem::perms::owner_read |
        std::filesystem::perms::owner_write | std::filesystem::perms::group_read |
        std::filesystem::perms::others_read, std::filesystem::perm_options::replace, filesystem_error);
    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error) {
        error = "Could not replace the active network configuration: " + filesystem_error.message();
        std::filesystem::remove(temporary, filesystem_error);
        return false;
    }
    const std::filesystem::path persistent_root("/mnt/lazarus-storage");
    if (std::filesystem::is_directory(persistent_root)) {
        std::filesystem::copy_file(path, persistent_root / "network.conf",
                                   std::filesystem::copy_options::overwrite_existing, filesystem_error);
        if (filesystem_error) {
            error = "Network settings were applied locally but could not be copied to persistent storage: " +
                    filesystem_error.message();
            return false;
        }
    }
    return true;
}

bool image_storage_online(const lazarus::BenchProfile& bench) {
    if (!bench.image_storage_device.empty()) {
        std::string mounted;
        std::string error;
        if (!run_program({"findmnt", "-rn", "-M", "/mnt/lazarus-storage", "-o", "TARGET"}, mounted, error) ||
            trim_copy(mounted) != "/mnt/lazarus-storage") {
            return false;
        }
    }
    for (const auto& path : bench.image_storage_paths) {
        if (!std::filesystem::is_directory(path)) continue;
        std::string output;
        std::string error;
        if (run_program({"findmnt", "-rn", "-T", path, "-o", "TARGET"}, output, error) &&
            !trim_copy(output).empty()) return true;
    }
    return false;
}

std::string image_storage_mount_source() {
    std::string source;
    std::string error;
    if (!run_program({"findmnt", "-rn", "-M", "/mnt/lazarus-storage", "-o", "SOURCE"}, source, error)) return {};
    return trim_copy(source);
}

bool path_is_mountpoint(const std::filesystem::path& path) {
    std::string output;
    std::string error;
    return run_program({"findmnt", "-rn", "-M", path.string(), "-o", "TARGET"}, output, error) &&
           trim_copy(output) == path.string();
}

bool matches_device_selector(const lazarus::DeviceIdentity& device, const std::string& selector) {
    return device.linux_path == selector || device.physical_path == selector || device.by_id_path == selector || device.by_path == selector;
}

std::string identity_for_profile(const lazarus::DeviceIdentity& device) {
    if (!device.by_id_path.empty()) {
        return device.by_id_path;
    }
    if (!device.by_path.empty()) return device.by_path;
    if (!device.physical_path.empty()) return device.physical_path;
    return device.linux_path;
}

std::string port_identity_for_profile(const lazarus::DeviceIdentity& device) {
    if (!device.port_path.empty()) return device.port_path;
    if (!device.by_path.empty()) return lazarus::physical_port_identity(device.by_path);
    return {};
}

std::optional<lazarus::DeviceIdentity> find_device(const lazarus::BenchProfile& bench, const std::string& selector) {
    const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
    for (const auto& device : devices) {
        if (matches_device_selector(device, selector)) {
            return device;
        }
    }
    return std::nullopt;
}

bool ensure_image_storage_ready(const lazarus::BenchProfile& bench, std::string& error) {
    if (image_storage_online(bench)) {
        error.clear();
        return true;
    }
    if (bench.image_storage_device.empty()) {
        error = "Image storage is not configured. An administrator must assign and prepare an image-storage disk.";
        return false;
    }
    const auto device = find_device(bench, bench.image_storage_device);
    if (!device) {
        error = "The configured image-storage disk is not connected. Connect it to its assigned storage port.";
        return false;
    }
    if (!bench.image_storage_port_paths.empty() && device->bench_role != lazarus::DeviceRole::ImageStorage) {
        error = "The image-storage disk is connected to the wrong physical port. Move it to the configured image-storage port.";
        return false;
    }
    DeviceActivityGuard activity(*device, "Prepare image storage");
    if (!activity.acquired()) {
        error = "Image storage is busy with another Lazarus operation.";
        return false;
    }
    std::string output;
    if (!run_program({"/usr/local/sbin/lazarus-mount-storage"}, output, error) ||
        !path_is_mountpoint("/mnt/lazarus-storage")) {
        error = "The configured image-storage filesystem could not be mounted. " + error +
                (output.empty() ? std::string{} : " " + trim_copy(output));
        return false;
    }
    if (!std::filesystem::is_directory("/mnt/lazarus-storage/images")) {
        error = "Image storage mounted, but its images directory is unavailable.";
        return false;
    }
    error.clear();
    return true;
}

std::string hex_encode_text(const std::string& value) {
    return hex_encode(reinterpret_cast<const unsigned char*>(value.data()), value.size());
}

std::optional<std::string> hex_decode_text(const std::string& value) {
    if (value.empty()) return std::string{};
    const auto decoded = hex_decode(value);
    if (decoded.empty()) return std::nullopt;
    return std::string(reinterpret_cast<const char*>(decoded.data()), decoded.size());
}

std::string sha256_text(const std::string& value) {
    std::vector<unsigned char> digest(EVP_MAX_MD_SIZE);
    unsigned int digest_size = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(context, value.data(), value.size()) != 1 ||
        EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1) {
        if (context != nullptr) EVP_MD_CTX_free(context);
        return {};
    }
    EVP_MD_CTX_free(context);
    return hex_encode(digest.data(), digest_size);
}

bool safe_relative_path(const std::string& value, std::filesystem::path& result, std::string& error) {
    result = std::filesystem::path(value).lexically_normal();
    if (value.find('\0') != std::string::npos || result.is_absolute() || result.has_root_path()) {
        error = "The requested path is not relative to the selected volume.";
        return false;
    }
    for (const auto& component : result) {
        if (component == "..") {
            error = "The requested path attempts to leave the selected volume.";
            return false;
        }
    }
    if (result == ".") result.clear();
    return true;
}

bool resolve_browse_path(const std::filesystem::path& root, const std::string& relative,
                         std::filesystem::path& resolved, std::string& error) {
    std::filesystem::path clean;
    if (!safe_relative_path(relative, clean, error)) return false;
    std::error_code filesystem_error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, filesystem_error);
    if (filesystem_error) {
        error = "The read-only volume root is unavailable: " + filesystem_error.message();
        return false;
    }
    resolved = std::filesystem::weakly_canonical(root / clean, filesystem_error);
    if (filesystem_error || (resolved != canonical_root && !path_is_beneath(resolved, canonical_root))) {
        error = "The requested path is outside the selected read-only volume.";
        return false;
    }
    return true;
}

bool supported_browse_filesystem(const std::string& filesystem) {
    static const std::set<std::string> supported = {"ntfs", "ntfs3", "vfat", "fat", "exfat", "ext2", "ext3", "ext4"};
    return supported.contains(filesystem);
}

std::string command_value(const std::vector<std::string>& arguments) {
    std::string output;
    std::string error;
    if (!run_program(arguments, output, error)) return {};
    return trim_copy(output);
}

std::string decode_blkid_value(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] == '\\' && index + 3 < value.size() && value[index + 1] == 'x') {
            const auto hex_digit = [](char character) -> int {
                if (character >= '0' && character <= '9') return character - '0';
                if (character >= 'a' && character <= 'f') return character - 'a' + 10;
                if (character >= 'A' && character <= 'F') return character - 'A' + 10;
                return -1;
            };
            const int high = hex_digit(value[index + 2]);
            const int low = hex_digit(value[index + 3]);
            if (high >= 0 && low >= 0) {
                decoded.push_back(static_cast<char>((high << 4) | low));
                index += 3;
                continue;
            }
        }
        decoded.push_back(value[index]);
    }
    return decoded;
}

std::string block_device_tag(const std::string& device_path, const std::string& tag) {
    std::string output;
    std::string error;
    if (!run_program({"blkid", device_path}, output, error)) return {};
    const auto marker = tag + "=\"";
    const auto start = output.find(marker);
    if (start == std::string::npos) return {};
    const auto value_start = start + marker.size();
    const auto end = output.find('"', value_start);
    if (end == std::string::npos) return {};
    return decode_blkid_value(output.substr(value_start, end - value_start));
}

bool mount_browse_volume(BrowseVolume& volume, const std::string& session_id, std::string& error) {
    if (!volume.mount_path.empty()) return true;
    if (!supported_browse_filesystem(volume.filesystem)) {
        error = "The " + (volume.filesystem.empty() ? std::string("unknown") : volume.filesystem) +
                " filesystem is not supported by the read-only browser.";
        return false;
    }
    const auto mount_path = std::filesystem::path("/run/arcology-lazarus/browse") / session_id / volume.token;
    std::error_code filesystem_error;
    std::filesystem::create_directories(mount_path, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the protected browse mount point: " + filesystem_error.message();
        return false;
    }
    std::string output;
    if (!run_program({"mount", "-o", "ro,nosuid,nodev,noexec", volume.device_path, mount_path.string()}, output, error)) {
        error = "Could not mount the image volume read-only: " + error;
        return false;
    }
    volume.mount_path = mount_path.string();
    return true;
}

bool close_browse_session(BrowseSession& session, std::string* reported_error = nullptr) {
    bool closed = true;
    for (auto& volume : session.volumes) {
        if (volume.mount_path.empty()) continue;
        std::string output;
        std::string error;
        if (!run_program({"umount", volume.mount_path}, output, error)) {
            closed = false;
            if (reported_error != nullptr && reported_error->empty()) {
                *reported_error = "Could not unmount a read-only image volume: " + error;
            }
            continue;
        }
        std::error_code filesystem_error;
        std::filesystem::remove(volume.mount_path, filesystem_error);
        volume.mount_path.clear();
    }
    if (closed && !session.loop_device.empty()) {
        std::string output;
        std::string error;
        if (run_program({"losetup", "-d", session.loop_device}, output, error)) {
            session.loop_device.clear();
        } else {
            closed = false;
            if (reported_error != nullptr && reported_error->empty()) {
                *reported_error = "Could not detach the read-only image loop device: " + error;
            }
        }
    }
    if (closed) {
        std::error_code filesystem_error;
        std::filesystem::remove_all(std::filesystem::path("/run/arcology-lazarus/browse") / session.id, filesystem_error);
    }
    return closed;
}

bool destination_volume_for_device(const lazarus::DeviceIdentity& device, std::string& volume_path,
                                   std::string& filesystem, std::string& error) {
    std::vector<std::string> candidates = device.partitions;
    if (candidates.empty()) candidates.push_back(device.linux_path);
    for (const auto& candidate : candidates) {
        const auto type = block_device_tag(candidate, "TYPE");
        if (supported_browse_filesystem(type)) {
            volume_path = candidate;
            filesystem = type;
            return true;
        }
    }
    error = "No supported filesystem was found on the removable-media device. FAT, exFAT, NTFS, and ext filesystems are supported.";
    return false;
}

bool copy_recovered_file(const std::filesystem::path& source, const std::filesystem::path& destination,
                         std::uint64_t& bytes, std::string& error) {
    const int input = ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (input < 0) {
        error = "Could not open source file read-only: " + source.filename().string() + ": " + std::strerror(errno);
        return false;
    }
    const int output = ::open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (output < 0) {
        error = "Could not create recovered file without overwriting existing data: " + destination.string() + ": " + std::strerror(errno);
        ::close(input);
        return false;
    }
    std::vector<char> buffer(1024 * 1024);
    bool ok = true;
    while (true) {
        const auto count = ::read(input, buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) {
            error = "Read failed while recovering " + source.filename().string() + ": " + std::strerror(errno);
            ok = false;
            break;
        }
        ssize_t offset = 0;
        while (offset < count) {
            const auto written = ::write(output, buffer.data() + offset, static_cast<std::size_t>(count - offset));
            if (written <= 0) {
                error = "Write failed while recovering " + source.filename().string() + ": " + std::strerror(errno);
                ok = false;
                break;
            }
            offset += written;
            bytes += static_cast<std::uint64_t>(written);
        }
        if (!ok) break;
    }
    if (::fsync(output) != 0 && ok) {
        error = "Could not flush recovered file " + destination.filename().string() + ": " + std::strerror(errno);
        ok = false;
    }
    ::close(output);
    ::close(input);
    if (!ok) ::unlink(destination.c_str());
    return ok;
}

bool copy_recovered_tree(const std::filesystem::path& source, const std::filesystem::path& destination,
                         std::uint64_t& files, std::uint64_t& directories, std::uint64_t& bytes,
                         std::size_t depth, std::string& error) {
    if (depth > 128) {
        error = "The selected directory tree exceeds Lazarus's safe recursion limit.";
        return false;
    }
    std::error_code filesystem_error;
    const auto status = std::filesystem::symlink_status(source, filesystem_error);
    if (filesystem_error || std::filesystem::is_symlink(status)) {
        error = "Symbolic links are not exported from images.";
        return false;
    }
    if (std::filesystem::is_regular_file(status)) {
        if (!copy_recovered_file(source, destination, bytes, error)) return false;
        ++files;
        return true;
    }
    if (!std::filesystem::is_directory(status)) {
        error = "Only regular files and directories can be exported.";
        return false;
    }
    if (!std::filesystem::create_directory(destination, filesystem_error) || filesystem_error) {
        error = "Could not create recovered directory without overwriting existing data: " + destination.string();
        return false;
    }
    ++directories;
    for (const auto& entry : std::filesystem::directory_iterator(source, filesystem_error)) {
        if (filesystem_error) {
            error = "Could not enumerate " + source.string() + ": " + filesystem_error.message();
            return false;
        }
        if (!copy_recovered_tree(entry.path(), destination / entry.path().filename(), files, directories, bytes,
                                 depth + 1, error)) return false;
    }
    return true;
}

bool has_imageable_layout(const lazarus::DiskInspection& inspection) {
    return inspection.gpt_header_valid || inspection.mbr_detected || !inspection.partitions.empty();
}

bool has_blocker(const std::vector<lazarus::SafetyFinding>& findings) {
    for (const auto& finding : findings) {
        if (finding.severity == lazarus::Severity::Blocker) {
            return true;
        }
    }
    return false;
}

std::string finding_json(const lazarus::SafetyFinding& finding) {
    return "{\"severity\":" + quote(lazarus::to_string(finding.severity)) +
           ",\"code\":" + quote(finding.code) +
           ",\"observed\":" + quote(finding.observed) +
           ",\"action\":" + quote(finding.action) + "}";
}

std::string findings_json(const std::vector<lazarus::SafetyFinding>& findings) {
    std::string out = "[";
    for (std::size_t i = 0; i < findings.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += finding_json(findings[i]);
    }
    out += "]";
    return out;
}

std::string failure_message(const std::vector<lazarus::SafetyFinding>& findings,
                            const std::string& fallback) {
    const lazarus::SafetyFinding* selected = nullptr;
    for (const auto& finding : findings) {
        if (selected == nullptr || finding.severity == lazarus::Severity::Blocker) {
            selected = &finding;
        }
        if (finding.severity == lazarus::Severity::Blocker) break;
    }
    if (selected == nullptr) return fallback;

    std::string message = selected->observed.empty() ? fallback : selected->observed;
    if (!selected->code.empty()) message += " [" + selected->code + "]";
    if (!selected->action.empty()) message += " Recommended: " + selected->action;
    return message;
}

std::string failure_error_field(bool succeeded,
                                const std::vector<lazarus::SafetyFinding>& findings,
                                const std::string& fallback) {
    return succeeded ? "" : "\"error\":" + quote(failure_message(findings, fallback)) + ",";
}

std::string string_array_json(const std::vector<std::string>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += quote(values[i]);
    }
    out += "]";
    return out;
}

bool request_boolean(const std::string& request, const std::string& key) {
    const auto position = request.find("\"" + key + "\"");
    if (position == std::string::npos) return false;
    const auto colon = request.find(':', position);
    if (colon == std::string::npos) return false;
    const auto value = request.find_first_not_of(" \t\r\n", colon + 1);
    return value != std::string::npos && request.compare(value, 4, "true") == 0;
}

std::string driver_plan_actions_json(const std::vector<lazarus::DriverPlanItem>& actions) {
    std::string output = "[";
    for (std::size_t index = 0; index < actions.size(); ++index) {
        if (index != 0) output += ",";
        const auto& action = actions[index];
        output += "{\"action\":" + quote(action.action) +
                  ",\"inf_path\":" + quote(action.inf_path) +
                  ",\"published_name\":" + quote(action.published_name) +
                  ",\"matching_hardware_ids\":" + string_array_json(action.matching_hardware_ids) +
                  ",\"reason\":" + quote(action.reason) + "}";
    }
    return output + "]";
}

std::string driver_plan_rows(const lazarus::DriverMigrationPlan& plan) {
    std::string rows;
    for (const auto& action : plan.actions) {
        rows += action.action + "\t" + (action.inf_path.empty() ? action.published_name : action.inf_path) +
                "\t" + action.reason + "\n";
    }
    return rows;
}

std::string sha256_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Could not open staged file for hashing: " + path.string();
        return {};
    }
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr || EVP_DigestInit_ex(context, EVP_sha256(), nullptr) != 1) {
        if (context != nullptr) EVP_MD_CTX_free(context);
        error = "Could not initialize SHA-256 for staged driver files.";
        return {};
    }
    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), buffer.size());
        const auto count = stream.gcount();
        if (count > 0 && EVP_DigestUpdate(context, buffer.data(), static_cast<std::size_t>(count)) != 1) {
            EVP_MD_CTX_free(context);
            error = "Could not hash staged driver file: " + path.string();
            return {};
        }
    }
    if (!stream.eof()) {
        EVP_MD_CTX_free(context);
        error = "Could not read the complete staged driver file: " + path.string();
        return {};
    }
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    if (EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1) {
        EVP_MD_CTX_free(context);
        error = "Could not finalize SHA-256 for staged driver file.";
        return {};
    }
    EVP_MD_CTX_free(context);
    return hex_encode(digest.data(), digest_size);
}

bool copy_driver_package(const std::filesystem::path& source, const std::filesystem::path& destination,
                         std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(destination, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the driver staging directory: " + filesystem_error.message();
        return false;
    }
    std::uint64_t total_bytes = 0;
    std::size_t file_count = 0;
    for (std::filesystem::recursive_directory_iterator iterator(
             source, std::filesystem::directory_options::skip_permission_denied, filesystem_error), end;
         iterator != end; iterator.increment(filesystem_error)) {
        if (filesystem_error) {
            error = "Could not enumerate the complete driver package: " + filesystem_error.message();
            return false;
        }
        const auto status = iterator->symlink_status(filesystem_error);
        if (filesystem_error || std::filesystem::is_symlink(status)) {
            error = "Driver package changed or contains a symbolic link during staging.";
            return false;
        }
        auto relative = std::filesystem::relative(iterator->path(), source, filesystem_error);
        if (filesystem_error || relative.empty() || relative.has_root_path()) {
            error = "Could not confine a driver package path during staging.";
            return false;
        }
        const auto target = destination / relative;
        if (std::filesystem::is_directory(status)) {
            std::filesystem::create_directories(target, filesystem_error);
            if (filesystem_error) {
                error = "Could not create a staged driver subdirectory: " + filesystem_error.message();
                return false;
            }
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            error = "Driver package contains a non-regular file: " + relative.string();
            return false;
        }
        const auto size = iterator->file_size(filesystem_error);
        if (filesystem_error || ++file_count > 50000 || size > 1024ULL * 1024ULL * 1024ULL ||
            total_bytes > 8ULL * 1024ULL * 1024ULL * 1024ULL - size) {
            error = "Driver package exceeds Lazarus staging limits.";
            return false;
        }
        total_bytes += size;
        std::filesystem::copy_file(iterator->path(), target, std::filesystem::copy_options::none, filesystem_error);
        if (filesystem_error) {
            error = "Could not stage driver file " + relative.string() + ": " + filesystem_error.message();
            return false;
        }
    }
    return true;
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return "";
    }
    std::string text;
    std::getline(in, text, '\0');
    return text;
}

std::string sysfs_hex_component(const std::filesystem::path& path) {
    auto value = trim_copy(read_text_file(path));
    if (value.starts_with("0x") || value.starts_with("0X")) value.erase(0, 2);
    if (value.empty() || value.size() > 8 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char character) { return std::isxdigit(character) != 0; })) {
        return {};
    }
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    while (value.size() < 4) value.insert(value.begin(), '0');
    return value;
}

std::vector<std::string> destination_storage_hardware_ids(const std::string& device_path) {
    const auto device_name = std::filesystem::path(device_path).filename();
    if (device_name.empty()) return {};
    std::error_code filesystem_error;
    auto current = std::filesystem::canonical(std::filesystem::path("/sys/class/block") / device_name / "device",
                                              filesystem_error);
    if (filesystem_error) return {};

    while (current != current.root_path() && !current.empty()) {
        const auto class_code = sysfs_hex_component(current / "class");
        const auto vendor = sysfs_hex_component(current / "vendor");
        const auto device = sysfs_hex_component(current / "device");
        if (class_code.starts_with("01") && !vendor.empty() && !device.empty()) {
            std::vector<std::string> ids;
            const auto subsystem_vendor = sysfs_hex_component(current / "subsystem_vendor");
            const auto subsystem_device = sysfs_hex_component(current / "subsystem_device");
            const auto base = "PCI\\VEN_" + vendor + "&DEV_" + device;
            if (!subsystem_vendor.empty() && !subsystem_device.empty()) {
                ids.push_back(base + "&SUBSYS_" + subsystem_device + subsystem_vendor);
            }
            ids.push_back(base);
            return ids;
        }
        current = current.parent_path();
    }
    return {};
}

std::vector<std::string> automatic_driver_package_paths(const lazarus::BenchProfile& bench) {
    std::set<std::string> package_roots;
    std::size_t entries_seen = 0;
    auto storage_roots = bench.image_storage_paths;
    if (storage_roots.empty() && !bench.image_storage_path.empty()) storage_roots.push_back(bench.image_storage_path);
    for (const auto& storage_root : storage_roots) {
        const auto vault = std::filesystem::path(storage_root) / "DriverVault";
        std::error_code filesystem_error;
        if (!std::filesystem::is_directory(vault, filesystem_error)) continue;
        std::filesystem::recursive_directory_iterator iterator(
            vault, std::filesystem::directory_options::skip_permission_denied, filesystem_error);
        const std::filesystem::recursive_directory_iterator end;
        for (; !filesystem_error && iterator != end && entries_seen < 50000 && package_roots.size() < 512;
             iterator.increment(filesystem_error), ++entries_seen) {
            const auto status = iterator->symlink_status(filesystem_error);
            if (filesystem_error) break;
            if (std::filesystem::is_symlink(status)) {
                if (std::filesystem::is_directory(status)) iterator.disable_recursion_pending();
                continue;
            }
            if (!std::filesystem::is_regular_file(status)) continue;
            auto extension = iterator->path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            if (extension != ".inf") continue;
            const auto parent = std::filesystem::weakly_canonical(iterator->path().parent_path(), filesystem_error);
            if (!filesystem_error) package_roots.insert(parent.string());
        }
    }
    return {package_roots.begin(), package_roots.end()};
}

std::filesystem::path case_insensitive_child(const std::filesystem::path& parent, const std::string& wanted) {
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        return value;
    };
    const auto target = lower(wanted);
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(parent, error)) {
        if (error) break;
        if (lower(entry.path().filename().string()) == target) return entry.path();
    }
    return {};
}

std::filesystem::path case_insensitive_path(std::filesystem::path current,
                                            std::initializer_list<const char*> components) {
    for (const auto* component : components) {
        current = case_insensitive_child(current, component);
        if (current.empty()) return {};
    }
    return current;
}

class HiveHandle final {
public:
    explicit HiveHandle(const std::filesystem::path& path) : handle_(hivex_open(path.c_str(), HIVEX_OPEN_WRITE)) {}
    HiveHandle(const HiveHandle&) = delete;
    HiveHandle& operator=(const HiveHandle&) = delete;
    ~HiveHandle() { if (handle_ != nullptr) hivex_close(handle_); }
    hive_h* get() const { return handle_; }
    bool close() {
        if (handle_ == nullptr) return true;
        const bool ok = hivex_close(handle_) == 0;
        handle_ = nullptr;
        return ok;
    }
private:
    hive_h* handle_ = nullptr;
};

hive_node_h hive_path(hive_h* hive, hive_node_h parent,
                      const std::vector<std::string>& components, bool create) {
    auto current = parent;
    for (const auto& component : components) {
        auto child = hivex_node_get_child(hive, current, component.c_str());
        if (child == 0 && create) child = hivex_node_add_child(hive, current, component.c_str());
        if (child == 0) return 0;
        current = child;
    }
    return current;
}

std::string utf16le(const std::string& value, bool double_terminator = false) {
    std::string encoded;
    encoded.reserve((value.size() + (double_terminator ? 2 : 1)) * 2);
    for (const unsigned char character : value) {
        encoded.push_back(static_cast<char>(character));
        encoded.push_back('\0');
    }
    encoded.append(double_terminator ? 4 : 2, '\0');
    return encoded;
}

bool hive_set(hive_h* hive, hive_node_h node, const std::string& key,
              hive_type type, const std::string& bytes, std::string& error) {
    hive_set_value value{
        const_cast<char*>(key.c_str()), type, bytes.size(), const_cast<char*>(bytes.data())};
    if (hivex_node_set_value(hive, node, &value, 0) == 0) return true;
    error = "Could not update the offline Windows SYSTEM hive at value '" + key + "': " + std::strerror(errno);
    return false;
}

bool hive_set_dword(hive_h* hive, hive_node_h node, const std::string& key,
                    std::uint32_t number, std::string& error) {
    std::string value(4, '\0');
    value[0] = static_cast<char>(number & 0xffU);
    value[1] = static_cast<char>((number >> 8U) & 0xffU);
    value[2] = static_cast<char>((number >> 16U) & 0xffU);
    value[3] = static_cast<char>((number >> 24U) & 0xffU);
    return hive_set(hive, node, key, hive_t_REG_DWORD, value, error);
}

bool hive_set_string(hive_h* hive, hive_node_h node, const std::string& key,
                     hive_type type, const std::string& text, std::string& error) {
    return hive_set(hive, node, key, type, utf16le(text), error);
}

std::string registry_safe_name(std::string value) {
    for (auto& character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (!std::isalnum(byte) && character != '_' && character != '-') character = '_';
    }
    return value;
}

bool install_bootstrap_registry(hive_h* hive, const lazarus::DriverMigrationPlan& plan,
                                std::vector<std::string>& facts, std::string& error) {
    const auto root = hivex_root(hive);
    const auto select = hive_path(hive, root, {"Select"}, false);
    if (root == 0 || select == 0) {
        error = "The offline SYSTEM hive does not contain the Select key.";
        return false;
    }
    const auto current_value = hivex_node_get_value(hive, select, "Current");
    if (current_value == 0) {
        error = "The offline SYSTEM hive does not identify its current control set.";
        return false;
    }
    const auto current_number = hivex_value_dword(hive, current_value);
    if (current_number <= 0 || current_number > 999) {
        error = "The offline SYSTEM hive contains an invalid current control-set number.";
        return false;
    }
    char control_set_name[16]{};
    std::snprintf(control_set_name, sizeof(control_set_name), "ControlSet%03d", current_number);
    const auto driver_database = hive_path(hive, root, {"DriverDatabase"}, false);

    for (const auto& action : plan.actions) {
        if (action.action != "add") continue;
        const auto service = hive_path(hive, root, {control_set_name, "Services", action.service_name}, true);
        if (service == 0 ||
            !hive_set_dword(hive, service, "Type", action.service_type, error) ||
            !hive_set_dword(hive, service, "Start", action.start_type, error) ||
            !hive_set_dword(hive, service, "ErrorControl", action.error_control, error) ||
            !hive_set_string(hive, service, "Group", hive_t_REG_SZ,
                             action.load_order_group.empty() ? "SCSI miniport" : action.load_order_group, error) ||
            !hive_set_string(hive, service, "ImagePath", hive_t_REG_EXPAND_SZ,
                             "system32\\drivers\\" + action.service_binary, error)) {
            return false;
        }

        for (const auto& full_id : action.matching_hardware_ids) {
            const auto separator = full_id.find('\\');
            if (separator == std::string::npos || separator + 1 >= full_id.size()) {
                error = "A matched storage-controller hardware ID could not be represented in the Windows registry.";
                return false;
            }
            const auto bus = full_id.substr(0, separator);
            const auto device_id = full_id.substr(separator + 1);
            if (driver_database == 0) {
                auto critical_name = bus + "#" + device_id;
                std::replace(critical_name.begin(), critical_name.end(), '\\', '#');
                const auto critical = hive_path(
                    hive, root, {control_set_name, "Control", "CriticalDeviceDatabase", critical_name}, true);
                if (critical == 0 ||
                    !hive_set_string(hive, critical, "Service", hive_t_REG_SZ, action.service_name, error) ||
                    !hive_set_string(hive, critical, "ClassGUID", hive_t_REG_SZ,
                                     "{4D36E97B-E325-11CE-BFC1-08002BE10318}", error)) return false;
                continue;
            }

            const auto safe_service = registry_safe_name(action.service_name);
            const auto inf_name = "lazarus-" + safe_service + ".inf";
            const auto inf_label = inf_name + "_amd64_0000000000000000";
            const auto configuration = "lazarus_" + safe_service + "_conf";
            const auto inf_files = hive_path(hive, root, {"DriverDatabase", "DriverInfFiles", inf_name}, true);
            const auto device_ids = hive_path(hive, root, {"DriverDatabase", "DeviceIds", bus, device_id}, true);
            const auto package = hive_path(hive, root, {"DriverDatabase", "DriverPackages", inf_label}, true);
            const auto package_configuration = hive_path(
                hive, root, {"DriverDatabase", "DriverPackages", inf_label, "Configurations", configuration}, true);
            const auto descriptor = hive_path(
                hive, root, {"DriverDatabase", "DriverPackages", inf_label, "Descriptors", bus, device_id}, true);
            if (inf_files == 0 || device_ids == 0 || package == 0 || package_configuration == 0 || descriptor == 0) {
                error = "Could not create the offline DriverDatabase bootstrap entries.";
                return false;
            }
            const auto inf_multi = utf16le(inf_label, true);
            const std::string device_marker{"\x01\xff\x00\x00", 4};
            const unsigned char version_bytes[] = {
                0x00,0xff,0x09,0x00,0x00,0x00,0x00,0x00,
                0x7b,0xe9,0x36,0x4d,0x25,0xe3,0xce,0x11,
                0xbf,0xc1,0x08,0x00,0x2b,0xe1,0x03,0x18,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
            const std::string version(reinterpret_cast<const char*>(version_bytes), sizeof(version_bytes));
            if (!hive_set(hive, inf_files, "", hive_t_REG_MULTI_SZ, inf_multi, error) ||
                !hive_set_string(hive, inf_files, "Active", hive_t_REG_SZ, inf_label, error) ||
                !hive_set(hive, inf_files, "Configurations", hive_t_REG_MULTI_SZ,
                          utf16le(configuration, true), error) ||
                !hive_set(hive, device_ids, inf_name, hive_t_REG_BINARY, device_marker, error) ||
                !hive_set(hive, package, "Version", hive_t_REG_BINARY, version, error) ||
                !hive_set_dword(hive, package_configuration, "ConfigFlags", 0, error) ||
                !hive_set_string(hive, package_configuration, "Service", hive_t_REG_SZ, action.service_name, error) ||
                !hive_set_string(hive, descriptor, "Configuration", hive_t_REG_SZ, configuration, error)) return false;
        }
        facts.push_back("Registered " + action.service_name + " as a boot-start storage service in " + control_set_name + ".");
    }
    return true;
}

bool apply_native_windows_bootstrap(const std::filesystem::path& windows_directory,
                                    const lazarus::DriverMigrationPlan& plan,
                                    const std::vector<std::string>& package_paths,
                                    const std::string& job_id,
                                    std::vector<std::string>& facts,
                                    std::string& error) {
    const auto system32 = case_insensitive_child(windows_directory, "System32");
    const auto config = system32.empty() ? std::filesystem::path{} : case_insensitive_child(system32, "config");
    const auto drivers = system32.empty() ? std::filesystem::path{} : case_insensitive_child(system32, "drivers");
    const auto system_hive = config.empty() ? std::filesystem::path{} : case_insensitive_child(config, "SYSTEM");
    if (system32.empty() || config.empty() || drivers.empty() || system_hive.empty()) {
        error = "The selected NTFS volume does not contain a complete offline Windows SYSTEM hive and driver directory.";
        return false;
    }

    const auto lazarus_root = windows_directory / "Lazarus" / "UniversalRestore" / job_id;
    std::error_code filesystem_error;
    std::filesystem::create_directories(lazarus_root / "packages", filesystem_error);
    if (filesystem_error) {
        error = "Could not create the Lazarus rollback directory inside restored Windows: " + filesystem_error.message();
        return false;
    }
    const auto hive_backup = lazarus_root / "SYSTEM.before";
    std::filesystem::copy_file(system_hive, hive_backup, std::filesystem::copy_options::none, filesystem_error);
    if (filesystem_error) {
        error = "Could not create the mandatory offline SYSTEM hive rollback copy: " + filesystem_error.message();
        return false;
    }

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> driver_rollbacks;
    const auto rollback = [&]() {
        std::error_code rollback_error;
        std::filesystem::copy_file(hive_backup, system_hive, std::filesystem::copy_options::overwrite_existing, rollback_error);
        for (const auto& [target, backup] : driver_rollbacks) {
            rollback_error.clear();
            if (backup.empty()) std::filesystem::remove(target, rollback_error);
            else std::filesystem::copy_file(backup, target, std::filesystem::copy_options::overwrite_existing, rollback_error);
        }
    };

    std::set<std::string> staged_packages;
    std::size_t package_number = 0;
    for (const auto& action : plan.actions) {
        if (action.action != "add") continue;
        const auto source_binary = std::filesystem::path(action.service_binary_path);
        if (!std::filesystem::is_regular_file(source_binary)) {
            error = "The planned boot driver disappeared before servicing: " + source_binary.string();
            rollback();
            return false;
        }
        std::filesystem::path package_root;
        for (const auto& candidate : package_paths) {
            if (path_is_beneath(source_binary, candidate)) {
                package_root = candidate;
                break;
            }
        }
        if (package_root.empty()) {
            error = "The planned boot driver is outside the confined DriverVault package roots.";
            rollback();
            return false;
        }
        if (staged_packages.insert(package_root.string()).second) {
            if (!copy_driver_package(package_root, lazarus_root / "packages" / ("package-" + std::to_string(++package_number)), error)) {
                rollback();
                return false;
            }
        }

        auto target_binary = case_insensitive_child(drivers, action.service_binary);
        if (target_binary.empty()) target_binary = drivers / action.service_binary;
        std::filesystem::path previous;
        if (std::filesystem::exists(target_binary)) {
            previous = lazarus_root / (action.service_binary + ".before");
            std::filesystem::copy_file(target_binary, previous, std::filesystem::copy_options::none, filesystem_error);
            if (filesystem_error) {
                error = "Could not preserve the existing Windows boot driver before replacement: " + filesystem_error.message();
                rollback();
                return false;
            }
        }
        driver_rollbacks.emplace_back(target_binary, previous);
        std::filesystem::copy_file(source_binary, target_binary, std::filesystem::copy_options::overwrite_existing, filesystem_error);
        if (filesystem_error) {
            error = "Could not copy the replacement boot driver into Windows: " + filesystem_error.message();
            rollback();
            return false;
        }
        facts.push_back("Copied " + action.service_binary + " into Windows/System32/drivers.");
    }

    HiveHandle hive(system_hive);
    if (hive.get() == nullptr) {
        error = "Could not open the offline Windows SYSTEM hive for a transactional update: " + std::string(std::strerror(errno));
        rollback();
        return false;
    }
    if (!install_bootstrap_registry(hive.get(), plan, facts, error) || hivex_commit(hive.get(), nullptr, 0) != 0) {
        if (error.empty()) error = "Could not commit the offline Windows SYSTEM hive: " + std::string(std::strerror(errno));
        hive.close();
        rollback();
        return false;
    }
    if (!hive.close()) {
        error = "The offline Windows SYSTEM hive was written but could not be closed cleanly.";
        rollback();
        return false;
    }
    facts.push_back("A complete pre-change SYSTEM hive and replaced driver files remain in Windows/Lazarus/UniversalRestore/" + job_id + ".");
    return true;
}

std::string date_component(const std::string& value) {
    for (std::size_t offset = 0; offset + 10 <= value.size(); ++offset) {
        const auto digit = [&value](std::size_t index) {
            return std::isdigit(static_cast<unsigned char>(value[index])) != 0;
        };
        if (digit(offset) && digit(offset + 1) && digit(offset + 2) && digit(offset + 3) &&
            value[offset + 4] == '-' && digit(offset + 5) && digit(offset + 6) &&
            value[offset + 7] == '-' && digit(offset + 8) && digit(offset + 9)) {
            return value.substr(offset, 10);
        }
    }
    return "";
}

std::string file_date(const std::filesystem::path& path) {
    std::error_code error;
    const auto file_time = std::filesystem::last_write_time(path, error);
    if (error) return "unknown date";
    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        file_time - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
    const std::time_t time = std::chrono::system_clock::to_time_t(system_time);
    std::tm local{};
    localtime_r(&time, &local);
    char date[16]{};
    std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
    return date;
}

std::string friendly_path_component(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    return value;
}

bool looks_like_lazarus_image(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path, error) &&
           (std::filesystem::exists(path / "metadata.json", error) ||
            std::filesystem::exists(path / "job-journal.json", error)) &&
           ((std::filesystem::exists(path / "disk.raw", error) &&
             std::filesystem::exists(path / "hashes.dat", error)) ||
            std::filesystem::exists(path / "INCOMPLETE", error));
}

BackupSummary read_backup_summary(const std::filesystem::path& path) {
    auto metadata = read_text_file(path / "metadata.json");
    if (metadata.empty()) metadata = read_text_file(path / "job-journal.json");
    std::error_code error;
    BackupSummary summary;
    summary.image_directory = path.string();
    summary.ticket_number = extract_json_string(metadata, "ticket_number");
    summary.customer_name = extract_json_string(metadata, "customer_name");
    summary.technician = extract_json_string(metadata, "technician");
    summary.purpose = extract_json_string(metadata, "purpose");
    summary.created_date = date_component(extract_json_string(metadata, "created_at"));
    const auto directory_date = date_component(path.filename().string());
    if (summary.created_date.empty()) summary.created_date = directory_date;
    if (!directory_date.empty()) {
        if (summary.customer_name.empty()) summary.customer_name = friendly_path_component(path.parent_path().filename().string());
        if (summary.ticket_number.empty()) summary.ticket_number = path.parent_path().parent_path().filename().string();
    }
    if (summary.created_date.empty()) {
        summary.created_date = file_date(std::filesystem::exists(path / "metadata.json")
            ? path / "metadata.json" : path / "job-journal.json");
    }
    summary.finalized = std::filesystem::exists(path / "FINALIZED", error);
    summary.incomplete = std::filesystem::exists(path / "INCOMPLETE", error);
    return summary;
}

std::vector<BackupSummary> scan_backups(const lazarus::BenchProfile& bench) {
    std::vector<BackupSummary> backups;
    for (const auto& storage : bench.image_storage_paths) {
        std::error_code error;
        if (!std::filesystem::exists(storage, error)) {
            continue;
        }
        std::filesystem::recursive_directory_iterator it(storage, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        while (!error && it != end) {
            const auto path = it->path();
            if (looks_like_lazarus_image(path)) {
                backups.push_back(read_backup_summary(path));
                it.disable_recursion_pending();
            }
            it.increment(error);
        }
    }
    std::sort(backups.begin(), backups.end(), [](const BackupSummary& left, const BackupSummary& right) {
        return left.image_directory < right.image_directory;
    });
    return backups;
}

std::string backup_search_text(const BackupSummary& backup) {
    return backup.image_directory + "\t" + backup.ticket_number + "\t" + backup.customer_name + "\t" + backup.technician + "\t" + backup.purpose;
}

std::string backup_title(const BackupSummary& backup) {
    const auto ticket = backup.ticket_number.empty() ? "No ticket" : backup.ticket_number;
    const auto customer = backup.customer_name.empty() ? "Unknown customer" : backup.customer_name;
    const auto date = backup.created_date.empty() ? "unknown date" : backup.created_date;
    return ticket + " | " + customer + " | " + date + (backup.incomplete ? " | INTERRUPTED" : "");
}

std::string human_capacity(std::uint64_t bytes);

std::string html_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += character; break;
        }
    }
    return escaped;
}

struct ReportMetric {
    std::string label;
    std::string value;
};

struct JobPreset {
    const char* id;
    const char* label;
    const char* purpose;
    lazarus::ImagingMode mode;
    lazarus::CompressionMode compression;
    bool verify_after_imaging;
};

constexpr JobPreset kJobPresets[] = {
    {"backup-before-repair", "Backup Before Repair", "Backup Before Repair", lazarus::ImagingMode::Raw, lazarus::CompressionMode::Zstd, true},
    {"ssd-upgrade", "SSD Upgrade", "SSD Upgrade", lazarus::ImagingMode::Raw, lazarus::CompressionMode::Zstd, true},
    {"data-recovery", "Data Recovery", "Data Recovery", lazarus::ImagingMode::Rescue, lazarus::CompressionMode::Zstd, true},
    {"hardware-migration", "Hardware Migration", "Hardware Migration", lazarus::ImagingMode::Raw, lazarus::CompressionMode::Zstd, true},
    {"custom", "Custom", "", lazarus::ImagingMode::Raw, lazarus::CompressionMode::Zstd, false},
};

const JobPreset* find_job_preset(const std::string& id) {
    const auto found = std::find_if(std::begin(kJobPresets), std::end(kJobPresets), [&id](const JobPreset& preset) {
        return id == preset.id;
    });
    return found == std::end(kJobPresets) ? nullptr : &*found;
}

std::string job_presets_json() {
    std::string output = "[";
    for (std::size_t index = 0; index < std::size(kJobPresets); ++index) {
        if (index != 0) output += ',';
        const auto& preset = kJobPresets[index];
        output += "{\"id\":" + quote(preset.id) + ",\"label\":" + quote(preset.label) +
                  ",\"purpose\":" + quote(preset.purpose) + ",\"imaging_mode\":" +
                  quote(lazarus::to_string(preset.mode)) + ",\"compression\":" +
                  quote(lazarus::to_string(preset.compression)) + ",\"verify_after_imaging\":" +
                  std::string(preset.verify_after_imaging ? "true" : "false") + "}";
    }
    return output + "]";
}

std::pair<lazarus::JobInfo, lazarus::DeviceIdentity> report_context_from_image(
    const std::filesystem::path& image_directory) {
    auto document = read_text_file(image_directory / "metadata.json");
    if (document.empty()) document = read_text_file(image_directory / "job-journal.json");
    lazarus::JobInfo job;
    job.ticket_number = extract_json_string(document, "ticket_number");
    job.customer_name = extract_json_string(document, "customer_name");
    job.technician = extract_json_string(document, "technician");
    job.purpose = extract_json_string(document, "purpose");
    lazarus::DeviceIdentity device;
    device.linux_path = extract_json_string(document, "linux_path");
    device.physical_path = extract_json_string(document, "physical_path");
    device.by_id_path = extract_json_string(document, "by_id_path");
    device.by_path = extract_json_string(document, "by_path");
    device.model = extract_json_string(document, "model");
    device.serial = extract_json_string(document, "serial");
    device.serial_ending = extract_json_string(document, "serial_ending");
    device.size_bytes = extract_json_u64(document, "size_bytes");
    device.logical_block_size = static_cast<std::uint32_t>(extract_json_u64(document, "logical_block_size"));
    return {std::move(job), std::move(device)};
}

bool write_completion_reports(const lazarus::BenchProfile& bench,
                              const std::filesystem::path& image_directory,
                              const std::string& basename,
                              const std::string& operation,
                              const std::string& outcome,
                              const lazarus::JobInfo& job,
                              const lazarus::DeviceIdentity& device,
                              const std::vector<ReportMetric>& metrics,
                              const std::vector<std::string>& facts,
                              const std::vector<lazarus::SafetyFinding>& findings,
                              std::string& error) {
    std::error_code filesystem_error;
    std::filesystem::create_directories(image_directory, filesystem_error);
    if (filesystem_error) {
        error = "Could not create report directory: " + filesystem_error.message();
        return false;
    }

    std::ostringstream text;
    text << bench.branding.product_name << "\n" << operation << "\n\n"
         << "Outcome: " << outcome << "\n"
         << "Ticket: " << job.ticket_number << "\n"
         << "Customer: " << job.customer_name << "\n"
         << "Technician: " << job.technician << "\n"
         << "Purpose: " << job.purpose << "\n"
         << "Generated: " << file_date(image_directory) << "\n\n"
         << "DEVICE\n"
         << "Model: " << device.model << "\n"
         << "Capacity: " << human_capacity(device.size_bytes) << "\n"
         << "Serial ending: " << device.serial_ending << "\n"
         << "Physical connection: " << device.physical_path << "\n";
    for (const auto& metric : metrics) text << metric.label << ": " << metric.value << "\n";
    text << "\nFACTUAL RESULTS\n";
    for (const auto& fact : facts) text << "- " << fact << "\n";
    if (!findings.empty()) {
        text << "\nFINDINGS\n";
        for (const auto& finding : findings) {
            text << "- [" << lazarus::to_string(finding.severity) << "] " << finding.observed;
            if (!finding.action.empty()) text << " Recommended: " << finding.action;
            text << "\n";
        }
    }
    text << "\n" << bench.branding.report_footer << "\n";

    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\"><title>"
         << html_escape(operation) << "</title><style>"
         << "@page{size:letter;margin:.55in}body{font-family:DejaVu Sans,Arial,sans-serif;color:#17212a;margin:0}"
         << "header{border-bottom:4px solid " << html_escape(bench.branding.accent) << ";padding-bottom:14px;margin-bottom:20px}"
         << "h1{font-size:25px;margin:0}h2{font-size:17px;margin:5px 0 0;color:" << html_escape(bench.branding.accent) << "}"
         << ".outcome{border:2px solid " << html_escape(bench.branding.accent) << ";padding:12px;font-weight:bold;margin:15px 0}"
         << ".grid{display:grid;grid-template-columns:1fr 1fr;gap:7px 24px}.section{margin-top:20px}"
         << ".section h3{font-size:13px;letter-spacing:.08em;border-bottom:1px solid #bcc6ce;padding-bottom:5px}"
         << "dt{font-size:10px;color:#60717e;text-transform:uppercase}dd{margin:2px 0 8px;font-weight:600}"
         << "li{margin:6px 0}.footer{margin-top:28px;border-top:1px solid #bcc6ce;padding-top:9px;font-size:10px;color:#60717e}"
         << "</style></head><body><header><h1>" << html_escape(bench.branding.product_name)
         << "</h1><h2>" << html_escape(operation) << "</h2></header><div class=\"outcome\">Outcome: "
         << html_escape(outcome) << "</div><section class=\"section\"><h3>JOB</h3><dl class=\"grid\">"
         << "<div><dt>Ticket</dt><dd>" << html_escape(job.ticket_number) << "</dd></div>"
         << "<div><dt>Customer</dt><dd>" << html_escape(job.customer_name) << "</dd></div>"
         << "<div><dt>Technician</dt><dd>" << html_escape(job.technician) << "</dd></div>"
         << "<div><dt>Purpose</dt><dd>" << html_escape(job.purpose) << "</dd></div></dl></section>"
         << "<section class=\"section\"><h3>DEVICE AND OPERATION</h3><dl class=\"grid\">"
         << "<div><dt>Model</dt><dd>" << html_escape(device.model) << "</dd></div>"
         << "<div><dt>Capacity</dt><dd>" << html_escape(human_capacity(device.size_bytes)) << "</dd></div>"
         << "<div><dt>Serial ending</dt><dd>" << html_escape(device.serial_ending) << "</dd></div>"
         << "<div><dt>Physical connection</dt><dd>" << html_escape(device.physical_path) << "</dd></div>";
    for (const auto& metric : metrics) {
        html << "<div><dt>" << html_escape(metric.label) << "</dt><dd>" << html_escape(metric.value) << "</dd></div>";
    }
    html << "</dl></section><section class=\"section\"><h3>FACTUAL RESULTS</h3><ul>";
    for (const auto& fact : facts) html << "<li>" << html_escape(fact) << "</li>";
    html << "</ul></section>";
    if (!findings.empty()) {
        html << "<section class=\"section\"><h3>FINDINGS</h3><ul>";
        for (const auto& finding : findings) {
            html << "<li><strong>" << html_escape(lazarus::to_string(finding.severity)) << ":</strong> "
                 << html_escape(finding.observed);
            if (!finding.action.empty()) html << " <em>Recommended: " << html_escape(finding.action) << "</em>";
            html << "</li>";
        }
        html << "</ul></section>";
    }
    html << "<div class=\"footer\">" << html_escape(bench.branding.report_footer) << "</div></body></html>";

    for (const auto& output : std::vector<std::pair<std::filesystem::path, std::string>>{
             {image_directory / (basename + ".txt"), text.str()},
             {image_directory / (basename + ".html"), html.str()}}) {
        std::ofstream stream(output.first, std::ios::trunc);
        stream << output.second;
        stream.close();
        if (!stream) {
            error = "Could not write report: " + output.first.string();
            return false;
        }
    }
    return true;
}

std::string human_capacity(std::uint64_t bytes) {
    static constexpr const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(unit == 0 ? 0 : 1);
    out << value << " " << units[unit];
    return out.str();
}

std::pair<std::string, std::string> disconnect_state(const lazarus::BenchProfile& bench,
                                                      const lazarus::DeviceIdentity& device) {
    if (const auto active = device_activity(device)) {
        if (active->phase == "flush" || active->phase == "finalize") {
            return {"flushing", "FLUSHING - DO NOT DISCONNECT"};
        }
        return {"in-use", "IN USE - DO NOT DISCONNECT"};
    }
    if (device.is_system_disk) return {"system", "SYSTEM DEVICE - DO NOT DISCONNECT"};
    if ((device.bench_role == lazarus::DeviceRole::ImageStorage ||
         (!bench.image_storage_device.empty() && matches_device_selector(device, bench.image_storage_device))) &&
        path_is_mountpoint("/mnt/lazarus-storage")) {
        return {"mounted-storage", "IMAGE STORAGE - DO NOT DISCONNECT"};
    }
    return {"safe", "SAFE TO DISCONNECT"};
}

std::string device_fingerprint(const lazarus::BenchProfile& bench) {
    const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
    std::ostringstream out;
    for (const auto& device : devices) {
        const auto state = disconnect_state(bench, device);
        out << activity_key(device) << ':' << device.size_bytes << ':' << lazarus::to_string(device.bench_role)
            << ':' << state.first << ';';
    }
    return out.str();
}

std::string device_rows_text(const lazarus::BenchProfile& bench) {
    const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
    std::string rows;
    for (const auto& device : devices) {
        const auto label = lazarus::label_for_device(bench, device);
        const auto safety = disconnect_state(bench, device);
        std::string title = device.linux_path + " | " + lazarus::to_string(device.bench_role) + " | " + human_capacity(device.size_bytes);
        if (!label.empty()) {
            title += " | " + label;
        }
        std::string detail = device.model;
        if (!device.serial_ending.empty()) {
            detail += " | serial ending " + device.serial_ending;
        }
        detail += " | " + device.physical_path;
        std::string filesystems;
        std::string mountpoints;
        std::string probe_error;
        run_program({"lsblk", "-nr", "-o", "FSTYPE", device.linux_path}, filesystems, probe_error);
        run_program({"lsblk", "-nr", "-o", "MOUNTPOINTS", device.linux_path}, mountpoints, probe_error);
        const auto first_nonempty_line = [](const std::string& text) {
            std::istringstream input(text);
            std::string line;
            while (std::getline(input, line)) {
                line = trim_copy(line);
                if (!line.empty()) return line;
            }
            return std::string{};
        };
        const auto filesystem = first_nonempty_line(filesystems);
        const auto mountpoint = first_nonempty_line(mountpoints);
        rows += device.linux_path + "\t" + title + "\t" + detail + "\t" + identity_for_profile(device) + "\t" +
                (device.is_system_disk ? "1" : "0") + "\t" + label + "\t" + device.model + "\t" +
                std::to_string(device.size_bytes) + "\t" + lazarus::to_string(device.bench_role) + "\t" +
                device.transport + "\t" + device.serial_ending + "\t" + safety.first + "\t" + safety.second +
                "\t" + device.physical_path + "\t" + port_identity_for_profile(device) + "\t" +
                filesystem + "\t" + mountpoint + "\n";
    }
    return rows;
}

std::string role_for_port(const lazarus::BenchProfile& bench, const std::string& identity) {
    const auto contains = [&](const std::vector<std::string>& entries) {
        return std::any_of(entries.begin(), entries.end(), [&](const std::string& entry) {
            return entry == identity || lazarus::physical_port_identity(entry) == identity;
        });
    };
    if (contains(bench.source_only_paths)) return "source-only";
    if (contains(bench.destination_only_paths)) return "destination-only";
    if (contains(bench.image_storage_port_paths)) return "image-storage";
    if (contains(bench.removable_media_paths)) return "removable-media";
    if (contains(bench.ignored_paths)) return "ignored";
    return "unknown";
}

std::set<std::string> discover_usb_ports() {
    std::set<std::string> ports;
    const std::filesystem::path usb_devices("/sys/bus/usb/devices");
    std::error_code error;
    if (!std::filesystem::is_directory(usb_devices, error)) return ports;
    const std::regex pci_address("([0-9a-fA-F]{4}:[0-9a-fA-F]{2}:[0-9a-fA-F]{2}\\.[0-9a-fA-F])");
    for (const auto& entry : std::filesystem::directory_iterator(usb_devices, error)) {
        if (error) break;
        const auto maxchild_text = trim_copy(read_text_file(entry.path() / "maxchild"));
        const auto devpath = trim_copy(read_text_file(entry.path() / "devpath"));
        if (maxchild_text.empty() || devpath.empty()) continue;
        unsigned int maxchild = 0;
        try { maxchild = static_cast<unsigned int>(std::stoul(maxchild_text)); } catch (...) { continue; }
        if (maxchild == 0) continue;
        const auto canonical = std::filesystem::canonical(entry.path(), error).string();
        if (error) { error.clear(); continue; }
        std::sregex_iterator match(canonical.begin(), canonical.end(), pci_address);
        std::sregex_iterator end;
        std::string controller;
        for (; match != end; ++match) controller = (*match)[1].str();
        if (controller.empty()) continue;
        for (unsigned int child = 1; child <= maxchild; ++child) {
            const auto route = devpath == "0" ? std::to_string(child) : devpath + "." + std::to_string(child);
            ports.insert("port:pci-" + controller + "-usb-0:" + route);
        }
    }
    return ports;
}

std::string port_rows_text(const lazarus::BenchProfile& bench) {
    struct PortRow {
        std::string identity;
        std::string label;
        std::string role;
        bool online = false;
        bool system = false;
        std::string device_path;
        std::string model;
        std::uint64_t size_bytes = 0;
        std::string transport;
        std::string disk_identity;
    };
    std::map<std::string, PortRow> ports;
    const auto ensure = [&](const std::string& configured) -> PortRow* {
        const auto identity = lazarus::physical_port_identity(configured);
        if (identity.empty()) return nullptr;
        auto& port = ports[identity];
        port.identity = identity;
        return &port;
    };
    for (const auto& entries : {bench.source_only_paths, bench.destination_only_paths,
                                bench.image_storage_port_paths, bench.removable_media_paths,
                                bench.ignored_paths}) {
        for (const auto& configured : entries) ensure(configured);
    }
    for (const auto& configured : bench.port_labels) {
        if (auto* port = ensure(configured.identity)) port->label = configured.label;
    }
    for (const auto& identity : discover_usb_ports()) ensure(identity);
    for (const auto& device : lazarus::discover_block_devices()) {
        const auto identity = port_identity_for_profile(device);
        if (identity.empty()) continue;
        auto& port = ports[identity];
        port.identity = identity;
        port.online = true;
        port.system = device.is_system_disk;
        port.device_path = device.linux_path;
        port.model = device.model;
        port.size_bytes = device.size_bytes;
        port.transport = device.transport;
        port.disk_identity = identity_for_profile(device);
        port.role = lazarus::to_string(lazarus::role_for_device(bench, device));
    }
    std::string rows;
    for (auto& [identity, port] : ports) {
        const auto configured_role = role_for_port(bench, identity);
        if (configured_role != "unknown") port.role = configured_role;
        if (port.role.empty()) port.role = "unknown";
        rows += port.identity + "\t" + port.label + "\t" + port.role + "\t" +
                (port.online ? "1" : "0") + "\t" + (port.system ? "1" : "0") + "\t" +
                port.device_path + "\t" + port.model + "\t" + std::to_string(port.size_bytes) + "\t" +
                port.transport + "\t" + port.disk_identity + "\n";
    }
    return rows;
}

std::string backup_rows_text(const lazarus::BenchProfile& bench) {
    std::string rows;
    for (const auto& backup : scan_backups(bench)) {
        const auto journal = read_text_file(std::filesystem::path(backup.image_directory) / "job-journal.json");
        auto source_selector = extract_json_string(journal, "by_id_path");
        if (source_selector.empty()) source_selector = extract_json_string(journal, "by_path");
        if (source_selector.empty()) source_selector = extract_json_string(journal, "physical_path");
        if (source_selector.empty()) source_selector = extract_json_string(journal, "linux_path");
        rows += backup.image_directory + "\t" + backup_title(backup) + "\t" + backup_search_text(backup) + "\t" +
                (backup.incomplete ? "interrupted" : (backup.finalized ? "finalized" : "unknown")) + "\t" +
                source_selector + "\t" + extract_json_string(journal, "imaging_mode") + "\t" +
                extract_json_string(journal, "compression") + "\n";
    }
    return rows;
}

bool is_install_target(const lazarus::DeviceIdentity& device) {
    if (device.is_system_disk) {
        return false;
    }
    return device.bench_role != lazarus::DeviceRole::SourceOnly &&
           device.bench_role != lazarus::DeviceRole::ImageStorage &&
           device.bench_role != lazarus::DeviceRole::RemovableMedia &&
           device.bench_role != lazarus::DeviceRole::Ignored;
}

std::string bench_summary_text(const ServiceConfig& config, const lazarus::BenchProfile& bench) {
    std::string text = "Bench: " + (bench.name.empty() ? std::string("(unnamed)") : bench.name);
    text += "\nProfile: " + config.bench_path;
    text += "\nImage storage:";
    for (const auto& storage : bench.image_storage_paths) {
        text += "\n  " + storage;
    }
    if (!bench.image_storage_device.empty()) {
        text += "\nStorage device: " + bench.image_storage_device;
    }
    text += "\nPort labels:";
    if (bench.port_labels.empty()) {
        text += "\n  (none)";
    } else {
        for (const auto& label : bench.port_labels) {
            text += "\n  " + label.label + " -> " + label.identity;
        }
    }
    return text;
}

bool valid_profile_scalar(const std::string& value, std::size_t maximum,
                          const std::string& field, std::string& error,
                          bool required = false) {
    if (required && trim_copy(value).empty()) {
        error = field + " is required.";
        return false;
    }
    if (value.size() > maximum) {
        error = field + " is too long.";
        return false;
    }
    if (value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos) {
        error = field + " must contain one line of text.";
        return false;
    }
    return true;
}

bool valid_branding_color(const std::string& value) {
    if ((value.size() != 7 && value.size() != 9) || value.front() != '#') return false;
    return std::all_of(value.begin() + 1, value.end(), [](unsigned char character) {
        return std::isxdigit(character) != 0;
    });
}

bool validate_profile_text(const lazarus::BenchProfile& bench, std::string& error) {
    if (!valid_profile_scalar(bench.name, 120, "Bench name", error, true) ||
        !valid_profile_scalar(bench.branding.name, 120, "Branding theme name", error, true) ||
        !valid_profile_scalar(bench.branding.product_name, 80, "Branding product name", error, true) ||
        !valid_profile_scalar(bench.branding.subtitle, 180, "Branding subtitle", error) ||
        !valid_profile_scalar(bench.branding.logo_path, 4096, "Branding logo path", error) ||
        !valid_profile_scalar(bench.branding.report_footer, 500, "Branding report footer", error)) {
        return false;
    }
    for (const auto* color : {&bench.branding.accent, &bench.branding.background,
                              &bench.branding.surface, &bench.branding.text,
                              &bench.branding.icon_color}) {
        if (!valid_branding_color(*color)) {
            error = "Branding colors must use #RRGGBB or #RRGGBBAA hexadecimal notation.";
            return false;
        }
    }
    return true;
}

bool save_bench_profile(const lazarus::BenchProfile& bench, const std::string& path, std::string& error) {
    std::lock_guard save_lock(profile_save_mutex);
    if (!validate_profile_text(bench, error)) {
        return false;
    }
    for (const auto& storage : bench.image_storage_paths) {
        if (storage.rfind("/mnt/lazarus-storage", 0) == 0 &&
            !path_is_mountpoint("/mnt/lazarus-storage")) {
            continue;
        }
        if (!ensure_writable_directory(storage, error)) {
            error = "Image storage '" + storage + "' is not writable: " + error;
            return false;
        }
    }
    const auto findings = lazarus::validate_bench_profile(bench);
    if (has_blocker(findings)) {
        error = "Bench profile has blocking safety findings.";
        return false;
    }

    const std::filesystem::path profile_path(path);
    std::error_code fs_error;
    if (profile_path.has_parent_path()) {
        std::filesystem::create_directories(profile_path.parent_path(), fs_error);
        if (fs_error) {
            error = fs_error.message();
            return false;
        }
    }
    auto temporary_path = profile_path;
    temporary_path += ".tmp." + std::to_string(::getpid());
    std::filesystem::remove(temporary_path, fs_error);
    fs_error.clear();
    std::ofstream out(temporary_path, std::ios::trunc);
    if (!out) {
        error = "Could not open a temporary profile for writing.";
        return false;
    }
    out << "# Arcology Lazarus bench profile.\n";
    out << "# Generated by lazarus-service. Review source and destination roles before real work.\n\n";
    out << "name=" << bench.name << "\n";
    out << "branding_theme=" << bench.branding.name << "\n";
    out << "branding_product_name=" << bench.branding.product_name << "\n";
    out << "branding_subtitle=" << bench.branding.subtitle << "\n";
    out << "branding_accent=" << bench.branding.accent << "\n";
    out << "branding_background=" << bench.branding.background << "\n";
    out << "branding_surface=" << bench.branding.surface << "\n";
    out << "branding_text=" << bench.branding.text << "\n";
    out << "branding_icon=" << bench.branding.icon_color << "\n";
    if (!bench.branding.logo_path.empty()) {
        out << "branding_logo=" << bench.branding.logo_path << "\n";
    }
    out << "branding_report_footer=" << bench.branding.report_footer << "\n\n";
    for (const auto& storage : bench.image_storage_paths) {
        out << "image_storage=" << storage << "\n";
    }
    if (!bench.image_storage_device.empty()) {
        out << "image_storage_device=" << bench.image_storage_device << "\n";
    }
    if (!bench.image_storage_volume.empty()) {
        out << "image_storage_volume=" << bench.image_storage_volume << "\n";
    }
    for (const auto& port : bench.image_storage_port_paths) {
        out << "image_storage_port=" << port << "\n";
    }
    out << "\n";
    for (const auto& source : bench.source_only_paths) {
        out << "source=" << source << "\n";
    }
    for (const auto& destination : bench.destination_only_paths) {
        out << "destination=" << destination << "\n";
    }
    for (const auto& removable : bench.removable_media_paths) {
        out << "removable_media=" << removable << "\n";
    }
    for (const auto& ignored : bench.ignored_paths) {
        out << "ignored=" << ignored << "\n";
    }
    for (const auto& label : bench.port_labels) {
        out << "port_label=" << label.identity << "|" << label.label << "\n";
    }
    if (!out) {
        error = "Failed while writing profile.";
        out.close();
        std::filesystem::remove(temporary_path, fs_error);
        return false;
    }
    out.flush();
    out.close();
    const int temporary_fd = ::open(temporary_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (temporary_fd < 0 || ::fsync(temporary_fd) != 0) {
        if (temporary_fd >= 0) ::close(temporary_fd);
        std::filesystem::remove(temporary_path, fs_error);
        error = "Could not flush the updated profile to storage.";
        return false;
    }
    ::close(temporary_fd);
    ::chmod(temporary_path.c_str(), 0644);

    std::filesystem::path persistent_profile;
    std::filesystem::path persistent_temporary;
    if (!bench.image_storage_device.empty() && path_is_mountpoint("/mnt/lazarus-storage")) {
        persistent_profile = "/mnt/lazarus-storage/bench.profile";
        persistent_temporary = "/mnt/lazarus-storage/.bench.profile.tmp";
        std::error_code mirror_error;
        std::filesystem::remove(persistent_temporary, mirror_error);
        mirror_error.clear();
        std::filesystem::copy_file(temporary_path, persistent_temporary,
                                   std::filesystem::copy_options::overwrite_existing, mirror_error);
        if (mirror_error) {
            std::filesystem::remove(temporary_path, fs_error);
            error = "Could not stage the profile in persistent image storage; the profile was not changed: " + mirror_error.message();
            return false;
        }
        ::chmod(persistent_temporary.c_str(), 0644);
    }

    std::filesystem::rename(temporary_path, profile_path, fs_error);
    if (fs_error) {
        const auto rename_message = fs_error.message();
        std::error_code cleanup_error;
        std::filesystem::remove(temporary_path, cleanup_error);
        if (!persistent_temporary.empty()) std::filesystem::remove(persistent_temporary, cleanup_error);
        error = "Could not atomically replace the active profile: " + rename_message;
        return false;
    }
    if (!persistent_temporary.empty()) {
        std::error_code mirror_error;
        std::filesystem::rename(persistent_temporary, persistent_profile, mirror_error);
        if (mirror_error) {
            error = "The active profile was saved, but its persistent mirror could not be finalized: " + mirror_error.message();
            return false;
        }
    }
    return true;
}

std::string partition_path_for_disk(const std::string& disk, unsigned int number) {
    const auto name = std::filesystem::path(disk).filename().string();
    const auto begins = [&](const char* prefix) { return name.rfind(prefix, 0) == 0; };
    if (begins("nvme") || begins("mmcblk") || begins("nbd") || begins("loop")) {
        return disk + "p" + std::to_string(number);
    }
    return disk + std::to_string(number);
}

std::string supported_storage_volume(const lazarus::DeviceIdentity& device, std::string& filesystem) {
    auto candidates = device.partitions;
    candidates.push_back(device.linux_path);
    for (const auto& candidate : candidates) {
        std::string output;
        std::string command_error;
        if (!run_program({"blkid", "-s", "TYPE", "-o", "value", candidate}, output, command_error)) continue;
        const auto type = trim_copy(output);
        if (type == "ext2" || type == "ext3" || type == "ext4" || type == "exfat" || type == "ntfs") {
            filesystem = type;
            return candidate;
        }
    }
    return "";
}

bool configure_image_storage(const ServiceConfig& config, const lazarus::BenchProfile& current,
                             const lazarus::DeviceIdentity& device, bool erase,
                             std::string& filesystem, std::string& error) {
    if (device.is_system_disk) {
        error = "The running Lazarus system disk cannot be reassigned as external image storage.";
        return false;
    }
    if (device.bench_role == lazarus::DeviceRole::SourceOnly) {
        error = "A source-only customer drive cannot be used as image storage.";
        return false;
    }
    if (device.size_bytes == 0) {
        error = "The selected disk reports zero capacity and cannot be prepared as image storage.";
        return false;
    }

    std::string read_only;
    std::string read_only_error;
    if (!run_program({"blockdev", "--getro", device.linux_path}, read_only, read_only_error)) {
        error = "Could not determine whether the selected storage disk is writable: " + read_only_error;
        return false;
    }
    if (trim_copy(read_only) != "0") {
        error = "The selected disk is read-only. Lazarus did not alter its partition table or filesystems.";
        return false;
    }

    std::string mounted_target;
    std::string command_error;
    if (path_is_mountpoint("/mnt/lazarus-storage")) {
        std::string mounted_source;
        if (run_program({"findmnt", "-rn", "-M", "/mnt/lazarus-storage", "-o", "SOURCE"}, mounted_source, command_error)) {
            mounted_source = trim_copy(mounted_source);
            const auto same_device = mounted_source == device.linux_path ||
                std::find(device.partitions.begin(), device.partitions.end(), mounted_source) != device.partitions.end();
            if (!same_device) {
                error = "Another image-storage device is mounted. Restart Lazarus after disconnecting it before assigning a replacement.";
                return false;
            }
        }
    }

    std::string selected_volume;
    if (erase) {
        std::string mounts;
        if (!run_program({"lsblk", "-nr", "-o", "MOUNTPOINTS", device.linux_path}, mounts, command_error)) {
            error = "Could not check whether the selected storage disk is mounted: " + command_error;
            return false;
        }
        if (!trim_copy(mounts).empty()) {
            error = "The selected storage disk or one of its partitions is mounted. Lazarus refused to erase it.";
            return false;
        }
        std::string output;
        if (!run_program({"wipefs", "-a", device.linux_path}, output, command_error) ||
            !run_program({"parted", "-s", "-a", "optimal", device.linux_path, "mklabel", "gpt"}, output, command_error) ||
            !run_program({"parted", "-s", "-a", "optimal", device.linux_path, "mkpart", "LAZARUS_STORAGE", "ext4", "1MiB", "100%"}, output, command_error)) {
            error = "Could not create the Lazarus storage partition: " + command_error;
            return false;
        }
        run_program({"partprobe", device.linux_path}, output, command_error);
        run_program({"udevadm", "settle", "--timeout=15"}, output, command_error);
        const auto partition = partition_path_for_disk(device.linux_path, 1);
        for (int attempt = 0; attempt < 30 && !std::filesystem::exists(partition); ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!std::filesystem::exists(partition) ||
            !run_program({"mkfs.ext4", "-F", "-L", "LAZARUS_STORAGE", partition}, output, command_error)) {
            error = "Could not format the Lazarus storage partition: " + command_error;
            return false;
        }
        filesystem = "ext4";
        selected_volume = partition;
    } else {
        selected_volume = supported_storage_volume(device, filesystem);
        if (selected_volume.empty()) {
            error = "No supported filesystem was found. Use 'Erase and Prepare' to create dedicated ext4 image storage.";
            return false;
        }
    }

    auto staged = current;
    const auto identity = identity_for_profile(device);
    const auto port_identity = port_identity_for_profile(device);
    staged.image_storage_device = identity;
    std::string volume_uuid;
    if (run_program({"blkid", "-s", "UUID", "-o", "value", selected_volume}, volume_uuid, command_error) &&
        !trim_copy(volume_uuid).empty()) {
        staged.image_storage_volume = "/dev/disk/by-uuid/" + trim_copy(volume_uuid);
    } else {
        staged.image_storage_volume = selected_volume;
    }
    auto remove_identity = [&](std::vector<std::string>& entries) {
        std::erase_if(entries, [&](const std::string& value) {
            return value == identity || value == port_identity || matches_device_selector(device, value) ||
                   (!port_identity.empty() && lazarus::physical_port_identity(value) == port_identity);
        });
    };
    remove_identity(staged.source_only_paths);
    remove_identity(staged.destination_only_paths);
    remove_identity(staged.removable_media_paths);
    remove_identity(staged.ignored_paths);
    if (!port_identity.empty()) staged.image_storage_port_paths = {port_identity};

    if (!save_bench_profile(staged, config.bench_path, error)) return false;

    std::string mount_output;
    if (!run_program({"/usr/local/sbin/lazarus-mount-storage"}, mount_output, command_error)) {
        std::string rollback_error;
        save_bench_profile(current, config.bench_path, rollback_error);
        error = "The storage filesystem was prepared but could not be mounted: " + command_error;
        if (!mount_output.empty()) error += " " + trim_copy(mount_output);
        return false;
    }

    staged.image_storage_paths = {"/mnt/lazarus-storage/images"};
    staged.image_storage_path = staged.image_storage_paths.front();
    if (!save_bench_profile(staged, config.bench_path, error)) return false;
    return true;
}

std::string device_json(const lazarus::BenchProfile& bench, const lazarus::DeviceIdentity& device) {
    const auto safety = disconnect_state(bench, device);
    return "{\"linux_path\":" + quote(device.linux_path) +
           ",\"identity\":" + quote(identity_for_profile(device)) +
           ",\"physical_path\":" + quote(device.physical_path) +
           ",\"port_path\":" + quote(port_identity_for_profile(device)) +
           ",\"by_id_path\":" + quote(device.by_id_path) +
           ",\"by_path\":" + quote(device.by_path) +
           ",\"model\":" + quote(device.model) +
           ",\"serial\":" + quote(device.serial) +
           ",\"serial_ending\":" + quote(device.serial_ending) +
           ",\"transport\":" + quote(device.transport) +
           ",\"size_bytes\":" + std::to_string(device.size_bytes) +
           ",\"logical_block_size\":" + std::to_string(device.logical_block_size) +
           ",\"role\":" + quote(lazarus::to_string(device.bench_role)) +
           ",\"label\":" + quote(lazarus::label_for_device(bench, device)) +
           ",\"disconnect_state\":" + quote(safety.first) +
           ",\"disconnect_message\":" + quote(safety.second) +
           ",\"system_disk\":" + std::string(device.is_system_disk ? "true" : "false") +
           ",\"removable\":" + std::string(device.removable ? "true" : "false") +
           ",\"rotational\":" + std::string(device.rotational ? "true" : "false") +
           ",\"partitions\":" + string_array_json(device.partitions) + "}";
}

std::string smart_attribute_json(const lazarus::SmartAttribute& attribute) {
    return "{\"name\":" + quote(attribute.name) +
           ",\"present\":" + std::string(attribute.present ? "true" : "false") +
           ",\"value\":" + std::to_string(attribute.value) + "}";
}

std::string smart_json(const lazarus::SmartDiagnosticResult& smart) {
    return "\"device\":" + quote(smart.device.linux_path) +
           ",\"smartctl_available\":" + std::string(smart.smartctl_available ? "true" : "false") +
           ",\"command_completed\":" + std::string(smart.command_completed ? "true" : "false") +
           ",\"exit_code\":" + std::to_string(smart.exit_code) +
           ",\"model\":" + quote(smart.model) +
           ",\"serial\":" + quote(smart.serial) +
           ",\"health\":" + quote(smart.health) +
           ",\"power_on_hours\":" + smart_attribute_json(smart.power_on_hours) +
           ",\"temperature_celsius\":" + smart_attribute_json(smart.temperature_celsius) +
           ",\"reallocated_sectors\":" + smart_attribute_json(smart.reallocated_sectors) +
           ",\"pending_sectors\":" + smart_attribute_json(smart.pending_sectors) +
           ",\"uncorrectable_errors\":" + smart_attribute_json(smart.uncorrectable_errors) +
           ",\"facts\":" + string_array_json(smart.facts) +
           ",\"findings\":" + findings_json(smart.findings);
}

std::string progress_json(const lazarus::ProgressEvent& event) {
    return "{\"type\":\"progress\",\"operation\":" + quote(event.operation) +
           ",\"phase\":" + quote(event.phase) +
           ",\"message\":" + quote(event.message) +
           ",\"bytes_done\":" + std::to_string(event.bytes_done) +
           ",\"bytes_total\":" + std::to_string(event.bytes_total) +
           ",\"chunks_done\":" + std::to_string(event.chunks_done) +
           ",\"chunks_total\":" + std::to_string(event.chunks_total) +
           ",\"indeterminate\":" + std::string(event.indeterminate ? "true" : "false") + "}";
}

std::string final_json(bool ok, const std::string& command, const std::string& fields) {
    return "{\"type\":\"final\",\"ok\":" + std::string(ok ? "true" : "false") +
           ",\"command\":" + quote(command) + (fields.empty() ? "" : "," + fields) + "}";
}

std::string error_json(const std::string& command, const std::string& message) {
    return final_json(false, command, "\"error\":" + quote(message));
}

void send_line(std::ostream& out, const std::string& line) {
    out << line << "\n";
    out.flush();
}

bool stream_command_output(const std::string& command, std::ostream& out, const std::string& operation, const std::string& phase, std::string& error) {
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) {
        error = std::strerror(errno);
        return false;
    }

    char* line = nullptr;
    std::size_t capacity = 0;
    while (getline(&line, &capacity, pipe) != -1) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
            text.pop_back();
        }
        if (text.empty()) {
            continue;
        }
        send_line(out, progress_json(lazarus::ProgressEvent{operation, phase, text}));
    }
    free(line);

    const int status = ::pclose(pipe);
    if (status == -1) {
        error = std::strerror(errno);
        return false;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFEXITED(status)) {
            error = "Installer exited with status " + std::to_string(WEXITSTATUS(status)) + ".";
        } else if (WIFSIGNALED(status)) {
            error = "Installer terminated by signal " + std::to_string(WTERMSIG(status)) + ".";
        } else {
            error = "Installer did not exit cleanly.";
        }
        return false;
    }
    return true;
}

std::string handle_request(const ServiceConfig& config, const std::string& request, std::ostream& out) {
    const auto command = extract_json_string(request, "command");
    const auto bench = lazarus::load_bench_profile(config.bench_path);

    if (command.empty()) {
        return error_json("", "Request did not include a command.");
    }
    if (command == "ping") {
        return final_json(true, command, "\"version\":" + quote(lazarus::version()));
    }
    if (command == "network_status") {
        const auto address = network_ipv4_address();
        return final_json(true, command,
                          "\"online\":" + std::string(network_link_online() ? "true" : "false") +
                              ",\"ipv4\":" + quote(address) +
                              ",\"interfaces_rows\":" + quote(network_interfaces_rows()) +
                              ",\"log\":" + quote(recent_network_log()));
    }
    if (command == "network_config") {
        const auto settings = load_network_settings(config.network_path);
        return final_json(true, command,
                          "\"mode\":" + quote(settings.mode) +
                              ",\"interface\":" + quote(settings.interface) +
                              ",\"address\":" + quote(settings.address) +
                              ",\"prefix\":" + quote(settings.prefix) +
                              ",\"gateway\":" + quote(settings.gateway) +
                              ",\"dns\":" + quote(settings.dns) +
                              ",\"interfaces_rows\":" + quote(network_interfaces_rows()) +
                              ",\"log\":" + quote(recent_network_log()));
    }
    if (command == "network_apply") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        NetworkSettings settings;
        settings.mode = extract_json_string(request, "mode");
        settings.interface = extract_json_string(request, "interface");
        settings.address = trim_copy(extract_json_string(request, "address"));
        settings.prefix = trim_copy(extract_json_string(request, "prefix"));
        settings.gateway = trim_copy(extract_json_string(request, "gateway"));
        settings.dns = trim_copy(extract_json_string(request, "dns"));
        if (settings.mode != "dhcp" && settings.mode != "static") {
            return error_json(command, "Network mode must be DHCP or Static.");
        }
        if (!valid_network_interface(settings.interface)) {
            return error_json(command, "Select Automatic or a currently detected network interface.");
        }
        if (settings.interface != "auto") {
            const auto interface_path = std::filesystem::path("/sys/class/net") / settings.interface;
            if (std::filesystem::is_directory(interface_path / "wireless") ||
                std::filesystem::exists(interface_path / "phy80211")) {
                return error_json(command, "Wireless configuration is not supported yet. Connect a wired Ethernet interface.");
            }
        }
        if (settings.mode == "static") {
            if (settings.interface == "auto") {
                return error_json(command, "Static addressing requires one specific wired interface.");
            }
            if (!valid_ipv4_address(settings.address)) {
                return error_json(command, "Enter a valid static IPv4 address.");
            }
            int prefix = 0;
            try { prefix = std::stoi(settings.prefix); } catch (...) { prefix = 0; }
            if (prefix < 1 || prefix > 32) {
                return error_json(command, "IPv4 prefix length must be between 1 and 32.");
            }
            if (!settings.gateway.empty() && !valid_ipv4_address(settings.gateway)) {
                return error_json(command, "Enter a valid IPv4 gateway or leave it empty.");
            }
            std::string dns_value;
            for (char character : settings.dns) dns_value.push_back(character == ' ' ? ',' : character);
            std::istringstream dns_stream(dns_value);
            std::string server;
            while (std::getline(dns_stream, server, ',')) {
                server = trim_copy(server);
                if (!server.empty() && !valid_ipv4_address(server)) {
                    return error_json(command, "DNS servers must be IPv4 addresses separated by commas.");
                }
            }
        } else {
            settings.address.clear();
            settings.prefix = "24";
            settings.gateway.clear();
            settings.dns.clear();
        }

        std::string error;
        if (!save_network_settings(config, settings, error)) return error_json(command, error);
        std::string output;
        if (!run_program({config.network_helper, "--config", config.network_path, "--restart"}, output, error)) {
            return error_json(command, "Network settings were saved, but could not be applied. " + error);
        }
        return final_json(true, command,
                          "\"mode\":" + quote(settings.mode) +
                              ",\"interface\":" + quote(settings.interface) +
                              ",\"message\":" + quote(settings.mode == "dhcp"
                                  ? "DHCP discovery restarted. Interface status will update when a lease is received."
                                  : "Static network settings were applied."));
    }
    if (command == "job_presets") {
        return final_json(true, command, "\"presets\":" + job_presets_json());
    }
    if (command == "admin_status") {
        std::error_code error;
        const bool credential_file_exists = std::filesystem::exists(config.security_path, error);
        const bool healthy = load_admin_record(config).has_value();
        return final_json(true, command,
                          "\"configured\":" + std::string(credential_file_exists ? "true" : "false") +
                              ",\"healthy\":" + std::string(healthy ? "true" : "false") +
                              ",\"minimum_password_length\":1");
    }
    if (command == "admin_setup") {
        std::error_code error;
        if (std::filesystem::exists(config.security_path, error)) {
            return error_json(command, "Administration credentials are already configured.");
        }
        const auto password = extract_json_string(request, "new_password");
        if (!valid_new_password(password)) {
            return error_json(command, "Admin password must contain between 1 and 256 characters.");
        }
        const auto recovery = recovery_key();
        AdminRecord record;
        record.password_salt = random_hex(kSaltBytes);
        record.recovery_salt = random_hex(kSaltBytes);
        record.password_hash = derive_secret(password, record.password_salt, record.iterations);
        record.recovery_hash = derive_secret(recovery, record.recovery_salt, record.iterations);
        if (recovery.empty() || record.password_hash.empty() || record.recovery_hash.empty()) {
            return error_json(command, "Secure credential generation failed.");
        }
        std::string save_error;
        if (!save_admin_record(config, record, save_error)) return error_json(command, save_error);
        const auto token = create_admin_session(false);
        return final_json(!token.empty(), command,
                          "\"token\":" + quote(token) + ",\"recovery_key\":" + quote(recovery) +
                              ",\"recovery_key_shown_once\":true");
    }
    if (command == "admin_login") {
        std::lock_guard admin_lock(admin_mutex);
        const auto now = std::chrono::steady_clock::now();
        if (now < admin_login_blocked_until) {
            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(admin_login_blocked_until - now).count() + 1;
            return error_json(command, "Too many failed attempts. Try again in " + std::to_string(remaining) + " seconds.");
        }
        const auto record = load_admin_record(config);
        if (!record) return error_json(command, "Administration credentials are not configured or are unreadable.");
        const auto password = extract_json_string(request, "password");
        auto recovery = extract_json_string(request, "recovery_key");
        std::transform(recovery.begin(), recovery.end(), recovery.begin(), [](unsigned char character) {
            return static_cast<char>(std::toupper(character));
        });
        const bool recovery_login = !recovery.empty();
        const auto candidate = recovery_login
            ? derive_secret(recovery, record->recovery_salt, record->iterations)
            : derive_secret(password, record->password_salt, record->iterations);
        const auto& expected = recovery_login ? record->recovery_hash : record->password_hash;
        if (!constant_time_equal(candidate, expected)) {
            ++failed_admin_logins;
            if (failed_admin_logins >= 3) {
                const auto delay = std::min(30U, 1U << std::min(5U, failed_admin_logins - 2));
                admin_login_blocked_until = now + std::chrono::seconds(delay);
            }
            return error_json(command, "Administration credentials were not accepted.");
        }
        failed_admin_logins = 0;
        admin_login_blocked_until = {};
        const auto token = create_admin_session(recovery_login);
        return final_json(!token.empty(), command,
                          "\"token\":" + quote(token) +
                              ",\"recovery_login\":" + std::string(recovery_login ? "true" : "false") +
                              ",\"expires_minutes\":15");
    }
    if (command == "admin_logout") {
        std::lock_guard admin_lock(admin_mutex);
        admin_sessions.erase(extract_json_string(request, "admin_token"));
        return final_json(true, command, "\"logged_out\":true");
    }
    if (command == "system_power") {
        if (operation_in_progress()) {
            return error_json(command, "A disk operation is active. Finish or stop it before shutting down or restarting.");
        }
        const auto action = extract_json_string(request, "action");
        const auto confirmation = extract_json_string(request, "confirmation");
        std::string executable;
        if (action == "shutdown" && confirmation == "SHUT DOWN") {
            executable = "/sbin/poweroff";
        } else if (action == "restart" && confirmation == "RESTART") {
            executable = "/sbin/reboot";
        } else {
            return error_json(command, "A valid shutdown or restart action and its exact confirmation are required.");
        }
        std::string power_error;
        if (!schedule_power_action(executable, power_error)) return error_json(command, power_error);
        return final_json(true, command,
                          "\"action\":" + quote(action) +
                              ",\"message\":" + quote(action == "restart"
                                  ? "Lazarus accepted the restart request."
                                  : "Lazarus accepted the shutdown request."));
    }
    if (command == "admin_change_password") {
        const auto token = extract_json_string(request, "admin_token");
        if (!validate_admin_session(token)) return error_json(command, "Admin authentication is required or has expired.");
        const auto password = extract_json_string(request, "new_password");
        if (!valid_new_password(password)) {
            return error_json(command, "Admin password must contain between 1 and 256 characters.");
        }
        auto record = load_admin_record(config);
        if (!record) return error_json(command, "Administration credentials could not be read.");
        record->password_salt = random_hex(kSaltBytes);
        record->password_hash = derive_secret(password, record->password_salt, record->iterations);
        if (record->password_hash.empty()) return error_json(command, "Password hashing failed.");
        std::string save_error;
        if (!save_admin_record(config, *record, save_error)) return error_json(command, save_error);
        return final_json(true, command, "\"password_changed\":true");
    }
    if (command == "admin_rotate_recovery") {
        const auto token = extract_json_string(request, "admin_token");
        if (!validate_admin_session(token)) return error_json(command, "Admin authentication is required or has expired.");
        auto record = load_admin_record(config);
        if (!record) return error_json(command, "Administration credentials could not be read.");
        const auto recovery = recovery_key();
        record->recovery_salt = random_hex(kSaltBytes);
        record->recovery_hash = derive_secret(recovery, record->recovery_salt, record->iterations);
        if (recovery.empty() || record->recovery_hash.empty()) return error_json(command, "Recovery-key generation failed.");
        std::string save_error;
        if (!save_admin_record(config, *record, save_error)) return error_json(command, save_error);
        return final_json(true, command,
                          "\"recovery_key\":" + quote(recovery) + ",\"recovery_key_shown_once\":true");
    }
    if (command == "printers") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        std::vector<PrinterInfo> printers;
        std::string default_printer;
        std::string error;
        if (!load_printers(printers, default_printer, error)) return error_json(command, error);
        std::string rows;
        for (const auto& printer : printers) {
            auto state = printer.state;
            auto uri = printer.uri;
            std::replace(state.begin(), state.end(), '\t', ' ');
            std::replace(state.begin(), state.end(), '\n', ' ');
            std::replace(uri.begin(), uri.end(), '\t', ' ');
            std::replace(uri.begin(), uri.end(), '\n', ' ');
            rows += printer.name + "\t" + (printer.is_default ? "default" : "configured") +
                    "\t" + state + "\t" + uri + "\n";
        }
        return final_json(true, command,
                          "\"cups_running\":true,\"default_printer\":" + quote(default_printer) +
                              ",\"printers_rows\":" + quote(rows));
    }
    if (command == "printer_discover") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        std::vector<DiscoveredPrinter> printers;
        std::string error;
        if (!discover_printers(printers, error)) return error_json(command, error);
        std::string rows;
        for (const auto& printer : printers) {
            auto name = printer.name;
            auto uri = printer.uri;
            std::replace(name.begin(), name.end(), '\t', ' ');
            std::replace(name.begin(), name.end(), '\n', ' ');
            std::replace(uri.begin(), uri.end(), '\t', ' ');
            std::replace(uri.begin(), uri.end(), '\n', ' ');
            rows += name + "\t" + printer_queue_name(name) + "\t" + uri + "\n";
        }
        return final_json(true, command,
                          "\"discovered_count\":" + std::to_string(printers.size()) +
                              ",\"discovered_rows\":" + quote(rows) +
                              ",\"message\":" + quote(printers.empty()
                                  ? "No driverless IPP printers answered during the discovery window."
                                  : std::to_string(printers.size()) + " driverless printer(s) discovered."));
    }
    if (command == "print_report") {
        auto report_path = extract_json_string(request, "report_path");
        std::filesystem::path report(report_path);
        const bool allowed = std::any_of(bench.image_storage_paths.begin(), bench.image_storage_paths.end(),
            [&report](const std::string& root) { return path_is_beneath(report, root); });
        if (!allowed || !std::filesystem::is_regular_file(report)) {
            return error_json(command, "The report is missing or is outside configured Lazarus image storage.");
        }
        if (report.extension() == ".html" && std::filesystem::exists(report.parent_path() / (report.stem().string() + ".txt"))) {
            report = report.parent_path() / (report.stem().string() + ".txt");
        }
        if (report.extension() != ".txt" && report.extension() != ".html") {
            return error_json(command, "Only Lazarus text or HTML report files may be printed.");
        }
        std::vector<PrinterInfo> printers;
        std::string default_printer;
        std::string printer_error;
        if (!load_printers(printers, default_printer, printer_error)) return error_json(command, printer_error);
        if (default_printer.empty()) return error_json(command, "No default printer is configured. Open Administration > Printers and Reports.");
        std::string output;
        if (!run_program({"lp", "-h", kCupsServer, "-d", default_printer,
                          "-o", "job-sheets=none", report.string()}, output, printer_error)) {
            return error_json(command, "The report could not be queued: " + printer_error);
        }
        return final_json(true, command, "\"printer\":" + quote(default_printer) +
                          ",\"report_path\":" + quote(report.string()) +
                          ",\"message\":" + quote("Report queued on " + default_printer + "."));
    }
    if (command == "printer_add") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        auto name = extract_json_string(request, "name");
        const auto address = trim_copy(extract_json_string(request, "address"));
        auto connection = extract_json_string(request, "connection");
        auto requested_uri = trim_copy(extract_json_string(request, "uri"));
        if (name.empty()) name = printer_queue_name(extract_json_string(request, "display_name"));
        if (!valid_printer_name(name)) {
            return error_json(command, "Printer name must use only letters, numbers, hyphens, or underscores.");
        }
        if (connection.empty()) connection = "auto";
        if (!requested_uri.empty() && !valid_printer_uri(requested_uri)) {
            return error_json(command, "Printer URI must begin with ipp://, ipps://, dnssd://, usb://, socket://, or lpd://.");
        }
        if (requested_uri.empty() && !valid_printer_address(address)) {
            return error_json(command, "Enter a valid printer IPv4 address, IPv6 address, or hostname without a URL scheme.");
        }
        if (requested_uri.empty() && connection != "auto" && connection != "ipp" &&
            connection != "ipps" && connection != "socket") {
            return error_json(command, "Printer connection must be Automatic, IPP, secure IPP, or JetDirect.");
        }

        std::vector<std::pair<std::string, std::string>> attempts;
        if (!requested_uri.empty()) {
            if (requested_uri.rfind("ipp://", 0) == 0 || requested_uri.rfind("ipps://", 0) == 0) {
                const auto hostname = printer_uri_hostname(requested_uri);
                if (!hostname.empty() && !ipv4_literal(hostname) && hostname.find(':') == std::string::npos) {
                    const auto numeric_uri = printer_uri_with_ipv4(requested_uri, hostname);
                    if (!numeric_uri) {
                        return error_json(command, "The printer URI hostname did not resolve. Use a numeric IPv4 address in the URI.");
                    }
                    requested_uri = *numeric_uri;
                }
            }
            const bool driverless = requested_uri.rfind("ipp://", 0) == 0 ||
                                    requested_uri.rfind("ipps://", 0) == 0 ||
                                    requested_uri.rfind("dnssd://", 0) == 0;
            attempts.emplace_back(requested_uri, driverless ? "everywhere" : "drv:///sample.drv/generpcl.ppd");
        } else {
            auto numeric_address = address;
            if (!ipv4_literal(numeric_address) && numeric_address.find(':') == std::string::npos) {
                const auto resolved = resolve_printer_ipv4(numeric_address);
                if (!resolved) {
                    return error_json(command, "The printer hostname did not resolve. Enter its numeric IPv4 address instead.");
                }
                numeric_address = *resolved;
            }
            const auto host = printer_host_for_uri(numeric_address);
            if (connection == "auto" || connection == "ipp") {
                attempts.emplace_back("ipp://" + host + "/ipp/print", "everywhere");
            }
            if (connection == "auto" || connection == "ipps") {
                attempts.emplace_back("ipps://" + host + "/ipp/print", "everywhere");
            }
            if (connection == "auto" || connection == "socket") {
                attempts.emplace_back("socket://" + host + ":9100", "drv:///sample.drv/generpcl.ppd");
            }
        }

        std::string output;
        std::string error;
        std::string selected_uri;
        std::string selected_model;
        std::vector<std::string> failures;
        for (const auto& [uri, model] : attempts) {
            output.clear();
            error.clear();
            if (configure_printer(name, uri, model, output, error)) {
                selected_uri = uri;
                selected_model = model;
                break;
            }
            failures.push_back(uri + ": " + (error.empty() ? "CUPS rejected the connection." : error));
            std::string ignored_output;
            std::string ignored_error;
            run_program({"lpadmin", "-h", kCupsServer, "-x", name}, ignored_output, ignored_error);
        }
        if (selected_uri.empty()) {
            std::string details;
            for (const auto& failure : failures) details += (details.empty() ? "" : " | ") + failure;
            return error_json(command, "CUPS could not configure a printer at " +
                (address.empty() ? requested_uri : address) + ". Attempts: " + details);
        }

        std::string default_output;
        std::string default_error;
        if (!run_program({"lpadmin", "-h", kCupsServer, "-d", name}, default_output, default_error)) {
            return error_json(command, "The printer was added at " + selected_uri +
                ", but CUPS could not make it the default printer. " + default_error);
        }
        if (!persist_printer_state(error)) {
            return error_json(command, "Printer was added to CUPS, but its persistent copy failed. " + error);
        }
        return final_json(true, command, "\"printer\":" + quote(name) +
                          ",\"uri\":" + quote(selected_uri) +
                          ",\"driver\":" + quote(selected_model) +
                          ",\"default_printer\":true" +
                          ",\"message\":" + quote("Printer configured at " + selected_uri + " and set as default."));
    }
    if (command == "printer_set_default" || command == "printer_test_page" || command == "printer_remove") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        const auto name = extract_json_string(request, "name");
        if (!valid_printer_name(name)) return error_json(command, "A valid configured printer name is required.");
        std::vector<PrinterInfo> printers;
        std::string default_printer;
        std::string error;
        if (!load_printers(printers, default_printer, error)) return error_json(command, error);
        const bool exists = std::any_of(printers.begin(), printers.end(), [&name](const PrinterInfo& printer) {
            return printer.name == name;
        });
        if (!exists) return error_json(command, "The selected printer is no longer configured.");

        std::vector<std::string> arguments;
        if (command == "printer_set_default") {
            arguments = {"lpadmin", "-h", kCupsServer, "-d", name};
        } else if (command == "printer_test_page") {
            arguments = {"lp", "-h", kCupsServer, "-d", name,
                         "-o", "job-sheets=none", "/usr/share/cups/data/testprint"};
        } else {
            if (extract_json_string(request, "confirmation") != "REMOVE") {
                return error_json(command, "Type REMOVE to confirm printer removal.");
            }
            arguments = {"lpadmin", "-h", kCupsServer, "-x", name};
        }
        std::string output;
        if (!run_program(arguments, output, error)) return error_json(command, error);
        if (command != "printer_test_page" && !persist_printer_state(error)) {
            return error_json(command, "CUPS accepted the change, but its persistent copy failed. " + error);
        }
        return final_json(true, command,
                          "\"printer\":" + quote(name) + ",\"service_output\":" + quote(trim_copy(output)));
    }
    if (command == "bench") {
        return final_json(true, command,
                          "\"name\":" + quote(bench.name) +
                              ",\"profile\":" + quote(config.bench_path) +
                              ",\"image_storage\":" + string_array_json(bench.image_storage_paths) +
                              ",\"findings\":" + findings_json(lazarus::validate_bench_profile(bench)));
    }
    if (command == "profile") {
        std::string storage_error;
        const bool storage_ready = ensure_image_storage_ready(bench, storage_error);
        const auto network_address = network_ipv4_address();
        return final_json(true, command,
                          "\"name\":" + quote(bench.name) +
                              ",\"profile\":" + quote(config.bench_path) +
                              ",\"summary_text\":" + quote(bench_summary_text(config, bench)) +
                              ",\"branding_theme\":" + quote(bench.branding.name) +
                              ",\"branding_product_name\":" + quote(bench.branding.product_name) +
                              ",\"branding_subtitle\":" + quote(bench.branding.subtitle) +
                              ",\"branding_accent\":" + quote(bench.branding.accent) +
                              ",\"branding_background\":" + quote(bench.branding.background) +
                              ",\"branding_surface\":" + quote(bench.branding.surface) +
                              ",\"branding_text\":" + quote(bench.branding.text) +
                              ",\"branding_icon\":" + quote(bench.branding.icon_color) +
                              ",\"branding_logo\":" + quote(bench.branding.logo_path) +
                              ",\"branding_report_footer\":" + quote(bench.branding.report_footer) +
                              ",\"image_storage_device\":" + quote(bench.image_storage_device) +
                              ",\"image_storage_volume\":" + quote(bench.image_storage_volume) +
                              ",\"image_storage_port_text\":" + quote(join_lines(bench.image_storage_port_paths)) +
                              ",\"image_storage_text\":" + quote(join_lines(bench.image_storage_paths)) +
                              ",\"source_text\":" + quote(join_lines(bench.source_only_paths)) +
                              ",\"destination_text\":" + quote(join_lines(bench.destination_only_paths)) +
                              ",\"removable_text\":" + quote(join_lines(bench.removable_media_paths)) +
                              ",\"ignored_text\":" + quote(join_lines(bench.ignored_paths)) +
                              ",\"labels_text\":" + quote(labels_to_text(bench.port_labels)) +
                              ",\"service_running\":true" +
                              ",\"bench_protected\":" + std::string(!has_blocker(lazarus::validate_bench_profile(bench)) ? "true" : "false") +
                              ",\"storage_online\":" + std::string(storage_ready ? "true" : "false") +
                              ",\"storage_error\":" + quote(storage_error) +
                              ",\"storage_mount_source\":" + quote(image_storage_mount_source()) +
                              ",\"network_online\":" + std::string(network_link_online() ? "true" : "false") +
                              ",\"network_ipv4\":" + quote(network_address) +
                              ",\"source_port_count\":" + std::to_string(bench.source_only_paths.size()) +
                              ",\"destination_port_count\":" + std::to_string(bench.destination_only_paths.size()) +
                              ",\"device_generation\":" + quote(device_fingerprint(bench)) +
                              ",\"devices_rows\":" + quote(device_rows_text(bench)) +
                              ",\"ports_rows\":" + quote(port_rows_text(bench)) +
                              ",\"backups_rows\":" + quote(backup_rows_text(bench)));
    }
    if (command == "save_profile") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        lazarus::BenchProfile edited;
        edited.name = extract_json_string(request, "name");
        edited.branding.name = extract_json_string(request, "branding_theme");
        edited.branding.product_name = extract_json_string(request, "branding_product_name");
        edited.branding.subtitle = extract_json_string(request, "branding_subtitle");
        edited.branding.accent = extract_json_string(request, "branding_accent");
        edited.branding.background = extract_json_string(request, "branding_background");
        edited.branding.surface = extract_json_string(request, "branding_surface");
        edited.branding.text = extract_json_string(request, "branding_text");
        edited.branding.icon_color = extract_json_string(request, "branding_icon");
        edited.branding.logo_path = extract_json_string(request, "branding_logo");
        edited.branding.report_footer = extract_json_string(request, "branding_report_footer");
        if (edited.branding.name.empty()) edited.branding.name = "Lazarus Default Theme";
        if (edited.branding.product_name.empty()) edited.branding.product_name = "Arcology Lazarus";
        if (edited.branding.subtitle.empty()) edited.branding.subtitle = "Offline Imaging | Recovery | Hardware Migration";
        if (edited.branding.accent.empty()) edited.branding.accent = "#f39a22";
        if (edited.branding.background.empty()) edited.branding.background = "#10161b";
        if (edited.branding.surface.empty()) edited.branding.surface = "#171f25";
        if (edited.branding.text.empty()) edited.branding.text = "#edf1f3";
        if (edited.branding.icon_color.empty()) edited.branding.icon_color = edited.branding.accent;
        if (edited.branding.report_footer.empty()) edited.branding.report_footer = "Generated locally by Arcology Lazarus. SMART results describe reported device facts.";
        edited.image_storage_paths = split_lines(extract_json_string(request, "image_storage_text"));
        edited.image_storage_path = edited.image_storage_paths.empty() ? "" : edited.image_storage_paths.front();
        edited.image_storage_device = extract_json_string(request, "image_storage_device");
        edited.image_storage_volume = extract_json_string(request, "image_storage_volume");
        edited.image_storage_port_paths = split_lines(extract_json_string(request, "image_storage_port_text"));
        edited.source_only_paths = split_lines(extract_json_string(request, "source_text"));
        edited.destination_only_paths = split_lines(extract_json_string(request, "destination_text"));
        edited.removable_media_paths = split_lines(extract_json_string(request, "removable_text"));
        edited.ignored_paths = split_lines(extract_json_string(request, "ignored_text"));
        edited.port_labels = labels_from_text(extract_json_string(request, "labels_text"));
        std::string error;
        if (!save_bench_profile(edited, config.bench_path, error)) {
            return error_json(command, error);
        }
        return final_json(true, command,
                          "\"summary_text\":" + quote(bench_summary_text(config, edited)) +
                              ",\"devices_rows\":" + quote(device_rows_text(edited)) +
                              ",\"ports_rows\":" + quote(port_rows_text(edited)) +
                              ",\"backups_rows\":" + quote(backup_rows_text(edited)));
    }
    if (command == "mount_image_storage") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        if (bench.image_storage_device.empty()) {
            return error_json(command, "No image-storage disk is assigned. Select a disk and configure it first.");
        }
        if (image_storage_online(bench)) {
            return final_json(true, command,
                              std::string{"\"message\":\"Image storage is already mounted.\","} +
                              "\"mount_path\":\"/mnt/lazarus-storage/images\"," +
                              "\"mount_source\":" + quote(image_storage_mount_source()));
        }
        std::string error;
        if (!ensure_image_storage_ready(bench, error)) return error_json(command, error);
        return final_json(true, command,
                          std::string{"\"message\":\"Image storage mounted read-write and is ready for backups.\","} +
                          "\"mount_path\":\"/mnt/lazarus-storage/images\"," +
                          "\"mount_source\":" + quote(image_storage_mount_source()) +
                          ",\"storage_online\":true");
    }
    if (command == "unmount_image_storage") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        if (!path_is_mountpoint("/mnt/lazarus-storage")) {
            return final_json(true, command, "\"message\":\"Image storage is already unmounted.\",\"storage_online\":false");
        }
        if (const auto device = find_device(bench, bench.image_storage_device)) {
            if (const auto active = device_activity(*device)) {
                return error_json(command, "Image storage is in use by '" + active->operation + "'. Wait for that operation to finish.");
            }
        }
        std::string output;
        std::string error;
        run_program({"sync"}, output, error);
        if (!run_program({"umount", "/mnt/lazarus-storage"}, output, error)) {
            return error_json(command, "Image storage could not be unmounted safely: " + error);
        }
        return final_json(true, command,
                          std::string{"\"message\":\"Image storage was flushed and unmounted. The disk is safe to disconnect.\","} +
                          "\"storage_online\":false");
    }
    if (command == "configure_image_storage") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        const auto selector = extract_json_string(request, "selector");
        const auto mode = extract_json_string(request, "mode");
        if (selector.empty()) return error_json(command, "Select a detected physical disk first.");
        const bool format = mode == "format" || mode == "erase";
        if (mode != "existing" && !format) {
            return error_json(command, "Storage mode must be existing or format.");
        }
        if (format && extract_json_string(request, "confirmation") != "ERASE") {
            return error_json(command, "Type ERASE before formatting the entire storage disk.");
        }
        const auto device = find_device(bench, selector);
        if (!device) return error_json(command, "The selected storage disk is no longer connected.");
        DeviceActivityGuard activity(*device, format ? "format image storage" : "mount image storage");
        if (!activity.acquired()) {
            return error_json(command, "The selected disk is already in use by another Lazarus operation.");
        }
        activity.phase(format ? "erase, partition, and format" : "inspect filesystem");
        std::string filesystem;
        std::string error;
        if (!configure_image_storage(config, bench, *device, format, filesystem, error)) {
            return error_json(command, error);
        }
        const auto configured = lazarus::load_bench_profile(config.bench_path);
        return final_json(true, command,
                          "\"message\":" + quote(format
                              ? "The selected disk was partitioned with GPT, formatted as ext4, mounted, and assigned as persistent image storage."
                              : "The existing filesystem was mounted and assigned as persistent image storage without formatting.") +
                              ",\"filesystem\":" + quote(filesystem) +
                              ",\"mount_path\":" + quote("/mnt/lazarus-storage/images") +
                              ",\"devices_rows\":" + quote(device_rows_text(configured)) +
                              ",\"ports_rows\":" + quote(port_rows_text(configured)) +
                              ",\"storage_online\":true");
    }
    if (command == "devices") {
        const auto devices = lazarus::apply_bench_policy(bench, lazarus::discover_block_devices());
        std::string device_list = "[";
        for (std::size_t i = 0; i < devices.size(); ++i) {
            if (i != 0) {
                device_list += ",";
            }
            device_list += device_json(bench, devices[i]);
        }
        device_list += "]";
        return final_json(true, command, "\"device_generation\":" + quote(device_fingerprint(bench)) +
                          ",\"devices\":" + device_list + ",\"devices_rows\":" + quote(device_rows_text(bench)));
    }
    if (command == "device_generation") {
        return final_json(true, command, "\"device_generation\":" + quote(device_fingerprint(bench)));
    }
    if (command == "backups") {
        return final_json(true, command, "\"backups_rows\":" + quote(backup_rows_text(bench)));
    }
    if (command == "driver_plan" || command == "driver_apply_offline") {
        auto package_paths = split_lines(extract_json_string(request, "package_paths_text"));
        lazarus::DriverMigrationPlanOptions plan_options;
        plan_options.target_hardware_ids = split_lines(extract_json_string(request, "hardware_ids_text"));
        plan_options.requested_removals = split_lines(extract_json_string(request, "removals_text"));
        plan_options.removal_risk_acknowledged = request_boolean(request, "removal_risk_acknowledged");
        const bool automatic = request_boolean(request, "automatic");
        if (command == "driver_apply_offline" && !plan_options.requested_removals.empty()) {
            return error_json(command, "Universal Restore does not remove existing Windows storage drivers. Existing drivers remain as fallback boot paths.");
        }
        if (automatic) {
            const auto selector = extract_json_string(request, "destination_selector");
            const auto destination = find_device(bench, selector);
            if (!destination || destination->is_system_disk || destination->bench_role != lazarus::DeviceRole::DestinationOnly) {
                return error_json(command, "Universal Restore requires a connected destination-only disk.");
            }
            if (!plan_options.requested_removals.empty()) {
                return error_json(command, "Universal Restore preserves existing storage drivers. Driver removal is available only in explicit expert servicing plans.");
            }
            plan_options.target_hardware_ids = destination_storage_hardware_ids(destination->linux_path);
            if (plan_options.target_hardware_ids.empty()) {
                return error_json(command,
                    "Lazarus could not identify the PCI storage controller that owns the destination disk. "
                    "Install the destination internally in the replacement computer, or load a replacement-hardware profile.");
            }
            package_paths = automatic_driver_package_paths(bench);
            if (package_paths.empty()) {
                return error_json(command,
                    "No extracted INF packages were found beneath DriverVault in configured image storage. "
                    "Import the replacement computer's storage-driver package before Universal Restore.");
            }
        }
        if (package_paths.empty()) return error_json(command, "At least one extracted driver package directory is required.");

        std::vector<lazarus::DriverPackageInspection> inspections;
        for (const auto& package_path : package_paths) {
            std::string path_error;
            if (!image_path_allowed(bench, package_path, true, path_error)) {
                return error_json(command, "Driver packages must be stored beneath configured image storage. " + path_error);
            }
            if (!std::filesystem::is_directory(package_path)) {
                return error_json(command, "Driver package path is not a directory: " + package_path);
            }
            inspections.push_back(lazarus::inspect_driver_package(package_path));
        }
        const auto plan = lazarus::create_driver_migration_plan(plan_options, inspections);
        if (command == "driver_plan") {
            return final_json(plan.ready_for_servicing, command,
                              failure_error_field(plan.ready_for_servicing, plan.findings, "The driver plan is not ready for servicing.") +
                                  "\"matching_storage_driver_found\":" + std::string(plan.matching_storage_driver_found ? "true" : "false") +
                                  ",\"requires_windows_pe\":false" +
                                  ",\"automatic\":" + std::string(automatic ? "true" : "false") +
                                  ",\"hardware_ids_text\":" + quote(join_lines(plan.target_hardware_ids)) +
                                  ",\"package_paths_text\":" + quote(join_lines(package_paths)) +
                                  ",\"actions\":" + driver_plan_actions_json(plan.actions) +
                                  ",\"actions_rows\":" + quote(driver_plan_rows(plan)) +
                                  ",\"facts\":" + string_array_json(plan.facts) +
                                  ",\"findings\":" + findings_json(plan.findings));
        }

        if (!plan.ready_for_servicing) {
            return final_json(false, command,
                              failure_error_field(false, plan.findings, "The driver plan is not ready for servicing.") +
                                  "\"actions_rows\":" + quote(driver_plan_rows(plan)) +
                                  ",\"facts\":" + string_array_json(plan.facts) +
                                  ",\"findings\":" + findings_json(plan.findings));
        }
        if (command == "driver_apply_offline") {
            if (extract_json_string(request, "confirmation") != "APPLY UNIVERSAL RESTORE") {
                return error_json(command, "Type APPLY UNIVERSAL RESTORE to modify the restored offline Windows installation.");
            }
            const auto destination_selector = extract_json_string(request, "destination_selector");
            auto destination = find_device(bench, destination_selector);
            if (!destination || destination->is_system_disk || destination->bench_role != lazarus::DeviceRole::DestinationOnly) {
                return error_json(command, "Offline Windows servicing requires a connected destination-only disk.");
            }
            DeviceActivityGuard activity(*destination, "Apply Universal Restore boot drivers");
            if (!activity.acquired()) return error_json(command, "The replacement disk is already in use.");
            activity.phase("locate Windows");
            send_line(out, progress_json(lazarus::ProgressEvent{
                "universal-restore", "locate-windows", "Locating the restored offline Windows installation."}));

            const auto mount_root = std::filesystem::path("/run/arcology-lazarus/windows-servicing") / random_hex(12);
            std::error_code filesystem_error;
            std::filesystem::create_directories(mount_root, filesystem_error);
            if (filesystem_error) return error_json(command, "Could not create the protected Windows servicing mount point: " + filesystem_error.message());

            std::filesystem::path windows_directory;
            std::filesystem::path mounted_partition;
            std::string error;
            std::string last_mount_error;
            std::vector<std::string> candidates = destination->partitions;
            if (candidates.empty()) candidates.push_back(destination->linux_path);
            for (const auto& candidate : candidates) {
                if (block_device_tag(candidate, "TYPE") != "ntfs") continue;
                std::string mounted_at;
                std::string findmnt_error;
                if (run_program({"findmnt", "-rn", "-S", candidate, "-o", "TARGET"}, mounted_at, findmnt_error) &&
                    !trim_copy(mounted_at).empty()) {
                    last_mount_error = "A candidate Windows partition is already mounted outside Lazarus: " + candidate;
                    continue;
                }
                std::string output;
                if (!run_program({"mount", "-o", "rw,nosuid,nodev,noexec", candidate, mount_root.string()}, output, error)) {
                    last_mount_error = error;
                    continue;
                }
                const auto candidate_windows = case_insensitive_child(mount_root, "Windows");
                const auto candidate_hive = candidate_windows.empty() ? std::filesystem::path{} :
                    case_insensitive_path(candidate_windows, {"System32", "config", "SYSTEM"});
                if (!candidate_windows.empty() && !candidate_hive.empty()) {
                    windows_directory = candidate_windows;
                    mounted_partition = candidate;
                    break;
                }
                std::string unmount_output;
                std::string unmount_error;
                if (!run_program({"umount", mount_root.string()}, unmount_output, unmount_error)) {
                    std::filesystem::remove(mount_root, filesystem_error);
                    return error_json(command, "A non-Windows NTFS partition could not be unmounted after inspection: " + unmount_error);
                }
            }
            if (windows_directory.empty()) {
                std::filesystem::remove(mount_root, filesystem_error);
                const auto detail = last_mount_error.empty() ?
                    "No NTFS partition contained Windows/System32/config/SYSTEM." : last_mount_error;
                return error_json(command, "Lazarus could not mount a writable offline Windows installation on the replacement disk. " + detail +
                    " Fast Startup, hibernation, BitLocker, or filesystem damage may require repair before migration.");
            }

            const auto job_id = std::string("universal-") + random_hex(8);
            std::vector<std::string> servicing_facts;
            activity.phase("install boot driver");
            send_line(out, progress_json(lazarus::ProgressEvent{
                "universal-restore", "install-driver", "Installing the matched boot-storage driver into offline Windows."}));
            const bool applied = apply_native_windows_bootstrap(
                windows_directory, plan, package_paths, job_id, servicing_facts, error);

            activity.phase("flush");
            const int mount_descriptor = ::open(mount_root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (mount_descriptor < 0 || ::syncfs(mount_descriptor) != 0) {
                if (error.empty()) error = "Could not flush the serviced Windows filesystem: " + std::string(std::strerror(errno));
            }
            if (mount_descriptor >= 0) ::close(mount_descriptor);
            std::string unmount_output;
            std::string unmount_error;
            const bool unmounted = run_program({"umount", mount_root.string()}, unmount_output, unmount_error);
            std::filesystem::remove(mount_root, filesystem_error);
            if (!unmounted && error.empty()) error = "Windows servicing finished, but the partition could not be unmounted: " + unmount_error;
            if (!applied || !error.empty() || !unmounted) {
                return final_json(false, command,
                                  "\"error\":" + quote(error.empty() ? "Offline Windows servicing did not complete." : error) +
                                      ",\"job_id\":" + quote(job_id) +
                                      ",\"target_partition\":" + quote(mounted_partition.string()) +
                                      ",\"safe_to_disconnect\":" + std::string(unmounted ? "true" : "false") +
                                      ",\"facts\":" + string_array_json(servicing_facts));
            }
            return final_json(true, command,
                              "\"job_id\":" + quote(job_id) +
                                  ",\"target_partition\":" + quote(mounted_partition.string()) +
                                  ",\"requires_windows_pe\":false" +
                                  ",\"safe_to_disconnect\":true" +
                                  ",\"facts\":" + string_array_json(servicing_facts) +
                                  ",\"message\":" + quote("Universal Restore boot-driver servicing completed inside Lazarus OS."));
        }
    }
    if (command == "inspect_source") {
        const auto selector = extract_json_string(request, "selector");
        auto device = find_device(bench, selector);
        if (!device) {
            return error_json(command, "No discovered device matched selector.");
        }
        auto open_result = lazarus::open_source_read_only(bench, *device);
        if (!open_result.handle.is_open()) {
            return final_json(false, command,
                              failure_error_field(false, open_result.findings, "The source drive could not be opened read-only.") +
                                  "\"findings\":" + findings_json(open_result.findings));
        }
        const auto inspection = lazarus::inspect_source_disk(open_result.handle);
        std::string partitions = "[";
        for (std::size_t i = 0; i < inspection.partitions.size(); ++i) {
            const auto& partition = inspection.partitions[i];
            if (i != 0) {
                partitions += ",";
            }
            partitions += "{\"number\":" + std::to_string(partition.number) +
                          ",\"kind\":" + quote(lazarus::to_string(partition.kind)) +
                          ",\"filesystem\":" + quote(lazarus::to_string(partition.filesystem)) +
                          ",\"first_lba\":" + std::to_string(partition.first_lba) +
                          ",\"last_lba\":" + std::to_string(partition.last_lba) +
                          ",\"size_bytes\":" + std::to_string(partition.size_bytes) + "}";
        }
        partitions += "]";
        const bool supported = has_imageable_layout(inspection) && !has_blocker(inspection.findings);
        return final_json(supported, command,
                          failure_error_field(supported, inspection.findings, "The selected source layout is not supported for imaging.") +
                              "\"device\":" + device_json(bench, *device) +
                              ",\"mbr_detected\":" + std::string(inspection.mbr_detected ? "true" : "false") +
                              ",\"gpt_detected\":" + std::string(inspection.gpt_detected ? "true" : "false") +
                              ",\"gpt_header_valid\":" + std::string(inspection.gpt_header_valid ? "true" : "false") +
                              ",\"partitions\":" + partitions +
                              ",\"facts\":" + string_array_json(inspection.facts) +
                              ",\"findings\":" + findings_json(inspection.findings));
    }
    if (command == "smart") {
        const auto selector = extract_json_string(request, "selector");
        auto device = find_device(bench, selector);
        if (!device) {
            return error_json(command, "No discovered device matched selector.");
        }
        DeviceActivityGuard activity(*device, "SMART diagnostics");
        if (!activity.acquired()) {
            return error_json(command, "The selected drive is already in use by another Lazarus operation.");
        }
        activity.phase("diagnostics");
        const auto smart = lazarus::collect_smart_diagnostics(*device);
        const bool succeeded = smart.health != "failed";
        return final_json(succeeded, command,
                          failure_error_field(succeeded, smart.findings, "SMART diagnostics did not complete.") +
                              smart_json(smart));
    }
    if (command == "browse_open") {
        const auto image_directory = extract_json_string(request, "image_directory");
        std::string path_error;
        if (!image_path_allowed(bench, image_directory, true, path_error)) return error_json(command, path_error);
        if (bench.image_storage_paths.empty()) return error_json(command, "No image-storage directory is configured.");

        std::error_code filesystem_error;
        const auto canonical_image = std::filesystem::weakly_canonical(image_directory, filesystem_error);
        const auto cache_key = filesystem_error ? std::string{} : sha256_text(canonical_image.string());
        if (cache_key.empty()) return error_json(command, "Could not derive a protected cache identity for this image.");
        const auto cache_path = std::filesystem::path(bench.image_storage_paths.front()) /
                                ".lazarus-browse-cache" / cache_key / "disk.raw";
        lazarus::ImageBrowseCacheResult cache;
        {
            std::lock_guard prepare_lock(browse_prepare_mutex);
            cache = lazarus::prepare_image_browse_cache({
                image_directory,
                cache_path.string(),
                [&out](const lazarus::ProgressEvent& event) { send_line(out, progress_json(event)); },
            });
        }
        if (!cache.prepared) {
            return final_json(false, command,
                              failure_error_field(false, cache.findings, "The read-only image browser could not be prepared.") +
                                  "\"findings\":" + findings_json(cache.findings));
        }

        std::string ignored_output;
        std::string ignored_error;
        run_program({"modprobe", "loop"}, ignored_output, ignored_error);
        std::string loop_output;
        std::string loop_error;
        if (!run_program({"losetup", "--find", "--show", "--read-only", "--partscan", cache_path.string()},
                         loop_output, loop_error)) {
            return error_json(command, "Could not attach the verified image read-only: " + loop_error);
        }
        const auto loop_device = trim_copy(loop_output);
        if (loop_device.empty()) return error_json(command, "losetup did not report the read-only loop device.");
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        std::string layout_output;
        std::string layout_error;
        if (!run_program({"lsblk", "-lnpo", "NAME,TYPE", loop_device}, layout_output, layout_error)) {
            run_program({"losetup", "-d", loop_device}, ignored_output, ignored_error);
            return error_json(command, "Could not enumerate image partitions: " + layout_error);
        }
        std::vector<std::pair<std::string, std::string>> layout;
        std::istringstream layout_lines(layout_output);
        std::string layout_line;
        while (std::getline(layout_lines, layout_line)) {
            std::istringstream fields(layout_line);
            std::string path;
            std::string type;
            fields >> path >> type;
            if (!path.empty() && (type == "part" || type == "loop")) layout.emplace_back(path, type);
        }
        const bool has_partitions = std::any_of(layout.begin(), layout.end(), [](const auto& item) { return item.second == "part"; });
        BrowseSession session;
        session.id = random_hex(16);
        session.image_directory = image_directory;
        session.cache_path = cache_path.string();
        session.loop_device = loop_device;
        for (const auto& [path, type] : layout) {
            if (has_partitions && type == "loop") continue;
            BrowseVolume volume;
            volume.token = random_hex(8);
            volume.device_path = path;
            volume.filesystem = block_device_tag(path, "TYPE");
            volume.label = block_device_tag(path, "LABEL");
            const auto size = command_value({"blockdev", "--getsize64", path});
            try { volume.size_bytes = std::stoull(size); } catch (...) { volume.size_bytes = 0; }
            session.volumes.push_back(std::move(volume));
        }
        if (session.id.empty() || session.volumes.empty()) {
            close_browse_session(session);
            return error_json(command, "The image was attached read-only, but no browseable volume candidates were found.");
        }
        std::string rows;
        for (std::size_t index = 0; index < session.volumes.size(); ++index) {
            const auto& volume = session.volumes[index];
            const auto display = volume.label.empty() ? "Partition " + std::to_string(index + 1) : volume.label;
            rows += volume.token + "\t" + hex_encode_text(display) + "\t" + volume.filesystem + "\t" +
                    std::to_string(volume.size_bytes) + "\t" +
                    (supported_browse_filesystem(volume.filesystem) ? "1" : "0") + "\n";
        }
        const auto session_id = session.id;
        {
            std::lock_guard lock(browse_mutex);
            browse_sessions.emplace(session.id, std::move(session));
        }
        return final_json(true, command,
                          "\"session_id\":" + quote(session_id) +
                              ",\"volumes_rows\":" + quote(rows) +
                              ",\"cache_reused\":" + std::string(cache.reused_existing_cache ? "true" : "false") +
                              ",\"message\":" + quote("The verified image is attached read-only."));
    }
    if (command == "browse_list") {
        const auto session_id = extract_json_string(request, "session_id");
        const auto volume_token = extract_json_string(request, "volume_token");
        const auto relative_path = extract_json_string(request, "relative_path");
        std::lock_guard lock(browse_mutex);
        const auto session_found = browse_sessions.find(session_id);
        if (session_found == browse_sessions.end()) return error_json(command, "The read-only browse session has expired or was closed.");
        auto volume_found = std::find_if(session_found->second.volumes.begin(), session_found->second.volumes.end(),
                                         [&](const BrowseVolume& volume) { return volume.token == volume_token; });
        if (volume_found == session_found->second.volumes.end()) return error_json(command, "The selected image volume was not found.");
        std::string mount_error;
        if (!mount_browse_volume(*volume_found, session_id, mount_error)) return error_json(command, mount_error);
        std::filesystem::path directory;
        if (!resolve_browse_path(volume_found->mount_path, relative_path, directory, mount_error) ||
            !std::filesystem::is_directory(directory)) {
            return error_json(command, mount_error.empty() ? "The requested image folder is not a directory." : mount_error);
        }
        struct Entry { std::string name; std::string relative; std::string type; std::uint64_t size; };
        std::vector<Entry> entries;
        std::error_code iteration_error;
        for (const auto& entry : std::filesystem::directory_iterator(directory, iteration_error)) {
            if (entries.size() >= 10000) break;
            const auto status = entry.symlink_status(iteration_error);
            if (iteration_error) break;
            Entry item;
            item.name = entry.path().filename().string();
            const auto child_relative = (std::filesystem::path(relative_path) / entry.path().filename()).lexically_normal();
            item.relative = child_relative == "." ? std::string{} : child_relative.string();
            item.type = std::filesystem::is_symlink(status) ? "link" :
                        (std::filesystem::is_directory(status) ? "directory" :
                         (std::filesystem::is_regular_file(status) ? "file" : "special"));
            if (item.type == "file") item.size = entry.file_size(iteration_error);
            entries.push_back(std::move(item));
        }
        if (iteration_error) return error_json(command, "Could not enumerate the selected image folder: " + iteration_error.message());
        std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
            if (left.type == "directory" && right.type != "directory") return true;
            if (left.type != "directory" && right.type == "directory") return false;
            return left.name < right.name;
        });
        std::string rows;
        for (const auto& entry : entries) {
            rows += hex_encode_text(entry.name) + "\t" + hex_encode_text(entry.relative) + "\t" + entry.type + "\t" +
                    std::to_string(entry.size) + "\n";
        }
        return final_json(true, command,
                          "\"relative_path\":" + quote(relative_path) +
                              ",\"entries_rows\":" + quote(rows) +
                              ",\"truncated\":" + std::string(entries.size() >= 10000 ? "true" : "false"));
    }
    if (command == "browse_export") {
        const auto session_id = extract_json_string(request, "session_id");
        const auto volume_token = extract_json_string(request, "volume_token");
        const auto destination_selector = extract_json_string(request, "destination_selector");
        const auto destination_folder = extract_json_string(request, "destination_folder");
        const auto encoded_sources = split_lines(extract_json_string(request, "source_paths_hex"));
        if (encoded_sources.empty()) return error_json(command, "Select at least one file or folder to recover.");
        auto destination_device = find_device(bench, destination_selector);
        if (!destination_device) return error_json(command, "The removable-media destination is no longer connected.");
        if (destination_device->is_system_disk || destination_device->bench_role != lazarus::DeviceRole::RemovableMedia) {
            return error_json(command, "Recovered files may only be copied to a device on a configured removable-media port.");
        }
        DeviceActivityGuard activity(*destination_device, "Recover files");
        if (!activity.acquired()) return error_json(command, "The removable-media destination is already in use.");

        std::lock_guard browse_lock(browse_mutex);
        const auto session_found = browse_sessions.find(session_id);
        if (session_found == browse_sessions.end()) return error_json(command, "The read-only browse session has expired or was closed.");
        auto volume_found = std::find_if(session_found->second.volumes.begin(), session_found->second.volumes.end(),
                                         [&](const BrowseVolume& volume) { return volume.token == volume_token; });
        if (volume_found == session_found->second.volumes.end()) return error_json(command, "The selected image volume was not found.");
        std::string error;
        if (!mount_browse_volume(*volume_found, session_id, error)) return error_json(command, error);

        std::vector<std::filesystem::path> sources;
        for (const auto& encoded : encoded_sources) {
            const auto decoded = hex_decode_text(encoded);
            if (!decoded) return error_json(command, "A selected recovery path was malformed.");
            std::filesystem::path source;
            if (!resolve_browse_path(volume_found->mount_path, *decoded, source, error) || !std::filesystem::exists(source)) {
                return error_json(command, error.empty() ? "A selected file no longer exists in the image." : error);
            }
            const auto status = std::filesystem::symlink_status(source);
            if (std::filesystem::is_symlink(status) ||
                (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status))) {
                return error_json(command, "Symbolic links and special files cannot be exported.");
            }
            sources.push_back(std::move(source));
        }

        std::string destination_volume;
        std::string destination_filesystem;
        if (!destination_volume_for_device(*destination_device, destination_volume, destination_filesystem, error)) {
            return error_json(command, error);
        }
        std::string mounted_at;
        std::string findmnt_error;
        if (run_program({"findmnt", "-rn", "-S", destination_volume, "-o", "TARGET"}, mounted_at, findmnt_error) &&
            !trim_copy(mounted_at).empty()) {
            return error_json(command, "The removable-media filesystem is already mounted outside Lazarus. Unmount it before recovery export.");
        }
        const auto destination_mount = std::filesystem::path("/run/arcology-lazarus/export") / random_hex(12);
        std::error_code filesystem_error;
        std::filesystem::create_directories(destination_mount, filesystem_error);
        if (filesystem_error) return error_json(command, "Could not create the protected export mount point: " + filesystem_error.message());
        activity.phase("mount destination");
        std::string mount_output;
        if (!run_program({"mount", "-o", "rw,nosuid,nodev,noexec", destination_volume, destination_mount.string()}, mount_output, error)) {
            std::filesystem::remove(destination_mount, filesystem_error);
            return error_json(command, "Could not mount the removable-media destination: " + error);
        }
        bool destination_mounted = true;
        const auto unmount_destination = [&]() {
            if (!destination_mounted) return true;
            std::string output;
            std::string unmount_error;
            const bool unmounted = run_program({"umount", destination_mount.string()}, output, unmount_error);
            if (!unmounted && error.empty()) error = "Recovered files were written, but the destination could not be unmounted: " + unmount_error;
            destination_mounted = !unmounted;
            return unmounted;
        };

        std::filesystem::path base_relative;
        if (!safe_relative_path(destination_folder, base_relative, error)) {
            unmount_destination();
            return error_json(command, error);
        }
        auto base = destination_mount / base_relative;
        std::filesystem::create_directories(base, filesystem_error);
        if (filesystem_error || (base != destination_mount && !path_is_beneath(base, destination_mount))) {
            if (error.empty()) error = "Could not create the selected folder on removable media.";
            unmount_destination();
            return error_json(command, error);
        }
        std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char timestamp[32]{};
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &local);
        const auto recovery_root = base / (std::string("Lazarus-Recovery-") + timestamp + "-" + random_hex(3));
        if (!std::filesystem::create_directory(recovery_root, filesystem_error) || filesystem_error) {
            error = "Could not create a unique recovery folder on removable media.";
            unmount_destination();
            return error_json(command, error);
        }

        activity.phase("copy files");
        std::uint64_t file_count = 0;
        std::uint64_t directory_count = 0;
        std::uint64_t bytes_copied = 0;
        bool copied = true;
        for (const auto& source : sources) {
            if (!copy_recovered_tree(source, recovery_root / source.filename(), file_count, directory_count,
                                     bytes_copied, 0, error)) {
                copied = false;
                break;
            }
            send_line(out, progress_json(lazarus::ProgressEvent{
                "recover-files", "copy", "Copied " + source.filename().string() + ".",
                bytes_copied, 0, file_count, 0, true,
            }));
        }
        activity.phase("flush");
        const int mount_fd = ::open(destination_mount.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (mount_fd < 0 || ::syncfs(mount_fd) != 0) {
            if (error.empty()) error = "Could not flush recovered files to removable media: " + std::string(std::strerror(errno));
            copied = false;
        }
        if (mount_fd >= 0) ::close(mount_fd);
        const bool unmounted = unmount_destination();
        std::filesystem::remove(destination_mount, filesystem_error);
        if (!copied || !unmounted) {
            return final_json(false, command,
                              "\"error\":" + quote(error.empty() ? "File recovery export did not complete." : error) +
                                  ",\"files_copied\":" + std::to_string(file_count) +
                                  ",\"bytes_copied\":" + std::to_string(bytes_copied) +
                                  ",\"safe_to_disconnect\":" + std::string(unmounted ? "true" : "false"));
        }
        return final_json(true, command,
                          "\"files_copied\":" + std::to_string(file_count) +
                              ",\"directories_copied\":" + std::to_string(directory_count) +
                              ",\"bytes_copied\":" + std::to_string(bytes_copied) +
                              ",\"destination_folder\":" + quote(recovery_root.filename().string()) +
                              ",\"safe_to_disconnect\":true," +
                              "\"message\":" + quote("Recovered files were flushed and the removable-media destination was unmounted safely."));
    }
    if (command == "browse_close") {
        const auto session_id = extract_json_string(request, "session_id");
        std::lock_guard lock(browse_mutex);
        const auto session_found = browse_sessions.find(session_id);
        if (session_found == browse_sessions.end()) return final_json(true, command, "\"closed\":true");
        std::string close_error;
        if (!close_browse_session(session_found->second, &close_error)) return error_json(command, close_error);
        browse_sessions.erase(session_found);
        return final_json(true, command, "\"closed\":true");
    }
    if (command == "verify_image") {
        const auto image_dir = extract_json_string(request, "image_directory");
        if (image_dir.empty()) {
            return error_json(command, "image_directory is required.");
        }
        std::string path_error;
        if (!image_path_configured(bench, image_dir, path_error)) return error_json(command, path_error);
        std::string storage_error;
        if (!ensure_image_storage_ready(bench, storage_error)) return error_json(command, storage_error);
        if (!image_path_allowed(bench, image_dir, true, path_error)) {
            return error_json(command, path_error);
        }
        const auto result = lazarus::verify_directory_image(image_dir, [&out](const lazarus::ProgressEvent& event) {
            send_line(out, progress_json(event));
        });
        const auto [report_job, report_device] = report_context_from_image(image_dir);
        std::string report_error;
        const bool report_written = write_completion_reports(
            bench, image_dir, "completion-report", "Backup Verification",
            result.verified ? "Verified" : "Verification failed", report_job, report_device,
            {{"Expected bytes", human_capacity(result.expected_bytes)},
             {"Verified bytes", human_capacity(result.actual_bytes)},
             {"Chunks verified", std::to_string(result.chunks_verified)},
             {"Partition table", result.partition_table_valid ? "Passed" : "Not validated"},
             {"Filesystem browsing", result.filesystem_readable ? "Passed" : "Not validated"},
             {"NTFS MFT", result.ntfs_mft_readable ? "Readable" : "Not validated"},
             {"Unreadable ranges", std::to_string(result.unreadable_ranges.size())}},
            result.facts, result.findings, report_error);
        return final_json(result.verified, command,
                          failure_error_field(result.verified, result.findings, "Image verification did not complete.") +
                              "\"image_directory\":" + quote(result.image_directory) +
                              ",\"expected_bytes\":" + std::to_string(result.expected_bytes) +
                              ",\"actual_bytes\":" + std::to_string(result.actual_bytes) +
                              ",\"stored_bytes\":" + std::to_string(result.stored_bytes) +
                              ",\"chunks_verified\":" + std::to_string(result.chunks_verified) +
                              ",\"partition_table_valid\":" + std::string(result.partition_table_valid ? "true" : "false") +
                              ",\"filesystem_readable\":" + std::string(result.filesystem_readable ? "true" : "false") +
                              ",\"ntfs_mft_readable\":" + std::string(result.ntfs_mft_readable ? "true" : "false") +
                              ",\"bitlocker_detected\":" + std::string(result.bitlocker_detected ? "true" : "false") +
                              ",\"unreadable_ranges\":" + std::to_string(result.unreadable_ranges.size()) +
                              ",\"report_written\":" + std::string(report_written ? "true" : "false") +
                              ",\"report_path\":" + quote((std::filesystem::path(image_dir) / "completion-report.html").string()) +
                              ",\"report_error\":" + quote(report_error) +
                              ",\"facts\":" + string_array_json(result.facts) +
                              ",\"findings\":" + findings_json(result.findings));
    }
    if (command == "image_source" || command == "resume_image") {
        std::string selector = extract_json_string(request, "selector");
        std::string output_dir = extract_json_string(request, "output_directory");
        lazarus::JobInfo job;
        job.ticket_number = extract_json_string(request, "ticket_number");
        job.customer_name = extract_json_string(request, "customer_name");
        job.technician = extract_json_string(request, "technician");
        job.purpose = extract_json_string(request, "purpose");
        std::string requested_mode = extract_json_string(request, "imaging_mode");
        std::string requested_compression = extract_json_string(request, "compression");
        const bool resume_request = command == "resume_image";
        if (resume_request) {
            output_dir = extract_json_string(request, "image_directory");
            const auto journal = read_text_file(std::filesystem::path(output_dir) / "job-journal.json");
            if (journal.empty()) return error_json(command, "The interrupted image has no persistent job journal and cannot be resumed from the UI.");
            job.ticket_number = extract_json_string(journal, "ticket_number");
            job.customer_name = extract_json_string(journal, "customer_name");
            job.technician = extract_json_string(journal, "technician");
            job.purpose = extract_json_string(journal, "purpose");
            requested_mode = extract_json_string(journal, "imaging_mode");
            requested_compression = extract_json_string(journal, "compression");
            if (selector.empty()) {
                selector = extract_json_string(journal, "by_id_path");
                if (selector.empty()) selector = extract_json_string(journal, "by_path");
                if (selector.empty()) selector = extract_json_string(journal, "physical_path");
                if (selector.empty()) selector = extract_json_string(journal, "linux_path");
            }
        }
        if (!lazarus::is_complete(job) || selector.empty() || output_dir.empty()) {
            return error_json(command, "selector, output_directory, ticket_number, customer_name, technician, and purpose are required.");
        }
        std::string path_error;
        if (!image_path_configured(bench, output_dir, path_error)) return error_json(command, path_error);
        std::string storage_error;
        if (!ensure_image_storage_ready(bench, storage_error)) return error_json(command, storage_error);
        if (!image_path_allowed(bench, output_dir, false, path_error)) {
            return error_json(command, path_error);
        }
        auto device = find_device(bench, selector);
        if (!device) {
            return error_json(command, "No discovered device matched selector.");
        }
        DeviceActivityGuard activity(*device, "Create backup");
        if (!activity.acquired()) {
            return error_json(command, "The selected source is already in use by another Lazarus operation.");
        }
        auto open_result = lazarus::open_source_read_only(bench, *device);
        if (!open_result.handle.is_open()) {
            return final_json(false, command,
                              failure_error_field(false, open_result.findings, "The source drive could not be opened read-only.") +
                                  "\"findings\":" + findings_json(open_result.findings));
        }
        const auto inspection = lazarus::inspect_source_disk(open_result.handle);
        if (!has_imageable_layout(inspection) || has_blocker(inspection.findings)) {
            return final_json(false, command,
                              failure_error_field(false, inspection.findings, "The selected source layout is not supported for imaging.") +
                                  "\"facts\":" + string_array_json(inspection.facts) +
                                           ",\"findings\":" + findings_json(inspection.findings));
        }
        lazarus::ImageWriteOptions options;
        options.output_directory = output_dir;
        const auto preset_id = extract_json_string(request, "preset");
        const JobPreset* preset = preset_id.empty() ? nullptr : find_job_preset(preset_id);
        if (!preset_id.empty() && preset == nullptr) return error_json(command, "The requested job preset is not defined by this Lazarus service.");
        options.compression = preset != nullptr ? preset->compression
            : (requested_compression == "zstd" ? lazarus::CompressionMode::Zstd : lazarus::CompressionMode::None);
        options.mode = preset != nullptr ? preset->mode
            : (requested_mode == "rescue" ? lazarus::ImagingMode::Rescue : lazarus::ImagingMode::Raw);
        const bool verify_after_imaging = resume_request || (preset != nullptr && preset->verify_after_imaging);
        options.progress = [&out, &activity](const lazarus::ProgressEvent& event) {
            activity.phase(event.phase);
            send_line(out, progress_json(event));
        };
        const auto result = lazarus::write_directory_image(job, open_result.handle, inspection, options);
        std::optional<lazarus::ImageVerificationResult> verification;
        if (result.finalized && verify_after_imaging) {
            activity.phase("verify");
            verification = lazarus::verify_directory_image(result.output_directory, [&out, &activity](const lazarus::ProgressEvent& event) {
                activity.phase(event.phase);
                send_line(out, progress_json(event));
            });
        }
        const bool operation_succeeded = result.finalized && (!verify_after_imaging || (verification && verification->verified));
        std::vector<std::string> report_facts = result.facts;
        std::vector<lazarus::SafetyFinding> report_findings = result.findings;
        if (verification) {
            report_facts.insert(report_facts.end(), verification->facts.begin(), verification->facts.end());
            report_findings.insert(report_findings.end(), verification->findings.begin(), verification->findings.end());
        }
        std::string report_error;
        const bool report_written = write_completion_reports(
            bench, result.output_directory, operation_succeeded ? "completion-report" : "image-creation-report",
            verify_after_imaging ? "Image Creation and Verification" : "Image Creation",
            operation_succeeded ? (result.completed_with_warnings ? "Verified with recovery warnings" :
                                  (verify_after_imaging ? "Verified" : "Image creation completed"))
                                : (result.finalized ? "Image finalized but verification failed" : "Image creation did not complete"),
            job, *device,
            {{"Source bytes read", human_capacity(result.bytes_read)},
             {"Stored bytes", human_capacity(result.bytes_stored)},
             {"Chunks written", std::to_string(result.chunks_written)},
             {"Imaging mode", lazarus::to_string(options.mode)},
             {"Compression", lazarus::to_string(options.compression)},
             {"Verification", verification ? (verification->verified ? "Passed" : "Failed") : "Not requested"},
             {"Unreadable ranges", std::to_string(result.unreadable_ranges.size())}},
            report_facts, report_findings, report_error);
        return final_json(operation_succeeded, command,
                          failure_error_field(operation_succeeded, report_findings,
                              result.finalized ? "Image finalized, but required verification did not pass." : "Image creation did not complete.") +
                              "\"output_directory\":" + quote(result.output_directory) +
                              ",\"bytes_written\":" + std::to_string(result.bytes_written) +
                              ",\"bytes_stored\":" + std::to_string(result.bytes_stored) +
                              ",\"chunks_written\":" + std::to_string(result.chunks_written) +
                              ",\"completed_with_warnings\":" + std::string(result.completed_with_warnings ? "true" : "false") +
                              ",\"unreadable_ranges\":" + std::to_string(result.unreadable_ranges.size()) +
                              ",\"resumed\":" + std::string(result.resumed ? "true" : "false") +
                              ",\"resumed_bytes\":" + std::to_string(result.resumed_bytes) +
                              ",\"verified\":" + std::string(verification && verification->verified ? "true" : "false") +
                              ",\"preset\":" + quote(preset == nullptr ? "custom" : preset->id) +
                              ",\"report_written\":" + std::string(report_written ? "true" : "false") +
                              ",\"report_path\":" + quote((std::filesystem::path(result.output_directory) /
                                  (operation_succeeded ? "completion-report.html" : "image-creation-report.html")).string()) +
                              ",\"report_error\":" + quote(report_error) +
                              ",\"facts\":" + string_array_json(result.facts) +
                              ",\"findings\":" + findings_json(result.findings));
    }
    if (command == "restore_image") {
        const auto image_dir = extract_json_string(request, "image_directory");
        const auto selector = extract_json_string(request, "selector");
        const auto confirmation = extract_json_string(request, "confirmation");
        if (image_dir.empty() || selector.empty()) {
            return error_json(command, "image_directory and selector are required.");
        }
        std::string path_error;
        if (!image_path_configured(bench, image_dir, path_error)) return error_json(command, path_error);
        std::string storage_error;
        if (!ensure_image_storage_ready(bench, storage_error)) return error_json(command, storage_error);
        if (!image_path_allowed(bench, image_dir, true, path_error)) {
            return error_json(command, path_error);
        }
        auto device = find_device(bench, selector);
        if (!device) {
            return error_json(command, "No discovered device matched selector.");
        }
        DeviceActivityGuard activity(*device, "Restore backup");
        if (!activity.acquired()) {
            return error_json(command, "The selected destination is already in use by another Lazarus operation.");
        }
        lazarus::ImageRestoreOptions options;
        options.image_directory = image_dir;
        options.confirmation = confirmation;
        options.progress = [&out, &activity](const lazarus::ProgressEvent& event) {
            activity.phase(event.phase);
            send_line(out, progress_json(event));
        };
        const auto result = lazarus::restore_directory_image(bench, *device, options);
        const auto [report_job, source_device] = report_context_from_image(image_dir);
        std::string report_error;
        const bool report_written = write_completion_reports(
            bench, image_dir, "restore-report", "Image Restore",
            result.restored ? "Restore completed and destination flushed" : "Restore did not complete",
            report_job, *device,
            {{"Image source model", source_device.model},
             {"Bytes restored", human_capacity(result.bytes_written)},
             {"Bytes read back", human_capacity(result.bytes_verified)},
             {"Chunks restored", std::to_string(result.chunks_written)},
             {"Read-back verification", result.readback_verified ? "Passed" : "Not passed"},
             {"Destination layout", result.destination_layout_validated ? "Validated" : "Not validated"}},
            result.facts, result.findings, report_error);
        return final_json(result.restored, command,
                          failure_error_field(result.restored, result.findings, "Restore did not complete.") +
                              "\"image_directory\":" + quote(result.image_directory) +
                              ",\"bytes_written\":" + std::to_string(result.bytes_written) +
                              ",\"bytes_verified\":" + std::to_string(result.bytes_verified) +
                              ",\"chunks_written\":" + std::to_string(result.chunks_written) +
                              ",\"readback_verified\":" + std::string(result.readback_verified ? "true" : "false") +
                              ",\"destination_layout_validated\":" + std::string(result.destination_layout_validated ? "true" : "false") +
                              ",\"report_written\":" + std::string(report_written ? "true" : "false") +
                              ",\"report_path\":" + quote((std::filesystem::path(image_dir) / "restore-report.html").string()) +
                              ",\"report_error\":" + quote(report_error) +
                              ",\"facts\":" + string_array_json(result.facts) +
                              ",\"findings\":" + findings_json(result.findings));
    }
    if (command == "install_os") {
        if (!validate_admin_session(extract_json_string(request, "admin_token"))) {
            return error_json(command, "Admin authentication is required or has expired.");
        }
        const auto selector = extract_json_string(request, "selector");
        const auto confirmation = extract_json_string(request, "confirmation");
        if (selector.empty()) {
            return error_json(command, "selector is required.");
        }
        if (confirmation != "ERASE") {
            return error_json(command, "confirmation must be ERASE.");
        }
        auto device = find_device(bench, selector);
        if (!device) {
            return error_json(command, "No discovered device matched selector.");
        }
        if (!is_install_target(*device)) {
            return error_json(command, "Selected device is not eligible as an install target.");
        }
        const auto installer = std::filesystem::path("/usr/local/sbin/lazarus-install-os");
        if (!std::filesystem::exists(installer)) {
            return error_json(command, "Installer script was not found in the live environment.");
        }
        const auto shell_command = shell_quote(installer.string()) + " " + shell_quote(device->linux_path) + " " + shell_quote(confirmation) + " 2>&1";
        std::string error;
        send_line(out, progress_json(lazarus::ProgressEvent{"install", "launching", "Starting Lazarus OS installer."}));
        const bool ok = stream_command_output(shell_command, out, "install", "installing", error);
        return final_json(ok, command,
                          "\"target\":" + quote(device->linux_path) +
                              ",\"identity\":" + quote(device->physical_path) +
                              ",\"findings\":[]" +
                              (ok ? "" : ",\"error\":" + quote(error)));
    }

    return error_json(command, "Unknown command.");
}

std::string safely_handle_request(const ServiceConfig& config, const std::string& request, std::ostream& out) noexcept {
    std::string command = "request";
    try {
        const auto parsed_command = extract_json_string(request, "command");
        if (!parsed_command.empty()) command = parsed_command;
        return handle_request(config, request, out);
    } catch (const std::exception& exception) {
        std::cerr << "Request '" << command << "' failed with an exception: " << exception.what() << "\n";
        return error_json(command, "The Lazarus service could not complete the request: " + std::string(exception.what()));
    } catch (...) {
        std::cerr << "Request '" << command << "' failed with an unknown exception.\n";
        return error_json(command, "The Lazarus service could not complete the request because of an internal error.");
    }
}

ServiceConfig parse_args(int argc, char** argv) {
    ServiceConfig config;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config.bench_path = argv[++i];
        } else if (arg == "--socket" && i + 1 < argc) {
            config.socket_path = argv[++i];
        } else if (arg == "--security" && i + 1 < argc) {
            config.security_path = argv[++i];
            config.security_backup_path.clear();
        } else if (arg == "--security-backup" && i + 1 < argc) {
            config.security_backup_path = argv[++i];
        } else if (arg == "--network-config" && i + 1 < argc) {
            config.network_path = argv[++i];
        } else if (arg == "--network-helper" && i + 1 < argc) {
            config.network_helper = argv[++i];
        } else if (arg == "--stdio") {
            config.stdio = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: lazarus-service [--config PATH] [--socket PATH] [--security PATH] "
                         "[--security-backup PATH] [--network-config PATH] [--network-helper PATH] [--stdio]\n";
            std::exit(0);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::exit(2);
        }
    }
    return config;
}

void serve_stream(const ServiceConfig& config, std::istream& in, std::ostream& out) {
    std::string request;
    while (std::getline(in, request)) {
        if (request.empty()) {
            continue;
        }
        send_line(out, safely_handle_request(config, request, out));
    }
}

void serve_fd(const ServiceConfig& config, int fd) {
    FILE* input = fdopen(fd, "r");
    if (input == nullptr) {
        close(fd);
        return;
    }
    char* line = nullptr;
    std::size_t capacity = 0;
    while (getline(&line, &capacity, input) != -1) {
        std::string request(line);
        while (!request.empty() && (request.back() == '\n' || request.back() == '\r')) {
            request.pop_back();
        }
        if (request.empty()) {
            continue;
        }
        SocketStreamBuffer response_buffer(fd);
        std::ostream response(&response_buffer);
        send_line(response, safely_handle_request(config, request, response));
        if (!response.good()) break;
    }
    free(line);
    fclose(input);
}

int serve_socket(const ServiceConfig& config) {
    const std::filesystem::path socket_path(config.socket_path);
    std::error_code error;
    if (socket_path.has_parent_path()) {
        std::filesystem::create_directories(socket_path.parent_path(), error);
        if (error) {
            std::cerr << "Could not create socket directory: " << error.message() << "\n";
            return 1;
        }
    }
    ::unlink(config.socket_path.c_str());

    const int server_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server_fd < 0) {
        std::cerr << "socket failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (config.socket_path.size() >= sizeof(address.sun_path)) {
        std::cerr << "Socket path is too long.\n";
        close(server_fd);
        return 1;
    }
    std::strncpy(address.sun_path, config.socket_path.c_str(), sizeof(address.sun_path) - 1);
    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::cerr << "bind failed: " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }
    if (const group* lazarus_group = ::getgrnam("lazarus")) {
        ::chown(config.socket_path.c_str(), 0, lazarus_group->gr_gid);
    }
    ::chmod(config.socket_path.c_str(), 0660);
    if (::listen(server_fd, 16) != 0) {
        std::cerr << "listen failed: " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    std::cerr << "Arcology Lazarus service listening on " << config.socket_path << "\n";
    while (true) {
        const int client_fd = ::accept4(server_fd, nullptr, nullptr, SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept failed: " << std::strerror(errno) << "\n";
            continue;
        }
        std::thread([config, client_fd] {
            serve_fd(config, client_fd);
        }).detach();
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    const auto config = parse_args(argc, argv);
    if (config.stdio) {
        serve_stream(config, std::cin, std::cout);
        return 0;
    }
    return serve_socket(config);
}
