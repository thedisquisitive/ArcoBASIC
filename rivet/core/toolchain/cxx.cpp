#include "rivet/toolchain.hpp"

#include "rivet/platform.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace rivet {

namespace {

std::string resolve_on_path(const std::string& name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return "";
    std::istringstream stream(path_env);
    std::string directory;
    while (std::getline(stream, directory, ':')) {
        if (directory.empty()) continue;
        std::filesystem::path candidate = std::filesystem::path(directory) / name;
        std::error_code error;
        if (std::filesystem::exists(candidate, error) && !std::filesystem::is_directory(candidate, error)) {
            return candidate.string();
        }
    }
    return "";
}

std::string first_line(const std::string& text) {
    auto newline = text.find('\n');
    return newline == std::string::npos ? text : text.substr(0, newline);
}

bool try_candidate(const std::string& name, const std::string& family, CxxToolchain& out) {
    std::string resolved = resolve_on_path(name);
    if (resolved.empty()) return false;
    platform::ProcessResult result = platform::run_process({resolved, "--version"});
    if (!result.ok) return false;
    out.found = true;
    out.path = resolved;
    out.family = family;
    out.version = first_line(result.output);
    return true;
}

} // namespace

CxxToolchain detect_cxx() {
    CxxToolchain toolchain;
    if (try_candidate("clang++", "clang", toolchain)) return toolchain;
    if (try_candidate("g++", "gcc", toolchain)) return toolchain;
    if (try_candidate("c++", "unknown", toolchain)) return toolchain;
    return toolchain; // found == false
}

std::vector<std::string> parse_depfile(const std::string& path) {
    std::vector<std::string> dependencies;
    std::ifstream input(path);
    if (!input) return dependencies; // first build: no prior depfile, not an error

    std::ostringstream buffer;
    buffer << input.rdbuf();
    std::string text = buffer.str();

    // Join line continuations ("... \\\n  ...") into one logical line first, so tokenizing
    // doesn't need to special-case a trailing backslash mid-scan.
    std::string joined;
    joined.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == '\n') {
            joined.push_back(' ');
            ++i; // skip the newline too
            continue;
        }
        joined.push_back(text[i]);
    }

    // Skip everything up to and including the first unescaped ':' (the rule's own target/output,
    // already tracked separately as the action's own `outputs`, not a dependency of itself).
    std::size_t colon = joined.find(':');
    std::string rest = colon == std::string::npos ? "" : joined.substr(colon + 1);

    // Tokenize on whitespace, honoring "\ " as an escaped space inside one path (a real thing
    // compilers emit for paths containing spaces).
    std::string current;
    for (std::size_t i = 0; i < rest.size(); ++i) {
        char c = rest[i];
        if (c == '\\' && i + 1 < rest.size() && rest[i + 1] == ' ') {
            current.push_back(' ');
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                dependencies.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(c);
    }
    if (!current.empty()) dependencies.push_back(current);

    return dependencies;
}

} // namespace rivet
