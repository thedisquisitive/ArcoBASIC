#include "arco/runtime.hpp"
#include "arco/runtime_handles.hpp"
#include "arco/random.hpp"
#include "arco/graphics.hpp"
#include "arco/gui.hpp"

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <iostream>
#include <iomanip>
#include <limits>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cerrno>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
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
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <io.h>
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
#elif defined(__EMSCRIPTEN__)
    // DISPLAY/WAYLAND_DISPLAY are an X11/Wayland desktop-session heuristic that means nothing in
    // a browser -- a web capsule linked with src/gui/canvas_backend.cpp always has a DOM/<canvas>
    // available (that's the only way it would have linked at all), so there's no separate
    // "headless" case to detect here the way there is for a desktop build.
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

// Only referenced from the non-Windows branches of network_resolve/tcp_connect below; the
// `sockaddr` family of types isn't declared at all on Windows since the POSIX socket headers
// are excluded there (see the #ifndef _WIN32 include block above), so this must not be compiled
// on Windows rather than merely stubbed.
#ifndef _WIN32
std::string sockaddr_address(const sockaddr* address) {
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
}
#endif

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

// Runs `command` through the platform shell and captures its combined stdout+stderr, so a plain
// capsule (not just arcosh, which has its own richer RUN with interactive-sudo/job-control
// handling arco_shell/arcosh.cpp) can shell out -- the IDE's "Run" button uses this to invoke
// ArcoFission against the file it's editing.
Value process_run(const std::string& command) {
    const std::string captured_command = command + " 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(captured_command.c_str(), "r");
#else
    FILE* pipe = popen(captured_command.c_str(), "r");
#endif
    if (!pipe) {
        return Value::Object{{"Ok", false}, {"Output", ""}, {"ExitCode", -1.0}, {"Error", "could not start command"}};
    }
    std::array<char, 4096> chunk{};
    std::string output;
    while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe) != nullptr) {
        output += chunk.data();
    }
#ifdef _WIN32
    const int code = _pclose(pipe);
#else
    const int raw_status = pclose(pipe);
    const int code = WIFEXITED(raw_status) ? WEXITSTATUS(raw_status) : -1;
#endif
    if (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }
    return Value::Object{{"Ok", code == 0}, {"Output", output}, {"ExitCode", static_cast<double>(code)}, {"Error", ""}};
}

#ifndef _WIN32
// RFC-0052 (arcosh/rfcs/RFC-0052_The_Arcology_Shell.md) WP-004: the real Linux process layer a
// hosted ArcoBASIC shell needs -- fork/execvp/waitpid with correct foreground job-control terminal
// handoff, distinct from process_run() above (which shells out via popen and captures output; no
// PATH-lookup-vs-real-exit-status distinction, no terminal control, fine for a one-shot IDE "Run"
// button, wrong for an interactive shell's own foreground commands). Behavioral reference only
// (not ported code): the retired src/shell/arcosh.cpp's run_foreground_shell_command.
Value process_execute_foreground(const std::string& executable, const std::vector<std::string>& argv_strings) {
    std::cout << std::flush;
    std::cerr << std::flush;

    // Self-pipe, write end CLOEXEC: if execvp succeeds the pipe closes on exec and this read()
    // returns 0 immediately; if it fails, the child writes its errno before _exit(127), so the
    // parent can tell "command not found/not executable" apart from the program itself legitimately
    // exiting 127 -- indistinguishable from the exit status alone, which is why every real shell
    // does this rather than just checking for a 127 exit code.
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return Value::Object{{"Ok", false}, {"Found", false}, {"ExitCode", -1.0}, {"Signaled", false},
                              {"TermSignal", 0.0}, {"Error", std::strerror(errno)}};
    }
    fcntl(pipe_fds[1], F_SETFD, FD_CLOEXEC);

    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    struct sigaction previous_int {};
    struct sigaction previous_ttou {};
    sigaction(SIGINT, &ignore, &previous_int);
    sigaction(SIGTTOU, &ignore, &previous_ttou);

    const bool interactive = isatty(STDIN_FILENO) != 0;
    const pid_t shell_pgrp = getpgrp();
    const pid_t previous_foreground_pgrp = interactive ? tcgetpgrp(STDIN_FILENO) : -1;

    const pid_t pid = fork();
    if (pid < 0) {
        const int fork_errno = errno;
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        sigaction(SIGINT, &previous_int, nullptr);
        sigaction(SIGTTOU, &previous_ttou, nullptr);
        return Value::Object{{"Ok", false}, {"Found", false}, {"ExitCode", -1.0}, {"Signaled", false},
                              {"TermSignal", 0.0}, {"Error", std::strerror(fork_errno)}};
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        setpgid(0, 0);
        struct sigaction default_action {};
        default_action.sa_handler = SIG_DFL;
        sigemptyset(&default_action.sa_mask);
        sigaction(SIGINT, &default_action, nullptr);
        sigaction(SIGQUIT, &default_action, nullptr);
        sigaction(SIGTSTP, &default_action, nullptr);
        sigaction(SIGTTIN, &default_action, nullptr);
        sigaction(SIGTTOU, &default_action, nullptr);

        std::vector<char*> argv;
        argv.reserve(argv_strings.size() + 1);
        for (const auto& value : argv_strings) argv.push_back(const_cast<char*>(value.c_str()));
        argv.push_back(nullptr);
        execvp(executable.c_str(), argv.data());

        const int exec_errno = errno;
        ssize_t written = write(pipe_fds[1], &exec_errno, sizeof(exec_errno));
        (void)written; // nothing to do if even this fails -- _exit(127) still gives a sane fallback
        _exit(127);
    }

    close(pipe_fds[1]);
    setpgid(pid, pid);
    if (interactive) tcsetpgrp(STDIN_FILENO, pid);

    int exec_errno = 0;
    const ssize_t read_count = read(pipe_fds[0], &exec_errno, sizeof(exec_errno));
    close(pipe_fds[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        // retry
    }

    if (interactive) tcsetpgrp(STDIN_FILENO, previous_foreground_pgrp >= 0 ? previous_foreground_pgrp : shell_pgrp);
    sigaction(SIGINT, &previous_int, nullptr);
    sigaction(SIGTTOU, &previous_ttou, nullptr);

    if (read_count > 0) {
        // execvp failed in the child before it ever became the intended program.
        return Value::Object{{"Ok", false}, {"Found", false}, {"ExitCode", -1.0}, {"Signaled", false},
                              {"TermSignal", 0.0}, {"Error", std::strerror(exec_errno)}};
    }

    if (WIFSIGNALED(status)) {
        return Value::Object{{"Ok", true}, {"Found", true}, {"ExitCode", -1.0}, {"Signaled", true},
                              {"TermSignal", static_cast<double>(WTERMSIG(status))}, {"Error", ""}};
    }
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return Value::Object{{"Ok", true}, {"Found", true}, {"ExitCode", static_cast<double>(exit_code)},
                          {"Signaled", false}, {"TermSignal", 0.0}, {"Error", ""}};
}

// RFC-0052 WP-011: real pipelines and redirection. A separate primitive from
// process_execute_foreground above, not a loop calling it once per stage -- every stage must be
// forked with its pipe plumbing already wired BEFORE any of them exec, and the WHOLE pipeline
// (not just its last stage) needs to be one process group for foreground job-control purposes:
// Ctrl-C during `slow | grep x` must stop `slow` too, not just `grep`. Also backs plain
// redirection with no pipe at all (`program > file`) -- a single-stage "pipeline" through the
// same open()/dup2 plumbing, rather than a second, mostly-duplicate code path.
//
// Returns one result object per stage (same {Ok, Found, ExitCode, Signaled, TermSignal, Error}
// shape Process.Execute's own single-command result already has), in order -- not one combined
// result -- so the ArcoBASIC caller can report "command not found" for whichever stage(s) failed
// to exec (any stage can, not only the first) while still classifying the pipeline's own overall
// outcome from the LAST stage's result, matching real shell semantics: `$?` reflects the last
// command in a pipeline; an earlier stage failing to exec is reported separately and does not
// change the pipeline's own exit status (`nonexistent-cmd | true` still exits 0).
Value process_execute_pipeline(const std::vector<std::pair<std::string, std::vector<std::string>>>& stages,
                                const std::string& stdin_path, const std::string& stdout_path,
                                bool append_stdout) {
    std::cout << std::flush;
    std::cerr << std::flush;

    const std::size_t stage_count = stages.size();
    const auto error_result = [](const std::string& message) {
        return Value::Object{{"Ok", false}, {"Found", false}, {"ExitCode", -1.0}, {"Signaled", false},
                              {"TermSignal", 0.0}, {"Error", message}};
    };
    if (stage_count == 0) {
        return Value(Value::Array{});
    }

    // One real pipe between each adjacent pair of stages, plus one SELF-pipe per stage (the same
    // exec-failure-detection technique process_execute_foreground uses -- write-end CLOEXEC, so a
    // successful execvp closes it and the parent's read returns 0 immediately, while a failed one
    // lets the child report its own errno before _exit(127)). One self-pipe per stage, not one
    // total, because any stage -- not only the first -- can fail to exec or fail to open a
    // redirection target.
    std::vector<std::array<int, 2>> stage_pipes(stage_count > 1 ? stage_count - 1 : 0);
    std::vector<std::array<int, 2>> self_pipes(stage_count);
    const auto close_all = [&]() {
        for (auto& fds : stage_pipes) { close(fds[0]); close(fds[1]); }
        for (auto& fds : self_pipes) { close(fds[0]); close(fds[1]); }
    };
    for (auto& fds : stage_pipes) {
        if (pipe(fds.data()) != 0) {
            const std::string error = std::strerror(errno);
            close_all();
            Value::Array results;
            for (std::size_t i = 0; i < stage_count; ++i) results.push_back(error_result(error));
            return Value(results);
        }
    }
    for (auto& fds : self_pipes) {
        if (pipe(fds.data()) != 0) {
            const std::string error = std::strerror(errno);
            close_all();
            Value::Array results;
            for (std::size_t i = 0; i < stage_count; ++i) results.push_back(error_result(error));
            return Value(results);
        }
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }

    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    struct sigaction previous_int {};
    struct sigaction previous_ttou {};
    sigaction(SIGINT, &ignore, &previous_int);
    sigaction(SIGTTOU, &ignore, &previous_ttou);

    const bool interactive = isatty(STDIN_FILENO) != 0;
    const pid_t shell_pgrp = getpgrp();
    const pid_t previous_foreground_pgrp = interactive ? tcgetpgrp(STDIN_FILENO) : -1;

    std::vector<pid_t> pids(stage_count, -1);
    pid_t pgid = 0;
    bool fork_failed = false;
    std::string fork_error;

    for (std::size_t i = 0; i < stage_count && !fork_failed; ++i) {
        const pid_t pid = fork();
        if (pid < 0) {
            fork_failed = true;
            fork_error = std::strerror(errno);
            break;
        }

        if (pid == 0) {
            // Child. Join the pipeline's own process group -- called here too, not just in the
            // parent below, so whichever of the two runs first (a real race across fork()) still
            // has the group correctly formed before anything (a signal, tcsetpgrp) depends on it.
            setpgid(0, i == 0 ? 0 : pgid);

            struct sigaction default_action {};
            default_action.sa_handler = SIG_DFL;
            sigemptyset(&default_action.sa_mask);
            sigaction(SIGINT, &default_action, nullptr);
            sigaction(SIGQUIT, &default_action, nullptr);
            sigaction(SIGTSTP, &default_action, nullptr);
            sigaction(SIGTTIN, &default_action, nullptr);
            sigaction(SIGTTOU, &default_action, nullptr);

            bool redirect_failed = false;
            int redirect_errno = 0;
            if (i == 0) {
                if (!stdin_path.empty()) {
                    const int fd = open(stdin_path.c_str(), O_RDONLY);
                    if (fd < 0) { redirect_failed = true; redirect_errno = errno; }
                    else { dup2(fd, STDIN_FILENO); close(fd); }
                }
            } else {
                dup2(stage_pipes[i - 1][0], STDIN_FILENO);
            }
            if (!redirect_failed) {
                if (i + 1 == stage_count) {
                    if (!stdout_path.empty()) {
                        const int flags = O_WRONLY | O_CREAT | (append_stdout ? O_APPEND : O_TRUNC);
                        const int fd = open(stdout_path.c_str(), flags, 0644);
                        if (fd < 0) { redirect_failed = true; redirect_errno = errno; }
                        else { dup2(fd, STDOUT_FILENO); close(fd); }
                    }
                } else {
                    dup2(stage_pipes[i][1], STDOUT_FILENO);
                }
            }

            for (auto& fds : stage_pipes) { close(fds[0]); close(fds[1]); }
            for (std::size_t k = 0; k < self_pipes.size(); ++k) {
                close(self_pipes[k][0]);
                if (k != i) close(self_pipes[k][1]);
            }

            if (redirect_failed) {
                ssize_t written = write(self_pipes[i][1], &redirect_errno, sizeof(redirect_errno));
                (void)written;
                _exit(127);
            }

            std::vector<char*> argv;
            argv.reserve(stages[i].second.size() + 2);
            argv.push_back(const_cast<char*>(stages[i].first.c_str()));
            for (const auto& value : stages[i].second) argv.push_back(const_cast<char*>(value.c_str()));
            argv.push_back(nullptr);
            execvp(stages[i].first.c_str(), argv.data());

            const int exec_errno = errno;
            ssize_t written = write(self_pipes[i][1], &exec_errno, sizeof(exec_errno));
            (void)written;
            _exit(127);
        }

        if (i == 0) pgid = pid;
        setpgid(pid, pgid);
        pids[i] = pid;
    }

    if (fork_failed) {
        for (std::size_t i = 0; i < stage_count; ++i) {
            if (pids[i] > 0) kill(pids[i], SIGKILL);
        }
        for (std::size_t i = 0; i < stage_count; ++i) {
            if (pids[i] > 0) { int status = 0; waitpid(pids[i], &status, 0); }
        }
        close_all();
        sigaction(SIGINT, &previous_int, nullptr);
        sigaction(SIGTTOU, &previous_ttou, nullptr);
        Value::Array results;
        for (std::size_t i = 0; i < stage_count; ++i) results.push_back(error_result(fork_error));
        return Value(results);
    }

    // Every child has its own copy of every fd via fork(); now that all of them have started
    // (and dup2'd whichever ones they actually need), the parent's copies are all redundant --
    // holding them open here would keep every pipe's write end alive even after the writing
    // child exits, so the reading child would never see end-of-file.
    for (auto& fds : stage_pipes) { close(fds[0]); close(fds[1]); }
    for (auto& fds : self_pipes) { close(fds[1]); }

    if (interactive) tcsetpgrp(STDIN_FILENO, pgid);

    std::vector<bool> stage_found(stage_count, true);
    std::vector<std::string> stage_error(stage_count);
    for (std::size_t i = 0; i < stage_count; ++i) {
        int stage_errno = 0;
        const ssize_t read_count = read(self_pipes[i][0], &stage_errno, sizeof(stage_errno));
        close(self_pipes[i][0]);
        if (read_count > 0) {
            stage_found[i] = false;
            stage_error[i] = std::strerror(stage_errno);
        }
    }

    std::vector<int> stage_status(stage_count, 0);
    for (std::size_t i = 0; i < stage_count; ++i) {
        while (waitpid(pids[i], &stage_status[i], 0) < 0 && errno == EINTR) {
            // retry
        }
    }

    if (interactive) tcsetpgrp(STDIN_FILENO, previous_foreground_pgrp >= 0 ? previous_foreground_pgrp : shell_pgrp);
    sigaction(SIGINT, &previous_int, nullptr);
    sigaction(SIGTTOU, &previous_ttou, nullptr);

    Value::Array results;
    results.reserve(stage_count);
    for (std::size_t i = 0; i < stage_count; ++i) {
        if (!stage_found[i]) {
            results.push_back(error_result(stage_error[i]));
            continue;
        }
        const int status = stage_status[i];
        if (WIFSIGNALED(status)) {
            results.push_back(Value::Object{{"Ok", true}, {"Found", true}, {"ExitCode", -1.0}, {"Signaled", true},
                                             {"TermSignal", static_cast<double>(WTERMSIG(status))}, {"Error", ""}});
            continue;
        }
        const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        results.push_back(Value::Object{{"Ok", true}, {"Found", true}, {"ExitCode", static_cast<double>(exit_code)},
                                         {"Signaled", false}, {"TermSignal", 0.0}, {"Error", ""}});
    }
    return Value(results);
}

