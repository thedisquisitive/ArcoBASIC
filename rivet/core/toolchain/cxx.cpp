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

bool try_archiver_candidate(const std::string& candidate, const std::string& family, ArchiverToolchain& out) {
    if (candidate.empty()) return false;
    std::string resolved = candidate.find('/') == std::string::npos ? resolve_on_path(candidate) : candidate;
    if (resolved.empty()) return false;

    std::error_code error;
    if (!std::filesystem::exists(resolved, error) || std::filesystem::is_directory(resolved, error)) return false;

    platform::ProcessResult result = platform::run_process({resolved, "--version"});
    if (!result.ok) return false;

    out.found = true;
    out.path = resolved;
    out.family = family;
    out.version = first_line(result.output);
    return true;
}

bool detect_cross_pair(const char* cxx_env, const char* ar_env, const std::string& default_cxx,
                       const std::string& default_ar, const std::string& family, CrossCxxToolchain& out) {
    const char* explicit_cxx = std::getenv(cxx_env);
    const char* explicit_ar = std::getenv(ar_env);
    std::string cxx_candidate = explicit_cxx && *explicit_cxx ? explicit_cxx : default_cxx;
    std::string ar_candidate = explicit_ar && *explicit_ar ? explicit_ar : default_ar;

    std::string cxx = cxx_candidate.find('/') == std::string::npos ? resolve_on_path(cxx_candidate) : cxx_candidate;
    std::string ar = ar_candidate.find('/') == std::string::npos ? resolve_on_path(ar_candidate) : ar_candidate;
    if (cxx.empty() || ar.empty()) return false;

    std::error_code error;
    if (!std::filesystem::exists(cxx, error) || std::filesystem::is_directory(cxx, error)) return false;
    if (!std::filesystem::exists(ar, error) || std::filesystem::is_directory(ar, error)) return false;

    platform::ProcessResult cxx_version = platform::run_process({cxx, "--version"});
    platform::ProcessResult ar_version = platform::run_process({ar, "--version"});
    if (!cxx_version.ok || !ar_version.ok) return false;

    out.found = true;
    out.cxx_path = cxx;
    out.cxx_version = first_line(cxx_version.output);
    out.archiver_path = ar;
    out.archiver_version = first_line(ar_version.output);
    out.family = family;
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

FissionToolchain detect_fission() {
    FissionToolchain toolchain;

    const char* explicit_path = std::getenv("ARCOFISSION_PATH");
    std::string candidate = explicit_path ? explicit_path : resolve_on_path("ArcoFission");
    if (candidate.empty()) return toolchain; // found == false

    // ArcoFission has no --version flag (confirmed: a bare invocation exits 2, a usage error, not
    // 0) -- detection is therefore "does this path exist and look like a real file", not "does
    // invoking it with a version flag succeed" the way detect_cxx() checks clang/gcc. Good enough
    // for this slice's actual need (a stable, real path to hand the scheduler as `tool`).
    std::error_code error;
    if (!std::filesystem::exists(candidate, error) || std::filesystem::is_directory(candidate, error)) {
        return toolchain;
    }

    toolchain.found = true;
    toolchain.path = candidate;
    toolchain.version = "(ArcoFission has no --version flag)";
    return toolchain;
}

ArchiverToolchain detect_archiver() {
    ArchiverToolchain toolchain;

    const char* explicit_path = std::getenv("AR");
    if (explicit_path && try_archiver_candidate(explicit_path, "unknown", toolchain)) return toolchain;
    if (try_archiver_candidate("llvm-ar", "llvm-ar", toolchain)) return toolchain;
    if (try_archiver_candidate("ar", "gnu-ar", toolchain)) return toolchain;
    return toolchain; // found == false
}

CrossCxxToolchain detect_mingw_x86_64() {
    CrossCxxToolchain toolchain;
    detect_cross_pair("ARCOFISSION_WINDOWS_CXX", "ARCOFISSION_WINDOWS_AR",
                      "x86_64-w64-mingw32-g++", "x86_64-w64-mingw32-ar",
                      "mingw-w64-x86_64", toolchain);
    return toolchain;
}

CrossCxxToolchain detect_emscripten() {
    CrossCxxToolchain toolchain;
    detect_cross_pair("ARCOFISSION_WEB_CXX", "ARCOFISSION_WEB_AR",
                      "em++", "emar", "emscripten", toolchain);
    return toolchain;
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
