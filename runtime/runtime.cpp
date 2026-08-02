#include "arco/runtime.hpp"
#include "arco/gui.hpp"

#include "../core/lexer.hpp"
#include "../core/parser.hpp"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cerrno>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(ARCO_NETWORK_CURL)
#include <curl/curl.h>
#endif

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace arco {

namespace {

struct ConditionalFrame {
    bool parent_active = true;
    bool active = true;
    bool branch_taken = false;
};

std::string trim_copy(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string upper_copy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string lower_copy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

std::string function_key(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

bool gui_session_available() {
#ifdef _WIN32
    return true;
#elif defined(__APPLE__)
    return true;
#else
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    return (display && *display) || (wayland && *wayland);
#endif
}

std::unordered_map<int, int> g_tcp_clients;
int g_next_tcp_client_id = 1;

bool network_available() {
#if defined(ARCO_NETWORK_CURL)
    return true;
#else
    return false;
#endif
}

std::string hex_byte(unsigned char value) {
    constexpr char digits[] = "0123456789ABCDEF";
    std::string output;
    output.push_back(digits[value >> 4]);
    output.push_back(digits[value & 0x0f]);
    return output;
}

bool is_url_unreserved(unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

std::string url_encode(const std::string& text) {
    std::string output;
    for (unsigned char c : text) {
        if (is_url_unreserved(c)) {
            output.push_back(static_cast<char>(c));
        } else {
            output.push_back('%');
            output += hex_byte(c);
        }
    }
    return output;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& text) {
    std::string output;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int high = hex_value(text[i + 1]);
            const int low = hex_value(text[i + 2]);
            if (high >= 0 && low >= 0) {
                output.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        output.push_back(text[i] == '+' ? ' ' : text[i]);
    }
    return output;
}

std::string query_string(const Value& values) {
    if (!values.is_object()) {
        throw std::runtime_error("Network.QueryString expects an object");
    }
    std::string output;
    for (const auto& [name, value] : values.as_object()) {
        if (!output.empty()) {
            output.push_back('&');
        }
        output += url_encode(name);
        output.push_back('=');
        output += url_encode(value.to_string());
    }
    return output;
}

std::string sockaddr_address(const sockaddr* address) {
#ifdef _WIN32
    (void)address;
    return "";
#else
    char buffer[INET6_ADDRSTRLEN]{};
    if (address->sa_family == AF_INET) {
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
        if (inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer))) {
            return buffer;
        }
    } else if (address->sa_family == AF_INET6) {
        const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
        if (inet_ntop(AF_INET6, &ipv6->sin6_addr, buffer, sizeof(buffer))) {
            return buffer;
        }
    }
    return "";
#endif
}

Value network_resolve(const std::string& host) {
    Value::Array addresses;
#ifdef _WIN32
    return Value::Object{{"Ok", false}, {"Host", host}, {"Addresses", addresses}, {"Error", "DNS resolution is not implemented on Windows yet"}};
#else
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const int status = getaddrinfo(host.c_str(), nullptr, &hints, &results);
    if (status != 0) {
        return Value::Object{{"Ok", false}, {"Host", host}, {"Addresses", addresses}, {"Error", gai_strerror(status)}};
    }
    for (addrinfo* item = results; item; item = item->ai_next) {
        const std::string address = sockaddr_address(item->ai_addr);
        bool already_seen = false;
        for (const auto& value : addresses) {
            if (value.to_string() == address) {
                already_seen = true;
                break;
            }
        }
        if (!address.empty() && !already_seen) {
            addresses.emplace_back(address);
        }
    }
    freeaddrinfo(results);
    return Value::Object{{"Ok", !addresses.empty()}, {"Host", host}, {"Addresses", addresses}, {"Error", addresses.empty() ? "no addresses found" : ""}};
#endif
}

int tcp_socket_for_client(int id) {
    const auto found = g_tcp_clients.find(id);
    if (found == g_tcp_clients.end()) {
        throw std::runtime_error("unknown TCP client: " + std::to_string(id));
    }
    return found->second;
}

Value tcp_connect(const std::string& host, int port) {
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("Network.TcpConnect port must be between 1 and 65535");
    }
#ifdef _WIN32
    return Value::Object{{"Ok", false}, {"Client", 0.0}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Error", "TCP clients are not implemented on Windows yet"}};
#else
    addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* results = nullptr;
    const std::string port_text = std::to_string(port);
    const int lookup_status = getaddrinfo(host.c_str(), port_text.c_str(), &hints, &results);
    if (lookup_status != 0) {
        return Value::Object{{"Ok", false}, {"Client", 0.0}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Error", gai_strerror(lookup_status)}};
    }

    int connected_socket = -1;
    std::string connected_address;
    std::string error;
    for (addrinfo* item = results; item; item = item->ai_next) {
        const int fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (fd < 0) {
            error = std::strerror(errno);
            continue;
        }
        if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
            connected_socket = fd;
            connected_address = sockaddr_address(item->ai_addr);
            break;
        }
        error = std::strerror(errno);
        close(fd);
    }
    freeaddrinfo(results);

    if (connected_socket < 0) {
        return Value::Object{{"Ok", false}, {"Client", 0.0}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Error", error.empty() ? "could not connect" : error}};
    }

    const int id = g_next_tcp_client_id++;
    g_tcp_clients[id] = connected_socket;
    return Value::Object{{"Ok", true}, {"Client", static_cast<double>(id)}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Address", connected_address}, {"Error", ""}};
#endif
}

Value tcp_send(int client_id, const std::string& data) {
#ifdef _WIN32
    (void)client_id;
    (void)data;
    return Value::Object{{"Ok", false}, {"Bytes", 0.0}, {"Error", "TCP clients are not implemented on Windows yet"}};
#else
    const int fd = tcp_socket_for_client(client_id);
    std::size_t sent_total = 0;
    while (sent_total < data.size()) {
        const ssize_t sent = send(fd, data.data() + sent_total, data.size() - sent_total, 0);
        if (sent < 0) {
            return Value::Object{{"Ok", false}, {"Bytes", static_cast<double>(sent_total)}, {"Error", std::strerror(errno)}};
        }
        if (sent == 0) {
            break;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return Value::Object{{"Ok", sent_total == data.size()}, {"Bytes", static_cast<double>(sent_total)}, {"Error", sent_total == data.size() ? "" : "connection closed before all bytes were sent"}};
#endif
}

Value tcp_read(int client_id, int max_bytes) {
    if (max_bytes <= 0) {
        throw std::runtime_error("Network.TcpRead max bytes must be positive");
    }
#ifdef _WIN32
    (void)client_id;
    (void)max_bytes;
    return Value::Object{{"Ok", false}, {"Data", ""}, {"Bytes", 0.0}, {"Closed", true}, {"Error", "TCP clients are not implemented on Windows yet"}};
#else
    const int fd = tcp_socket_for_client(client_id);
    const int capped = std::min(max_bytes, 1024 * 1024);
    std::string data(static_cast<std::size_t>(capped), '\0');
    const ssize_t count = recv(fd, data.data(), data.size(), 0);
    if (count < 0) {
        return Value::Object{{"Ok", false}, {"Data", ""}, {"Bytes", 0.0}, {"Closed", false}, {"Error", std::strerror(errno)}};
    }
    data.resize(static_cast<std::size_t>(count));
    return Value::Object{{"Ok", true}, {"Data", data}, {"Bytes", static_cast<double>(count)}, {"Closed", count == 0}, {"Error", ""}};
#endif
}

Value tcp_close(int client_id) {
#ifdef _WIN32
    (void)client_id;
    return true;
#else
    const auto found = g_tcp_clients.find(client_id);
    if (found == g_tcp_clients.end()) {
        return false;
    }
    close(found->second);
    g_tcp_clients.erase(found);
    return true;
#endif
}

#if defined(ARCO_NETWORK_CURL)
std::size_t curl_write_string(char* data, std::size_t size, std::size_t count, void* user) {
    auto* text = static_cast<std::string*>(user);
    text->append(data, size * count);
    return size * count;
}

curl_slist* append_headers(curl_slist* list, const Value& headers) {
    if (headers.is_null()) {
        return list;
    }
    if (headers.is_object()) {
        for (const auto& [name, value] : headers.as_object()) {
            list = curl_slist_append(list, (name + ": " + value.to_string()).c_str());
        }
        return list;
    }
    if (headers.is_array()) {
        for (const auto& header : headers.as_array()) {
            list = curl_slist_append(list, header.to_string().c_str());
        }
        return list;
    }
    throw std::runtime_error("network headers must be an object or array");
}

Value network_response(bool ok, long status, const std::string& body, const std::string& headers,
                       const std::string& error, const std::string& effective_url) {
    return Value::Object{
        {"Ok", ok},
        {"Status", static_cast<double>(status)},
        {"Body", body},
        {"Headers", headers},
        {"Error", error},
        {"Url", effective_url}
    };
}

Value http_request(const std::string& method, const std::string& url, const std::string& body = "",
                   const Value& headers = Value()) {
    static const bool initialized = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)initialized;

    CURL* curl = curl_easy_init();
    if (!curl) {
        return network_response(false, 0, "", "", "could not initialize libcurl", url);
    }

    std::string response_body;
    std::string response_headers;
    char error_buffer[CURL_ERROR_SIZE]{};
    curl_slist* request_headers = nullptr;
    request_headers = append_headers(request_headers, headers);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ArcoBASIC/0.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_write_string);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    if (request_headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, request_headers);
    }
    if (method == "POST") {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    } else if (method != "GET") {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        if (!body.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }
    }

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    char* effective_url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);

    std::string error;
    if (code != CURLE_OK) {
        error = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
    }
    const bool ok = code == CURLE_OK && (status == 0 || (status >= 200 && status < 300));
    Value result = network_response(ok, status, response_body, response_headers, error, effective_url ? effective_url : url);

    if (request_headers) {
        curl_slist_free_all(request_headers);
    }
    curl_easy_cleanup(curl);
    return result;
}
#else
Value http_request(const std::string&, const std::string& url, const std::string& = "", const Value& = Value()) {
    return Value::Object{
        {"Ok", false},
        {"Status", 0.0},
        {"Body", ""},
        {"Headers", ""},
        {"Error", "networking was not enabled in this build"},
        {"Url", url}
    };
}
#endif

Value network_download(const std::string& url, const std::string& path, const Value& headers = Value()) {
    Value result = http_request("GET", url, "", headers);
    if (result.get_property("Ok").truthy()) {
        std::ofstream output(path, std::ios::binary);
        if (!output) {
            throw std::runtime_error("Network.Download could not write file: " + path);
        }
        output << result.get_property("Body").to_string();
        result.set_property("Path", path);
    }
    return result;
}

std::string http_reason(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 500:
            return "Internal Server Error";
        default:
            return "OK";
    }
}

std::string mime_type_for_path(const std::filesystem::path& path) {
    const std::string ext = lower_copy(path.extension().string());
    if (ext == ".html" || ext == ".htm") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "application/javascript; charset=utf-8";
    if (ext == ".json") return "application/json; charset=utf-8";
    if (ext == ".txt") return "text/plain; charset=utf-8";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

bool send_all(int fd, const std::string& data) {
#ifdef _WIN32
    (void)fd;
    (void)data;
    return false;
#else
    std::size_t sent_total = 0;
    while (sent_total < data.size()) {
        const ssize_t sent = send(fd, data.data() + sent_total, data.size() - sent_total, 0);
        if (sent <= 0) {
            return false;
        }
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
#endif
}

std::string read_http_request(int fd) {
#ifdef _WIN32
    (void)fd;
    return "";
#else
    std::string request;
    char buffer[1024]{};
    while (request.size() < 64 * 1024) {
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count <= 0) {
            break;
        }
        request.append(buffer, static_cast<std::size_t>(count));
        if (request.find("\r\n\r\n") != std::string::npos || request.find("\n\n") != std::string::npos) {
            break;
        }
    }
    return request;
#endif
}

std::string http_response(int status, const std::string& content_type, const std::string& body, bool include_body = true) {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << " " << http_reason(status) << "\r\n";
    out << "Content-Type: " << content_type << "\r\n";
    out << "Content-Length: " << body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "X-Content-Type-Options: nosniff\r\n";
    out << "\r\n";
    if (include_body) {
        out << body;
    }
    return out.str();
}

bool path_is_inside(const std::filesystem::path& root, const std::filesystem::path& path) {
    const auto root_text = root.lexically_normal().string();
    const auto path_text = path.lexically_normal().string();
    return path_text == root_text || path_text.rfind(root_text + "/", 0) == 0;
}

std::string static_response_body(const std::filesystem::path& root, const std::string& request_target,
                                 int& status, std::string& content_type) {
    std::string target = request_target.empty() ? "/" : request_target;
    const auto query = target.find('?');
    if (query != std::string::npos) {
        target = target.substr(0, query);
    }
    target = url_decode(target);
    if (target.empty() || target.front() != '/') {
        status = 400;
        content_type = "text/plain; charset=utf-8";
        return "Bad Request\n";
    }
    if (target == "/") {
        target = "/index.html";
    }

    std::filesystem::path relative = std::filesystem::path(target.substr(1)).lexically_normal();
    if (relative.empty() || relative.string().rfind("..", 0) == 0 || relative.is_absolute()) {
        status = 403;
        content_type = "text/plain; charset=utf-8";
        return "Forbidden\n";
    }

    std::filesystem::path file_path = (root / relative).lexically_normal();
    if (std::filesystem::is_directory(file_path)) {
        file_path = file_path / "index.html";
    }
    if (!path_is_inside(root, file_path)) {
        status = 403;
        content_type = "text/plain; charset=utf-8";
        return "Forbidden\n";
    }
    if (!std::filesystem::exists(file_path) || std::filesystem::is_directory(file_path)) {
        status = 404;
        content_type = "text/plain; charset=utf-8";
        return "Not Found\n";
    }

    status = 200;
    content_type = mime_type_for_path(file_path);
    std::ifstream input(file_path, std::ios::binary);
    if (!input) {
        status = 404;
        content_type = "text/plain; charset=utf-8";
        return "Not Found\n";
    }
    std::ostringstream body;
    body << input.rdbuf();
    return body.str();
}

Value serve_static_site(const std::string& root_path, int port, const std::string& host, int max_requests) {
    if (port <= 0 || port > 65535) {
        throw std::runtime_error("Web.ServeStatic port must be between 1 and 65535");
    }
    if (max_requests < 0) {
        throw std::runtime_error("Web.ServeStatic max requests cannot be negative");
    }
    const std::filesystem::path root = std::filesystem::absolute(root_path).lexically_normal();
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        throw std::runtime_error("Web.ServeStatic root directory does not exist: " + root_path);
    }
#ifdef _WIN32
    (void)host;
    return Value::Object{{"Ok", false}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Root", root.string()}, {"Requests", 0.0}, {"Error", "Web.ServeStatic is not implemented on Windows yet"}};
#else
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    addrinfo* results = nullptr;
    const std::string port_text = std::to_string(port);
    const int lookup = getaddrinfo(host.empty() ? nullptr : host.c_str(), port_text.c_str(), &hints, &results);
    if (lookup != 0) {
        return Value::Object{{"Ok", false}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Root", root.string()}, {"Requests", 0.0}, {"Error", gai_strerror(lookup)}};
    }

    int server_fd = -1;
    std::string error;
    for (addrinfo* item = results; item; item = item->ai_next) {
        server_fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (server_fd < 0) {
            error = std::strerror(errno);
            continue;
        }
        int reuse = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        if (bind(server_fd, item->ai_addr, item->ai_addrlen) == 0 && listen(server_fd, 16) == 0) {
            break;
        }
        error = std::strerror(errno);
        close(server_fd);
        server_fd = -1;
    }
    freeaddrinfo(results);
    if (server_fd < 0) {
        return Value::Object{{"Ok", false}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Root", root.string()}, {"Requests", 0.0}, {"Error", error.empty() ? "could not listen" : error}};
    }

    int served = 0;
    while (max_requests == 0 || served < max_requests) {
        const int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd < 0) {
            error = std::strerror(errno);
            break;
        }
        served++;
        try {
            const std::string request = read_http_request(client_fd);
            std::istringstream input(request);
            std::string method;
            std::string target;
            std::string version;
            input >> method >> target >> version;
            if (method.empty() || target.empty()) {
                (void)send_all(client_fd, http_response(400, "text/plain; charset=utf-8", "Bad Request\n"));
            } else if (method != "GET" && method != "HEAD") {
                (void)send_all(client_fd, http_response(405, "text/plain; charset=utf-8", "Method Not Allowed\n"));
            } else {
                int status = 200;
                std::string content_type;
                const std::string body = static_response_body(root, target, status, content_type);
                (void)send_all(client_fd, http_response(status, content_type, body, method != "HEAD"));
            }
        } catch (const std::exception& error_response) {
            (void)send_all(client_fd, http_response(500, "text/plain; charset=utf-8", std::string("Internal Server Error\n") + error_response.what() + "\n"));
        }
        close(client_fd);
    }
    close(server_fd);
    return Value::Object{{"Ok", error.empty()}, {"Host", host}, {"Port", static_cast<double>(port)}, {"Root", root.string()}, {"Requests", static_cast<double>(served)}, {"Error", error}};
#endif
}

std::string object_runtime_class(const Value& value) {
    if (!value.is_object()) {
        return "";
    }
    const auto& object = value.as_object();
    const auto instance = object.find("__class");
    if (instance != object.end()) {
        return instance->second.to_string();
    }
    const auto class_object = object.find("__name");
    if (class_object != object.end()) {
        return class_object->second.to_string();
    }
    return "";
}

bool object_has_string_property(const Value& value, const std::string& name) {
    if (!value.is_object()) {
        return false;
    }
    const auto& object = value.as_object();
    const auto found = object.find(name);
    return found != object.end() && found->second.is_string();
}

std::vector<std::string> split_words(std::string text) {
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream in(text);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) {
        words.push_back(word);
    }
    return words;
}

std::string unquote(const std::string& text) {
    const std::string value = trim_copy(text);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

struct ImportDirective {
    std::string path;
    std::string alias;
};

ImportDirective parse_import_directive(const std::string& args) {
    const std::string text = trim_copy(args);
    if (text.empty()) {
        throw std::runtime_error("#IMPORT expects a module name");
    }

    std::string path;
    std::size_t position = 0;
    if (text.front() == '"') {
        std::size_t end = 1;
        while (end < text.size()) {
            if (text[end] == '"' && text[end - 1] != '\\') {
                break;
            }
            end++;
        }
        if (end >= text.size()) {
            throw std::runtime_error("#IMPORT has an unterminated module name");
        }
        path = text.substr(1, end - 1);
        position = end + 1;
    } else {
        const auto end = text.find_first_of(" \t");
        path = text.substr(0, end);
        position = end == std::string::npos ? text.size() : end;
    }

    const std::string rest = trim_copy(text.substr(position));
    if (rest.empty()) {
        return {path, ""};
    }
    const auto split = rest.find_first_of(" \t");
    const std::string keyword = upper_copy(rest.substr(0, split == std::string::npos ? std::string::npos : split));
    if (keyword != "AS") {
        throw std::runtime_error("#IMPORT expected AS before alias");
    }
    const std::string alias = split == std::string::npos ? "" : trim_copy(rest.substr(split + 1));
    if (alias.empty()) {
        throw std::runtime_error("#IMPORT AS expects an alias");
    }
    for (char c : alias) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.') {
            throw std::runtime_error("#IMPORT alias must be an identifier");
        }
    }
    return {path, alias};
}

std::string bare_param_name(std::string param) {
    param = trim_copy(std::move(param));
    const auto equals = param.find('=');
    if (equals != std::string::npos) {
        param = trim_copy(param.substr(0, equals));
    }
    const auto as_pos = upper_copy(param).find(" AS ");
    if (as_pos != std::string::npos) {
        param = trim_copy(param.substr(0, as_pos));
    }
    return param;
}

std::vector<std::string> split_params(const std::string& params) {
    std::vector<std::string> values;
    std::string current;
    int depth = 0;
    bool in_string = false;
    for (std::size_t i = 0; i < params.size(); ++i) {
        const char c = params[i];
        if (c == '"' && (i == 0 || params[i - 1] != '\\')) {
            in_string = !in_string;
        }
        if (!in_string) {
            if (c == '(' || c == '[' || c == '{') {
                depth++;
            } else if (c == ')' || c == ']' || c == '}') {
                depth--;
            } else if (c == ',' && depth == 0) {
                values.push_back(trim_copy(current));
                current.clear();
                continue;
            }
        }
        current.push_back(c);
    }
    if (!trim_copy(current).empty()) {
        values.push_back(trim_copy(current));
    }
    return values;
}

std::string imported_function_leaf(const std::string& name) {
    const auto dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

std::string alias_import_wrappers(const std::string& source, const std::string& alias) {
    if (alias.empty()) {
        return "";
    }

    std::ostringstream wrappers;
    std::istringstream input(source);
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.size() < 8 || upper_copy(trimmed.substr(0, 8)) != "FUNCTION") {
            continue;
        }

        std::string header = trim_copy(trimmed.substr(8));
        const auto open = header.find('(');
        const auto close = header.find(')', open == std::string::npos ? 0 : open + 1);
        if (open == std::string::npos || close == std::string::npos) {
            continue;
        }

        const std::string original_name = trim_copy(header.substr(0, open));
        if (original_name.empty()) {
            continue;
        }
        const std::string wrapper_name = alias + "." + imported_function_leaf(original_name);
        if (function_key(wrapper_name) == function_key(original_name)) {
            continue;
        }

        const std::string param_text = header.substr(open + 1, close - open - 1);
        const auto params = split_params(param_text);
        std::vector<std::string> arg_names;
        arg_names.reserve(params.size());
        for (const auto& param : params) {
            const std::string name = bare_param_name(param);
            if (!name.empty()) {
                arg_names.push_back(name);
            }
        }

        wrappers << "FUNCTION " << wrapper_name << "(" << param_text << ")\n";
        wrappers << "RETURN " << original_name << "(";
        for (std::size_t i = 0; i < arg_names.size(); ++i) {
            if (i != 0) {
                wrappers << ", ";
            }
            wrappers << arg_names[i];
        }
        wrappers << ")\n";
        wrappers << "END FUNCTION\n";
    }
    return wrappers.str();
}

std::optional<std::filesystem::path> executable_directory() {
#ifndef _WIN32
    std::array<char, 4096> path{};
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length <= 0) {
        return std::nullopt;
    }
    path[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(path.data()).parent_path();
#else
    return std::nullopt;
#endif
}

std::vector<std::filesystem::path> import_candidates(const std::string& import_name) {
    const std::filesystem::path requested(import_name);
    std::vector<std::filesystem::path> bases = {
        std::filesystem::current_path(),
        std::filesystem::current_path() / "stdlib",
        std::filesystem::current_path() / "../stdlib",
        std::filesystem::path("/usr/local/share/arcobasic/stdlib"),
        std::filesystem::path("/usr/share/arcobasic/stdlib")
    };
    if (const auto exe_dir = executable_directory()) {
        bases.push_back(*exe_dir / "../share/arcobasic/stdlib");
    }
    if (const char* stdlib_env = std::getenv("ARCOBASIC_STDLIB")) {
        if (*stdlib_env) {
            bases.emplace_back(stdlib_env);
        }
    }

    std::vector<std::filesystem::path> candidates;
    auto add_candidate = [&candidates](const std::filesystem::path& path) {
        candidates.push_back(path);
        if (!path.has_extension()) {
            candidates.push_back(path.string() + ".abas");
            candidates.push_back(path.string() + ".arc");
            candidates.push_back(path.string() + ".bas");
        }
    };

    add_candidate(requested);
    if (requested.is_relative()) {
        for (const auto& base : bases) {
            add_candidate(base / requested);
        }
    }
    return candidates;
}

std::filesystem::path resolve_import_path(const std::string& import_name) {
    for (const auto& candidate : import_candidates(import_name)) {
        if (std::filesystem::exists(candidate) && !std::filesystem::is_directory(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("could not include " + import_name);
}

Value parse_define_value(const std::string& text) {
    const std::string value = trim_copy(text);
    if (value.empty()) {
        return true;
    }
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        return unquote(value);
    }
    const std::string upper = upper_copy(value);
    if (upper == "TRUE") {
        return true;
    }
    if (upper == "FALSE") {
        return false;
    }
    if (value.rfind("0b", 0) == 0 || value.rfind("0B", 0) == 0) {
        return static_cast<double>(std::stoll(value.substr(2), nullptr, 2));
    }
    if (!value.empty() && value.front() == '%') {
        return static_cast<double>(std::stoll(value.substr(1), nullptr, 2));
    }
    if (value.rfind("0x", 0) == 0 || value.rfind("0X", 0) == 0) {
        return static_cast<double>(std::stoll(value.substr(2), nullptr, 16));
    }
    if (value.rfind("&H", 0) == 0 || value.rfind("&h", 0) == 0) {
        return static_cast<double>(std::stoll(value.substr(2), nullptr, 16));
    }
    return std::stod(value);
}

bool active_conditions(const std::vector<ConditionalFrame>& frames) {
    for (const auto& frame : frames) {
        if (!frame.active) {
            return false;
        }
    }
    return true;
}

bool symbol_enabled(const std::set<std::string>& defines, const std::string& symbol) {
    return defines.find(upper_copy(symbol)) != defines.end();
}

std::string read_text_file(const std::string& path) {
    const auto resolved = resolve_import_path(path);
    std::ifstream input(resolved);
    if (!input) {
        throw std::runtime_error("could not include " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string read_plain_file(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not read " + path);
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_plain_file(const std::string& path, const std::string& text, std::ios::openmode mode) {
    std::ofstream output(path, mode);
    if (!output) {
        throw std::runtime_error("could not write " + path);
    }
    output << text;
}

Value::Array bytes_from_string(const std::string& text) {
    Value::Array bytes;
    bytes.reserve(text.size());
    for (unsigned char byte : text) {
        bytes.emplace_back(static_cast<double>(byte));
    }
    return bytes;
}

std::string string_from_bytes(const Value& value) {
    std::string text;
    const auto& bytes = value.as_array();
    text.reserve(bytes.size());
    for (const auto& byte : bytes) {
        int numeric = static_cast<int>(byte.as_number());
        numeric = std::clamp(numeric, 0, 255);
        text.push_back(static_cast<char>(numeric));
    }
    return text;
}

std::vector<std::string> source_lines(const std::string& code) {
    std::vector<std::string> lines;
    std::istringstream input(code);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    if (!code.empty() && code.back() == '\n') {
        lines.emplace_back();
    }
    return lines;
}

bool parse_line_column_error(const std::string& error, int& line, int& column) {
    const std::string prefix = "line ";
    if (error.rfind(prefix, 0) != 0) {
        return false;
    }
    const auto comma = error.find(", column ", prefix.size());
    if (comma == std::string::npos) {
        return false;
    }
    const auto colon = error.find(':', comma + 9);
    if (colon == std::string::npos) {
        return false;
    }
    try {
        line = std::stoi(error.substr(prefix.size(), comma - prefix.size()));
        column = std::stoi(error.substr(comma + 9, colon - (comma + 9)));
    } catch (const std::exception&) {
        return false;
    }
    return line > 0 && column > 0;
}

bool parse_at_line_error(const std::string& error, int& line) {
    const std::string marker = " at line ";
    const auto position = error.rfind(marker);
    if (position == std::string::npos) {
        return false;
    }
    try {
        line = std::stoi(error.substr(position + marker.size()));
    } catch (const std::exception&) {
        return false;
    }
    return line > 0;
}

std::string format_source_diagnostic(const std::string& error, const std::string& code) {
    int line = 0;
    int column = 0;
    const auto lines = source_lines(code);
    if (parse_line_column_error(error, line, column)) {
        if (static_cast<std::size_t>(line) > lines.size()) {
            return error;
        }

        std::ostringstream output;
        output << error << '\n';
        output << lines[static_cast<std::size_t>(line - 1)] << '\n';
        for (int i = 1; i < column; ++i) {
            output << ' ';
        }
        output << '^';
        return output.str();
    }

    if (parse_at_line_error(error, line)) {
        if (static_cast<std::size_t>(line) > lines.size()) {
            return error;
        }
        std::ostringstream output;
        output << error << '\n';
        output << lines[static_cast<std::size_t>(line - 1)];
        return output.str();
    }
    return error;
}

std::string format_runtime_diagnostic(const std::string& error, const std::string& code, int line, int column) {
    const auto lines = source_lines(code);
    if (line <= 0 || column <= 0 || static_cast<std::size_t>(line) > lines.size()) {
        return error;
    }
    std::ostringstream output;
    output << error << '\n';
    output << "runtime error at line " << line << ", column " << column << '\n';
    output << lines[static_cast<std::size_t>(line - 1)] << '\n';
    for (int i = 1; i < column; ++i) {
        output << ' ';
    }
    output << '^';
    return output.str();
}

long long value_to_int(const Value& value) {
    return static_cast<long long>(value.as_number());
}

Value bit_binary(const std::vector<Value>& args, const std::string& name, char op) {
    if (args.size() != 2) {
        throw std::runtime_error(name + " expects 2 arguments");
    }
    const long long left = value_to_int(args[0]);
    const long long right = value_to_int(args[1]);
    switch (op) {
        case '&':
            return static_cast<double>(left & right);
        case '|':
            return static_cast<double>(left | right);
        case '^':
            return static_cast<double>(left ^ right);
        case '<':
            return static_cast<double>(left << right);
        case '>':
            return static_cast<double>(left >> right);
        default:
            throw std::runtime_error("unknown bit operation");
    }
}

void expect_arg_count(const std::vector<Value>& args, const std::string& name, std::size_t min, std::size_t max) {
    if (args.size() < min || args.size() > max) {
        throw std::runtime_error(name + " expects " + std::to_string(min == max ? min : min) + (min == max ? "" : " or " + std::to_string(max)) + " arguments");
    }
}

Runtime::HostFunction unary_math_function(const std::string& name, double (*fn)(double)) {
    return [name, fn](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, name, 1, 1);
        return fn(args[0].as_number());
    };
}

Runtime::HostFunction binary_math_function(const std::string& name, double (*fn)(double, double)) {
    return [name, fn](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, name, 2, 2);
        return fn(args[0].as_number(), args[1].as_number());
    };
}

Value math_min_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Math.Min", 1, 64);
    double result = args[0].as_number();
    for (std::size_t i = 1; i < args.size(); ++i) {
        result = std::min(result, args[i].as_number());
    }
    return result;
}

Value math_max_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Math.Max", 1, 64);
    double result = args[0].as_number();
    for (std::size_t i = 1; i < args.size(); ++i) {
        result = std::max(result, args[i].as_number());
    }
    return result;
}

Value math_clamp_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Math.Clamp", 3, 3);
    const double lo = std::min(args[1].as_number(), args[2].as_number());
    const double hi = std::max(args[1].as_number(), args[2].as_number());
    return std::clamp(args[0].as_number(), lo, hi);
}

Value math_lerp_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Math.Lerp", 3, 3);
    const double from = args[0].as_number();
    const double to = args[1].as_number();
    const double amount = args[2].as_number();
    return from + (to - from) * amount;
}

Value math_constants_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Math.Constants", 0, 0);
    constexpr double pi = 3.14159265358979323846264338327950288;
    constexpr double e = 2.71828182845904523536028747135266250;
    return Value::Object{{"PI", pi}, {"Pi", pi}, {"TAU", pi * 2.0}, {"Tau", pi * 2.0}, {"E", e}, {"DegToRad", pi / 180.0}, {"RadToDeg", 180.0 / pi}};
}