// RFC-0052 WP-012: real POSIX job control -- see .agents/reports/
// ARCO_SH_RFC0052_WP012_JOB_CONTROL_RESEARCH.md for the full protocol this implements (process
// groups, controlling-terminal hand-off, WUNTRACED stop detection, SIGCONT). A job here is one
// pipeline (one or more processes sharing one process group, exactly what process_execute_pipeline
// above already builds) whose START and WAIT are DELIBERATELY separate calls -- Process.Execute/
// ExecutePipeline above always do both together and block until the whole thing exits, which is
// fundamentally incompatible with a job that can be (a) backgrounded (started, never waited
// synchronously) or (b) stopped mid-wait (Ctrl-Z) and resumed/re-waited an arbitrary number of
// prompt-loop iterations later. Keyed by pgid (the job's own process group id) -- already a unique,
// stable, kernel-assigned identifier for exactly this concept, so no separate handle/registry ID
// scheme is needed the way Random.Create/TCP clients need one for their own multiple-instances case.
struct JobRecord {
    // Still-alive, not-yet-reaped members, in PIPELINE STAGE ORDER (never reordered) -- a
    // just-detected stop leaves every member from that point on in this list untouched (still
    // alive, just not running); reaping removes finished members from the FRONT as waitpid finds
    // them, so by the time the list is empty every member has actually exited/died.
    std::vector<pid_t> pending_pids;
    std::vector<std::string> executables;
};

std::unordered_map<pid_t, JobRecord>& job_registry() {
    static std::unordered_map<pid_t, JobRecord> registry;
    return registry;
}

// Ignores SIGTTOU around a tcsetpgrp call -- the shell itself may transiently not be the
// foreground process group at the exact moment it makes this call (a real race, not a
// theoretical one: e.g. resuming a job that's about to hand the terminal right back), and
// SIGTTOU's default action would stop the shell for doing the very thing that's supposed to fix
// that. Every direct tcsetpgrp call in this file goes through this helper for that reason --
// Process.SetupShellJobControl also leaves SIGTTOU permanently ignored for the shell's own
// lifetime (see its own comment), so in practice this save/restore is close to a no-op once that
// has run, but every job-control function here stays correct even if called before it (or from a
// context that never calls it, e.g. a future non-arcosh consumer of these same primitives).
void tcsetpgrp_ignoring_sigttou(int fd, pid_t pgid) {
    if (!isatty(fd)) return;
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    struct sigaction previous{};
    sigaction(SIGTTOU, &ignore, &previous);
    tcsetpgrp(fd, pgid);
    sigaction(SIGTTOU, &previous, nullptr);
}

// One-time interactive setup (RFC-0052 research doc Section 3): claims the controlling terminal
// for the shell's own process group and leaves SIGINT/SIGQUIT/SIGTSTP/SIGTTIN/SIGTTOU ignored
// PERMANENTLY (never saved-and-restored per command the way Process.Execute/ExecutePipeline's own
// narrower, non-job-control save/restore dance does) -- a real job-control shell's own top-level
// process is never interrupted/stopped by a terminal signal for its entire interactive lifetime,
// including while it's simply sitting at the prompt blocked in Console.ReadLine with no job
// running at all. Every forked child still resets all five back to SIG_DFL before exec (see
// process_start_job below), unaffected by this.
Value process_setup_shell_job_control() {
    if (!isatty(STDIN_FILENO)) {
        // No controlling terminal to own (piped/redirected stdin -- every earlier WP's own
        // non-interactive mode, ctest included) -- nothing to seize, not an error.
        return Value::Object{{"Ok", true}, {"Error", ""}};
    }
    // APUE 9.14's own idiom: if arcosh was itself started in the BACKGROUND of another job-control
    // shell (`arcosh &`), it must wait until it's actually brought to the foreground before seizing
    // job control -- sending itself SIGTTIN (still SIG_DFL at this exact point, default action
    // Stop) is what "wait until foregrounded" means for a process that isn't reading the terminal
    // itself yet.
    pid_t shell_pgrp = getpgrp();
    while (tcgetpgrp(STDIN_FILENO) != shell_pgrp) {
        kill(-shell_pgrp, SIGTTIN);
        shell_pgrp = getpgrp();
    }
    if (setpgid(0, 0) != 0 && errno != EPERM) {
        // EPERM means this process is already a session leader and cannot change its own group --
        // true for a genuine login shell, not a real failure.
        return Value::Object{{"Ok", false}, {"Error", std::string("setpgid: ") + std::strerror(errno)}};
    }
    shell_pgrp = getpgrp();
    tcsetpgrp_ignoring_sigttou(STDIN_FILENO, shell_pgrp);
    struct sigaction ignore{};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    sigaction(SIGINT, &ignore, nullptr);
    sigaction(SIGQUIT, &ignore, nullptr);
    sigaction(SIGTSTP, &ignore, nullptr);
    sigaction(SIGTTIN, &ignore, nullptr);
    sigaction(SIGTTOU, &ignore, nullptr);
    return Value::Object{{"Ok", true}, {"Error", ""}};
}

// Starts one job's processes (identical fork/pipe/dup2/self-pipe plumbing to
// process_execute_pipeline above -- see that function's own much larger comment for the full
// design rationale) but never waits for any of them -- WAITING is process_wait_job/
// process_poll_job's job below, deliberately separate so a backgrounded job can be started now
// and waited on (or never waited on synchronously at all) an arbitrary number of prompt-loop
// iterations later.
Value process_start_job(const std::vector<std::pair<std::string, std::vector<std::string>>>& stages,
                         const std::string& stdin_path, const std::string& stdout_path,
                         bool append_stdout, bool foreground) {
    std::cout << std::flush;
    std::cerr << std::flush;

    const std::size_t stage_count = stages.size();
    const auto start_error = [](const std::string& message) {
        return Value::Object{{"Ok", false}, {"Error", message}, {"Pgid", -1.0}, {"NotFound", Value(Value::Array{})}};
    };
    if (stage_count == 0) {
        return start_error("job has no stages");
    }

    std::vector<std::array<int, 2>> stage_pipes(stage_count > 1 ? stage_count - 1 : 0);
    std::vector<std::array<int, 2>> self_pipes(stage_count);
    const auto close_all = [&]() {
        for (auto& fds : stage_pipes) { close(fds[0]); close(fds[1]); }
        for (auto& fds : self_pipes) { close(fds[0]); close(fds[1]); }
    };
    for (auto& fds : stage_pipes) {
        if (pipe(fds.data()) != 0) {
            const std::string error = std::strerror(errno);
            close_all();
            return start_error(error);
        }
    }
    for (auto& fds : self_pipes) {
        if (pipe(fds.data()) != 0) {
            const std::string error = std::strerror(errno);
            close_all();
            return start_error(error);
        }
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }

    std::vector<pid_t> pids(stage_count, -1);
    pid_t pgid = 0;
    bool fork_failed = false;
    std::string fork_error;

    for (std::size_t i = 0; i < stage_count && !fork_failed; ++i) {
        const pid_t pid = fork();
        if (pid < 0) {
            fork_failed = true;
            fork_error = std::strerror(errno);
            break;
        }

        if (pid == 0) {
            setpgid(0, i == 0 ? 0 : pgid);

            struct sigaction default_action {};
            default_action.sa_handler = SIG_DFL;
            sigemptyset(&default_action.sa_mask);
            sigaction(SIGINT, &default_action, nullptr);
            sigaction(SIGQUIT, &default_action, nullptr);
            sigaction(SIGTSTP, &default_action, nullptr);
            sigaction(SIGTTIN, &default_action, nullptr);
            sigaction(SIGTTOU, &default_action, nullptr);

            bool redirect_failed = false;
            int redirect_errno = 0;
            if (i == 0) {
                if (!stdin_path.empty()) {
                    const int fd = open(stdin_path.c_str(), O_RDONLY);
                    if (fd < 0) { redirect_failed = true; redirect_errno = errno; }
                    else { dup2(fd, STDIN_FILENO); close(fd); }
                }
            } else {
                dup2(stage_pipes[i - 1][0], STDIN_FILENO);
            }
            if (!redirect_failed) {
                if (i + 1 == stage_count) {
                    if (!stdout_path.empty()) {
                        const int flags = O_WRONLY | O_CREAT | (append_stdout ? O_APPEND : O_TRUNC);
                        const int fd = open(stdout_path.c_str(), flags, 0644);
                        if (fd < 0) { redirect_failed = true; redirect_errno = errno; }
                        else { dup2(fd, STDOUT_FILENO); close(fd); }
                    }
                } else {
                    dup2(stage_pipes[i][1], STDOUT_FILENO);
                }
            }

            for (auto& fds : stage_pipes) { close(fds[0]); close(fds[1]); }
            for (std::size_t k = 0; k < self_pipes.size(); ++k) {
                close(self_pipes[k][0]);
                if (k != i) close(self_pipes[k][1]);
            }

            if (redirect_failed) {
                ssize_t written = write(self_pipes[i][1], &redirect_errno, sizeof(redirect_errno));
                (void)written;
                _exit(127);
            }

            std::vector<char*> argv;
            argv.reserve(stages[i].second.size() + 2);
            argv.push_back(const_cast<char*>(stages[i].first.c_str()));
            for (const auto& value : stages[i].second) argv.push_back(const_cast<char*>(value.c_str()));
            argv.push_back(nullptr);
            execvp(stages[i].first.c_str(), argv.data());

            const int exec_errno = errno;
            ssize_t written = write(self_pipes[i][1], &exec_errno, sizeof(exec_errno));
            (void)written;
            _exit(127);
        }

        if (i == 0) pgid = pid;
        setpgid(pid, pgid);
        pids[i] = pid;
    }

    if (fork_failed) {
        for (std::size_t i = 0; i < stage_count; ++i) {
            if (pids[i] > 0) kill(pids[i], SIGKILL);
        }
        for (std::size_t i = 0; i < stage_count; ++i) {
            if (pids[i] > 0) { int status = 0; waitpid(pids[i], &status, 0); }
        }
        close_all();
        return start_error(fork_error);
    }

    for (auto& fds : stage_pipes) { close(fds[0]); close(fds[1]); }
    for (auto& fds : self_pipes) { close(fds[1]); }

    if (foreground) {
        tcsetpgrp_ignoring_sigttou(STDIN_FILENO, pgid);
    }

    // {Executable, Error} objects, not bare names -- the specific errno string (e.g. "Permission
    // denied" vs "No such file or directory") is what lets the ArcoBASIC caller tell a genuinely
    // missing command apart from one that exists but can't be run, the same distinction
    // ClassifyProcessResult already makes for Process.Execute's own single-command result.
    Value::Array not_found;
    for (std::size_t i = 0; i < stage_count; ++i) {
        int stage_errno = 0;
        const ssize_t read_count = read(self_pipes[i][0], &stage_errno, sizeof(stage_errno));
        close(self_pipes[i][0]);
        if (read_count > 0) {
            not_found.push_back(Value(Value::Object{{"Executable", stages[i].first}, {"Error", std::string(std::strerror(stage_errno))}}));
        }
    }

    JobRecord record;
    record.pending_pids = pids;
    for (const auto& stage : stages) record.executables.push_back(stage.first);
    job_registry()[pgid] = std::move(record);

    return Value::Object{{"Ok", true}, {"Error", ""}, {"Pgid", static_cast<double>(pgid)}, {"NotFound", Value(not_found)}};
}

