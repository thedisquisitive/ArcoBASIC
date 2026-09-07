#include "fissure/platform.hpp"

#include <array>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <unistd.h>

namespace fissure::platform {

ProcessResult run_process(const std::string& command) {
    ProcessResult result;
    auto start = std::chrono::steady_clock::now();

    const std::string captured_command = command + " 2>&1";
    FILE* pipe = popen(captured_command.c_str(), "r");
    if (!pipe) {
        result.ok = false;
        result.exit_code = -1;
        result.output = "could not start command";
        result.duration = std::chrono::steady_clock::now() - start;
        return result;
    }

    std::array<char, 4096> chunk{};
    while (fgets(chunk.data(), static_cast<int>(chunk.size()), pipe) != nullptr) {
        result.output += chunk.data();
    }

    int raw_status = pclose(pipe);
    result.exit_code = WIFEXITED(raw_status) ? WEXITSTATUS(raw_status) : -1;
    result.ok = result.exit_code == 0;
    if (!result.output.empty() && result.output.back() == '\n') result.output.pop_back();
    result.duration = std::chrono::steady_clock::now() - start;
    return result;
}

std::string current_directory() {
    std::array<char, PATH_MAX> buffer{};
    if (getcwd(buffer.data(), buffer.size()) == nullptr) {
        throw std::runtime_error("fissure: could not determine current directory");
    }
    return std::string(buffer.data());
}

} // namespace fissure::platform