std::string bits_to_string(unsigned long long value, int width) {
    std::string bits;
    if (value == 0) {
        bits = "0";
    } else {
        while (value != 0) {
            bits.push_back((value & 1ULL) ? '1' : '0');
            value >>= 1U;
        }
        std::reverse(bits.begin(), bits.end());
    }
    if (width > static_cast<int>(bits.size())) {
        bits.insert(bits.begin(), static_cast<std::size_t>(width - bits.size()), '0');
    }
    return bits;
}

Value shift_function(const std::vector<Value>& args) {
    expect_arg_count(args, "SHIFT", 2, 2);
    const long long value = value_to_int(args[0]);
    const long long amount = value_to_int(args[1]);
    return static_cast<double>(amount >= 0 ? (value << amount) : (value >> -amount));
}

Value bit_test_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BIT", 2, 2);
    return (value_to_int(args[0]) & (1LL << value_to_int(args[1]))) != 0;
}

Value bit_set_function(const std::vector<Value>& args) {
    expect_arg_count(args, "SETBIT", 2, 2);
    return static_cast<double>(value_to_int(args[0]) | (1LL << value_to_int(args[1])));
}

Value bit_clear_function(const std::vector<Value>& args) {
    expect_arg_count(args, "CLEARBIT", 2, 2);
    return static_cast<double>(value_to_int(args[0]) & ~(1LL << value_to_int(args[1])));
}