// Blocking wait for a job started by process_start_job -- WUNTRACED is what turns a SIGTSTP/
// SIGTTIN-caused stop into a normal waitpid return (WIFSTOPPED) instead of leaving this call
// blocked until the job fully exits, which would make Ctrl-Z during a foreground job do nothing
// observable (the exact "faked" job control AP-0052-015 rules out). Reaps pending members in
// STAGE ORDER (never -pgid, which reaps in arbitrary/kernel-chosen order) so the job's own final
// reported status is always its LAST STAGE's, matching real shell `$?` pipeline semantics --
// identical reasoning to ExecutePipelineAndRecord's own per-stage waitpid loop, just able to stop
// partway through and resume later instead of always running to completion in one call.
Value process_wait_job(pid_t pgid, bool foreground) {
    auto found = job_registry().find(pgid);
    if (found == job_registry().end()) {
        return Value::Object{{"Ok", false}, {"Error", "unknown job"}, {"Stopped", false}, {"Done", true},
                              {"ExitCode", -1.0}, {"Signaled", false}, {"TermSignal", 0.0}};
    }
    JobRecord& record = found->second;

    bool stopped = false;
    int last_exit_code = -1;
    bool last_signaled = false;
    int last_term_signal = 0;
    std::vector<pid_t> still_pending;

    for (std::size_t i = 0; i < record.pending_pids.size(); ++i) {
        const pid_t pid = record.pending_pids[i];
        int status = 0;
        pid_t result;
        while ((result = waitpid(pid, &status, WUNTRACED)) < 0 && errno == EINTR) {
            // retry
        }
        if (result < 0) {
            // Already gone (shouldn't normally happen -- this record is the only owner of this
            // pid) -- nothing left to reap for it, just drop it.
            continue;
        }
        if (WIFSTOPPED(status)) {
            stopped = true;
            for (std::size_t j = i; j < record.pending_pids.size(); ++j) still_pending.push_back(record.pending_pids[j]);
            break;
        }
        if (WIFSIGNALED(status)) {
            last_signaled = true;
            last_term_signal = WTERMSIG(status);
            last_exit_code = -1;
        } else if (WIFEXITED(status)) {
            last_signaled = false;
            last_exit_code = WEXITSTATUS(status);
        }
    }

    const bool done = !stopped;
    if (stopped) {
        record.pending_pids = still_pending;
    } else {
        job_registry().erase(found);
    }

    if (foreground) {
        tcsetpgrp_ignoring_sigttou(STDIN_FILENO, getpgrp());
    }

    return Value::Object{{"Ok", true}, {"Error", ""}, {"Stopped", stopped}, {"Done", done},
                          {"ExitCode", static_cast<double>(last_exit_code)}, {"Signaled", last_signaled},
                          {"TermSignal", static_cast<double>(last_term_signal)}};
}

// Non-blocking (WNOHANG) equivalent of process_wait_job, for a BACKGROUND job the shell isn't
// actively waiting on -- polled once per prompt line (and inside `jobs` itself) so a background
// job finishing or stopping on its own schedule (e.g. it tried to read the terminal and got
// SIGTTIN'd) is discovered and reported "as soon as reasonably possible", never by blocking the
// shell's own prompt.
Value process_poll_job(pid_t pgid) {
    auto found = job_registry().find(pgid);
    if (found == job_registry().end()) {
        return Value::Object{{"Ok", true}, {"Changed", false}, {"Stopped", false}, {"Done", true},
                              {"ExitCode", -1.0}, {"Signaled", false}, {"TermSignal", 0.0}};
    }
    JobRecord& record = found->second;

    bool changed = false;
    bool stopped = false;
    int last_exit_code = -1;
    bool last_signaled = false;
    int last_term_signal = 0;
    std::vector<pid_t> still_pending;

    for (std::size_t i = 0; i < record.pending_pids.size(); ++i) {
        const pid_t pid = record.pending_pids[i];
        int status = 0;
        const pid_t result = waitpid(pid, &status, WNOHANG | WUNTRACED);
        if (result == 0) {
            still_pending.push_back(pid);
            continue;
        }
        if (result < 0) {
            continue;
        }
        changed = true;
        if (WIFSTOPPED(status)) {
            stopped = true;
            still_pending.push_back(pid);
            for (std::size_t j = i + 1; j < record.pending_pids.size(); ++j) still_pending.push_back(record.pending_pids[j]);
            break;
        }
        if (WIFSIGNALED(status)) {
            last_signaled = true;
            last_term_signal = WTERMSIG(status);
            last_exit_code = -1;
        } else if (WIFEXITED(status)) {
            last_signaled = false;
            last_exit_code = WEXITSTATUS(status);
        }
    }

    const bool done = still_pending.empty() && !stopped;
    if (done) {
        job_registry().erase(found);
    } else {
        record.pending_pids = still_pending;
    }

    return Value::Object{{"Ok", true}, {"Changed", changed}, {"Stopped", stopped}, {"Done", done},
                          {"ExitCode", static_cast<double>(last_exit_code)}, {"Signaled", last_signaled},
                          {"TermSignal", static_cast<double>(last_term_signal)}};
}

// Resumes a stopped (or already-running, in which case this is a harmless no-op -- SIGCONT on a
// process that was never stopped has no observable effect) job's process group -- `bg`/`fg` both
// route through this, differing only in `foreground` (whether it also reclaims the terminal).
// Never waits; the caller calls process_wait_job/process_poll_job separately if it wants to block.
Value process_continue_job(pid_t pgid, bool foreground) {
    if (foreground) {
        tcsetpgrp_ignoring_sigttou(STDIN_FILENO, pgid);
    }
    if (kill(-pgid, SIGCONT) != 0) {
        return Value::Object{{"Ok", false}, {"Error", std::strerror(errno)}};
    }
    return Value::Object{{"Ok", true}, {"Error", ""}};
}
#endif

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

const BitVector& require_bit_vector(const Value& value, const std::string& operation) {
    if (!value.is_bit_vector()) throw std::runtime_error(operation + " expects a BITVECTOR");
    return value.as_bit_vector();
}

std::size_t require_bit_index(const Value& value, std::size_t length, const std::string& operation,
                              bool allow_end = false) {
    const double number = value.as_number();
    const double maximum = static_cast<double>(length + (allow_end ? 1 : 0));
    if (!std::isfinite(number) || std::floor(number) != number || number < 0 || number >= maximum) {
        throw std::runtime_error(operation + " index " + value.to_string() +
                                 " is out of range for bit length " + std::to_string(length));
    }
    return static_cast<std::size_t>(number);
}

