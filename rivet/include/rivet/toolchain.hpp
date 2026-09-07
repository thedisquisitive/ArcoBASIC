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

// Parses a Makefile-rule-shaped `.d` depfile (as emitted by `-MMD -MF`, e.g.
// "obj/foo.o: src/foo.cpp include/foo.hpp \\\n  include/bar.hpp") and returns every
// dependency path listed after the first colon. RFC section 23: use the compiler's own
// dependency output, don't write a brittle universal include-scanner -- this only ever reads
// that already-trivially-formatted output. Returns an empty vector (not an error) if the file
// doesn't exist yet -- the very first build of a project has no prior depfile to read.
std::vector<std::string> parse_depfile(const std::string& path);

} // namespace rivet
