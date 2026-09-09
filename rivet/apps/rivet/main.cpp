// Rivet CLI (rivet/docs/rivet-rfc.md section 29). This slice implements exactly what the RFC's own
// section 61 acceptance bar needs: `rivet build [--jobs N]`, `rivet why rebuild <file>`,
// `rivet clean [--all]`. The remaining section 29 commands (explain/why <non-rebuild>/graph/
// inspect/targets/profiles/toolchains/cache/fingerprint/doctor) are real, disclosed, deferred
// work -- see rivet/RIVET_PROGRESS.md -- not stubbed here with fake output.

#include "rivet/action.hpp"
#include "rivet/cache.hpp"
#include "rivet/fingerprint.hpp"
#include "rivet/scheduler.hpp"
#include "rivet/store.hpp"
#include "rivet/toolchain.hpp"
#include "rivet/vm.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

std::optional<fs::path> executable_directory() {
    std::array<char, 4096> buffer{};
    ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) return std::nullopt;
    buffer[static_cast<std::size_t>(length)] = '\0';
    return fs::path(buffer.data()).parent_path();
}

// Same candidate-path search fissure/apps/fissure/main.cpp already established for locating its
// own adapters/ directory next to the built binary.
fs::path find_rivet_root() {
    std::vector<fs::path> candidates;
    if (auto exe_dir = executable_directory()) {
        candidates.push_back(*exe_dir / "share" / "rivet");
        candidates.push_back(*exe_dir / ".." / "share" / "rivet");
        candidates.push_back(*exe_dir / ".." / "rivet");
        candidates.push_back(*exe_dir / ".." / ".." / "rivet");
    }
    candidates.push_back(fs::path("rivet"));
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate / "stdlib" / "rivet.abas")) return fs::weakly_canonical(candidate);
    }
    throw std::runtime_error("rivet: could not locate stdlib/rivet.abas (looked next to the "
                              "executable and relative to the current directory)");
}