bool require_bit_value(const Value& value, const std::string& operation) {
    const double number = value.as_number();
    if (!std::isfinite(number) || std::floor(number) != number || (number != 0 && number != 1)) {
        throw std::runtime_error(operation + " bit value must be integral 0 or 1; received " + value.to_string());
    }
    return number == 1;
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

std::uint64_t random_safe_integer(const Value& value, const std::string& label) {
    constexpr double maximum_safe_integer = 9007199254740991.0;
    if (!value.is_number()) {
        throw std::runtime_error(label + " must be a non-negative safe integer");
    }
    const double number = value.as_number();
    if (!std::isfinite(number) || number < 0 || number > maximum_safe_integer || std::floor(number) != number) {
        throw std::runtime_error(label + " must be a non-negative safe integer");
    }
    return static_cast<std::uint64_t>(number);
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

// The millisecond-of-second component (0-999) alone -- Time.Now()'s own fixed "YYYY-MM-DD
// HH:MM:SS" format has no sub-second field to slice out (std::time_t's own resolution is whole
// seconds), so a broken-down "millisecond" prompt segment (arcosh, RFC-0052 section 20) needs
// this as its own primitive rather than reusing Time.Now()'s string.
Value time_milliseconds_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Time.Milliseconds", 0, 0);
    const auto now = std::chrono::system_clock::now();
    const auto since_epoch = now.time_since_epoch();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(since_epoch).count() % 1000;
    return static_cast<double>(millis);
}

Value sleep_function(const std::vector<Value>& args) {
    expect_arg_count(args, "Sleep", 1, 1);
    const auto milliseconds = static_cast<int>(args[0].as_number());
    if (milliseconds > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
    return {};
}

arco::graphics::Color runtime_color(const Value& value) {
    if (!value.is_object()) throw std::runtime_error("GRAPHICS color expects COLOR.RGB/RGBA");
    const auto component = [&value](const char* name) {
        const auto field = value.get_property(name);
        const auto number = field.as_number();
        if (number < 0 || number > 255) throw std::runtime_error(std::string("color component out of range: ") + name);
        return static_cast<std::uint8_t>(number);
    };
    return arco::graphics::Color::RGBA(component("R"), component("G"), component("B"), component("A"));
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

UserError::UserError(std::string message, int source_line, int source_column)
    : message_(std::move(message)), source_line_(source_line), source_column_(source_column) {}

const char* UserError::what() const noexcept {
    return message_.c_str();
}

int UserError::source_line() const noexcept {
    return source_line_;
}

int UserError::source_column() const noexcept {
    return source_column_;
}

// ArcoSH's plugin system (RFC-0052 section 19 / WP-009): ONE dedicated, process-lifetime Runtime
// used ONLY for compiling/running plugin `.abas` source, never the disposable per-call one
// Runtime.RunString already provides for ITS OWN, different purpose (re-running the resident
// numbered program from a clean slate every RUN). This split is load-bearing, not a style choice --
// see .agents/reports/ARCO_SH_RFC0052_WP009_PLUGIN_SYSTEM_RESEARCH.md for the full finding:
// Runtime::call_callable resolves a CALLABLE by name against WHICHEVER Runtime instance is doing
// the invoking, so a callable produced by RunString's own throwaway `nested` Runtime becomes a
// dangling reference ("value is not a live CALLABLE") the instant RunString returns -- a plugin's
// registered command/prompt-segment/completer callback needs to keep working long after its own
// LoadPlugin call returned, which only a genuinely persistent instance can do.
//
// A free function, NOT a static local declared directly in Runtime::Runtime()'s own body -- that
// ordering would try to construct a Runtime (to satisfy the static) WHILE THE VERY FIRST Runtime
// constructor call is still running, a self-referential deadlock the C++ runtime correctly detects
// and aborts on (std::recursive_init_error). Wrapped in a free function instead, the static is
// lazily constructed the first time this function is actually CALLED (i.e. the first time some
// lambda registered inside a constructor that already finished executing invokes it), exactly
// matching Runtime.EvalImmediate's own already-working `static Runtime session` pattern one level
// up -- the only difference is this one is reachable from more than one lambda.
Runtime& plugin_runtime() {
    static Runtime instance;
    return instance;
}

// Which plugin's own top-level code is currently executing, set by Shell.LoadPlugin immediately
// before (and cleared immediately after) running that plugin's source -- read by
// Shell.PluginRegister so every capability a plugin registers is tagged with WHO registered it,
// for `plugins`'s own enumeration (RFC-0052 section 19.2's own "GitTools: command.gitstatus, ..."
// example groups capabilities by plugin name). Empty outside of an active LoadPlugin call (a
// capability registered directly by native host code rather than a real plugin, which nothing
// does today, would simply be tagged with an empty plugin name).
std::string& current_loading_plugin() {
    static std::string name;
    return name;
}

struct PluginCapabilityEntry {
    std::string capability;
    std::string name;
    std::string plugin;
    Value value;
};

// keyed on "capability:name" -- a plugin re-registering the same (capability, name) pair (e.g.
// reloading itself) simply overwrites its own previous entry rather than accumulating duplicates.
std::unordered_map<std::string, PluginCapabilityEntry>& plugin_capability_registry() {
    static std::unordered_map<std::string, PluginCapabilityEntry> registry;
    return registry;
}

#ifndef _WIN32
// Backs Curses.EnableRawMode/DisableRawMode (the stdlib TUI toolkit's own low-level terminal-mode
// primitive -- see stdlib/curses.abas): the ORIGINAL termios settings, saved once so they can be
// restored exactly, and whether raw mode is currently active at all (so a second EnableRawMode
// call is a safe no-op rather than clobbering the saved original with an already-raw state).
// Process-wide, not per-Runtime-instance, for the same reason global_store()/plugin_runtime()
// elsewhere in this file are: there is exactly one real controlling terminal for the whole
// process, regardless of how many Runtime C++ objects exist.
struct CursesRawModeState {
    bool active = false;
    termios original{};
};
CursesRawModeState& curses_raw_mode_state() {
    static CursesRawModeState state;
    return state;
}
// Registered with std::atexit the first time raw mode is ever enabled (see Curses.EnableRawMode)
// so a program that enables raw mode and then crashes, throws uncaught, or simply forgets to call
// Curses.DisableRawMode never leaves the user's real terminal stuck in cbreak/no-echo mode after
// the process exits -- the single most user-hostile failure mode a terminal-mode-switching
// program can have.
void curses_restore_terminal_atexit() {
    auto& state = curses_raw_mode_state();
    if (state.active) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &state.original);
        state.active = false;
    }
}
#endif

Runtime::Runtime()
    : output_(&std::cout),
      default_random_(std::make_shared<Pcg32>(automatic_random_seed(), Pcg32::default_sequence)) {
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
        if (args[0].is_tuple()) {
            return static_cast<double>(args[0].as_tuple().size());
        }
        if (args[0].is_object()) {
            return static_cast<double>(args[0].as_object().size());
        }
        if (args[0].is_bit_vector()) {
            return static_cast<double>(args[0].as_bit_vector().length);
        }
        if (args[0].is_range()) {
            return static_cast<double>(args[0].as_range().length);
        }
        if (args[0].is_string()) {
            return static_cast<double>(utf8_codepoints(args[0].to_string()).size());
        }
        return static_cast<double>(args[0].to_string().size());
    });
    register_function("Range", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Range", 1, 3);
        long long start = 0;
        long long stop = 0;
        long long step = 1;
        if (args.size() == 1) {
            stop = exact_slice_integer(args[0].as_number(), "stop");
        } else {
            start = exact_slice_integer(args[0].as_number(), "start");
            stop = exact_slice_integer(args[1].as_number(), "stop");
            if (args.size() == 3) step = exact_slice_integer(args[2].as_number(), "step");
        }
        return Value(RangeValue{start, stop, step, range_length(start, stop, step)});
    });
    register_function("Bits.FromString", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.FromString", 1, 1);
        if (!args[0].is_string()) throw std::runtime_error("Bits.FromString expects a string");
        return Value(BitVector::from_string(args[0].to_string()));
    });
    register_function("Bits.ToString", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.ToString", 1, 1);
        return require_bit_vector(args[0], "Bits.ToString").string();
    });
    register_function("Bits.FromArray", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.FromArray", 1, 1);
        if (!args[0].is_array()) throw std::runtime_error("Bits.FromArray expects an array");
        std::string text;
        text.reserve(args[0].as_array().size());
        for (const auto& value : args[0].as_array()) text.push_back(require_bit_value(value, "Bits.FromArray") ? '1' : '0');
        return Value(BitVector::from_string(text));
    });
    register_function("Bits.ToArray", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.ToArray", 1, 1);
        const auto& bits = require_bit_vector(args[0], "Bits.ToArray");
        Value::Array values;
        values.reserve(bits.length);
        for (std::size_t i = 0; i < bits.length; ++i) values.emplace_back(bits.get(i) ? 1.0 : 0.0);
        return values;
    });
    register_function("Bits.Get", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.Get", 2, 2);
        const auto& bits = require_bit_vector(args[0], "Bits.Get");
        return bits.get(require_bit_index(args[1], bits.length, "Bits.Get")) ? 1.0 : 0.0;
    });
    register_function("Bits.Set", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.Set", 3, 3);
        const auto& bits = require_bit_vector(args[0], "Bits.Set");
        const auto index = require_bit_index(args[1], bits.length, "Bits.Set");
        std::string text = bits.string();
        text[index] = require_bit_value(args[2], "Bits.Set") ? '1' : '0';
        return Value(BitVector::from_string(text));
    });
    register_function("Bits.Flip", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.Flip", 2, 2);
        const auto& bits = require_bit_vector(args[0], "Bits.Flip");
        const auto index = require_bit_index(args[1], bits.length, "Bits.Flip");
        std::string text = bits.string();
        text[index] = text[index] == '0' ? '1' : '0';
        return Value(BitVector::from_string(text));
    });
    register_function("Bits.Count", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.Count", 1, 2);
        const auto& bits = require_bit_vector(args[0], "Bits.Count");
        const bool wanted = args.size() == 1 || require_bit_value(args[1], "Bits.Count");
        std::size_t count = 0;
        for (std::size_t i = 0; i < bits.length; ++i) if (bits.get(i) == wanted) ++count;
        return static_cast<double>(count);
    });
    register_function("Bits.Slice", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.Slice", 2, 3);
        const auto& bits = require_bit_vector(args[0], "Bits.Slice");
        const auto start = require_bit_index(args[1], bits.length, "Bits.Slice", true);
        std::size_t length = bits.length - start;
        if (args.size() == 3) {
            const double requested = args[2].as_number();
            if (!std::isfinite(requested) || std::floor(requested) != requested || requested < 0 ||
                requested > static_cast<double>(bits.length - start)) {
                throw std::runtime_error("Bits.Slice length is out of range");
            }
            length = static_cast<std::size_t>(requested);
        }
        return Value(BitVector::from_string(bits.string().substr(start, length)));
    });
    register_function("Bits.Replace", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.Replace", 4, 4);
        const auto& bits = require_bit_vector(args[0], "Bits.Replace");
        const auto start = require_bit_index(args[1], bits.length, "Bits.Replace", true);
        const double requested = args[2].as_number();
        if (!std::isfinite(requested) || std::floor(requested) != requested || requested < 0 ||
            requested > static_cast<double>(bits.length - start)) {
            throw std::runtime_error("Bits.Replace length is out of range");
        }
        std::string text = bits.string();
        text.replace(start, static_cast<std::size_t>(requested),
                     require_bit_vector(args[3], "Bits.Replace").string());
        return Value(BitVector::from_string(text));
    });
    register_function("Bits.Reverse", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Bits.Reverse", 1, 1);
        std::string text = require_bit_vector(args[0], "Bits.Reverse").string();
        std::reverse(text.begin(), text.end());
        return Value(BitVector::from_string(text));
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
        if (args[0].is_tuple()) return "Tuple";
        if (args[0].is_bit_vector()) return "BitVector";
        if (args[0].is_range()) return "Range";
        if (is_callable(args[0])) return "Callable";
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
    // Classic BASIC CHR$ -- returns the one-character string for a Unicode code point. Added for
    // RFC-0052 WP-008 (Display and Themes): the lexer's own string-literal escapes are limited to
    // \n/\r/\t/\"/\\/\0 (src/frontend/lexer.cpp), with no way to embed a raw ESC (0x1B) byte to
    // build an ANSI SGR sequence -- CHR(27) is the general-purpose, not shell-specific, way to get
    // one. Encodes to UTF-8 for any code point, not just ASCII, matching how every other string in
    // this runtime is represented (to_string()/is_string() throughout is plain UTF-8 std::string).
    register_function("Chr", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Chr", 1, 1);
        const double raw = args[0].as_number();
        if (!std::isfinite(raw) || std::floor(raw) != raw || raw < 0 || raw > 0x10FFFF) {
            throw std::runtime_error("Chr argument must be a valid Unicode code point");
        }
        const unsigned int code_point = static_cast<unsigned int>(raw);
        std::string encoded;
        if (code_point < 0x80) {
            encoded.push_back(static_cast<char>(code_point));
        } else if (code_point < 0x800) {
            encoded.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            encoded.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point < 0x10000) {
            encoded.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            encoded.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            encoded.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            encoded.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            encoded.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            encoded.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            encoded.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
        return encoded;
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
    register_function("Array.SortBy", [this](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Array.SortBy", 2, 3);
        if (!args[0].is_array()) throw std::runtime_error("Array.SortBy expects an array");
        if (!is_callable(args[1])) throw std::runtime_error("Array.SortBy expects a CALLABLE key");
        struct Decorated { Value key; std::size_t index; Value value; };
        std::vector<Decorated> decorated;
        decorated.reserve(args[0].as_array().size());
        for (std::size_t i = 0; i < args[0].as_array().size(); ++i) {
            Value key = call_callable(args[1], {args[0].as_array()[i]});
            if (!key.is_number() && !key.is_string()) throw std::runtime_error("Array.SortBy keys must be numbers or strings");
            if (!decorated.empty() && decorated.front().key.is_number() != key.is_number()) {
                throw std::runtime_error("Array.SortBy keys must all have the same orderable type");
            }
            decorated.push_back({std::move(key), i, args[0].as_array()[i]});
        }
        const bool descending = args.size() == 3 && args[2].truthy();
        std::stable_sort(decorated.begin(), decorated.end(), [descending](const Decorated& a, const Decorated& b) {
            const bool less = a.key.is_number() ? a.key.as_number() < b.key.as_number() : a.key.to_string() < b.key.to_string();
            const bool greater = a.key.is_number() ? a.key.as_number() > b.key.as_number() : a.key.to_string() > b.key.to_string();
            return descending ? greater : less;
        });
        Value::Array result;
        for (const auto& item : decorated) result.push_back(item.value);
        return result;
    });
    const auto extrema_by = [this](const std::vector<Value>& args, bool maximum) -> Value {
        expect_arg_count(args, maximum ? "Array.MaxBy" : "Array.MinBy", 2, 2);
        if (!args[0].is_array() || args[0].as_array().empty()) {
            throw std::runtime_error(std::string(maximum ? "Array.MaxBy" : "Array.MinBy") + " expects a non-empty array");
        }
        if (!is_callable(args[1])) throw std::runtime_error("key argument must be a CALLABLE");
        std::size_t best = 0;
        Value best_key = call_callable(args[1], {args[0].as_array()[0]});
        if (!best_key.is_number() && !best_key.is_string()) throw std::runtime_error("key must be a number or string");
        for (std::size_t i = 1; i < args[0].as_array().size(); ++i) {
            Value key = call_callable(args[1], {args[0].as_array()[i]});
            if (key.is_number() != best_key.is_number() || (!key.is_number() && !key.is_string())) {
                throw std::runtime_error("keys must all have the same orderable type");
            }
            const bool better = best_key.is_number()
                ? (maximum ? key.as_number() > best_key.as_number() : key.as_number() < best_key.as_number())
                : (maximum ? key.to_string() > best_key.to_string() : key.to_string() < best_key.to_string());
            if (better) { best = i; best_key = std::move(key); }
        }
        return args[0].as_array()[best];
    };
    register_function("Array.MinBy", [extrema_by](const std::vector<Value>& args) { return extrema_by(args, false); });
    register_function("Array.MaxBy", [extrema_by](const std::vector<Value>& args) { return extrema_by(args, true); });
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
    register_function("Process.Run", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.Run", 1, 1);
        return process_run(args[0].to_string());
    });
    // RFC-0052 WP-004: real foreground execution with job-control terminal handoff -- see
    // process_execute_foreground's own comment for how this differs from Process.Run above.
    register_function("Process.Execute", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.Execute", 2, 2);
#ifdef _WIN32
        (void)args;
        return Value::Object{{"Ok", false}, {"Found", false}, {"ExitCode", -1.0}, {"Signaled", false},
                              {"TermSignal", 0.0}, {"Error", "Process.Execute is not implemented on Windows yet"}};
#else
        const std::string executable = args[0].to_string();
        std::vector<std::string> argv_strings;
        argv_strings.push_back(executable);
        for (const auto& value : args[1].as_array()) argv_strings.push_back(value.to_string());
        return process_execute_foreground(executable, argv_strings);
#endif
    });
    // RFC-0052 WP-011: real pipelines and redirection -- see process_execute_pipeline's own much
    // larger comment for the job-control/fd-plumbing design. `stages` is an array of
    // {Executable, Arguments} objects (built by arcosh.abas's own ParseCommand -- the first
    // pipeline stage plus every PipelineStages entry after it, always at least one stage, even for
    // plain redirection with no real pipe); stdinPath/stdoutPath are "" for "no redirection here,
    // inherit the shell's own stdin/stdout" (stdinPath only meaningful for the FIRST stage,
    // stdoutPath only for the LAST -- intermediate stages are always pipe-connected to each other
    // regardless). Returns an ARRAY of per-stage results, not one combined result -- see the
    // function's own comment for why.
    register_function("Process.ExecutePipeline", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.ExecutePipeline", 4, 4);
#ifdef _WIN32
        (void)args;
        return Value(Value::Array{Value::Object{{"Ok", false}, {"Found", false}, {"ExitCode", -1.0}, {"Signaled", false},
                              {"TermSignal", 0.0}, {"Error", "Process.ExecutePipeline is not implemented on Windows yet"}}});
#else
        std::vector<std::pair<std::string, std::vector<std::string>>> stages;
        for (const auto& stage_value : args[0].as_array()) {
            const auto& stage_object = stage_value.as_object();
            const auto executable_it = stage_object.find("Executable");
            const auto arguments_it = stage_object.find("Arguments");
            std::string executable = executable_it != stage_object.end() ? executable_it->second.to_string() : std::string();
            std::vector<std::string> arguments;
            if (arguments_it != stage_object.end()) {
                for (const auto& value : arguments_it->second.as_array()) arguments.push_back(value.to_string());
            }
            stages.emplace_back(std::move(executable), std::move(arguments));
        }
        const std::string stdin_path = args[1].to_string();
        const std::string stdout_path = args[2].to_string();
        const bool append_stdout = args[3].truthy();
        return process_execute_pipeline(stages, stdin_path, stdout_path, append_stdout);
#endif
    });
    // RFC-0052 WP-012: real POSIX job control. See .agents/reports/
    // ARCO_SH_RFC0052_WP012_JOB_CONTROL_RESEARCH.md and process_setup_shell_job_control's own
    // comment. Called once, at interactive startup -- idempotent and safe to call again (or not at
    // all, e.g. non-interactively), but only needs to run once for the shell's whole lifetime.
    register_function("Process.SetupShellJobControl", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.SetupShellJobControl", 0, 0);
#ifdef _WIN32
        (void)args;
        return Value::Object{{"Ok", false}, {"Error", "job control is not implemented on Windows"}};
#else
        return process_setup_shell_job_control();
#endif
    });
    // Starts a job (one or more pipeline stages, identical shape to Process.ExecutePipeline's own
    // `stages` argument) WITHOUT waiting for it -- see process_start_job's own comment for why
    // this is a separate primitive from Process.ExecutePipeline rather than an extra flag on it.
    // `foreground` hands the new job the controlling terminal immediately; a backgrounded job
    // keeps the shell itself as the foreground group. Returns the job's pgid (the handle every
    // other Process.*Job primitive below takes) and which stage(s), if any, failed to exec.
    register_function("Process.StartJob", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.StartJob", 5, 5);
#ifdef _WIN32
        (void)args;
        return Value::Object{{"Ok", false}, {"Error", "job control is not implemented on Windows"}, {"Pgid", -1.0}, {"NotFound", Value(Value::Array{})}};
#else
        std::vector<std::pair<std::string, std::vector<std::string>>> stages;
        for (const auto& stage_value : args[0].as_array()) {
            const auto& stage_object = stage_value.as_object();
            const auto executable_it = stage_object.find("Executable");
            const auto arguments_it = stage_object.find("Arguments");
            std::string executable = executable_it != stage_object.end() ? executable_it->second.to_string() : std::string();
            std::vector<std::string> arguments;
            if (arguments_it != stage_object.end()) {
                for (const auto& value : arguments_it->second.as_array()) arguments.push_back(value.to_string());
            }
            stages.emplace_back(std::move(executable), std::move(arguments));
        }
        const std::string stdin_path = args[1].to_string();
        const std::string stdout_path = args[2].to_string();
        const bool append_stdout = args[3].truthy();
        const bool foreground = args[4].truthy();
        return process_start_job(stages, stdin_path, stdout_path, append_stdout, foreground);
#endif
    });
    // Blocking wait on a job Process.StartJob returned a pgid for -- see process_wait_job's own
    // comment. `foreground` controls whether this reclaims the terminal for the shell once the
    // job stops or finishes (never meaningful for a job that was never given the terminal in the
    // first place, but harmless either way since it only ever hands the terminal BACK to the
    // shell's own group, never away from it).
    register_function("Process.WaitJob", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.WaitJob", 2, 2);
#ifdef _WIN32
        (void)args;
        return Value::Object{{"Ok", false}, {"Error", "job control is not implemented on Windows"}, {"Stopped", false},
                              {"Done", true}, {"ExitCode", -1.0}, {"Signaled", false}, {"TermSignal", 0.0}};
#else
        const pid_t pgid = static_cast<pid_t>(args[0].as_number());
        const bool foreground = args[1].truthy();
        return process_wait_job(pgid, foreground);
#endif
    });
    // Non-blocking poll on a job -- see process_poll_job's own comment. Used for background jobs
    // so the shell can notice one finished/stopped on its own schedule without ever blocking the
    // prompt.
    register_function("Process.PollJob", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.PollJob", 1, 1);
#ifdef _WIN32
        (void)args;
        return Value::Object{{"Ok", false}, {"Changed", false}, {"Stopped", false}, {"Done", true},
                              {"ExitCode", -1.0}, {"Signaled", false}, {"TermSignal", 0.0}};
#else
        const pid_t pgid = static_cast<pid_t>(args[0].as_number());
        return process_poll_job(pgid);
#endif
    });
    // Resumes a stopped job's process group (`bg`/`fg` both use this, differing only in whether
    // `foreground` also reclaims the terminal) -- see process_continue_job's own comment. Never
    // waits; call Process.WaitJob/PollJob separately to actually block on (or poll) the result.
    register_function("Process.ContinueJob", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.ContinueJob", 2, 2);
#ifdef _WIN32
        (void)args;
        return Value::Object{{"Ok", false}, {"Error", "job control is not implemented on Windows"}};
#else
        const pid_t pgid = static_cast<pid_t>(args[0].as_number());
        const bool foreground = args[1].truthy();
        return process_continue_job(pgid, foreground);
#endif
    });
    // RFC-0052 (arcosh/rfcs/RFC-0052_The_Arcology_Shell.md) WP-006: the resident numbered-program
    // editor (LIST/RUN/NEW) needs a way to actually EXECUTE the accumulated program text -- classic
    // BASIC line numbers/GOTO already parse and run correctly through this same interpreter (see
    // Runtime::run_string, used throughout tests/unit/runtime_tests.cpp for exactly this), but
    // nothing exposed that capability to a hosted ArcoBASIC program itself. A fresh, isolated
    // Runtime per call (not `this`, not the native backend's own shared host-bridge Runtime) --
    // classic RUN starts clean, and the resident program's variables have no business leaking into
    // or out of the caller's own state.
    register_function("Runtime.RunString", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Runtime.RunString", 1, 1);
        Runtime nested;
        const RunResult run_result = nested.run_string(args[0].to_string());
        return Value::Object{{"Ok", run_result.ok}, {"Error", run_result.error},
                              {"Exited", run_result.exited}, {"ExitCode", static_cast<double>(run_result.exit_code)}};
    });
    // RFC-0052 AP-0052-009 (WP-006) also explicitly requires "immediate ArcoBASIC input where
    // supported" (section 8's own "Input Classification" -- Shell command / ArcoBASIC immediate
    // input / Numbered ArcoBASIC program line / Shell built-in are four SEPARATE categories, not
    // three) -- typing `PRINT "hello"` or `x = 5` directly at the prompt and having it run right
    // away, the same way a numbered line gets recorded rather than run. Unlike Runtime.RunString
    // just above (a FRESH Runtime every call, correct for "run the whole resident program again
    // from scratch"), immediate input needs the OPPOSITE lifetime: one variable set on one line
    // (`x = 5`) must still be visible on the NEXT line (`PRINT x`), exactly like classic BASIC
    // immediate mode / any ordinary REPL. A single function-local static Runtime, reused for the
    // life of the process, gives exactly that -- Runtime::run_string() itself never resets
    // `globals_` between calls (only its own instruction-count bookkeeping), so calling it
    // repeatedly on the SAME instance naturally accumulates state with no extra plumbing needed
    // here. arcosh owns exactly one such session (there is only ever one interactive prompt), so a
    // handle/registry (the pattern Random.Create/TCP clients use for multiple concurrent
    // instances) would be unneeded complexity for this singleton case.
    register_function("Runtime.EvalImmediate", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Runtime.EvalImmediate", 1, 1);
        static Runtime session;
        const RunResult run_result = session.run_string(args[0].to_string());
        return Value::Object{{"Ok", run_result.ok}, {"Error", run_result.error},
                              {"Exited", run_result.exited}, {"ExitCode", static_cast<double>(run_result.exit_code)}};
    });
    register_function("Process.Env", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Process.Env", 1, 1);
        const char* value = std::getenv(args[0].to_string().c_str());
        return value ? Value(value) : Value("");
    });
    // Console.ReadLine/Console.IsTTY and Path.Cwd did not exist anywhere before RFC-0052
    // (arcosh/rfcs/RFC-0052_The_Arcology_Shell.md, WP-001) -- the new arcosh is a hosted ArcoBASIC
    // program compiled by Fission rather than hand-written C++ like the retired arco_shell, so its
    // interactive loop needs a way to read stdin and query terminal capability as ordinary runtime
    // primitives. Same extraction/addition pattern as Process.Run/Process.Env/Path.* above.
    register_function("Console.ReadLine", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Console.ReadLine", 0, 0);
        std::string line;
        if (!std::getline(std::cin, line)) {
            return Value::Object{{"Ok", false}, {"Text", std::string()}};
        }
        return Value::Object{{"Ok", true}, {"Text", line}};
    });
    // PRINT always appends a newline (PrintStmt::exec in parser.cpp), so an interactive prompt
    // that must share a line with the user's typed input (e.g. "arcosh:/home/user> ") needs a
    // newline-free write. Goes through Runtime::output() like PRINT so test redirection still
    // works.
    register_function("Console.Write", [this](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Console.Write", 1, 1);
        output() << args[0].to_string();
        output().flush();
        return Value();
    });
    register_function("Console.IsTTY", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Console.IsTTY", 0, 0);
#ifdef _WIN32
        return _isatty(_fileno(stdout)) != 0;
#else
        return isatty(fileno(stdout)) != 0;
#endif
    });
    // Low-level primitives for the stdlib curses/TUI toolkit (stdlib/curses.abas): cbreak mode
    // (ICANON+ECHO off, ISIG left ON so Ctrl-C still generates a real SIGINT rather than arriving
    // as a literal 0x03 byte a TUI app would otherwise have to special-case itself -- deliberately
    // NOT full raw() mode), a single-byte read with a caller-chosen timeout (the building block
    // stdlib/curses.abas's own ArcoBASIC-level ReadKey/escape-sequence decoding is built from --
    // decoding itself lives in ArcoBASIC specifically so it can be unit-tested with synthetic byte
    // strings, no real terminal required), and a terminal-size query. Windows is a real, disclosed
    // gap for now (Curses.EnableRawMode returns Ok: FALSE there rather than silently doing
    // nothing) -- the ArcoBASIC-level API is designed platform-agnostic (nothing about it assumes
    // POSIX termios), but only the Linux/POSIX backend is implemented in this pass.
    register_function("Curses.EnableRawMode", [](const std::vector<Value>&) -> Value {
#ifdef _WIN32
        return Value::Object{{"Ok", false}, {"Error", std::string("raw mode is not implemented on this platform yet")}};
#else
        auto& state = curses_raw_mode_state();
        if (state.active) return Value::Object{{"Ok", true}, {"Error", std::string()}};
        termios original{};
        if (tcgetattr(STDIN_FILENO, &original) != 0) {
            return Value::Object{{"Ok", false}, {"Error", std::string("tcgetattr failed: ") + std::strerror(errno)}};
        }
        termios raw = original;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            return Value::Object{{"Ok", false}, {"Error", std::string("tcsetattr failed: ") + std::strerror(errno)}};
        }
        const bool first_time = !state.active;
        state.original = original;
        state.active = true;
        if (first_time) std::atexit(curses_restore_terminal_atexit);
        return Value::Object{{"Ok", true}, {"Error", std::string()}};
#endif
    });
    register_function("Curses.DisableRawMode", [](const std::vector<Value>&) -> Value {
#ifndef _WIN32
        curses_restore_terminal_atexit();
#endif
        return Value();
    });
    // `timeoutMs`: 0 polls without blocking, a positive value waits up to that many milliseconds,
    // -1 blocks indefinitely (poll()'s own convention, passed straight through) -- stdlib/
    // curses.abas's own ReadKey uses -1 for the FIRST byte of a keypress (genuinely waiting for
    // the user) and a short positive timeout for any SUBSEQUENT bytes (deciding whether a lone ESC
    // byte is a real standalone Escape keypress or the start of a longer arrow/function-key
    // sequence -- the classic terminal-input ambiguity every curses-like reader has to resolve).
    // Returns "" for a timeout, EOF, or any read error -- ReadKey treats empty the same way in
    // every case, so this primitive doesn't need to distinguish them itself.
    register_function("Curses.ReadRawByteWithTimeout", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Curses.ReadRawByteWithTimeout", 1, 1);
#ifdef _WIN32
        return Value(std::string());
#else
        const int timeout_ms = static_cast<int>(args[0].as_number());
        pollfd poll_fd{STDIN_FILENO, POLLIN, 0};
        if (poll(&poll_fd, 1, timeout_ms) <= 0) return Value(std::string());
        char byte = 0;
        if (::read(STDIN_FILENO, &byte, 1) != 1) return Value(std::string());
        return Value(std::string(1, byte));
#endif
    });
    register_function("Curses.TerminalSize", [](const std::vector<Value>&) -> Value {
#ifdef _WIN32
        return Value::Object{{"Rows", 24.0}, {"Cols", 80.0}};
#else
        winsize size{};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0 || size.ws_row == 0 || size.ws_col == 0) {
            return Value::Object{{"Rows", 24.0}, {"Cols", 80.0}};
        }
        return Value::Object{{"Rows", static_cast<double>(size.ws_row)}, {"Cols", static_cast<double>(size.ws_col)}};
#endif
    });
    register_function("Path.Cwd", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Path.Cwd", 0, 0);
        std::error_code ec;
        const auto path = std::filesystem::current_path(ec);
        return ec ? Value(std::string()) : Value(path.string());
    });
    // RFC-0052 (arcosh/rfcs/RFC-0052_The_Arcology_Shell.md) WP-002/`cd`: changing the process's
    // own working directory needs a real chdir syscall, so -- like Console.*/Path.Cwd above --
    // this one small primitive lives here even though colon-path translation itself stays in
    // ArcoBASIC (arcosh/src/arcosh.abas).
    register_function("Directory.Change", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Directory.Change", 1, 1);
        std::error_code ec;
        std::filesystem::current_path(args[0].to_string(), ec);
        return Value::Object{{"Ok", !ec}, {"Error", ec ? ec.message() : std::string()}};
    });
    // Added for RFC-0052's own Configuration section (22): a profile directory (~/.arcosh/,
    // plus themes/ and profiles/ subdirectories) needs real recursive creation, the same "mkdir
    // -p" semantics shells always need for exactly this. std::filesystem::create_directories
    // already succeeds (returns false, no error) if the directory already exists -- checked via
    // the error_code, not the bool return, so "already existed" and "just created it" both
    // report Ok here, only a genuine failure (e.g. a parent path component is a file, not a
    // directory) sets Error.
    register_function("Directory.Create", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Directory.Create", 1, 1);
        std::error_code ec;
        std::filesystem::create_directories(args[0].to_string(), ec);
        return Value::Object{{"Ok", !ec}, {"Error", ec ? ec.message() : std::string()}};
    });
    // Added for RFC-0052 Configuration section (22): enumerating saved themes/profiles (e.g.
    // "theme list") needs to see what's actually on disk. Bare entry NAMES only (matching every
    // other cross-platform directory listing convention) -- a caller that needs full paths joins
    // them with the directory itself. A missing directory is reported as Ok with zero entries,
    // not an error: an ArcoSH profile directory that's never had a theme/profile saved into it
    // yet is the normal, expected first-run state, not a fault.
    register_function("Directory.List", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Directory.List", 1, 1);
        const std::string path = args[0].to_string();
        Value::Array entries;
        if (!std::filesystem::exists(path)) {
            return Value::Object{{"Ok", true}, {"Entries", Value(entries)}, {"Error", ""}};
        }
        std::error_code ec;
        std::filesystem::directory_iterator it(path, ec);
        if (ec) {
            return Value::Object{{"Ok", false}, {"Entries", Value(entries)}, {"Error", ec.message()}};
        }
        for (const auto& entry : it) {
            entries.push_back(Value(entry.path().filename().string()));
        }
        return Value::Object{{"Ok", true}, {"Entries", Value(entries)}, {"Error", ""}};
    });
    // Added for RFC-0052 section 20's own suggested prompt segment ("hostname"). POSIX
    // gethostname() truncated/null-terminated defensively -- a real hostname is always far
    // shorter than this buffer, but a misconfigured system returning something unexpectedly long
    // must not overrun it.
    register_function("Host.Hostname", [](const std::vector<Value>&) -> Value {
#ifdef _WIN32
        return Value(std::string("localhost"));
#else
        char buffer[256];
        buffer[0] = '\0';
        if (gethostname(buffer, sizeof(buffer)) != 0) {
            return Value(std::string(""));
        }
        buffer[sizeof(buffer) - 1] = '\0';
        return Value(std::string(buffer));
#endif
    });
    // Added for RFC-0052 section 20's own suggested "user" prompt segment. Reads the REAL
    // effective OS identity via geteuid()/getpwuid() rather than trusting $USER/$LOGNAME (which
    // can be stale or absent -- e.g. under `su` without `-l`, cron, some containers) -- exactly
    // the kind of "who is this actually running as" question a sysadmin's prompt segment exists
    // to answer honestly. Falls back to the USER environment variable only if the OS lookup
    // itself fails (getpwuid() returning null is rare but not impossible -- e.g. a uid with no
    // /etc/passwd entry, common in some minimal containers), and to "" if that's unset too, the
    // same "never fails, just returns less" contract Host.Hostname() above already has.
    register_function("Host.CurrentUser", [](const std::vector<Value>&) -> Value {
#ifdef _WIN32
        const char* user = std::getenv("USERNAME");
        return Value(std::string(user ? user : ""));
#else
        if (const struct passwd* entry = getpwuid(geteuid())) {
            return Value(std::string(entry->pw_name));
        }
        const char* user = std::getenv("USER");
        if (!user) user = std::getenv("LOGNAME");
        return Value(std::string(user ? user : ""));
#endif
    });
    // `Shell.LoadPlugin(pluginName, source)` -- runs a plugin's ENTIRE source through the one
    // persistent plugin_runtime() (see its own much larger comment above for why this exact
    // instance, and no other, is what keeps a registered callable genuinely invokable afterward).
    // Tags every Shell.PluginRegister call the plugin's own top-level code makes while it runs
    // with `pluginName`, via current_loading_plugin(), so `plugins`'s own enumeration can group
    // capabilities by which plugin registered them (RFC-0052 section 19.2). Same {Ok, Error}
    // result shape as Runtime.RunString/EvalImmediate.
    register_function("Shell.LoadPlugin", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Shell.LoadPlugin", 2, 2);
        current_loading_plugin() = args[0].to_string();
        const RunResult run_result = plugin_runtime().run_string(args[1].to_string());
        current_loading_plugin().clear();
        return Value::Object{{"Ok", run_result.ok}, {"Error", run_result.error}};
    });
    // `Shell.PluginRegister(capability, name, value)` -- the primitive underneath every
    // RFC-0052-section-19.1-named wrapper (`Shell.RegisterCommand`, `Shell.RegisterPromptSegment`,
    // `Shell.RegisterTheme`, `Shell.RegisterCompleter` -- all plain ArcoBASIC functions in
    // arcosh.abas itself, matching section 19.1's own "the precise API SHALL be proven by
    // implementation before being frozen"). `value` is a CALLABLE for a callback-shaped capability
    // (command/prompt.segment/completion) or a plain OBJECT for a pure-data one (theme) -- this
    // primitive itself doesn't need to know which, since Shell.PluginInvoke is what actually
    // calls a callable, never this one.
    register_function("Shell.PluginRegister", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Shell.PluginRegister", 3, 3);
        const std::string capability = args[0].to_string();
        const std::string name = args[1].to_string();
        plugin_capability_registry()[capability + ":" + name] =
            PluginCapabilityEntry{capability, name, current_loading_plugin(), args[2]};
        return Value();
    });
    // `Shell.PluginInvoke(capability, name, arg)` -- looks up a registered CALLABLE and invokes it
    // with exactly one argument (matching this whole file's own established "bundle everything a
    // callback needs into one context object" convention -- see arcosh.abas's own
    // MakePromptContext), via plugin_runtime() specifically, never `this`/whichever Runtime
    // instance happens to be doing the invoking (see plugin_runtime()'s own comment for why that
    // distinction is load-bearing here). Throws a plain, catchable runtime_error (arcosh.abas's
    // own call sites are expected to wrap this in TRY/CATCH -- RFC-0052 section 19.3's "a broken
    // optional plugin SHOULD NOT make the shell unrecoverable" applies just as much to a plugin
    // that throws when INVOKED, e.g. rendering a prompt segment, as it does to one that fails to
    // LOAD at all) for an unregistered capability/name, exactly like calling any unknown host
    // function already does elsewhere in this file.
    register_function("Shell.PluginInvoke", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Shell.PluginInvoke", 3, 3);
        const std::string key = args[0].to_string() + ":" + args[1].to_string();
        const auto found = plugin_capability_registry().find(key);
        if (found == plugin_capability_registry().end()) {
            throw std::runtime_error("no such " + args[0].to_string() + " plugin capability: " + args[1].to_string());
        }
        return plugin_runtime().call_callable(found->second.value, {args[2]});
    });
    register_function("Shell.PluginHasCapability", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Shell.PluginHasCapability", 2, 2);
        return plugin_capability_registry().count(args[0].to_string() + ":" + args[1].to_string()) != 0;
    });
    // `Shell.PluginCapabilities()` -- backs the `plugins` command's own enumeration (RFC-0052
    // section 19.2): every currently-registered {Capability, Name, Plugin} triple, in no
    // particular order (arcosh.abas's own RunPluginsCommand sorts/groups it for display).
    // Deliberately omits the raw Value -- a callable is meaningless outside Shell.PluginInvoke,
    // and a plugin-contributed theme's own content is read back through the ordinary theme
    // commands, not this enumeration.
    register_function("Shell.PluginCapabilities", [](const std::vector<Value>&) -> Value {
        Value::Array entries;
        for (const auto& [key, entry] : plugin_capability_registry()) {
            entries.push_back(Value::Object{{"Capability", entry.capability}, {"Name", entry.name}, {"Plugin", entry.plugin}});
        }
        return entries;
    });
    // Returns a registered theme/data-shaped capability's own VALUE directly (unlike
    // PluginCapabilities' deliberately Value-less enumeration) -- e.g. `theme use gittools:neon`
    // needs the plugin-contributed theme OBJECT itself, not just proof that it exists.
    register_function("Shell.PluginCapabilityValue", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Shell.PluginCapabilityValue", 2, 2);
        const auto found = plugin_capability_registry().find(args[0].to_string() + ":" + args[1].to_string());
        return found != plugin_capability_registry().end() ? found->second.value : Value();
    });
    // Testing-only: clears every registered capability so --selftest-plugin's own separate load
    // scenarios don't see each other's leftover registrations within the one process a ctest run
    // is. Never called by arcosh's own ordinary startup/runtime path -- plugins are loaded once,
    // deliberately, and nothing in this pass reloads them mid-session.
    register_function("Shell.PluginResetForTesting", [](const std::vector<Value>&) -> Value {
        plugin_capability_registry().clear();
        return Value();
    });
    // Not meant to be called directly from ArcoBASIC source -- the AMIR builder emits a call to
    // this as Main's first instructions (see AstAmirBuilder::build in fission.cpp) so a bare
    // `Args` reference has a real local to read. Compiled functions treat every free identifier
    // as a plain local with no mechanism to fall back to a runtime global when the slot was never
    // stored into, so `Args` (set by whichever entry point actually has real argv -- arco_cli,
    // native capsules; empty by default otherwise, see execute_bytecode) would otherwise be
    // "undefined bytecode local: Args" the moment any script referenced it, same as it always was
    // outside of arcosh (the only place that ever set the Args global before this).
    register_function("Runtime.Args", [this](const std::vector<Value>&) -> Value {
        return has_global("Args") ? get_global("Args") : Value(Value::Array{});
    });
    // General script-scope-global fallback for the bytecode compiler: every FUNCTION compiles to
    // its own independent set of locals with no visibility into the enclosing script's top-level
    // variables (the same gap Args had above, but for any ordinary top-level variable, e.g.
    // arcoflow/arcoflow.abas's `app = {...}`). AstAmirBuilder::apply_script_global_scoping in
    // fission.cpp is the actual mechanism -- these two are just the read/write primitive it calls
    // through, mirroring Main's assignments here and seeding every other function's same-named
    // local from here in a synthetic prologue.
    register_function("Runtime.GetGlobal", [this](const std::vector<Value>& args) -> Value {
        if (args.empty()) throw std::runtime_error("Runtime.GetGlobal expects a name");
        const std::string name = args[0].to_string();
        return has_global(name) ? get_global(name) : Value();
    });
    register_function("Runtime.SetGlobal", [this](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("Runtime.SetGlobal expects a name and a value");
        set_global(args[0].to_string(), args[1]);
        return Value();
    });
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
    // Path.* previously existed only in arco_shell (src/shell/arcosh.cpp) -- plain std::filesystem
    // wrappers with no shell-specific dependency, so a capsule (e.g. arcoflow/arcoflow.abas,
    // which needs Path.BaseName for its window title) had no way to reach them despite File.*
    // already being universal. Same gap as Process.Run/Process.Env/GUI.TextMono earlier.
    register_function("Path.Join", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            throw std::runtime_error("Path.Join expects at least 1 argument");
        }
        std::filesystem::path path(args[0].to_string());
        for (std::size_t i = 1; i < args.size(); ++i) {
            path /= args[i].to_string();
        }
        return path.string();
    });
    register_function("Path.Home", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Path.Home", 0, 0);
        if (const char* home = std::getenv("HOME")) return Value(home);
        if (const char* user_profile = std::getenv("USERPROFILE")) return Value(user_profile);
        return Value("");
    });
    register_function("Path.BaseName", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Path.BaseName", 1, 1);
        return std::filesystem::path(args[0].to_string()).filename().string();
    });
    register_function("Path.DirName", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Path.DirName", 1, 1);
        return std::filesystem::path(args[0].to_string()).parent_path().string();
    });
    register_function("Path.Extension", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Path.Extension", 1, 1);
        return std::filesystem::path(args[0].to_string()).extension().string();
    });
    // The ArcoFlow project format (see [[project_arcoflow_ide]] in agent memory / the working
    // agreement in the IDE's own project notes): a ".arcoproj" file is a single ArcoBASIC
    // object-literal expression, not a new file format with its own parser -- reusing the
    // language's own object-literal syntax means Project.Load needs no new grammar at all, just
    // a fresh, throwaway Runtime to evaluate the expression in isolation from the caller's own
    // globals (a project file assigning e.g. `Name` must never collide with the caller's `Name`).
    // Expected shape (all fields optional, callers fall back on absence):
    //   { Name: "...", Entry: "main.abas", Files: ["main.abas", ...],
    //     Window: {Width: 1000, Height: 700}, ArcoFissionPath: "..." }
    register_function("Project.Load", [](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Project.Load", 1, 1);
        const std::string path = args[0].to_string();
        const std::string text = read_plain_file(path);
        if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
            throw std::runtime_error("Project.Load: " + path + " is empty");
        }
        Runtime loader;
        const RunResult result = loader.run_string("LET __ARCOFLOW_PROJECT__ = " + text + "\n");
        if (!result.ok) {
            throw std::runtime_error("Project.Load: " + path + ": " + result.error);
        }
        if (!loader.has_global("__ARCOFLOW_PROJECT__")) {
            throw std::runtime_error("Project.Load: " + path + " did not evaluate to a value");
        }
        return loader.get_global("__ARCOFLOW_PROJECT__");
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
    // "Exit"/"ExitProgram"/"ExitTheProgram" (and lowercase-first aliases) were previously
    // registered only by ArcoSH's own register_shell_builtins (src/shell/arcosh.cpp) -- fine for
    // the interactive shell, but that left them entirely unavailable to a native/hosted capsule
    // (confirmed directly: an ArcoFission `native` build of any script calling ExitTheProgram()
    // fails at runtime with "unknown host function: exittheprogram"). ExitSignal itself is
    // already a core Runtime type, not shell-specific, so the actual mechanism was always
    // available here -- only the ArcoBASIC-callable wrapper wasn't. Registered once, for real,
    // rather than duplicating this closure in arcosh.cpp -- that registration is left alone since
    // re-registering the same name is harmless, not because it's still needed.
    const auto exit_the_program = [](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) {
            throw std::runtime_error("Exit expects 0 or 1 arguments");
        }
        const int code = args.empty() ? 0 : static_cast<int>(args[0].as_number());
        throw ExitSignal(code);
    };
    register_function("Exit", exit_the_program);
    register_function("exit", exit_the_program);
    register_function("ExitProgram", exit_the_program);
    register_function("exitProgram", exit_the_program);
    register_function("ExitTheProgram", exit_the_program);
    register_function("exitTheProgram", exit_the_program);
    const auto random_from_value = [this](const Value* value, const std::string& function) -> std::shared_ptr<Pcg32> {
        if (value == nullptr || value->is_null()) {
            return default_random_;
        }
        if (!value->is_handle() || !object_handles_.valid(value->as_handle(), "RANDOM")) {
            throw std::runtime_error(function + " received an invalid or destroyed RANDOM handle");
        }
        return std::static_pointer_cast<Pcg32>(object_handles_.object(value->as_handle(), "RANDOM"));
    };
    register_function("Random.Create", [this](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Create", 0, 2);
        const std::uint64_t seed = args.empty() || args[0].is_null()
            ? automatic_random_seed()
            : random_safe_integer(args[0], "Random.Create seed");
        const std::uint64_t sequence = args.size() < 2
            ? Pcg32::default_sequence
            : random_safe_integer(args[1], "Random.Create sequence");
        auto generator = std::make_shared<Pcg32>(seed, sequence);
        const auto handle = object_handles_.create("RANDOM", generator);
        if (!resources_.register_resource(ResourceRecord{handle, "RANDOM", "Execution Context", "Hosted PCG32 Provider",
                                                          ResourceLifetime::Explicit, ResourceLifecycle::Ready, {"Hosted Runtime"}})) {
            (void)object_handles_.destroy(handle);
            throw std::runtime_error("Random.Create resource registration failed");
        }
        return Value(handle);
    });
    register_function("Random.Clone", [this, random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Clone", 1, 1);
        if (args[0].is_null()) {
            throw std::runtime_error("Random.Clone requires an explicit RANDOM handle");
        }
        auto generator = random_from_value(&args[0], "Random.Clone");
        auto clone = std::make_shared<Pcg32>(*generator);
        const auto handle = object_handles_.create("RANDOM", clone);
        if (!resources_.register_resource(ResourceRecord{handle, "RANDOM", "Execution Context", "Hosted PCG32 Provider",
                                                          ResourceLifetime::Explicit, ResourceLifecycle::Ready, {"Hosted Runtime"}})) {
            (void)object_handles_.destroy(handle);
            throw std::runtime_error("Random.Clone resource registration failed");
        }
        return Value(handle);
    });
    register_function("Random.Destroy", [this](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Destroy", 1, 1);
        if (!args[0].is_handle() || !object_handles_.valid(args[0].as_handle(), "RANDOM")) {
            throw std::runtime_error("Random.Destroy received an invalid or destroyed RANDOM handle");
        }
        const RuntimeHandle handle = args[0].as_handle();
        if (!object_handles_.destroy(handle)) {
            throw std::runtime_error("Random.Destroy received an invalid or destroyed RANDOM handle");
        }
        (void)resources_.unregister_resource(handle);
        return true;
    });
    register_function("Random.Reseed", [random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Reseed", 2, 3);
        if (args[0].is_null()) {
            throw std::runtime_error("Random.Reseed requires an explicit RANDOM handle");
        }
        auto generator = random_from_value(&args[0], "Random.Reseed");
        const std::uint64_t seed = random_safe_integer(args[1], "Random.Reseed seed");
        const std::uint64_t sequence = args.size() < 3
            ? Pcg32::default_sequence
            : random_safe_integer(args[2], "Random.Reseed sequence");
        generator->reseed(seed, sequence);
        return true;
    });
    register_function("Random.Float", [random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Float", 0, 1);
        return random_from_value(args.empty() ? nullptr : &args[0], "Random.Float")->unit_interval();
    });
    register_function("Math.Random", [random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Math.Random", 0, 0);
        return random_from_value(nullptr, "Math.Random")->unit_interval();
    });
    register_function("Random.Integer", [random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Integer", 2, 3);
        const std::uint64_t minimum = random_safe_integer(args[0], "Random.Integer minimum");
        const std::uint64_t maximum = random_safe_integer(args[1], "Random.Integer maximum");
        if (minimum > maximum) {
            throw std::runtime_error("Random.Integer requires minimum <= maximum");
        }
        const std::uint64_t width = maximum - minimum + 1U;
        if (width > (UINT64_C(1) << 32U)) {
            throw std::runtime_error("Random.Integer range size must not exceed 2^32");
        }
        auto generator = random_from_value(args.size() < 3 ? nullptr : &args[2], "Random.Integer");
        return static_cast<double>(minimum + generator->bounded(width));
    });
    register_function("Random.Choice", [random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Choice", 1, 2);
        if (!args[0].is_array() || args[0].as_array().empty()) {
            throw std::runtime_error("Random.Choice requires a non-empty array");
        }
        const auto& values = args[0].as_array();
        if (values.size() > (UINT64_C(1) << 32U)) {
            throw std::runtime_error("Random.Choice source length must not exceed 2^32");
        }
        auto generator = random_from_value(args.size() < 2 ? nullptr : &args[1], "Random.Choice");
        return values[generator->bounded(values.size())];
    });
    register_function("Random.Sample", [random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Sample", 2, 3);
        if (!args[0].is_array()) {
            throw std::runtime_error("Random.Sample source must be an array");
        }
        Value::Array values = args[0].as_array();
        if (values.size() > (UINT64_C(1) << 32U)) {
            throw std::runtime_error("Random.Sample source length must not exceed 2^32");
        }
        const std::uint64_t count = random_safe_integer(args[1], "Random.Sample count");
        if (count > values.size()) {
            throw std::runtime_error("Random.Sample count must be between 0 and the source length");
        }
        auto generator = random_from_value(args.size() < 3 ? nullptr : &args[2], "Random.Sample");
        for (std::size_t index = 0; index < static_cast<std::size_t>(count); ++index) {
            const std::size_t selected = index + generator->bounded(values.size() - index);
            std::swap(values[index], values[selected]);
        }
        values.resize(static_cast<std::size_t>(count));
        return values;
    });
    register_function("Random.Shuffle", [random_from_value](const std::vector<Value>& args) -> Value {
        expect_arg_count(args, "Random.Shuffle", 1, 2);
        if (!args[0].is_array()) {
            throw std::runtime_error("Random.Shuffle source must be an array");
        }
        Value::Array values = args[0].as_array();
        if (values.size() > (UINT64_C(1) << 32U)) {
            throw std::runtime_error("Random.Shuffle source length must not exceed 2^32");
        }
        auto generator = random_from_value(args.size() < 2 ? nullptr : &args[1], "Random.Shuffle");
        for (std::size_t remaining = values.size(); remaining > 1; --remaining) {
            const std::size_t selected = generator->bounded(remaining);
            std::swap(values[remaining - 1], values[selected]);
        }
        return values;
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
    register_function("Time.Milliseconds", time_milliseconds_function);
    register_function("DATE", time_now_function);
    register_function("Date", time_now_function);
    register_function("Sleep", sleep_function);
    register_function("COLOR.RGB", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("COLOR.RGB expects red, green, and blue");
        return Value::Object{{"__color", true}, {"R", args[0]}, {"G", args[1]}, {"B", args[2]}, {"A", 255}};
    });
    register_function("COLOR.RGBA", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 4) throw std::runtime_error("COLOR.RGBA expects red, green, blue, and alpha");
        return Value::Object{{"__color", true}, {"R", args[0]}, {"G", args[1]}, {"B", args[2]}, {"A", args[3]}};
    });
    register_function("COLOR.Black", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("COLOR.Black expects no arguments");
        return Value::Object{{"__color", true}, {"R", 0}, {"G", 0}, {"B", 0}, {"A", 255}};
    });
    register_function("COLOR.White", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("COLOR.White expects no arguments");
        return Value::Object{{"__color", true}, {"R", 255}, {"G", 255}, {"B", 255}, {"A", 255}};
    });
    register_function("COLOR.Magenta", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("COLOR.Magenta expects no arguments");
        return Value::Object{{"__color", true}, {"R", 255}, {"G", 0}, {"B", 255}, {"A", 255}};
    });
    const auto surface_from_handle = [this](const Value& value) -> std::shared_ptr<graphics::Surface> {
        if (!value.is_handle() || !object_handles_.valid(value.as_handle(), "SURFACE")) {
            throw std::runtime_error("invalid SURFACE handle");
        }
        return std::static_pointer_cast<graphics::Surface>(object_handles_.object(value.as_handle(), "SURFACE"));
    };
    register_function("GRAPHICS.CreateSurface", [this](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || args.size() > 3) throw std::runtime_error("GRAPHICS.CreateSurface expects width, height, and optional pixel format");
        const auto width = static_cast<std::uint32_t>(args[0].as_number());
        const auto height = static_cast<std::uint32_t>(args[1].as_number());
        const auto format = static_cast<graphics::PixelFormat>(args.size() == 3 ? static_cast<std::uint32_t>(args[2].as_number()) : 0);
        auto created = graphics::CreateSurface(width, height, format);
        if (!created) throw std::runtime_error("GRAPHICS.CreateSurface failed");
        auto surface = std::make_shared<graphics::Surface>(std::move(created.surface));
        const auto handle = object_handles_.create("SURFACE", surface);
        if (!resources_.register_resource(ResourceRecord{handle, "SURFACE", "Execution Context", "Software Graphics Provider",
                                                          ResourceLifetime::Explicit, ResourceLifecycle::Ready, {"Graphics Provider"}})) {
            (void)object_handles_.destroy(handle);
            throw std::runtime_error("GRAPHICS.CreateSurface resource registration failed");
        }
        return Value(handle);
    });
    register_function("GRAPHICS.PrimarySurface", [this](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GRAPHICS.PrimarySurface expects no arguments");
        if (!primary_surface_handle_) {
            // Hosted bootstrap backend: the physical display adapter supplies this surface on
            // UEFI. Keeping it in the runtime table makes ownership and binding semantics equal.
            auto created = graphics::CreateSurface(800, 600, graphics::PixelFormat::RedGreenBlueReserved8);
            if (!created) throw std::runtime_error("GRAPHICS.PrimarySurface unavailable");
            auto surface = std::make_shared<graphics::Surface>(std::move(created.surface));
            primary_surface_handle_ = object_handles_.create("SURFACE", surface, false);
            if (!resources_.register_resource(ResourceRecord{*primary_surface_handle_, "SURFACE", "Graphics Runtime", "Software Graphics Provider",
                                                              ResourceLifetime::RuntimeOwned, ResourceLifecycle::Ready, {"Graphics Provider", "Primary Display Backend"}})) {
                primary_surface_handle_.reset();
                throw std::runtime_error("GRAPHICS.PrimarySurface resource registration failed");
            }
        }
        return Value(*primary_surface_handle_);
    });
    register_function("GRAPHICS.DestroySurface", [this, surface_from_handle](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GRAPHICS.DestroySurface expects a SURFACE");
        if (args[0].is_null()) return {};
        (void)surface_from_handle(args[0]);
        if (!object_handles_.valid(args[0].as_handle(), "SURFACE")) throw std::runtime_error("invalid SURFACE handle");
        if (!object_handles_.destroy(args[0].as_handle())) {
            if (!object_handles_.destroyable(args[0].as_handle())) return false;
            throw std::runtime_error("invalid SURFACE handle");
        }
        (void)resources_.unregister_resource(args[0].as_handle());
        return true;
    });
    register_function("GRAPHICS.Bind", [surface_from_handle](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GRAPHICS.Bind expects a SURFACE");
        if (args[0].is_null()) { graphics::Unbind(); return {}; }
        if (!graphics::Bind(*surface_from_handle(args[0]))) throw std::runtime_error("cannot bind invalid SURFACE");
        return {};
    });
    register_function("GRAPHICS.PushSurface", [surface_from_handle](const std::vector<Value>& args) -> Value {
        if (args.size() != 1) throw std::runtime_error("GRAPHICS.PushSurface expects a SURFACE");
        if (!graphics::PushSurface(*surface_from_handle(args[0]))) throw std::runtime_error("cannot push invalid SURFACE");
        return {};
    });
    register_function("GRAPHICS.PopSurface", [](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("GRAPHICS.PopSurface expects no arguments");
        if (!graphics::PopSurface()) throw std::runtime_error("GRAPHICS surface binding stack is empty");
        return {};
    });
    register_function("GRAPHICS.Clear", [](const std::vector<Value>& args) -> Value {
        if (args.size() > 1) throw std::runtime_error("GRAPHICS.Clear expects an optional color");
        const auto color = args.empty() ? graphics::Color::Black() : runtime_color(args[0]);
        auto* surface = graphics::CurrentSurface();
        if (surface == nullptr) throw std::runtime_error("GRAPHICS.Clear has no bound SURFACE");
        graphics::FillRect(*surface, 0, 0, static_cast<std::int32_t>(surface->Width), static_cast<std::int32_t>(surface->Height), color);
        return {};
    });
    register_function("GRAPHICS.FillRect", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 5) throw std::runtime_error("GRAPHICS.FillRect expects x, y, width, height, and color");
        auto* surface = graphics::CurrentSurface();
        if (surface == nullptr) throw std::runtime_error("GRAPHICS.FillRect has no bound SURFACE");
        graphics::FillRect(*surface, static_cast<std::int32_t>(args[0].as_number()), static_cast<std::int32_t>(args[1].as_number()),
                           static_cast<std::int32_t>(args[2].as_number()), static_cast<std::int32_t>(args[3].as_number()), runtime_color(args[4]));
        return {};
    });
    register_function("GRAPHICS.DrawText", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 4) throw std::runtime_error("GRAPHICS.DrawText expects x, y, text, and color");
        auto* surface = graphics::CurrentSurface();
        if (surface == nullptr) throw std::runtime_error("GRAPHICS.DrawText has no bound SURFACE");
        graphics::DrawText(*surface, static_cast<std::int32_t>(args[0].as_number()), static_cast<std::int32_t>(args[1].as_number()),
                           args[2].to_string(), runtime_color(args[3]));
        return {};
    });
    register_function("RESOURCE.Describe", [this](const std::vector<Value>& args) -> Value {
        if (!args.empty()) throw std::runtime_error("RESOURCE.Describe expects no arguments");
        return resources_.describe();
    });
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
    register_function("GUI.SetScale", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 2) throw std::runtime_error("GUI.SetScale expects window and factor");
        gui::set_scale(static_cast<int>(args[0].as_number()), args[1].as_number());
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
    register_function("GUI.Clear3D", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 16) {
            throw std::runtime_error("GUI.Clear3D expects window, eyeX, eyeY, eyeZ, targetX, targetY, targetZ, "
                                      "upX, upY, upZ, fovYDegrees, nearPlane, farPlane, backgroundRed, backgroundGreen, backgroundBlue");
        }
        gui::clear_3d(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(),
                      args[4].as_number(), args[5].as_number(), args[6].as_number(), args[7].as_number(), args[8].as_number(),
                      args[9].as_number(), args[10].as_number(), args[11].as_number(), args[12].as_number(),
                      args[13].as_number(), args[14].as_number(), args[15].as_number());
        return {};
    });
    register_function("GUI.Triangle3D", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 13 || args.size() > 14) {
            throw std::runtime_error("GUI.Triangle3D expects window, x1,y1,z1, x2,y2,z2, x3,y3,z3, red, green, blue, and optional alpha");
        }
        gui::triangle_3d(static_cast<int>(args[0].as_number()), args[1].as_number(), args[2].as_number(), args[3].as_number(),
                         args[4].as_number(), args[5].as_number(), args[6].as_number(), args[7].as_number(), args[8].as_number(),
                         args[9].as_number(), args[10].as_number(), args[11].as_number(), args[12].as_number(),
                         args.size() == 14 ? args[13].as_number() : 1.0);
        return {};
    });
    register_function("GUI.Text", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 8 || args.size() > 9) throw std::runtime_error("GUI.Text expects window, text, x, y, size, red, green, blue, and optional alpha");
        gui::text(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number(), args[3].as_number(), args[4].as_number(),
                  args[5].as_number(), args[6].as_number(), args[7].as_number(), args.size() == 9 ? args[8].as_number() : 1.0);
        return {};
    });
    register_function("GUI.TextMono", [](const std::vector<Value>& args) -> Value {
        if (args.size() < 8 || args.size() > 9) throw std::runtime_error("GUI.TextMono expects window, text, x, y, size, red, green, blue, and optional alpha");
        gui::text_mono(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number(), args[3].as_number(), args[4].as_number(),
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
    register_function("GUI.MeasureTextMono", [](const std::vector<Value>& args) -> Value {
        if (args.size() != 3) throw std::runtime_error("GUI.MeasureTextMono expects window, text, and size");
        return gui::measure_text_mono(static_cast<int>(args[0].as_number()), args[1].to_string(), args[2].as_number());
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
    std::size_t source_line = 0;

    while (std::getline(input, line)) {
        ++source_line;
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
            // Under a systems profile (arcology-os/docs/systems/uefi-target.md section 2), #TARGET's
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
        } else if (directive == "INSTRUCTION_LIMIT") {
            constexpr std::uint64_t maximum_safe_integer = 9007199254740991ULL;
            if (metadata_.instruction_limit.has_value()) {
                throw std::runtime_error("duplicate #INSTRUCTION_LIMIT directive at line " +
                                         std::to_string(source_line));
            }
            if (args.empty() || !std::all_of(args.begin(), args.end(), [](unsigned char c) {
                    return std::isdigit(c) != 0;
                })) {
                throw std::runtime_error(
                    "#INSTRUCTION_LIMIT expects a decimal integer from 1 through 9007199254740991 at line " +
                    std::to_string(source_line));
            }
            std::uint64_t requested = 0;
            try {
                requested = std::stoull(args);
            } catch (const std::exception&) {
                throw std::runtime_error(
                    "#INSTRUCTION_LIMIT expects a decimal integer from 1 through 9007199254740991 at line " +
                    std::to_string(source_line));
            }
            if (requested == 0 || requested > maximum_safe_integer) {
                throw std::runtime_error(
                    "#INSTRUCTION_LIMIT expects a decimal integer from 1 through 9007199254740991 at line " +
                    std::to_string(source_line));
            }
            metadata_.instruction_limit = requested;
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
    if (metadata_.runtime_mode == "NONE" && metadata_.instruction_limit.has_value()) {
        throw std::runtime_error("#INSTRUCTION_LIMIT is available only for hosted execution");
    }

    return output.str();
}

RunResult Runtime::run_string(const std::string& code) {
    std::string processed;
    try {
        reset_instruction_count();
        processed = preprocess_source(code);
        prepare_execution(metadata_.instruction_limit);
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
                const auto* user_error = dynamic_cast<const UserError*>(&error);
                const int line = user_error && user_error->source_line() > 0 ? user_error->source_line() : statements[pc]->source_line;
                const int column = user_error && user_error->source_column() > 0 ? user_error->source_column() : statements[pc]->source_column;
                throw std::runtime_error(format_runtime_diagnostic(error.what(), processed, line, column));
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
    if (type == "bitvector") {
        return value.is_bit_vector();
    }
    if (type == "tuple") {
        return value.is_tuple();
    }
    if (type == "range") {
        return value.is_range();
    }
    if (type == "object") {
        return value.is_object();
    }
    if (type == "ref" || type == "reference") {
        return is_reference(value);
    }
    // Runtime Object Handles are opaque, identity-bearing values. NULL is the invalid value for
    // every handle type and is accepted by typed APIs; non-null values must carry the exact type.
    if (type == "surface" || type == "window" || type == "image" || type == "font" ||
        type == "file" || type == "directory" || type == "timer" || type == "thread" ||
        type == "mutex" || type == "socket" || type == "random" || type == "callable") {
        return value.is_null() || (value.is_handle() && function_key(value.as_handle().type) == type);
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

    // Plain (non-dotted) name: ALWAYS shadow into the innermost active scope (or globals_ if none
    // is active), matching this interpreter's real, deliberate semantics -- a bare assignment
    // inside a FUNCTION always creates a function-local, even if a same-named variable happens to
    // exist at script scope or in an unrelated outer call frame. This is a genuine language design
    // choice (BASIC here has no explicit "declare a new local" syntax distinct from plain
    // assignment, so "local by default" -- the same default Python's own bare assignment uses,
    // requiring an explicit `global` statement to opt OUT of it -- is what keeps two functions'
    // same-named local variables from ever silently colliding).
    //
    // A previous version of this function tried to make plain-name writes search outward and
    // mutate an existing same-named binding wherever found (to let a function update a
    // script-level counter across separate calls), mirroring the dotted-path branch above. That
    // broke three real, independent test suites (arco_runtime_tests, arcosh_alpha_smoke,
    // arcology_commons_unit_tests) that all use a generic local variable name (e.g. "restored")
    // inside more than one function -- under that change, the SECOND function's own local
    // assignment silently found and overwrote the FIRST function's already-existing local of the
    // same name instead of creating its own, isolated one. Confirmed via the full regression suite
    // (ctest), not assumed: reverted rather than kept as a "mostly works" tradeoff. The real,
    // narrower need that prompted the attempt (a function persisting a counter across calls) has
    // a correct, already-existing, EXPLICIT mechanism instead: a CLASS SHARED field, which goes
    // through the dotted-path branch above precisely because it's spelled as `ClassName.Field`,
    // an unambiguous opt-in a bare identifier can never be.
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

Value Runtime::get_member(const Value& target, const std::string& property) const {
    const std::string runtime_class = object_runtime_class(target);
    if (!runtime_class.empty()) {
        ensure_member_access(runtime_class, property, false);
    }
    if (is_reference(target) && function_key(property) == "value") {
        return reference_value(target);
    }
    return target.get_property(property);
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
                    value = get_member(value, property);
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
                value = get_member(value, property);
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
    effective_limits_ = limits;
}

const RuntimeLimits& Runtime::limits() const {
    return limits_;
}

void Runtime::set_instruction_limit_policy(bool allow_source_requests,
                                           std::optional<std::size_t> hard_maximum) {
    allow_source_instruction_limits_ = allow_source_requests;
    instruction_limit_hard_maximum_ = hard_maximum;
}

void Runtime::set_instruction_limit_override(std::optional<std::size_t> limit) {
    instruction_limit_override_ = limit;
}

void Runtime::prepare_execution(std::optional<std::uint64_t> source_request) {
    if (source_request.has_value() && !allow_source_instruction_limits_) {
        throw std::runtime_error("source #INSTRUCTION_LIMIT requests are not authorized by this host");
    }

    std::size_t selected = limits_.instruction_limit;
    if (source_request.has_value()) {
        if (*source_request > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
            throw std::runtime_error("requested instruction limit is not representable by this host");
        }
        selected = static_cast<std::size_t>(*source_request);
    }
    if (instruction_limit_override_.has_value()) {
        selected = *instruction_limit_override_;
    }

    if (instruction_limit_hard_maximum_.has_value() &&
        (selected == 0 || selected > *instruction_limit_hard_maximum_)) {
        throw std::runtime_error(
            "requested instruction limit " + (selected == 0 ? std::string("unlimited") : std::to_string(selected)) +
            " exceeds host maximum " + std::to_string(*instruction_limit_hard_maximum_));
    }
    effective_limits_.instruction_limit = selected;
    reset_instruction_count();
}

void Runtime::tick() {
    instruction_count_++;
    if (effective_limits_.instruction_limit > 0 && instruction_count_ > effective_limits_.instruction_limit) {
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

Value Runtime::call_host_function_prepared(const std::string& lowered_key, const std::vector<Value>& args) {
    tick();
    const auto found = host_functions_.find(lowered_key);
    if (found == host_functions_.end()) {
        throw std::runtime_error("unknown host function: " + lowered_key);
    }
    return found->second(args);
}

Value Runtime::make_callable(const std::string& name, std::optional<Value> receiver, bool require_registered) {
    if (receiver.has_value()) {
        if (!receiver->is_object()) throw std::runtime_error("ADDRESSOF bound receiver must be a class instance");
        if (require_registered) {
            const auto dot = name.rfind('.');
            const std::string method = dot == std::string::npos ? name : name.substr(dot + 1);
            std::string class_name = receiver->get_property("__class").to_string();
            bool resolved = false;
            while (!class_name.empty()) {
                if (has_function(class_name + "." + method)) {
                    ensure_member_access(class_name, method, true);
                    resolved = true;
                    break;
                }
                const auto found = classes_.find(function_key(class_name));
                if (found == classes_.end()) break;
                class_name = found->second.parent;
            }
            if (!resolved) throw std::runtime_error("ADDRESSOF cannot resolve callable: " + name);
        }
    } else if (require_registered && !has_function(name)) {
        throw std::runtime_error("ADDRESSOF cannot resolve callable: " + name);
    }
    auto descriptor = std::make_shared<CallableDescriptor>();
    descriptor->name = name;
    descriptor->receiver = std::move(receiver);
    return Value(object_handles_.create("CALLABLE", std::move(descriptor), false));
}

bool Runtime::is_callable(const Value& value) const {
    return value.is_handle() && value.as_handle().type == "CALLABLE" &&
           object_handles_.valid(value.as_handle(), "CALLABLE");
}

CallableDescriptor Runtime::callable_descriptor(const Value& value) const {
    if (!is_callable(value)) throw std::runtime_error("value is not a live CALLABLE");
    const auto descriptor = std::static_pointer_cast<CallableDescriptor>(
        object_handles_.object(value.as_handle(), "CALLABLE"));
    if (!descriptor) throw std::runtime_error("value is not a live CALLABLE");
    return *descriptor;
}

Value Runtime::call_callable(const Value& callable, const std::vector<Value>& args) {
    const CallableDescriptor descriptor = callable_descriptor(callable);
    if (descriptor.receiver.has_value()) {
        const auto dot = descriptor.name.rfind('.');
        const std::string method = dot == std::string::npos ? descriptor.name : descriptor.name.substr(dot + 1);
        return call_method(*descriptor.receiver, method, args);
    }
    return call_host_function(descriptor.name, args);
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