Value bit_toggle_function(const std::vector<Value>& args) {
    expect_arg_count(args, "TOGGLEBIT", 2, 2);
    return static_cast<double>(value_to_int(args[0]) ^ (1LL << value_to_int(args[1])));
}

Value bits_text_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BitsToString", 1, 2);
    const int width = args.size() == 2 ? static_cast<int>(value_to_int(args[1])) : 0;
    return bits_to_string(static_cast<unsigned long long>(value_to_int(args[0])), width);
}

Value string_to_bits_function(const std::vector<Value>& args) {
    expect_arg_count(args, "StringToBits", 1, 1);
    long long value = 0;
    for (char c : args[0].to_string()) {
        if (c != '0' && c != '1') {
            throw std::runtime_error("StringToBits expects a binary string");
        }
        value = (value << 1) | (c == '1' ? 1 : 0);
    }
    return static_cast<double>(value);
}

Value bitcount_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BITCOUNT", 1, 1);
    unsigned long long value = static_cast<unsigned long long>(value_to_int(args[0]));
    int count = 0;
    while (value != 0) {
        count += static_cast<int>(value & 1ULL);
        value >>= 1U;
    }
    return static_cast<double>(count);
}

Value rotate_function(const std::vector<Value>& args, bool left) {
    expect_arg_count(args, left ? "ROTATELEFT" : "ROTATERIGHT", 2, 2);
    const unsigned long long value = static_cast<unsigned long long>(value_to_int(args[0]));
    const unsigned int amount = static_cast<unsigned int>(value_to_int(args[1])) % 64U;
    if (amount == 0) {
        return static_cast<double>(value);
    }
    const unsigned long long rotated = left ? ((value << amount) | (value >> (64U - amount))) : ((value >> amount) | (value << (64U - amount)));
    return static_cast<double>(rotated);
}

Value bits_table_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BitsTable", 1, 2);
    const long long value = value_to_int(args[0]);
    int width = args.size() == 2 ? static_cast<int>(value_to_int(args[1])) : 8;
    if (width < 1) {
        width = 1;
    }
    std::ostringstream out;
    out << "Bit  Value  Set\n\n";
    for (int bit = width - 1; bit >= 0; --bit) {
        const long long bit_value = 1LL << bit;
        out << bit << "    " << bit_value << "    " << ((value & bit_value) ? "Yes" : "No");
        if (bit != 0) {
            out << '\n';
        }
    }
    return out.str();
}

Value hex_to_string_function(const std::vector<Value>& args) {
    expect_arg_count(args, "HexToString", 1, 1);
    std::ostringstream out;
    out << std::uppercase << std::hex << value_to_int(args[0]);
    return out.str();
}

Value string_to_hex_function(const std::vector<Value>& args) {
    expect_arg_count(args, "StringToHex", 1, 1);
    return static_cast<double>(std::stoll(args[0].to_string(), nullptr, 16));
}

Value bytes_to_hex_function(const std::vector<Value>& args) {
    expect_arg_count(args, "BytesToHex", 1, 1);
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    if (args[0].is_array()) {
        for (const auto& byte : args[0].as_array()) {
            out << std::setw(2) << (value_to_int(byte) & 0xFF);
        }
    } else {
        for (unsigned char c : args[0].to_string()) {
            out << std::setw(2) << static_cast<int>(c);
        }
    }
    return out.str();
}

Value hex_to_bytes_function(const std::vector<Value>& args) {
    expect_arg_count(args, "HexToBytes", 1, 1);
    std::string text = args[0].to_string();
    if (text.size() % 2 != 0) {
        text.insert(text.begin(), '0');
    }
    Value::Array bytes;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        bytes.emplace_back(static_cast<double>(std::stoll(text.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

Value array_push_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Push", 2, 2);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    array.push_back(args[1]);
    return static_cast<double>(array.size());
}

Value array_new_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.New", 0, 2);
    Value::Array array;
    if (!args.empty()) {
        const int size = static_cast<int>(args[0].as_number());
        if (size < 0) {
            throw std::runtime_error("Array.New size cannot be negative");
        }
        const Value fill = args.size() == 2 ? args[1] : Value{};
        array.resize(static_cast<std::size_t>(size), fill);
    }
    return array;
}

Value array_length_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Length", 1, 1);
    return static_cast<double>(args[0].as_array().size());
}

Value array_empty_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Empty", 1, 1);
    return args[0].as_array().empty();
}

Value array_clear_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Clear", 1, 1);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    array.clear();
    return static_cast<double>(array.size());
}

Value array_pop_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Pop", 1, 1);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    if (array.empty()) {
        return {};
    }
    Value value = array.back();
    array.pop_back();
    return value;
}

Value array_shift_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Shift", 1, 1);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    if (array.empty()) {
        return {};
    }
    Value value = array.front();
    array.erase(array.begin());
    return value;
}

Value array_unshift_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Unshift", 2, 2);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    array.insert(array.begin(), args[1]);
    return static_cast<double>(array.size());
}

Value array_insert_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Insert", 3, 3);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    const int index = static_cast<int>(args[1].as_number());
    if (index < 0 || static_cast<std::size_t>(index) > array.size()) {
        throw std::runtime_error("Array.Insert index out of range");
    }
    array.insert(array.begin() + index, args[2]);
    return static_cast<double>(array.size());
}

Value array_remove_at_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.RemoveAt", 2, 2);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    const int index = static_cast<int>(args[1].as_number());
    if (index < 0 || static_cast<std::size_t>(index) >= array.size()) {
        throw std::runtime_error("Array.RemoveAt index out of range");
    }
    Value removed = array[static_cast<std::size_t>(index)];
    array.erase(array.begin() + index);
    return removed;
}

Value array_remove_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Remove", 2, 2);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    const auto found = std::find_if(array.begin(), array.end(), [&](const Value& value) {
        return values_equal(value, args[1]);
    });
    if (found == array.end()) {
        return false;
    }
    array.erase(found);
    return true;
}

Value array_resize_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Resize", 2, 3);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    const int size = static_cast<int>(args[1].as_number());
    if (size < 0) {
        throw std::runtime_error("Array.Resize size cannot be negative");
    }
    const Value fill = args.size() == 3 ? args[2] : Value{};
    array.resize(static_cast<std::size_t>(size), fill);
    return static_cast<double>(array.size());
}

Value array_extend_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Extend", 2, 2);
    Value array_value = args[0];
    auto& array = array_value.as_array();
    const auto& other = args[1].as_array();
    array.insert(array.end(), other.begin(), other.end());
    return static_cast<double>(array.size());
}

Value array_first_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.First", 1, 1);
    const auto& array = args[0].as_array();
    if (array.empty()) {
        return {};
    }
    return array.front();
}

Value array_last_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Last", 1, 1);
    const auto& array = args[0].as_array();
    if (array.empty()) {
        return {};
    }
    return array.back();
}

Value array_find_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Find", 2, 2);
    const auto& array = args[0].as_array();
    for (std::size_t i = 0; i < array.size(); ++i) {
        if (values_equal(array[i], args[1])) {
            return static_cast<double>(i);
        }
    }
    return -1.0;
}

Value array_reverse_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Reverse", 1, 1);
    Value::Array result = args[0].as_array();
    std::reverse(result.begin(), result.end());
    return result;
}

Value array_join_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Join", 2, 2);
    const auto& array = args[0].as_array();
    const std::string separator = args[1].to_string();
    std::ostringstream output;
    for (std::size_t i = 0; i < array.size(); ++i) {
        if (i != 0) {
            output << separator;
        }
        output << array[i].to_string();
    }
    return output.str();
}

Value array_contains_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Contains", 2, 2);
    const auto& array = args[0].as_array();
    return std::any_of(array.begin(), array.end(), [&](const Value& value) {
        return values_equal(value, args[1]);
    });
}

Value array_sort_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Array.Sort", 1, 1);
    Value::Array result = args[0].as_array();
    std::sort(result.begin(), result.end(), [](const Value& left, const Value& right) {
        if (left.is_number() && right.is_number()) {
            return left.as_number() < right.as_number();
        }
        return left.to_string() < right.to_string();
    });
    return result;
}

Value object_keys_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Keys", 1, 1);
    Value::Array keys;
    for (const auto& [key, value] : args[0].as_object()) {
        (void)value;
        keys.emplace_back(key);
    }
    return keys;
}

Value object_has_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Has", 2, 2);
    const auto& object = args[0].as_object();
    return object.find(args[1].to_string()) != object.end();
}

Value object_get_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Get", 2, 3);
    const auto& object = args[0].as_object();
    const auto found = object.find(args[1].to_string());
    if (found != object.end()) {
        return found->second;
    }
    return args.size() == 3 ? args[2] : Value();
}

Value object_set_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Object.Set", 3, 3);
    Value::Object object = args[0].as_object();
    object[args[1].to_string()] = args[2];
    return object;
}

Value clone_value(const Value& value) {
    if (value.is_array()) {
        Value::Array copy;
        for (const auto& item : value.as_array()) {
            copy.push_back(clone_value(item));
        }
        return copy;
    }
    if (value.is_object()) {
        Value::Object copy;
        for (const auto& [key, item] : value.as_object()) {
            copy[key] = clone_value(item);
        }
        return copy;
    }
    return value;
}