// RFC section 31: search upward from cwd for build.abas.
fs::path find_project_root() {
    fs::path current = fs::current_path();
    for (;;) {
        if (fs::exists(current / "build.abas")) return current;
        fs::path parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    throw std::runtime_error("rivet: no build.abas found in this directory or any parent");
}

std::string relative_to(const fs::path& path, const fs::path& base) {
    std::error_code error;
    fs::path result = fs::relative(path, base, error);
    return error ? path.string() : result.generic_string();
}

std::string display_name_for(const rivet::BuildAction& action, const fs::path& project_root) {
    fs::path display(action.display_name);
    if (display.is_absolute()) return relative_to(display, project_root);
    return display.generic_string();
}

struct BuildOptions {
    unsigned jobs = std::max(1u, std::thread::hardware_concurrency());
    unsigned verbosity = 0;
};

std::string shellish_join(const std::vector<std::string>& args) {
    std::ostringstream out;
    bool first = true;
    for (const auto& arg : args) {
        if (!first) out << ' ';
        first = false;
        if (arg.find_first_of(" \t\n'\"$\\") == std::string::npos) {
            out << arg;
        } else {
            out << '\'';
            for (char ch : arg) {
                if (ch == '\'') out << "'\\''";
                else out << ch;
            }
            out << '\'';
        }
    }
    return out.str();
}

void write_build_status(const fs::path& status_path, int pid, const std::string& phase,
                        std::size_t total, int compiled, int linked, int cached, int failed,
                        const std::vector<std::string>& active, const std::string& last_event) {
    fs::create_directories(status_path.parent_path());
    std::ofstream file(status_path);
    file << "pid: " << pid << "\n"
         << "phase: " << phase << "\n"
         << "total-actions: " << total << "\n"
         << "compiled: " << compiled << "\n"
         << "linked: " << linked << "\n"
         << "cached: " << cached << "\n"
         << "failed: " << failed << "\n"
         << "active: " << active.size() << "\n";
    for (const auto& item : active) file << "  " << item << "\n";
    if (!last_event.empty()) file << "last: " << last_event << "\n";
}

int run_status() {
    fs::path project_root = find_project_root();
    fs::path status_path = project_root / ".rivet" / "state" / "current-build.txt";
    if (!fs::exists(status_path)) {
        std::cout << "rivet: no build status recorded for " << project_root.generic_string() << "\n";
        return 0;
    }
    std::ifstream file(status_path);
    std::cout << file.rdbuf();
    return 0;
}

int run_build(const BuildOptions& options) {
    fs::path project_root = find_project_root();
    // RFC section 31: "Invoking rivet build from a child directory should locate the project
    // root" -- locating it isn't enough on its own. Every relative path this VM's contracts
    // produce (RIVET.Filesystem.WalkFiles's own inputs, BuildTarget.Build()'s own outputs) is
    // relative to the project root, and the scheduler passes those straight to the compiler with
    // no explicit `cwd` -- so the process's OWN working directory must actually BE the project
    // root for a relative path like "src/math.cpp" to resolve at all once the user isn't already
    // sitting in it.
    fs::current_path(project_root);
    fs::path rivet_root = find_rivet_root();
    fs::path state_dir = project_root / ".rivet" / "state";
    fs::create_directories(state_dir);

    rivet::VM vm(project_root.string());
    vm.load_stdlib(rivet_root.string());
    vm.load_build_script((project_root / "build.abas").string());

    rivet::StateStore store = rivet::StateStore::open((state_dir / "rivet.db").string());
    rivet::CacheManager cache(store);
    rivet::Scheduler scheduler(cache, options.jobs);

    int compiled = 0, linked = 0, cached = 0, failed = 0;
    std::mutex event_mutex;
    std::vector<std::string> active;
    fs::path status_path = state_dir / "current-build.txt";
    const std::size_t total_actions = vm.graph().actions().size();

    write_build_status(status_path, static_cast<int>(getpid()), "starting", total_actions,
                       compiled, linked, cached, failed, active, "loaded build graph");

    if (options.verbosity >= 1) {
        std::cout << "[RIVET] project " << project_root.generic_string() << "\n"
                  << "[RIVET] actions " << total_actions << ", jobs " << options.jobs
                  << ", verbosity " << options.verbosity << "\n";
    }

    auto on_event = [&](const std::string& kind, const rivet::ActionResult& result) {
        std::lock_guard<std::mutex> lock(event_mutex);
        const rivet::BuildAction* action = vm.graph().find(result.identity);
        std::string display = action ? display_name_for(*action, project_root) : result.identity;
        std::string last_event;
        if (kind == "start") {
            active.push_back(display);
            last_event = "start " + display;
            if (options.verbosity >= 1) {
                std::cout << "[START]   " << display << "\n";
                if (options.verbosity >= 2 && action) {
                    std::cout << "  id:     " << action->identity << "\n"
                              << "  target: " << action->target << "\n"
                              << "  tool:   " << action->tool << "\n"
                              << "  argv:   " << shellish_join(action->arguments) << "\n"
                              << "  origin: " << action->origin.script << ":" << action->origin.function << "\n";
                    if (!action->dependencies.empty()) std::cout << "  deps:   " << shellish_join(action->dependencies) << "\n";
                }
            }
        } else if (kind == "cache") {
            std::cout << "[CACHE]   " << display << "\n";
            ++cached;
            last_event = "cache " + display;
            if (options.verbosity >= 1) std::cout << "  reason: " << result.reason << "\n";
        } else if (kind == "ran") {
            if (action && action->type == rivet::ActionType::Archive) {
                std::cout << "[ARCHIVE] " << display << "\n";
                ++linked;
            } else if (action && action->type == rivet::ActionType::Link) {
                std::cout << "[LINK]    " << display << "\n";
                ++linked;
            } else {
                std::cout << "[COMPILE] " << display << "\n";
                ++compiled;
            }
            last_event = "ran " + display;
            if (options.verbosity >= 1) std::cout << "  duration: " << result.duration_seconds << "s\n";
            if (options.verbosity >= 2 && !result.output.empty()) std::cout << result.output << "\n";
        } else if (kind == "failed") {
            std::cout << "[FAIL]    " << display << " (exit " << result.exit_code << ")\n";
            if (!result.output.empty()) std::cout << result.output << "\n";
            if (action) {
                std::cout << "  tool:   " << action->tool << "\n"
                          << "  origin: " << action->origin.script << ":" << action->origin.function << "\n";
            }
            ++failed;
            last_event = "failed " + display;
        } else if (kind == "skipped") {
            std::cout << "[SKIP]    " << display << " (" << result.reason << ")\n";
            last_event = "skipped " + display;
        }
        if (kind != "start") {
            active.erase(std::remove(active.begin(), active.end(), display), active.end());
        }
        write_build_status(status_path, static_cast<int>(getpid()),
                           kind == "start" ? "running" : "running", total_actions,
                           compiled, linked, cached, failed, active, last_event);
    };

    std::vector<rivet::ActionResult> results = scheduler.run(vm.graph(), on_event);
    (void)results;

    std::cout << "[DONE] " << compiled << " compiled, " << linked << " linked, " << cached
              << " cached, " << failed << " failed\n";
    write_build_status(status_path, static_cast<int>(getpid()), failed > 0 ? "failed" : "complete",
                       total_actions, compiled, linked, cached, failed, active, "build finished");
    return failed > 0 ? 1 : 0;
}

int run_why_rebuild(const std::string& file_argument) {
    fs::path project_root = find_project_root();
    fs::path db_path = project_root / ".rivet" / "state" / "rivet.db";
    if (!fs::exists(db_path)) {
        std::cerr << "rivet: no build state at " << db_path << " -- run `rivet build` first\n";
        return 1;
    }
    rivet::StateStore store = rivet::StateStore::open(db_path.string());

    fs::path absolute = fs::path(file_argument).is_absolute() ? fs::path(file_argument) : project_root / file_argument;
    std::string normalized = relative_to(absolute, project_root);

    std::vector<std::string> referencing = store.actions_referencing_input(normalized);
    if (referencing.empty()) {
        std::cout << "rivet: no recorded action references '" << normalized << "'\n";
        return 0;
    }

    bool current_exists = fs::exists(absolute);
    rivet::Digest current_hash;
    if (current_exists) current_hash = rivet::hash_file(absolute.string());

    for (const auto& identity : referencing) {
        // The historical answer: what the LAST run of this action actually decided, and why --
        // read straight from the store, not re-derived by comparing against current disk state
        // (which would be misleadingly "unchanged" immediately after a build that already
        // incorporated this exact file's change -- see rivet/RIVET_PROGRESS.md's own note on why
        // `why` must be reason-persisted rather than live-recomputed).
        auto record = store.last_record(identity);
        if (record) {
            std::cout << "[" << to_string(record->status) << "] " << identity << "\n  "
                      << record->reason << "\n";
            if (record->status == rivet::ActionStatus::Ran) {
                for (const auto& dependent : store.dependents_of(identity)) {
                    std::cout << "[REBUILD] " << dependent << "\n  cause: depends on " << identity
                              << ", which was rebuilt\n";
                }
            }
        }

        // The predictive answer: has the file changed AGAIN since that last recorded run (e.g.
        // asked before the next `rivet build` rather than right after one) -- a separate,
        // additional note, not a replacement for the historical one above.
        for (const auto& input : store.recorded_inputs(identity)) {
            if (input.path != normalized) continue;
            if (!current_exists) {
                std::cout << "  note: `" << normalized << "` no longer exists on disk since that run\n";
            } else if (input.digest != current_hash) {
                std::cout << "  note: `" << normalized << "` has changed again since that run -- the "
                          << "next `rivet build` will rebuild " << identity << "\n";
            }
        }
    }
    return 0;
}

int run_clean(bool all) {
    fs::path project_root = find_project_root();
    fs::path db_path = project_root / ".rivet" / "state" / "rivet.db";
    std::vector<fs::path> touched_directories;
    if (fs::exists(db_path)) {
        rivet::StateStore store = rivet::StateStore::open(db_path.string());
        for (const auto& output : store.all_recorded_outputs()) {
            fs::path absolute = fs::path(output).is_absolute() ? fs::path(output) : project_root / output;
            std::error_code error;
            fs::remove(absolute, error);
            fs::remove(fs::path(absolute.string() + ".d"), error);
            touched_directories.push_back(absolute.parent_path());
        }
    }
    std::error_code error;
    fs::remove_all(project_root / ".rivet" / "obj", error);
    if (all) fs::remove_all(project_root / ".rivet", error);

    // Best-effort tidy-up: remove any output directory `rivet build` created that's now empty
    // (e.g. `build/`), without ever touching one that still has something in it (a directory a
    // person also keeps other files in, or that another target's own outputs still live under).
    // RFC section 33: "Rivet should never casually delete arbitrary files merely because a
    // wildcard happens to match" -- this only ever removes directories this exact clean pass just
    // emptied, walking upward from each one only while it stays empty and stays inside the
    // project root.
    for (fs::path directory : touched_directories) {
        while (!directory.empty() && directory != project_root) {
            std::error_code remove_error;
            if (!fs::is_empty(directory, remove_error) || remove_error) break;
            fs::remove(directory, remove_error);
            if (remove_error) break;
            directory = directory.parent_path();
        }
    }

    std::cout << "rivet: clean\n";
    return 0;
}

void print_usage() {
    std::cerr << "usage: rivet <command> [options]\n\n"
                 "  build [--jobs N]        build the project rooted at the nearest build.abas\n"
                 "        [-v|--verbose] [--verbosity N]\n"
                 "  status                  show the current or last build heartbeat\n"
                 "  why rebuild <file>      explain why a file's dependent actions would rebuild\n"
                 "  clean [--all]           remove Rivet-owned build outputs (--all also clears state)\n\n"
                 "not yet implemented (see rivet/RIVET_PROGRESS.md):\n"
                 "  explain, graph, inspect, targets, profiles, toolchains, cache, fingerprint, doctor\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    std::string command = argv[1];
    try {
        if (command == "build") {
            BuildOptions options;
            if (const char* env_verbosity = std::getenv("RIVET_VERBOSITY")) {
                options.verbosity = static_cast<unsigned>(std::stoul(env_verbosity));
            }
            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--jobs" && i + 1 < argc) options.jobs = static_cast<unsigned>(std::stoul(argv[++i]));
                else if (arg == "-v" || arg == "--verbose") ++options.verbosity;
                else if (arg == "--verbosity" && i + 1 < argc) options.verbosity = static_cast<unsigned>(std::stoul(argv[++i]));
                else {
                    std::cerr << "rivet build: unrecognized option '" << arg << "'\n";
                    return 2;
                }
            }
            options.jobs = std::max(1u, options.jobs);
            return run_build(options);
        }
        if (command == "status") {
            return run_status();
        }
        if (command == "why") {
            if (argc < 4 || std::string(argv[2]) != "rebuild") {
                std::cerr << "usage: rivet why rebuild <file>\n";
                return 2;
            }
            return run_why_rebuild(argv[3]);
        }
        if (command == "clean") {
            bool all = argc >= 3 && std::string(argv[2]) == "--all";
            return run_clean(all);
        }
        print_usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "rivet: " << error.what() << "\n";
        return 1;
    }
}
