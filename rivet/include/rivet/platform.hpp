#pragma once

// Real argv-based process execution -- deliberately a NEW, separate implementation from
// `fissure::platform::run_process` (fissure/include/fissure/platform.hpp), not a shared one. That
// one takes a single shell command *string* (fine for VCS/test-command probes); Rivet needs real
// argv (no shell involved at all) specifically to avoid shell-quoting hazards on compiler argument
// lists (include paths with spaces, etc.) and so a thread pool can run genuinely N-wide without
// serializing through one shell interpreter. Nothing above this layer (scheduler, toolchain
// detection) calls fork/exec/posix_spawn directly -- this is the one platform boundary, matching
// the discipline `fissure/include/fissure/platform.hpp` already established for its own subsystem.

#include <chrono>
#include <string>
#include <vector>

namespace rivet::platform {

struct ProcessResult {
    bool ok = false;
    int exit_code = -1;
    std::string output; // combined stdout+stderr
    std::chrono::duration<double> duration{0.0};
};

// Blocking. `argv[0]` is resolved via PATH the same way execvp does. `cwd` is the working
// directory the child runs in (empty = inherit the caller's).
ProcessResult run_process(const std::vector<std::string>& argv, const std::string& cwd = "");

} // namespace rivet::platform