std::string document_escape(const std::string& text) {
    std::ostringstream output;
    for (char c : text) {
        switch (c) {
            case '\\':
                output << "\\\\";
                break;
            case '\n':
                output << "\\n";
                break;
            case '\r':
                output << "\\r";
                break;
            case '\t':
                output << "\\t";
                break;
            default:
                output << c;
                break;
        }
    }
    return output.str();
}

std::string document_unescape(const std::string& text) {
    std::string output;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            const char next = text[++i];
            if (next == 'n') {
                output.push_back('\n');
            } else if (next == 'r') {
                output.push_back('\r');
            } else if (next == 't') {
                output.push_back('\t');
            } else {
                output.push_back(next);
            }
        } else {
            output.push_back(text[i]);
        }
    }
    return output;
}

Value document_new_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.New", 0, 1);
    const std::string text = args.empty() ? "" : args[0].to_string();
    return Value::Object{{"Text", text}, {"Runs", Value::Array{}}, {"Path", ""}, {"Dirty", false}, {"Undo", Value::Array{}}, {"Redo", Value::Array{}}};
}

int document_run_start(const Value& run_value) {
    const auto& run = run_value.as_object();
    const auto found = run.find("Start");
    if (found == run.end()) {
        return 0;
    }
    return static_cast<int>(found->second.as_number());
}

int document_run_length(const Value& run_value) {
    const auto& run = run_value.as_object();
    const auto found = run.find("Length");
    if (found == run.end()) {
        return 0;
    }
    return static_cast<int>(found->second.as_number());
}

Value::Object document_format_from_run(const Value& run_value) {
    Value::Object format;
    for (const auto& [key, item] : run_value.as_object()) {
        if (key != "Start" && key != "Length") {
            format[key] = clone_value(item);
        }
    }
    return format;
}

Value document_make_run(int start, int length, const Value::Object& format) {
    Value::Object run;
    run["Start"] = start;
    run["Length"] = length;
    for (const auto& [key, item] : format) {
        run[key] = clone_value(item);
    }
    return run;
}

bool document_formats_equal(const Value& left_value, const Value& right_value) {
    const auto left = document_format_from_run(left_value);
    const auto right = document_format_from_run(right_value);
    if (left.size() != right.size()) {
        return false;
    }
    for (const auto& [key, item] : left) {
        const auto found = right.find(key);
        if (found == right.end() || !values_equal(item, found->second)) {
            return false;
        }
    }
    return true;
}

void document_normalize_runs(Value::Object& object, int text_length) {
    text_length = std::max(0, text_length);
    Value::Array runs;
    const auto found = object.find("Runs");
    if (found != object.end() && found->second.is_array()) {
        for (const auto& run_value : found->second.as_array()) {
            if (!run_value.is_object()) {
                continue;
            }
            int start = std::clamp(document_run_start(run_value), 0, text_length);
            int length = std::max(0, document_run_length(run_value));
            if (start + length > text_length) {
                length = text_length - start;
            }
            if (length <= 0) {
                continue;
            }
            runs.push_back(document_make_run(start, length, document_format_from_run(run_value)));
        }
    }
    std::sort(runs.begin(), runs.end(), [](const Value& left, const Value& right) {
        const int left_start = document_run_start(left);
        const int right_start = document_run_start(right);
        if (left_start != right_start) {
            return left_start < right_start;
        }
        return document_run_length(left) < document_run_length(right);
    });

    Value::Array merged;
    int occupied_until = 0;
    for (const auto& run_value : runs) {
        int start = document_run_start(run_value);
        int length = document_run_length(run_value);
        if (start < occupied_until) {
            const int trim = occupied_until - start;
            start += trim;
            length -= trim;
        }
        if (length <= 0) {
            continue;
        }
        Value adjusted = document_make_run(start, length, document_format_from_run(run_value));
        if (!merged.empty()) {
            Value& previous = merged.back();
            const int previous_start = document_run_start(previous);
            const int previous_length = document_run_length(previous);
            if (previous_start + previous_length == start && document_formats_equal(previous, adjusted)) {
                previous.as_object()["Length"] = previous_length + length;
                occupied_until = previous_start + previous_length + length;
                continue;
            }
        }
        occupied_until = start + length;
        merged.push_back(std::move(adjusted));
    }
    object["Runs"] = std::move(merged);
}

void document_normalize_runs(Value::Object& object) {
    document_normalize_runs(object, static_cast<int>(object["Text"].to_string().size()));
}

std::optional<Value::Object> document_format_at(const Value::Object& object, int offset) {
    const auto found = object.find("Runs");
    if (found == object.end() || !found->second.is_array()) {
        return std::nullopt;
    }
    for (const auto& run_value : found->second.as_array()) {
        if (!run_value.is_object()) {
            continue;
        }
        const int start = document_run_start(run_value);
        const int end = start + document_run_length(run_value);
        if ((offset >= start && offset < end) || (offset == end && offset > start)) {
            return document_format_from_run(run_value);
        }
    }
    return std::nullopt;
}

void document_adjust_runs_for_replace(Value::Object& object, int start, int length, int inserted_length) {
    const int old_text_length = static_cast<int>(object["Text"].to_string().size());
    document_normalize_runs(object, old_text_length);
    const auto inherited_format = inserted_length > 0 ? document_format_at(object, start) : std::nullopt;
    const int delete_end = start + length;
    const int delta = inserted_length - length;
    Value::Array adjusted;
    const auto found = object.find("Runs");
    if (found != object.end() && found->second.is_array()) {
        for (const auto& run_value : found->second.as_array()) {
            const int run_start = document_run_start(run_value);
            const int run_end = run_start + document_run_length(run_value);
            const auto format = document_format_from_run(run_value);
            if (run_end <= start) {
                adjusted.push_back(document_make_run(run_start, run_end - run_start, format));
            } else if (run_start >= delete_end) {
                adjusted.push_back(document_make_run(run_start + delta, run_end - run_start, format));
            } else {
                if (run_start < start) {
                    adjusted.push_back(document_make_run(run_start, start - run_start, format));
                }
                if (run_end > delete_end) {
                    adjusted.push_back(document_make_run(start + inserted_length, run_end - delete_end, format));
                }
            }
        }
    }
    if (inherited_format) {
        adjusted.push_back(document_make_run(start, inserted_length, *inherited_format));
    }
    object["Runs"] = std::move(adjusted);
    document_normalize_runs(object, old_text_length + inserted_length - length);
}

Value document_insert_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.InsertText", 3, 3);
    Value document = clone_value(args[0]);
    auto& object = document.as_object();
    const std::string text = object["Text"].to_string();
    const int requested = static_cast<int>(args[1].as_number());
    const std::size_t index = static_cast<std::size_t>(std::clamp(requested, 0, static_cast<int>(text.size())));
    const std::string inserted = args[2].to_string();
    document_adjust_runs_for_replace(object, static_cast<int>(index), 0, static_cast<int>(inserted.size()));
    object["Text"] = text.substr(0, index) + inserted + text.substr(index);
    object["Dirty"] = true;
    return document;
}

Value document_delete_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.DeleteRange", 3, 3);
    Value document = clone_value(args[0]);
    auto& object = document.as_object();
    const std::string text = object["Text"].to_string();
    const int requested_start = static_cast<int>(args[1].as_number());
    const int requested_length = static_cast<int>(args[2].as_number());
    const std::size_t start = static_cast<std::size_t>(std::clamp(requested_start, 0, static_cast<int>(text.size())));
    const std::size_t length = std::min(text.size() - start, static_cast<std::size_t>(std::max(0, requested_length)));
    document_adjust_runs_for_replace(object, static_cast<int>(start), static_cast<int>(length), 0);
    object["Text"] = text.substr(0, start) + text.substr(std::min(text.size(), start + length));
    object["Dirty"] = true;
    return document;
}

Value document_replace_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.ReplaceRange", 4, 4);
    Value document = clone_value(args[0]);
    auto& object = document.as_object();
    const std::string text = object["Text"].to_string();
    const int requested_start = static_cast<int>(args[1].as_number());
    const int requested_length = static_cast<int>(args[2].as_number());
    const std::size_t start = static_cast<std::size_t>(std::clamp(requested_start, 0, static_cast<int>(text.size())));
    const std::size_t length = std::min(text.size() - start, static_cast<std::size_t>(std::max(0, requested_length)));
    const std::string inserted = args[3].to_string();
    document_adjust_runs_for_replace(object, static_cast<int>(start), static_cast<int>(length), static_cast<int>(inserted.size()));
    object["Text"] = text.substr(0, start) + inserted + text.substr(std::min(text.size(), start + length));
    object["Dirty"] = true;
    return document;
}

Value document_line_column_at_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.LineColumnAt", 2, 2);
    const std::string text = args[0].get_property("Text").to_string();
    int target = static_cast<int>(args[1].as_number());
    target = std::clamp(target, 0, static_cast<int>(text.size()));
    int line = 0;
    int column = 0;
    for (int i = 0; i < target; ++i) {
        if (text[static_cast<std::size_t>(i)] == '\n') {
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
    return Value::Object{{"Line", line}, {"Column", column}};
}

Value document_offset_at_line_column_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.OffsetAtLineColumn", 3, 3);
    const std::string text = args[0].get_property("Text").to_string();
    const int target_line = std::max(0, static_cast<int>(args[1].as_number()));
    const int target_column = std::max(0, static_cast<int>(args[2].as_number()));
    int line = 0;
    int column = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (line == target_line && column == target_column) {
            return static_cast<double>(i);
        }
        if (text[i] == '\n') {
            if (line == target_line) {
                return static_cast<double>(i);
            }
            ++line;
            column = 0;
        } else {
            ++column;
        }
    }
    return static_cast<double>(text.size());
}

Value document_apply_format_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.ApplyFormat", 4, 4);
    Value document = clone_value(args[0]);
    auto& object = document.as_object();
    const std::string text = object["Text"].to_string();
    const int requested_start = static_cast<int>(args[1].as_number());
    const int requested_length = static_cast<int>(args[2].as_number());
    const int start = std::clamp(requested_start, 0, static_cast<int>(text.size()));
    const int length = std::max(0, std::min(requested_length, static_cast<int>(text.size()) - start));
    if (length == 0) {
        return document;
    }
    document_normalize_runs(object);
    Value::Array adjusted;
    const auto found = object.find("Runs");
    if (found != object.end() && found->second.is_array()) {
        const int format_end = start + length;
        for (const auto& run_value : found->second.as_array()) {
            const int run_start = document_run_start(run_value);
            const int run_end = run_start + document_run_length(run_value);
            const auto format = document_format_from_run(run_value);
            if (run_end <= start || run_start >= format_end) {
                adjusted.push_back(clone_value(run_value));
            } else {
                if (run_start < start) {
                    adjusted.push_back(document_make_run(run_start, start - run_start, format));
                }
                if (run_end > format_end) {
                    adjusted.push_back(document_make_run(format_end, run_end - format_end, format));
                }
            }
        }
    }
    adjusted.push_back(document_make_run(start, length, args[3].as_object()));
    object["Runs"] = std::move(adjusted);
    document_normalize_runs(object);
    object["Dirty"] = true;
    return document;
}

Value document_runs_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.Runs", 1, 1);
    const auto& object = args[0].as_object();
    const auto found = object.find("Runs");
    if (found == object.end() || !found->second.is_array()) {
        return Value::Array{};
    }
    return found->second;
}

Value document_plain_text_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Document.PlainText", 1, 1);
    return args[0].get_property("Text");
}

std::string serialize_document_value(const Value& document) {
    const auto& object = document.as_object();
    std::ostringstream output;
    output << "ARCOWRITE 1\n";
    output << "TEXT " << document_escape(object.at("Text").to_string()) << "\n";
    const auto runs = object.find("Runs");
    if (runs != object.end() && runs->second.is_array()) {
        for (const auto& run_value : runs->second.as_array()) {
            const auto& run = run_value.as_object();
            output << "RUN " << run.at("Start").to_string() << ' ' << run.at("Length").to_string() << ' '
                   << (object_get_function({run_value, "Bold", false}).truthy() ? "1" : "0") << ' '
                   << (object_get_function({run_value, "Italic", false}).truthy() ? "1" : "0") << ' '
                   << object_get_function({run_value, "FontSize", 18}).to_string() << ' '
                   << document_escape(object_get_function({run_value, "Align", "left"}).to_string()) << "\n";
        }
    }
    return output.str();
}

Value parse_document_text(const std::string& data) {
    if (data.rfind("ARCOWRITE 1\n", 0) != 0) {
        return document_new_function({data});
    }
    std::istringstream input(data);
    std::string line;
    std::getline(input, line);
    Value document = document_new_function({""});
    auto& object = document.as_object();
    Value::Array runs;
    while (std::getline(input, line)) {
        if (line.rfind("TEXT ", 0) == 0) {
            object["Text"] = document_unescape(line.substr(5));
        } else if (line.rfind("RUN ", 0) == 0) {
            std::istringstream run_input(line.substr(4));
            double start = 0;
            double length = 0;
            int bold = 0;
            int italic = 0;
            double font_size = 18;
            std::string align = "left";
            run_input >> start >> length >> bold >> italic >> font_size;
            if (run_input >> align) {
                align = document_unescape(align);
            }
            runs.emplace_back(Value::Object{{"Start", start}, {"Length", length}, {"Bold", bold != 0}, {"Italic", italic != 0}, {"FontSize", font_size}, {"Align", align}});
        }
    }
    object["Runs"] = runs;
    document_normalize_runs(object);
    object["Dirty"] = false;
    return document;
}

std::string trim_text(const std::string& value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

Value string_insert_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Insert", 3, 3);
    const std::string text = args[0].to_string();
    const int requested = static_cast<int>(args[1].as_number());
    const std::size_t index = static_cast<std::size_t>(std::clamp(requested, 0, static_cast<int>(text.size())));
    return text.substr(0, index) + args[2].to_string() + text.substr(index);
}

