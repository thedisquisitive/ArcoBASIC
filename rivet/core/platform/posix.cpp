#include "rivet/platform.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unistd.h>
#include <sys/wait.h>

namespace rivet::platform {

ProcessResult run_process(const std::vector<std::string>& argv, const std::string& cwd) {
    if (argv.empty()) throw std::runtime_error("rivet: run_process called with an empty argv");

    ProcessResult result;
    auto start = std::chrono::steady_clock::now();

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        result.output = std::string("could not create pipe: ") + std::strerror(errno);
        result.duration = std::chrono::steady_clock::now() - start;
        return result;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        result.output = std::string("fork failed: ") + std::strerror(errno);
        result.duration = std::chrono::steady_clock::now() - start;
        return result;
    }

    if (pid == 0) {
        // Child: stdout+stderr both go to the write end of the pipe, matching
        // arco::process_run's own "combined output" convention (fissure/core/platform/posix.cpp
        // follows the same one) so callers don't need to reason about two separate streams.
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        if (!cwd.empty() && chdir(cwd.c_str()) != 0) {
            _exit(127);
        }

        std::vector<char*> exec_argv;
        exec_argv.reserve(argv.size() + 1);
        for (const auto& arg : argv) exec_argv.push_back(const_cast<char*>(arg.c_str()));
        exec_argv.push_back(nullptr);

        execvp(exec_argv[0], exec_argv.data());
        // execvp only returns on failure.
        _exit(127);
    }

    // Parent.
    close(pipe_fds[1]);
    std::array<char, 65536> chunk{};
    ssize_t bytes_read;
    while ((bytes_read = read(pipe_fds[0], chunk.data(), chunk.size())) > 0) {
        result.output.append(chunk.data(), static_cast<std::size_t>(bytes_read));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.ok = result.exit_code == 0;
    result.duration = std::chrono::steady_clock::now() - start;
    return result;
}

} // namespace rivet::platform
