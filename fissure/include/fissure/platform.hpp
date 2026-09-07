#pragma once

// RFC section 18: "Native platform boundary examples: process launch and control... No extension
// should need to know whether Fissure is running on Linux, Windows, or another supported platform
// unless it explicitly requests platform-specific capabilities." This header is that boundary for
// process execution specifically -- core/platform/posix.cpp is the one (of potentially several,
// selected at build time) implementation. Nothing above this layer (impact engine, scheduler,
// probe runner, VM host contracts) calls popen/fork/CreateProcess directly.

#include <chrono>
#include <string>

namespace fissure::platform {

struct ProcessResult {
    bool ok = false;
    int exit_code = -1;
    std::string output; // combined stdout+stderr, matching this codebase's own arco::process_run convention
    std::chrono::duration<double> duration{0.0};
};

// Runs `command` through the platform's shell, capturing combined output and wall-clock duration.
// Blocking -- the scheduler (core/scheduler) is the layer responsible for running several of
// these concurrently, not this function.
ProcessResult run_process(const std::string& command);

// Returns the current working directory, or throws std::runtime_error on failure.
std::string current_directory();

} // namespace fissure::platform