Value string_delete_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Delete", 3, 3);
    const std::string text = args[0].to_string();
    const int requested_start = static_cast<int>(args[1].as_number());
    const int requested_length = static_cast<int>(args[2].as_number());
    const std::size_t start = static_cast<std::size_t>(std::clamp(requested_start, 0, static_cast<int>(text.size())));
    const std::size_t length = static_cast<std::size_t>(std::max(0, requested_length));
    return text.substr(0, start) + text.substr(std::min(text.size(), start + length));
}

Value string_join_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Join", 2, 2);
    const auto& items = args[0].as_array();
    const std::string separator = args[1].to_string();
    std::ostringstream output;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            output << separator;
        }
        output << items[i].to_string();
    }
    return output.str();
}

Value string_split_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Split", 2, 2);
    const std::string text = args[0].to_string();
    const std::string delimiter = args[1].to_string();
    if (delimiter.empty()) {
        throw std::runtime_error("String.Split delimiter cannot be empty");
    }
    Value::Array parts;
    std::size_t start = 0;
    while (true) {
        const auto found = text.find(delimiter, start);
        if (found == std::string::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, found - start));
        start = found + delimiter.size();
    }
    return parts;
}

Value string_replace_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Replace", 3, 3);
    std::string text = args[0].to_string();
    const std::string from = args[1].to_string();
    const std::string to = args[2].to_string();
    if (from.empty()) {
        return text;
    }
    std::size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
    return text;
}

Value string_lines_function(const std::vector<Value>& args) {
    expect_arg_count(args, "String.Lines", 1, 1);
    std::istringstream input(args[0].to_string());
    std::string line;
    Value::Array lines;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.emplace_back(line);
    }
    return lines;
}

Value format_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Format", 1, 64);
    std::string text = args[0].to_string();
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string key = "{" + std::to_string(i - 1) + "}";
        const std::string value = args[i].to_string();
        std::size_t position = 0;
        while ((position = text.find(key, position)) != std::string::npos) {
            text.replace(position, key.size(), value);
            position += value.size();
        }
    }
    return text;
}

Value time_timestamp_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Time.Timestamp", 0, 0);
    const auto now = std::chrono::system_clock::now();
    return static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count());
}

Value time_now_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Time.Now", 0, 0);
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream output;
    output << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

Value sleep_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Sleep", 1, 1);
    const auto milliseconds = static_cast<int>(args[0].as_number());
    if (milliseconds > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
    return {};
}

} // namespace

ExitSignal::ExitSignal(int code) : code_(code) {}

const char* ExitSignal::what() const noexcept {
    return "program exited";
}

int ExitSignal::code() const noexcept {
    return code_;
}

ReturnSignal::ReturnSignal(Value value) : value_(std::move(value)) {}

const char* ReturnSignal::what() const noexcept {
    return "function returned";
}

const Value& ReturnSignal::value() const noexcept {
    return value_;
}

GotoSignal::GotoSignal(int line) : line_(line) {}

const char* GotoSignal::what() const noexcept {
    return "goto";
}

int GotoSignal::line() const noexcept {
    return line_;
}

const char* StopSignal::what() const noexcept {
    return "program stopped";
}

