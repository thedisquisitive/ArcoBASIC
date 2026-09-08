#pragma once

// Minimal, real toolchain detection (RFC section 20) -- deliberately not a pluggable adapter
// registry yet (that's M5, see RIVET_PROGRESS.md's disclosed-gaps list). This slice only needs
// "find *a* working C++ compiler."

#include <string>
#include <vector>

namespace rivet {

struct CxxToolchain {
    bool found = false;
    std::string path;    // resolved absolute path (from PATH lookup)
    std::string family;  // "clang", "gcc", or "unknown"
    std::string version; // first line of `<path> --version`
};

// Tries clang++, g++, c++ on PATH, in that order.
CxxToolchain detect_cxx();

struct FissionToolchain {
    bool found = false;
    std::string path;
    std::string version; // first line of `<path> reveal --stage AST` usage banner (ArcoFission
                          // has no `--version` flag -- its own bare-invocation usage text is the
                          // closest thing, and it's already what apps/arco/main.cpp-adjacent
                          // tooling greps for elsewhere in this repo)
};

// RFC section 38 (Fission Integration). Checks the ARCOFISSION_PATH environment variable first --
// the same convention every ArcoBASIC-embedding tool in this repo already follows (arcade/
// build.sh, fissure/adapters/vcs/git.ab indirectly via FISSURE.Process.Exec, and this session's
// own real incident: a bare PATH lookup once silently resolved to a stale system-wide
// /usr/bin/ArcoFission instead of this repo's own actively-built one). Falls back to a plain PATH
// search for "ArcoFission" only if the env var isn't set -- callers that care about staleness
// should set ARCOFISSION_PATH explicitly, exactly as documented throughout this repo's own docs.
FissionToolchain detect_fission();

struct ArchiverToolchain {
    bool found = false;
    std::string path;
    std::string family;  // "llvm-ar", "gnu-ar", or "unknown"
    std::string version; // first line of `<path> --version`
};

// Checks AR first, then llvm-ar/ar on PATH.
ArchiverToolchain detect_archiver();

struct CrossCxxToolchain {
    bool found = false;
    std::string cxx_path;
    std::string cxx_version;
    std::string archiver_path;
    std::string archiver_version;
    std::string family;
};

CrossCxxToolchain detect_mingw_x86_64();
CrossCxxToolchain detect_emscripten();

// Parses a Makefile-rule-shaped `.d` depfile (as emitted by `-MMD -MF`, e.g.
// "obj/foo.o: src/foo.cpp include/foo.hpp \\\n  include/bar.hpp") and returns every
// dependency path listed after the first colon. RFC section 23: use the compiler's own
// dependency output, don't write a brittle universal include-scanner -- this only ever reads
// that already-trivially-formatted output. Returns an empty vector (not an error) if the file
// doesn't exist yet -- the very first build of a project has no prior depfile to read.
std::vector<std::string> parse_depfile(const std::string& path);

} // namespace rivet