Runtime::Runtime() : output_(&std::cout) {
    register_class("REF");
    register_class_field("REF", "Value", 0, "");
    register_class_field("REF", "Valid", 0, "Boolean");
    register_class_field("REF", "TypeName", 0, "String");
    register_class_method("REF", "Exists", 0);
    register_class_method("REF", "Clear", 0);
    register_class_method("REF", "Set", 0);
    register_function("PRINT", [this](const std::vector<Value>& args) -> Value {
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (i != 0) {
                *output_ << ' ';
            }
            *output_ << args[i].to_string();
        }
        *output_ << '\n';
        return {};
    });
    register_function("LEN", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("LEN expects 1 argument");
        }
        if (args[0].is_array()) {
            return static_cast<double>(args[0].as_array().size());
        }
        if (args[0].is_object()) {
            return static_cast<double>(args[0].as_object().size());
        }
        return static_cast<double>(args[0].to_string().size());
    });
    register_function("Upper", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Upper expects 1 argument");
        }
        std::string value = args[0].to_string();
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return value;
    });
    register_function("Lower", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Lower expects 1 argument");
        }
        std::string value = args[0].to_string();
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    });
    register_function("TYPEOF", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("TYPEOF expects 1 argument");
        }
        if (args[0].is_null()) {
            return "Null";
        }
        if (args[0].is_bool()) {
            return "Boolean";
        }
        if (args[0].is_number()) {
            return "Number";
        }
        if (args[0].is_string()) {
            return "String";
        }
        if (args[0].is_array()) {
            return "Array";
        }
        if (is_reference(args[0])) {
            return "Reference";
        }
        return "Object";
    });
    register_function("CLASSOF", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("CLASSOF expects 1 argument");
        }
        if (!args[0].is_object()) {
            return "";
        }
        const auto& object = args[0].as_object();
        const auto found = object.find("__class");
        return found == object.end() ? "" : found->second.to_string();
    });
    register_function("ISA", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("ISA expects object and class name");
        }
        return is_instance_of(args[0], args[1].to_string());
    });
    register_function("IMPLEMENTS", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("IMPLEMENTS expects object and interface name");
        }
        return implements_interface(args[0], args[1].to_string());
    });
    register_function("ISNULL", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("ISNULL expects 1 argument");
        }
        return args[0].is_null();
    });
    register_function("NUMBER", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("NUMBER expects 1 argument");
        }
        if (args[0].is_number() || args[0].is_bool()) {
            return args[0].as_number();
        }
        return std::stod(args[0].to_string());
    });
    register_function("STRING", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("STRING expects 1 argument");
        }
        return args[0].to_string();
    });
    register_function("REF", [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 1 || args.size() > 2) {
            throw std::runtime_error("REF expects 1 or 2 arguments");
        }
        return make_reference(args[0], args.size() == 2 ? args[1].to_string() : "");
    });
    register_function("REF.Exists", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("REF.Exists expects 1 argument");
        }
        if (!is_reference(args[0])) {
            return false;
        }
        try {
            const auto& object = args[0].as_object();
            const auto valid = object.find("Valid");
            if (valid == object.end() || !valid->second.truthy()) {
                return false;
            }
            const auto target = object.find("__target");
            if (target != object.end() && target->second.is_string()) {
                (void)get_global(target->second.to_string());
            }
            return true;
        } catch (const std::exception&) {
            return false;
        }
    });
    register_function("REF.Clear", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("REF.Clear expects 1 argument");
        }
        clear_reference(args[0]);
        return true;
    });
    register_function("REF.Set", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) {
            throw std::runtime_error("REF.Set expects 2 arguments");
        }
        set_reference_value(args[0], args[1]);
        return true;
    });
    register_function("Array.New", array_new_function);
    register_function("Array.Length", array_length_function);
    register_function("Array.Size", array_length_function);
    register_function("Array.Empty", array_empty_function);
    register_function("Array.IsEmpty", array_empty_function);
    register_function("Array.Push", array_push_function);
    register_function("Array.Add", array_push_function);
    register_function("Array.Append", array_push_function);
    register_function("Array.Pop", array_pop_function);
    register_function("Array.Shift", array_shift_function);
    register_function("Array.Unshift", array_unshift_function);
    register_function("Array.Insert", array_insert_function);
    register_function("Array.RemoveAt", array_remove_at_function);
    register_function("Array.Remove", array_remove_function);
    register_function("Array.Clear", array_clear_function);
    register_function("Array.Resize", array_resize_function);
    register_function("Array.Extend", array_extend_function);
    register_function("Array.First", array_first_function);
    register_function("Array.Last", array_last_function);
    register_function("Array.Find", array_find_function);
    register_function("Array.Reverse", array_reverse_function);
    register_function("Array.Join", array_join_function);
    register_function("Array.Contains", array_contains_function);
    register_function("Array.Sort", array_sort_function);
    register_function("Object.Keys", object_keys_function);
    register_function("Object.Has", object_has_function);
    register_function("Object.Get", object_get_function);
    register_function("Object.Set", object_set_function);
    register_function("Network.Available", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.Available", 0, 0);
        return network_available();
    });
    register_function("Network.Get", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || args.size() > 2) {
            throw std::runtime_error("Network.Get expects url and optional headers");
        }
        return http_request("GET", args[0].to_string(), "", args.size() == 2 ? args[1] : Value());
    });
    register_function("Network.Post", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 4) {
            throw std::runtime_error("Network.Post expects url, body, optional content type, and optional headers");
        }
        Value headers = args.size() == 4 ? args[3] : Value();
        if (args.size() >= 3 && !args[2].is_null()) {
            Value::Object merged;
            if (headers.is_object()) {
                merged = headers.as_object();
            } else if (!headers.is_null()) {
                throw std::runtime_error("Network.Post headers must be an object when content type is provided");
            }
            merged["Content-Type"] = args[2].to_string();
            headers = Value(std::move(merged));
        }
        return http_request("POST", args[0].to_string(), args[1].to_string(), headers);
    });
    register_function("Network.Download", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) {
            throw std::runtime_error("Network.Download expects url, path, and optional headers");
        }
        return network_download(args[0].to_string(), args[1].to_string(), args.size() == 3 ? args[2] : Value());
    });
    register_function("Network.UrlEncode", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.UrlEncode", 1, 1);
        return url_encode(args[0].to_string());
    });
    register_function("Network.UrlDecode", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.UrlDecode", 1, 1);
        return url_decode(args[0].to_string());
    });
    register_function("Network.QueryString", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.QueryString", 1, 1);
        return query_string(args[0]);
    });
    auto resolve_dns_function = [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.ResolveDNS", 1, 1);
        return network_resolve(args[0].to_string());
    };
    register_function("Network.ResolveDNS", resolve_dns_function);
    register_function("Net.ResolveDNS", resolve_dns_function);
    register_function("Net.Resolve", resolve_dns_function);
    auto tcp_connect_function = [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.TcpConnect", 2, 2);
        return tcp_connect(args[0].to_string(), static_cast<int>(args[1].as_number()));
    };
    auto tcp_send_function = [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.TcpSend", 2, 2);
        return tcp_send(static_cast<int>(args[0].as_number()), args[1].to_string());
    };
    auto tcp_read_function = [](const std::vector<Value>& args) -> Value {
        if (args.empty() || args.size() > 2) {
            throw std::runtime_error("Network.TcpRead expects client and optional max bytes");
        }
        return tcp_read(static_cast<int>(args[0].as_number()), args.size() == 2 ? static_cast<int>(args[1].as_number()) : 4096);
    };
    auto tcp_close_function = [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Network.TcpClose", 1, 1);
        return tcp_close(static_cast<int>(args[0].as_number()));
    };
    register_function("Network.TcpConnect", tcp_connect_function);
    register_function("Network.TcpSend", tcp_send_function);
    register_function("Network.TcpRead", tcp_read_function);
    register_function("Network.TcpClose", tcp_close_function);
    register_function("Net.TcpConnect", tcp_connect_function);
    register_function("Net.TcpSend", tcp_send_function);
    register_function("Net.TcpRead", tcp_read_function);
    register_function("Net.TcpClose", tcp_close_function);
    auto serve_static_function = [](const std::vector<Value>& args) -> Value {
        if (args.empty() || args.size() > 4) {
            throw std::runtime_error("Web.ServeStatic expects root, optional port, optional host, and optional max requests");
        }
        const std::string root = args[0].to_string();
        const int port = args.size() >= 2 ? static_cast<int>(args[1].as_number()) : 8080;
        const std::string host = args.size() >= 3 ? args[2].to_string() : "127.0.0.1";
        const int max_requests = args.size() >= 4 ? static_cast<int>(args[3].as_number()) : 0;
        return serve_static_site(root, port, host, max_requests);
    };
    register_function("Web.ServeStatic", serve_static_function);
    register_function("Web.Static", serve_static_function);
    register_function("File.Exists", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "File.Exists", 1, 1);
        return std::filesystem::exists(args[0].to_string());
    });
    register_function("File.ReadText", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "File.ReadText", 1, 1);
        return read_plain_file(args[0].to_string());
    });
    register_function("File.WriteText", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "File.WriteText", 2, 2);
        write_plain_file(args[0].to_string(), args[1].to_string(), std::ios::binary | std::ios::trunc);
        return true;
    });
    register_function("File.AppendText", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "File.AppendText", 2, 2);
        write_plain_file(args[0].to_string(), args[1].to_string(), std::ios::binary | std::ios::app);
        return true;
    });
    register_function("File.ReadBytes", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "File.ReadBytes", 1, 1);
        return bytes_from_string(read_plain_file(args[0].to_string()));
    });
    register_function("File.WriteBytes", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "File.WriteBytes", 2, 2);
        write_plain_file(args[0].to_string(), string_from_bytes(args[1]), std::ios::binary | std::ios::trunc);
        return true;
    });
    register_function("Bytes.New", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bytes.New", 0, 2);
        const int requested_size = args.empty() ? 0 : static_cast<int>(args[0].as_number());
        const int fill = args.size() == 2 ? std::clamp(static_cast<int>(args[1].as_number()), 0, 255) : 0;
        if (requested_size < 0) {
            throw std::runtime_error("Bytes.New size cannot be negative");
        }
        return Value::Array(static_cast<std::size_t>(requested_size), Value(static_cast<double>(fill)));
    });
    register_function("Bytes.Length", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bytes.Length", 1, 1);
        return static_cast<double>(args[0].as_array().size());
    });
    register_function("Bytes.GetU8", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bytes.GetU8", 2, 2);
        const auto& bytes = args[0].as_array();
        const int index = static_cast<int>(args[1].as_number());
        if (index < 0 || static_cast<std::size_t>(index) >= bytes.size()) {
            throw std::runtime_error("Bytes.GetU8 index out of range");
        }
        return static_cast<double>(std::clamp(static_cast<int>(bytes[static_cast<std::size_t>(index)].as_number()), 0, 255));
    });
    register_function("Bytes.SetU8", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bytes.SetU8", 3, 3);
        Value bytes_value = args[0];
        auto& bytes = bytes_value.as_array();
        const int index = static_cast<int>(args[1].as_number());
        if (index < 0 || static_cast<std::size_t>(index) >= bytes.size()) {
            throw std::runtime_error("Bytes.SetU8 index out of range");
        }
        bytes[static_cast<std::size_t>(index)] = static_cast<double>(std::clamp(static_cast<int>(args[2].as_number()), 0, 255));
        return bytes_value;
    });
    register_function("Bytes.FromText", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bytes.FromText", 1, 1);
        return bytes_from_string(args[0].to_string());
    });
    register_function("Bytes.ToText", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bytes.ToText", 1, 1);
        return string_from_bytes(args[0]);
    });
    register_function("String.Trim", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.Trim", 1, 1);
        return trim_text(args[0].to_string());
    });
    register_function("String.Insert", string_insert_function);
    register_function("String.Delete", string_delete_function);
    register_function("String.Join", string_join_function);
    register_function("String.Split", string_split_function);
    register_function("String.Replace", string_replace_function);
    register_function("String.Contains", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.Contains", 2, 2);
        return args[0].to_string().find(args[1].to_string()) != std::string::npos;
    });
    register_function("String.IndexOf", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.IndexOf", 2, 3);
        const std::string text = args[0].to_string();
        const std::string needle = args[1].to_string();
        const int requested_start = args.size() == 3 ? static_cast<int>(args[2].as_number()) : 0;
        const std::size_t start = requested_start < 0 ? 0 : static_cast<std::size_t>(requested_start);
        if (start > text.size()) return -1.0;
        const std::size_t found = text.find(needle, start);
        return found == std::string::npos ? -1.0 : static_cast<double>(found);
    });
    register_function("String.StartsWith", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.StartsWith", 2, 2);
        const std::string text = args[0].to_string();
        const std::string prefix = args[1].to_string();
        return text.rfind(prefix, 0) == 0;
    });
    register_function("String.EndsWith", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.EndsWith", 2, 2);
        const std::string text = args[0].to_string();
        const std::string suffix = args[1].to_string();
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    });
    register_function("String.Lines", string_lines_function);
    register_function("String.Length", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.Length", 1, 1);
        const std::string text = args[0].to_string();
        std::size_t count = 0;
        for (unsigned char byte : text) if ((byte & 0xc0) != 0x80) ++count;
        return static_cast<double>(count);
    });
    register_function("String.Slice", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "String.Slice", 2, 3);
        const std::string text = args[0].to_string();
        const int requested_start = static_cast<int>(args[1].as_number());
        const int requested_length = args.size() == 3 ? static_cast<int>(args[2].as_number()) : 0x7fffffff;
        const int start = std::max(0, requested_start);
        const int length = std::max(0, requested_length);
        int codepoint = 0;
        std::size_t byte_start = text.size();
        std::size_t byte_end = text.size();
        for (std::size_t i = 0; i < text.size(); ++i) {
            if ((static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) continue;
            if (codepoint == start) byte_start = i;
            if (codepoint == start + length) { byte_end = i; break; }
            ++codepoint;
        }
        if (start == codepoint) byte_start = text.size();
        if (byte_start == text.size()) return "";
        return text.substr(byte_start, byte_end - byte_start);
    });
    register_function("Format", format_function);
    auto register_math = [this](const std::string& bare, const std::string& namespaced, HostFunction function) {
        register_function(bare, function);
        register_function(namespaced, std::move(function));
    };
    register_math("SIN", "Math.Sin", unary_math_function("Math.Sin", static_cast<double (*)(double)>(std::sin)));
    register_math("COS", "Math.Cos", unary_math_function("Math.Cos", static_cast<double (*)(double)>(std::cos)));
    register_math("TAN", "Math.Tan", unary_math_function("Math.Tan", static_cast<double (*)(double)>(std::tan)));
    register_math("ASIN", "Math.Asin", unary_math_function("Math.Asin", static_cast<double (*)(double)>(std::asin)));
    register_math("ACOS", "Math.Acos", unary_math_function("Math.Acos", static_cast<double (*)(double)>(std::acos)));
    register_math("ATAN", "Math.Atan", unary_math_function("Math.Atan", static_cast<double (*)(double)>(std::atan)));
    register_math("ATAN2", "Math.Atan2", binary_math_function("Math.Atan2", static_cast<double (*)(double, double)>(std::atan2)));
    register_math("SQRT", "Math.Sqrt", unary_math_function("Math.Sqrt", static_cast<double (*)(double)>(std::sqrt)));
    register_math("FLOOR", "Math.Floor", unary_math_function("Math.Floor", static_cast<double (*)(double)>(std::floor)));
    register_math("CEIL", "Math.Ceil", unary_math_function("Math.Ceil", static_cast<double (*)(double)>(std::ceil)));
    register_math("ROUND", "Math.Round", unary_math_function("Math.Round", static_cast<double (*)(double)>(std::round)));
    register_math("ABS", "Math.Abs", unary_math_function("Math.Abs", static_cast<double (*)(double)>(std::fabs)));
    register_math("MIN", "Math.Min", math_min_function);
    register_math("MAX", "Math.Max", math_max_function);
    register_math("CLAMP", "Math.Clamp", math_clamp_function);
    register_math("LERP", "Math.Lerp", math_lerp_function);
    register_math("POW", "Math.Pow", binary_math_function("Math.Pow", static_cast<double (*)(double, double)>(std::pow)));
    register_math("EXP", "Math.Exp", unary_math_function("Math.Exp", static_cast<double (*)(double)>(std::exp)));
    register_math("LOG", "Math.Log", unary_math_function("Math.Log", static_cast<double (*)(double)>(std::log)));
    register_math("LOG10", "Math.Log10", unary_math_function("Math.Log10", static_cast<double (*)(double)>(std::log10)));
    register_function("Math.Constants", math_constants_function);
    register_function("Math.PI", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Math.PI", 0, 0);
        return 3.14159265358979323846264338327950288;
    });
    register_function("PI", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "PI", 0, 0);
        return 3.14159265358979323846264338327950288;
    });
    register_function("Math.TAU", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Math.TAU", 0, 0);
        return 6.28318530717958647692528676655900576;
    });
    register_function("TAU", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "TAU", 0, 0);
        return 6.28318530717958647692528676655900576;
    });
    register_function("Document.New", document_new_function);
    register_function("Document.InsertText", document_insert_function);
    register_function("Document.DeleteRange", document_delete_function);
    register_function("Document.ReplaceRange", document_replace_function);
    register_function("Document.LineColumnAt", document_line_column_at_function);
    register_function("Document.OffsetAtLineColumn", document_offset_at_line_column_function);
    register_function("Document.ApplyFormat", document_apply_format_function);
    register_function("Document.Runs", document_runs_function);
    register_function("Document.PlainText", document_plain_text_function);
    register_function("Document.Serialize", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Document.Serialize", 1, 1);
        return serialize_document_value(args[0]);
    });
    register_function("Document.Parse", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Document.Parse", 1, 1);
        return parse_document_text(args[0].to_string());
    });
    register_function("Document.Save", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Document.Save", 2, 2);
        write_plain_file(args[0].to_string(), serialize_document_value(args[1]), std::ios::binary | std::ios::trunc);
        return true;
    });
    register_function("Document.Load", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Document.Load", 1, 1);
        return parse_document_text(read_plain_file(args[0].to_string()));
    });
    register_function("Document.Text", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Document.Text", 1, 1);
        return args[0].get_property("Text");
    });
    register_function("Document.LineAt", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Document.LineAt", 2, 2);
        const auto lines = string_lines_function({args[0].get_property("Text")}).as_array();
        const int index = static_cast<int>(args[1].as_number());
        if (index < 0 || static_cast<std::size_t>(index) >= lines.size()) {
            return "";
        }
        return lines[static_cast<std::size_t>(index)];
    });
    register_function("Bit.And", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.And", '&'); });
    register_function("Bit.Or", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.Or", '|'); });
    register_function("Bit.Xor", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.Xor", '^'); });
    register_function("Bit.ShiftLeft", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.ShiftLeft", '<'); });
    register_function("Bit.ShiftRight", [](const std::vector<Value>& args) -> Value { return bit_binary(args, "Bit.ShiftRight", '>'); });
    register_function("Bit.Not", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) {
            throw std::runtime_error("Bit.Not expects 1 argument");
        }
        return static_cast<double>(~value_to_int(args[0]));
    });
    register_function("SHIFT", shift_function);
    register_function("BIT", bit_test_function);
    register_function("SETBIT", bit_set_function);
    register_function("CLEARBIT", bit_clear_function);
    register_function("TOGGLEBIT", bit_toggle_function);
    register_function("BITCOUNT", bitcount_function);
    register_function("ROTATELEFT", [](const std::vector<Value>& args) -> Value { return rotate_function(args, true); });
    register_function("ROTATERIGHT", [](const std::vector<Value>& args) -> Value { return rotate_function(args, false); });
    register_function("BitsToString", bits_text_function);
    register_function("BitsToBinary", bits_text_function);
    register_function("StringToBits", string_to_bits_function);
    register_function("BitsTable", bits_table_function);
    register_function("HexToString", hex_to_string_function);
    register_function("StringToHex", string_to_hex_function);
    register_function("BytesToHex", bytes_to_hex_function);
    register_function("HexToBytes", hex_to_bytes_function);
    register_function("Time.Now", time_now_function);
    register_function("Time.Timestamp", time_timestamp_function);
    register_function("DATE", time_now_function);
    register_function("Date", time_now_function);
    register_function("Sleep", sleep_function);
    register_function("GUI.Available", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GUI.Available expects no arguments");
        return gui_session_available() && gui::available();
    });
    register_function("GUI.Backend", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GUI.Backend expects no arguments");
        return gui::backend();
    });
    register_function("GUI.Application", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) throw std::runtime_error("GUI.Application expects app id, display name, and optional icon path");
        gui::set_application(args[0].to_string(), args[1].to_string(), args.size() == 3 ? args[2].to_string() : "");
        return {};
    });
    register_function("GUI.Window", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("GUI.Window expects title, width, and height");
        return gui::create_window(args[0].to_string(), static_cast<int>(args[1].as_number()), static_cast<int>(args[2].as_number()));
    });
    register_function("GUI.Close", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.Close expects a window");
        gui::destroy_window(static_cast<int>(args[0].as_number()));
        return {};
    });
    register_function("GUI.ShouldClose", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.ShouldClose expects a window");
        return gui::should_close(static_cast<int>(args[0].as_number()));
    });
    register_function("GUI.SetShouldClose", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetShouldClose expects a window and boolean");
        gui::set_should_close(static_cast<int>(args[0].as_number()), args[1].truthy());
        return {};
    });
    register_function("GUI.SetTitle", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetTitle expects a window and title");
        gui::set_title(static_cast<int>(args[0].as_number()), args[1].to_string());
        return {};
    });
    register_function("GUI.Size", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.Size expects a window");
        return gui::window_size(static_cast<int>(args[0].as_number()));
    });
    register_function("GUI.Clear", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 4 || args.size() > 5) throw std::runtime_error("GUI.Clear expects window, red, green, blue, and optional alpha");
        gui::clear(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args.size() == 5 ? args[4].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Pixel", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 6 || args.size() > 7) throw std::runtime_error("GUI.Pixel expects window, x, y, red, green, blue, and optional alpha");
        gui::pixel(static_cast<int>(args[0].as_number()), static_cast<int>(args[1].as_number()), static_cast<int>(args[2].as_number()),
                   args[3].as_number(), args[4].as_number(), args[5].as_number(), args.size() == 7 ? args[6].as_number() : 1.0);
        return {};
    });
    register_function("GUI.FillRect", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 8 || args.size() > 9) throw std::runtime_error("GUI.FillRect expects window, x, y, width, height, red, green, blue, and optional alpha");
        gui::fill_rect(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number(),
                       args[5].as_number(), args[6].as_number(), args[7].as_number(), args.size() == 9 ? args[8].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Column", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 7 || args.size() > 8) throw std::runtime_error("GUI.Column expects window, x, y1, y2, red, green, blue, and optional alpha");
        gui::column(static_cast<int>(args[0].as_number()), static_cast<int>(args[1].as_number()), static_cast<int>(args[2].as_number()),
                    static_cast<int>(args[3].as_number()), args[4].as_number(), args[5].as_number(), args[6].as_number(), args.size() == 8 ? args[7].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Rectangle", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 8 || args.size() > 9) throw std::runtime_error("GUI.Rectangle expects window, x, y, width, height, red, green, blue, and optional alpha");
        gui::rectangle(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number(),
                       args[5].as_number(), args[6].as_number(), args[7].as_number(), args.size() == 9 ? args[8].as_number() : 1.0);
        return {};
    });
    register_function("GUI.RoundedRectangle", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 9 || args.size() > 10) throw std::runtime_error("GUI.RoundedRectangle expects window, x, y, width, height, radius, red, green, blue, and optional alpha");
        gui::rounded_rectangle(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number(), args[5].as_number(),
                               args[6].as_number(), args[7].as_number(), args[8].as_number(), args.size() == 10 ? args[9].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Line", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 9 || args.size() > 10) throw std::runtime_error("GUI.Line expects window, x1, y1, x2, y2, thickness, red, green, blue, and optional alpha");
        gui::line(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number(), args[5].as_number(),
                  args[6].as_number(), args[7].as_number(), args[8].as_number(), args.size() == 10 ? args[9].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Circle", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 7 || args.size() > 8) throw std::runtime_error("GUI.Circle expects window, centerX, centerY, radius, red, green, blue, and optional alpha");
        gui::circle(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(),
                    args[4].as_number(), args[5].as_number(), args[6].as_number(), args.size() == 8 ? args[7].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Text", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 8 || args.size() > 9) throw std::runtime_error("GUI.Text expects window, text, x, y, size, red, green, blue, and optional alpha");
        gui::text(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number(), args[3].as_number(), args[4].as_number(),
                  args[5].as_number(), args[6].as_number(), args[7].as_number(), args.size() == 9 ? args[8].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Image", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 6 || args.size() > 7) throw std::runtime_error("GUI.Image expects window, path, x, y, width, height, and optional opacity");
        gui::image(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number(), args[3].as_number(),
                   args[4].as_number(), args[5].as_number(), args.size() == 7 ? args[6].as_number() : 1.0);
        return {};
    });
    register_function("GUI.MeasureText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("GUI.MeasureText expects window, text, and size");
        return gui::measure_text(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number());
    });
    register_function("GUI.SetClip", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 5) throw std::runtime_error("GUI.SetClip expects window, x, y, width, and height");
        gui::set_clip(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(), args[4].as_number());
        return {};
    });
    register_function("GUI.ResetClip", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.ResetClip expects a window");
        gui::reset_clip(static_cast<int>(args[0].as_number()));
        return {};
    });
    register_function("GUI.ClipboardText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.ClipboardText expects a window");
        return gui::clipboard_text(static_cast<int>(args[0].as_number()));
    });
    register_function("GUI.SetClipboardText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetClipboardText expects a window and text");
        gui::set_clipboard_text(static_cast<int>(args[0].as_number()), args[1].to_string());
        return {};
    });
    register_function("GUI.SetCursor", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetCursor expects a window and cursor name");
        gui::set_cursor(static_cast<int>(args[0].as_number()), lower_copy(args[1].to_string()));
        return {};
    });
    register_function("GUI.KeyDown", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.KeyDown expects a window and key name");
        return gui::key_down(static_cast<int>(args[0].as_number()), lower_copy(args[1].to_string()));
    });
    register_function("GUI.PointerPosition", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.PointerPosition expects a window");
        return gui::pointer_position(static_cast<int>(args[0].as_number()));
    });
    register_function("GUI.OpenFileDialog", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 1 || args.size() > 3) throw std::runtime_error("GUI.OpenFileDialog expects window, optional title, and optional initial path");
        return gui::open_file_dialog(static_cast<int>(args[0].as_number()), args.size() >= 2 ? args[1].to_string() : "Open File", args.size() == 3 ? args[2].to_string() : "");
    });
    register_function("GUI.SaveFileDialog", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 1 || args.size() > 3) throw std::runtime_error("GUI.SaveFileDialog expects window, optional title, and optional initial path");
        return gui::save_file_dialog(static_cast<int>(args[0].as_number()), args.size() >= 2 ? args[1].to_string() : "Save File", args.size() == 3 ? args[2].to_string() : "");
    });
    register_function("GUI.Confirm", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("GUI.Confirm expects window, title, and message");
        return gui::confirm(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].to_string());
    });
    register_function("GUI.Present", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GUI.Present expects a window");
        gui::present(static_cast<int>(args[0].as_number()));
        return {};
    });
    register_function("GUI.PollEvent", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GUI.PollEvent expects no arguments");
        return gui::poll_event();
    });
    register_function("GUI.WaitEvent", [this](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) throw std::runtime_error("GUI.WaitEvent expects optional timeout seconds");
        Value event = gui::wait_event(args.empty() ? 0.05 : args[0].as_number());
        reset_instruction_count();
        return event;
    });
}

std::string Runtime::preprocess_source(const std::string& code) {
    return preprocess_source(code, true);
}

std::string Runtime::preprocess_source(const std::string& code, bool reset_metadata) {
    if (reset_metadata) {
        metadata_ = {};
    }

    std::set<std::string> defines = {
#if defined(_WIN32)
        "TARGET_WINDOWS",
#elif defined(__APPLE__)
        "TARGET_MACOS",
#elif defined(__linux__)
        "TARGET_LINUX",
#endif
        "DEBUG"
    };

    std::vector<ConditionalFrame> conditionals;
    std::ostringstream output;
    std::istringstream input(code);
    std::string line;

    while (std::getline(input, line)) {
        const std::string trimmed = trim_copy(line);
        if (trimmed.rfind("#!", 0) == 0) {
            output << '\n';
            continue;
        }

        if (!trimmed.empty() && trimmed.front() == '@') {
            if (active_conditions(conditionals)) {
                metadata_.attributes.push_back(trimmed);
            }
            output << '\n';
            continue;
        }

        if (trimmed.empty() || trimmed.front() != '#') {
            if (active_conditions(conditionals)) {
                output << line << '\n';
            } else {
                output << '\n';
            }
            continue;
        }

        std::string rest = trim_copy(trimmed.substr(1));
        const auto split = rest.find_first_of(" \t");
        const std::string directive = upper_copy(rest.substr(0, split == std::string::npos ? std::string::npos : split));
        const std::string args = split == std::string::npos ? "" : trim_copy(rest.substr(split + 1));
        const bool active = active_conditions(conditionals);

        if (directive == "IFDEF" || directive == "IFNDEF" || directive == "IF") {
            const bool parent = active_conditions(conditionals);
            bool enabled = false;
            if (directive == "IFDEF") {
                enabled = symbol_enabled(defines, args);
            } else if (directive == "IFNDEF") {
                enabled = !symbol_enabled(defines, args);
            } else {
                enabled = symbol_enabled(defines, args) || upper_copy(args) == "TRUE" || args == "1";
            }
            conditionals.push_back({parent, parent && enabled, parent && enabled});
            output << '\n';
            continue;
        }
        if (directive == "ELSEIF") {
            if (conditionals.empty()) {
                throw std::runtime_error("#ELSEIF without #IF");
            }
            auto& frame = conditionals.back();
            const bool enabled = !frame.branch_taken && (symbol_enabled(defines, args) || upper_copy(args) == "TRUE" || args == "1");
            frame.active = frame.parent_active && enabled;
            frame.branch_taken = frame.branch_taken || frame.active;
            output << '\n';
            continue;
        }
        if (directive == "ELSE") {
            if (conditionals.empty()) {
                throw std::runtime_error("#ELSE without #IF");
            }
            auto& frame = conditionals.back();
            frame.active = frame.parent_active && !frame.branch_taken;
            frame.branch_taken = true;
            output << '\n';
            continue;
        }
        if (directive == "ENDIF") {
            if (conditionals.empty()) {
                throw std::runtime_error("#ENDIF without #IF");
            }
            conditionals.pop_back();
            output << '\n';
            continue;
        }

        if (!active) {
            output << '\n';
            continue;
        }

        if (directive == "DEFINE") {
            const auto name_end = args.find_first_of(" \t");
            const std::string name = name_end == std::string::npos ? args : args.substr(0, name_end);
            const std::string value = name_end == std::string::npos ? "" : trim_copy(args.substr(name_end + 1));
            if (!name.empty()) {
                defines.insert(upper_copy(name));
                if (!value.empty()) {
                    set_global(name, parse_define_value(value));
                } else {
                    set_global(name, true);
                }
            }
        } else if (directive == "UNDEF") {
            defines.erase(upper_copy(args));
        } else if (directive == "VERSION") {
            metadata_.version = unquote(args);
        } else if (directive == "AUTHOR") {
            metadata_.author = unquote(args);
        } else if (directive == "DESCRIPTION") {
            metadata_.description = unquote(args);
        } else if (directive == "ENTRY") {
            metadata_.entry = args;
        } else if (directive == "TARGET") {
            metadata_.targets = split_words(args);
            // Under a systems profile (docs/systems/uefi-target.md section 2), #TARGET's
            // first word additionally selects the codegen architecture; outside a systems
            // profile its existing platform-list meaning is unchanged.
            if (metadata_.profile == "UEFI" && !metadata_.targets.empty()) {
                const std::string requested_arch = upper_copy(metadata_.targets.front());
                if (requested_arch != "X86_64") {
                    throw std::runtime_error(
                        "Unsupported architecture for the UEFI profile: " + metadata_.targets.front() +
                        ". X86_64 is the only supported architecture in this milestone.");
                }
                metadata_.arch = requested_arch;
            }
        } else if (directive == "PROFILE") {
            const std::string profile = upper_copy(args);
            if (profile != "UEFI") {
                throw std::runtime_error(
                    "Unknown compilation profile: " + args + ". UEFI is the only accepted profile in this milestone.");
            }
            metadata_.profile = profile;
        } else if (directive == "RUNTIME") {
            const std::string mode = upper_copy(args);
            if (mode != "NONE") {
                throw std::runtime_error(
                    "Unknown #RUNTIME value: " + args + ". NONE is the only accepted value in this milestone.");
            }
            metadata_.runtime_mode = mode;
        } else if (directive == "CALLCONV") {
            const std::string convention = upper_copy(args);
            if (convention != "UEFI") {
                throw std::runtime_error(
                    "Unknown #CALLCONV value: " + args + ". UEFI is the only accepted calling convention in this milestone.");
            }
            metadata_.callconv = convention;
        } else if (directive == "EXPORT") {
            metadata_.export_symbol = unquote(args);
        } else if (directive == "REQUIRE") {
            metadata_.requirements.push_back(args);
        } else if (directive == "FEATURE") {
            metadata_.features.push_back(args);
        } else if (directive == "STRICT") {
            metadata_.strict = args.empty() || upper_copy(args) == "ON" || upper_copy(args) == "TRUE";
        } else if (directive == "EXPERIMENTAL") {
            metadata_.experimental = true;
            if (!args.empty()) {
                metadata_.notes.push_back("experimental: " + unquote(args));
            }
        } else if (directive == "DEPRECATED") {
            metadata_.deprecated = true;
            if (!args.empty()) {
                metadata_.warnings.push_back("deprecated: " + unquote(args));
            }
        } else if (directive == "WARNING") {
            metadata_.warnings.push_back(unquote(args));
            *output_ << "warning: " << unquote(args) << '\n';
        } else if (directive == "ERROR") {
            throw std::runtime_error(unquote(args));
        } else if (directive == "TODO") {
            metadata_.todos.push_back(unquote(args));
        } else if (directive == "NOTE") {
            metadata_.notes.push_back(unquote(args));
        } else if (directive == "REGION" || directive == "ENDREGION") {
        } else if (directive == "INCLUDE") {
            output << preprocess_source(read_text_file(unquote(args)), false);
        } else if (directive == "IMPORT") {
            const auto import = parse_import_directive(args);
            metadata_.imports.push_back(import.alias.empty() ? import.path : import.path + " AS " + import.alias);
            const std::string imported = preprocess_source(read_text_file(import.path), false);
            output << imported;
            output << alias_import_wrappers(imported, import.alias);
        } else if (directive == "PACK") {
            metadata_.pack = args;
        } else if (directive == "ALIGN") {
            metadata_.align = args;
        } else if (directive == "ENDIAN") {
            metadata_.endian = args;
        } else {
            metadata_.warnings.push_back("unknown directive: #" + directive);
        }
        output << '\n';
    }

    if (!conditionals.empty()) {
        throw std::runtime_error("unterminated conditional directive");
    }

    return output.str();
}

RunResult Runtime::run_string(const std::string& code) {
    std::string processed;
    try {
        reset_instruction_count();
        processed = preprocess_source(code);
        Lexer lexer(processed);
        Parser parser(lexer.scan_tokens(), metadata_.runtime_mode == "NONE");
        auto statements = parser.parse();
        std::unordered_map<int, std::size_t> labels;
        for (std::size_t i = 0; i < statements.size(); ++i) {
            if (statements[i]->line_label >= 0) {
                labels[statements[i]->line_label] = i;
            }
        }
        for (std::size_t pc = 0; pc < statements.size();) {
            try {
                statements[pc]->exec(*this);
                pc++;
            } catch (const GotoSignal& jump) {
                const auto target = labels.find(jump.line());
                if (target == labels.end()) {
                    throw std::runtime_error("undefined line number: " + std::to_string(jump.line()));
                }
                pc = target->second;
            } catch (const StopSignal&) {
                break;
            } catch (const ExitSignal&) {
                throw;
            } catch (const std::exception& error) {
                throw std::runtime_error(format_runtime_diagnostic(error.what(), processed, statements[pc]->source_line, statements[pc]->source_column));
            }
        }
        return {};
    } catch (const ExitSignal& exit) {
        return {true, "", true, exit.code()};
    } catch (const std::exception& error) {
        return {false, format_source_diagnostic(error.what(), processed.empty() ? code : processed)};
    }
}

const CompileMetadata& Runtime::compile_metadata() const {
    return metadata_;
}

void Runtime::register_function(const std::string& name, HostFunction function) {
    host_functions_[function_key(name)] = std::move(function);
}

bool Runtime::has_function(const std::string& name) const {
    return host_functions_.find(function_key(name)) != host_functions_.end();
}

void Runtime::register_class(std::string name, std::string parent, std::vector<std::string> interfaces) {
    const std::string key = function_key(name);
    auto found = classes_.find(key);
    if (found == classes_.end()) {
        classes_[key] = ClassMetadata{name, parent, interfaces};
    } else {
        found->second.name = name;
        found->second.parent = parent;
        found->second.interfaces = interfaces;
    }
}

void Runtime::register_class_field(const std::string& class_name, const std::string& field_name, int access, const std::string& type_name) {
    auto& metadata = classes_[function_key(class_name)];
    metadata.fields_access[function_key(field_name)] = access;
    metadata.fields_type[function_key(field_name)] = type_name;
}

void Runtime::register_class_method(const std::string& class_name, const std::string& method_name, int access) {
    classes_[function_key(class_name)].methods_access[function_key(method_name)] = access;
}

void Runtime::register_interface(std::string name, std::vector<MethodSignature> methods) {
    const std::string key = function_key(name);
    interfaces_[key] = InterfaceMetadata{name, std::move(methods)};
}

std::vector<MethodSignature> Runtime::interface_methods(const std::string& interface_name) const {
    const auto found = interfaces_.find(function_key(interface_name));
    if (found == interfaces_.end()) {
        throw std::runtime_error("unknown interface: " + interface_name);
    }
    return found->second.methods;
}

void Runtime::set_class_abstract_methods(const std::string& class_name, std::vector<std::string> methods) {
    classes_[function_key(class_name)].abstract_methods = std::move(methods);
}

std::vector<std::string> Runtime::class_abstract_methods(const std::string& class_name) const {
    const auto found = classes_.find(function_key(class_name));
    if (found == classes_.end()) {
        return {};
    }
    return found->second.abstract_methods;
}

bool Runtime::implements_interface(const Value& value, const std::string& interface_name) const {
    if (!value.is_object()) {
        return false;
    }
    std::string current = object_runtime_class(value);
    const std::string requested = function_key(interface_name);
    while (!current.empty()) {
        const auto found = classes_.find(function_key(current));
        if (found == classes_.end()) {
            return false;
        }
        for (const auto& item : found->second.interfaces) {
            if (function_key(item) == requested) {
                return true;
            }
        }
        current = found->second.parent;
    }
    return false;
}

bool Runtime::is_instance_of(const Value& value, const std::string& class_name) const {
    if (!value.is_object()) {
        return false;
    }
    std::string current;
    try {
        current = value.get_property("__class").to_string();
    } catch (const std::exception&) {
        return false;
    }
    const std::string requested = function_key(class_name);
    while (!current.empty()) {
        if (function_key(current) == requested) {
            return true;
        }
        const auto found = classes_.find(function_key(current));
        if (found == classes_.end()) {
            return false;
        }
        current = found->second.parent;
    }
    return false;
}

bool Runtime::value_matches_type(const Value& value, const std::string& type_name) const {
    const std::string type = function_key(type_name);
    if (type.empty() || type == "any" || type == "variant" || type == "value") {
        return true;
    }
    if (type == "null" || type == "nothing") {
        return value.is_null();
    }
    if (type == "bool" || type == "boolean") {
        return value.is_bool();
    }
    if (type == "number" || type == "double" || type == "integer" || type == "int") {
        return value.is_number();
    }
    if (type == "string" || type == "text") {
        return value.is_string();
    }
    if (type == "array" || type == "list") {
        return value.is_array();
    }
    if (type == "object") {
        return value.is_object();
    }
    if (type == "ref" || type == "reference") {
        return is_reference(value);
    }
    if (classes_.find(type) != classes_.end()) {
        return is_instance_of(value, type_name);
    }
    if (interfaces_.find(type) != interfaces_.end()) {
        return implements_interface(value, type_name);
    }
    return false;
}

bool Runtime::is_reference(const Value& value) const {
    if (!value.is_object()) {
        return false;
    }
    const auto& object = value.as_object();
    const auto found = object.find("__class");
    return found != object.end() && function_key(found->second.to_string()) == "ref";
}

Value Runtime::make_reference_to(std::string name, std::string type_name) const {
    const Value current = get_global(name);
    if (!type_name.empty() && !current.is_null() && !value_matches_type(current, type_name)) {
        throw std::runtime_error("REF target expects " + type_name);
    }
    return Value::Object{{"__class", "REF"}, {"__target", std::move(name)}, {"TypeName", std::move(type_name)}, {"Valid", true}};
}

Value Runtime::make_reference(Value value, std::string type_name) const {
    if (!type_name.empty() && !value.is_null() && !value_matches_type(value, type_name)) {
        throw std::runtime_error("REF value expects " + type_name);
    }
    return Value::Object{{"__class", "REF"}, {"Value", std::move(value)}, {"TypeName", std::move(type_name)}, {"Valid", true}};
}

Value Runtime::reference_value(const Value& reference) const {
    if (!is_reference(reference)) {
        throw std::runtime_error("value is not a reference");
    }
    const auto& object = reference.as_object();
    const auto valid = object.find("Valid");
    if (valid == object.end() || !valid->second.truthy()) {
        return {};
    }
    const auto target = object.find("__target");
    if (target != object.end() && target->second.is_string()) {
        try {
            return get_global(target->second.to_string());
        } catch (const std::exception&) {
            return {};
        }
    }
    const auto boxed = object.find("Value");
    if (boxed == object.end()) {
        return {};
    }
    return boxed->second;
}

void Runtime::set_reference_value(Value reference, Value value) {
    if (!is_reference(reference)) {
        throw std::runtime_error("value is not a reference");
    }
    auto& object = reference.as_object();
    const auto valid = object.find("Valid");
    if (valid == object.end() || !valid->second.truthy()) {
        throw std::runtime_error("reference has been cleared");
    }
    const auto target = object.find("__target");
    const auto type = object.find("TypeName");
    const std::string type_name = type != object.end() && type->second.is_string() ? type->second.to_string() : "";
    if (!type_name.empty() && !value.is_null() && !value_matches_type(value, type_name)) {
        throw std::runtime_error("REF.Value expects " + type_name);
    }
    if (target != object.end() && target->second.is_string()) {
        try {
            (void)get_global(target->second.to_string());
        } catch (const std::exception&) {
            throw std::runtime_error("reference target no longer exists: " + target->second.to_string());
        }
        set_global(target->second.to_string(), std::move(value));
        return;
    }
    object["Value"] = std::move(value);
}

void Runtime::clear_reference(Value reference) const {
    if (!is_reference(reference)) {
        throw std::runtime_error("value is not a reference");
    }
    auto& object = reference.as_object();
    object["Valid"] = false;
    object["Value"] = {};
}

void Runtime::push_class_context(std::string class_name) {
    class_contexts_.push_back(std::move(class_name));
}

void Runtime::pop_class_context() {
    if (class_contexts_.empty()) {
        throw std::runtime_error("no class context to pop");
    }
    class_contexts_.pop_back();
}

std::string Runtime::current_class_context() const {
    return class_contexts_.empty() ? "" : class_contexts_.back();
}

std::string Runtime::member_declaring_class(const std::string& runtime_class, const std::string& member, bool method) const {
    std::string current = runtime_class;
    const std::string member_key = function_key(member);
    while (!current.empty()) {
        const auto found = classes_.find(function_key(current));
        if (found == classes_.end()) {
            return "";
        }
        const auto& map = method ? found->second.methods_access : found->second.fields_access;
        if (map.find(member_key) != map.end()) {
            return found->second.name;
        }
        current = found->second.parent;
    }
    return "";
}

void Runtime::ensure_member_access(const std::string& runtime_class, const std::string& member, bool method) const {
    if (member.rfind("__", 0) == 0) {
        return;
    }
    const std::string declaring = member_declaring_class(runtime_class, member, method);
    if (declaring.empty()) {
        return;
    }
    const auto found = classes_.find(function_key(declaring));
    if (found == classes_.end()) {
        return;
    }
    const auto& map = method ? found->second.methods_access : found->second.fields_access;
    const auto access = map.find(function_key(member));
    if (access == map.end() || access->second == 0) {
        return;
    }
    const std::string context = current_class_context();
    if (function_key(context) == function_key(declaring)) {
        return;
    }
    if (access->second == 1) {
        const auto context_class = classes_.find(function_key(context));
        std::string current = context_class == classes_.end() ? "" : context;
        while (!current.empty()) {
            if (function_key(current) == function_key(declaring)) {
                return;
            }
            const auto found_class = classes_.find(function_key(current));
            if (found_class == classes_.end()) {
                break;
            }
            current = found_class->second.parent;
        }
    }
    throw std::runtime_error(std::string(access->second == 1 ? "protected " : "private ") + (method ? "method" : "field") + " access denied: " + declaring + "." + member);
}

void Runtime::ensure_field_assignment_type(const std::string& runtime_class, const std::string& field, const Value& value) const {
    if (value.is_null()) {
        return;
    }
    const std::string declaring = member_declaring_class(runtime_class, field, false);
    if (declaring.empty()) {
        return;
    }
    const auto found = classes_.find(function_key(declaring));
    if (found == classes_.end()) {
        return;
    }
    const auto type = found->second.fields_type.find(function_key(field));
    if (type == found->second.fields_type.end() || type->second.empty()) {
        return;
    }
    if (!value_matches_type(value, type->second)) {
        throw std::runtime_error(declaring + "." + field + " expects " + type->second);
    }
}

void Runtime::set_global(const std::string& name, Value value) {
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        const std::string root_name = name.substr(0, dot);
        const std::string property_path = name.substr(dot + 1);
        auto assign_property = [&](Value& root) {
            std::deque<Value> temporaries;
            Value* current = &root;
            std::size_t start = 0;
            while (true) {
                const auto next = property_path.find('.', start);
                const std::string property = property_path.substr(start, next == std::string::npos ? std::string::npos : next - start);
                if (is_reference(*current) && function_key(property) == "value") {
                    if (next == std::string::npos) {
                        set_reference_value(*current, std::move(value));
                        return true;
                    }
                    temporaries.push_back(reference_value(*current));
                    current = &temporaries.back();
                    start = next + 1;
                    continue;
                }
                if (next == std::string::npos) {
                    const std::string runtime_class = object_runtime_class(*current);
                    if (!runtime_class.empty()) {
                        ensure_member_access(runtime_class, property, false);
                        ensure_field_assignment_type(runtime_class, property, value);
                    }
                    current->set_property(property, std::move(value));
                    return true;
                }
                current = &current->as_object()[property];
                start = next + 1;
            }
        };

        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto found = scope->find(root_name);
            if (found != scope->end()) {
                assign_property(found->second);
                return;
            }
        }
        const auto found = globals_.find(root_name);
        if (found != globals_.end()) {
            assign_property(found->second);
            return;
        }
    }

    if (!scopes_.empty()) {
        scopes_.back()[name] = std::move(value);
        return;
    }
    globals_[name] = std::move(value);
}

void Runtime::set_indexed(const std::string& name, const std::vector<int>& indexes, Value value) {
    if (indexes.empty()) {
        set_global(name, std::move(value));
        return;
    }

    auto assign = [&](Value& root) {
        Value* current = &root;
        for (std::size_t i = 0; i < indexes.size(); ++i) {
            auto& array = current->as_array();
            const int index = indexes[i];
            if (index < 0 || static_cast<std::size_t>(index) >= array.size()) {
                throw std::runtime_error("array index out of range");
            }
            if (i + 1 == indexes.size()) {
                array[static_cast<std::size_t>(index)] = std::move(value);
                return;
            }
            current = &array[static_cast<std::size_t>(index)];
        }
    };

    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto found = scope->find(name);
        if (found != scope->end()) {
            assign(found->second);
            return;
        }
    }
    const auto found = globals_.find(name);
    if (found != globals_.end()) {
        assign(found->second);
        return;
    }
    throw std::runtime_error("undefined variable: " + name);
}

Value Runtime::get_global(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        const auto local = scope->find(name);
        if (local != scope->end()) {
            return local->second;
        }
    }

    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
            const auto root = scope->find(name.substr(0, dot));
            if (root != scope->end()) {
                Value value = root->second;
                std::size_t start = dot + 1;
                while (start < name.size()) {
                    const auto next = name.find('.', start);
                    const std::string property = name.substr(start, next == std::string::npos ? std::string::npos : next - start);
                    const std::string runtime_class = object_runtime_class(value);
                    if (!runtime_class.empty()) {
                        ensure_member_access(runtime_class, property, false);
                    }
                    if (is_reference(value) && function_key(property) == "value") {
                        value = reference_value(value);
                    } else {
                        value = value.get_property(property);
                    }
                    if (next == std::string::npos) {
                        return value;
                    }
                    start = next + 1;
                }
            }
        }
    }

    const auto found = globals_.find(name);
    if (found != globals_.end()) {
        return found->second;
    }

    if (dot != std::string::npos) {
        const auto root = globals_.find(name.substr(0, dot));
        if (root != globals_.end()) {
            Value value = root->second;
            std::size_t start = dot + 1;
            while (start < name.size()) {
                const auto next = name.find('.', start);
                const std::string property = name.substr(start, next == std::string::npos ? std::string::npos : next - start);
                const std::string runtime_class = object_runtime_class(value);
                if (!runtime_class.empty()) {
                    ensure_member_access(runtime_class, property, false);
                }
                if (is_reference(value) && function_key(property) == "value") {
                    value = reference_value(value);
                } else {
                    value = value.get_property(property);
                }
                if (next == std::string::npos) {
                    return value;
                }
                start = next + 1;
            }
        }
    }

    if (found == globals_.end()) {
        throw std::runtime_error("undefined variable: " + name);
    }
    return {};
}

bool Runtime::has_global(const std::string& name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
        if (scope->find(name) != scope->end()) {
            return true;
        }
    }
    return globals_.find(name) != globals_.end();
}

void Runtime::push_scope() {
    scopes_.push_back({});
}

void Runtime::pop_scope() {
    if (scopes_.empty()) {
        throw std::runtime_error("no local scope to pop");
    }
    scopes_.pop_back();
}

void Runtime::set_output(std::ostream& output) {
    output_ = &output;
}

std::ostream& Runtime::output() {
    return *output_;
}

void Runtime::set_limits(RuntimeLimits limits) {
    limits_ = limits;
}

const RuntimeLimits& Runtime::limits() const {
    return limits_;
}

void Runtime::tick() {
    instruction_count_++;
    if (limits_.instruction_limit > 0 && instruction_count_ > limits_.instruction_limit) {
        throw std::runtime_error("instruction limit exceeded");
    }
}

void Runtime::reset_instruction_count() {
    instruction_count_ = 0;
}

Value Runtime::call_host_function(const std::string& name, const std::vector<Value>& args) {
    tick();
    const auto dot = name.find('.');
    if (dot != std::string::npos) {
        const std::string class_name = name.substr(0, dot);
        const std::string method_name = name.substr(dot + 1);
        if (classes_.find(function_key(class_name)) != classes_.end()) {
            ensure_member_access(class_name, method_name, true);
        }
    }
    const auto found = host_functions_.find(function_key(name));
    if (found == host_functions_.end()) {
        throw std::runtime_error("unknown host function: " + name);
    }
    return found->second(args);
}

Value Runtime::call_method(Value receiver, const std::string& method, const std::vector<Value>& args) {
    if (!receiver.is_object()) {
        throw std::runtime_error("method call expects an object receiver");
    }
    std::string class_name = receiver.get_property("__class").to_string();
    std::vector<Value> values;
    values.reserve(args.size() + 1);
    values.push_back(receiver);
    values.insert(values.end(), args.begin(), args.end());
    while (!class_name.empty()) {
        const std::string function_name = class_name + "." + method;
        if (has_function(function_name)) {
            ensure_member_access(class_name, method, true);
            return call_host_function(function_name, values);
        }
        const auto found = classes_.find(function_key(class_name));
        if (found == classes_.end()) {
            break;
        }
        class_name = found->second.parent;
    }
    throw std::runtime_error("unknown method: " + receiver.get_property("__class").to_string() + "." + method);
}

} // namespace arco
