// Fissure CLI (RFC section 13). M1 implements exactly the command surface tasks 12/22.6 require:
// `fissure run` and `fissure explain <probe>`, plus a small `fissure status` for basic visibility.
// The remaining section 13.1 commands (impact/trace/graph/adapters/calibrate/history) are real,
// named, later milestone work -- not stubbed here with fake output, just absent, printed as such
// by the usage text below.

#include "fissure/events.hpp"
#include "fissure/graph.hpp"
#include "fissure/impact.hpp"
#include "fissure/scheduler.hpp"
#include "fissure/store.hpp"
#include "fissure/vm.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <optional>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

// Same `/proc/self/exe` technique src/runtime/runtime.cpp's own executable_directory() uses
// (confirmed as this repo's established convention for exe-relative resource resolution) --
// Fissure's adapters live next to its own source tree, not the target project being analyzed.
std::optional<fs::path> executable_directory() {
    std::array<char, 4096> buffer{};
    ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) return std::nullopt;
    buffer[static_cast<std::size_t>(length)] = '\0';
    return fs::path(buffer.data()).parent_path();
}

// Candidate roots, first existing one wins: next to the built binary (an install layout, e.g.
// share/fissure/adapters), the source tree relative to a build/ directory (the documented dev
// workflow -- see docs/README.md below), and finally cwd-relative (running fissure from within
// its own source checkout).
fs::path find_adapters_root() {
    std::vector<fs::path> candidates;
    if (auto exe_dir = executable_directory()) {
        candidates.push_back(*exe_dir / "share" / "fissure" / "adapters");
        candidates.push_back(*exe_dir / ".." / "fissure" / "adapters");
        candidates.push_back(*exe_dir / ".." / ".." / "fissure" / "adapters");
    }
    candidates.push_back(fs::path("fissure") / "adapters");
    candidates.push_back(fs::path("adapters"));
    for (const auto& candidate : candidates) {
        if (fs::exists(candidate) && fs::is_directory(candidate)) return fs::weakly_canonical(candidate);
    }
    throw std::runtime_error("fissure: could not locate the adapters/ directory (looked next to "
                              "the executable and relative to the current directory)");
}

// Minimal CLI event renderer (RFC section 13.2's own style, simplified -- section 22.3 ranks
// "presentation polish" last of five coding priorities, after correctness, stable contracts,
// explainability, and performance; a plain readable renderer satisfies section 14's actual
// requirement -- "structured events drive CLI output" -- without spending budget on the full
// box-drawing mockup before any of the higher priorities are solid).
void render(const fissure::Event& event) {
    using fissure::EventType;
    switch (event.type) {
        case EventType::DisturbanceDetected:
            std::cout << "[FISSURE] disturbance: " << event.fields.at("count") << " changed file(s)\n";
            break;
        case EventType::GraphUpdated: {
            auto level = event.fields.find("level");
            auto message = event.fields.find("message");
            if (level != event.fields.end() && message != event.fields.end()) {
                std::cout << "[fissure:" << level->second << "] " << message->second << "\n";
            }
            break;
        }
        case EventType::ImpactComputed:
            std::cout << "[FISSURE] impact: " << event.fields.at("affected") << " affected, "
                      << event.fields.at("unknown") << " unknown, " << event.fields.at("unaffected")
                      << " unaffected (" << event.fields.at("total") << " probe(s) total)\n";
            break;
        case EventType::ProbeSelected:
            std::cout << "  [" << event.fields.at("classification") << "] " << event.fields.at("probe") << " -- RUN\n";
            break;
        case EventType::ProbeSkipped:
            std::cout << "  [UNAFFECTED] " << event.fields.at("probe") << " -- SKIP\n";
            break;
        case EventType::ProbeStarted:
            break; // no separate line -- Passed/Failed below carries the result
        case EventType::ProbePassed:
            std::cout << "    PASS  " << event.fields.at("probe") << "  " << event.fields.at("duration_seconds") << "s\n";
            break;
        case EventType::ProbeFailed: {
            std::cout << "    FAIL  " << event.fields.at("probe") << "  " << event.fields.at("duration_seconds")
                      << "s  (exit " << event.fields.at("exit_code") << ")\n";
            auto output = event.fields.find("output");
            if (output != event.fields.end() && !output->second.empty()) {
                std::cout << "      " << output->second << "\n";
            }
            break;
        }
        case EventType::RunCompleted:
            std::cout << "[FISSURE] run complete: " << event.fields.at("passed") << " passed, "
                      << event.fields.at("failed") << " failed, " << event.fields.at("skipped") << " skipped\n";
            break;
        case EventType::ConfidenceChanged:
        case EventType::CalibrationRequired:
            break; // not emitted yet -- see AGENT_PROGRESS.md design decision 5
    }
}

struct Options {
    bool full = false;
    std::string since_ref;
};

// Loads every M1 reference adapter plus a project-local fissure.ab if the target project defines
// one (RFC section 7.7's own ProjectImpact convention -- probes are registered there). Adapter
// load order matters only in that generic.ab's file-node scan should happen before anything that
// might reference those nodes; M1's adapters don't yet have a real ordering dependency beyond
// that, so this list is simply generic -> vcs -> test -> project.
void load_adapters(fissure::VM& vm, const fs::path& adapters_root, const fs::path& project_root) {
    vm.load_script((adapters_root / "language" / "generic.ab").string());
    vm.load_script((adapters_root / "vcs" / "git.ab").string());
    vm.load_script((adapters_root / "test" / "command.ab").string());
    fs::path project_config = project_root / "fissure.ab";
    if (fs::exists(project_config)) {
        vm.load_script(project_config.string());
    } else {
        std::cout << "[fissure] no fissure.ab found at " << project_root
                  << " -- no probes registered, nothing to run\n";
    }
}

int run_command(const Options& options) {
    fs::path project_root = fs::current_path();
    fs::path state_dir = project_root / ".fissure";
    fs::create_directories(state_dir);

    fissure::EventBus events;
    events.subscribe(render);

    fissure::Graph graph;
    fissure::VM vm(graph, events, project_root.string());
    if (!options.since_ref.empty()) vm.set_since_ref(options.since_ref);

    load_adapters(vm, find_adapters_root(), project_root);

    // Well-known entry points (RFC section 7.2's own adapter responsibility names, called by
    // convention rather than a formal registry -- M1's scope). A script that doesn't define one
    // (e.g. command.ab has nothing to discover on its own) is skipped silently, not an error.
    vm.call_if_defined("DiscoverFiles");
    vm.call_if_defined("DetectChanges");

    std::vector<std::string> disturbance = vm.reported_changes();
    events.emit(fissure::EventType::DisturbanceDetected, {{"count", std::to_string(disturbance.size())}});

    fissure::ImpactEngine impact_engine;
    std::vector<fissure::ProbeVerdict> verdicts = impact_engine.classify(graph, disturbance);

    int affected = 0, unaffected = 0, unknown = 0;
    for (const auto& verdict : verdicts) {
        switch (verdict.classification) {
            case fissure::Classification::Affected: ++affected; break;
            case fissure::Classification::Unaffected: ++unaffected; break;
            case fissure::Classification::Unknown: ++unknown; break;
        }
    }
    events.emit(fissure::EventType::ImpactComputed, {
        {"affected", std::to_string(affected)},
        {"unaffected", std::to_string(unaffected)},
        {"unknown", std::to_string(unknown)},
        {"total", std::to_string(verdicts.size())},
    });

    std::vector<std::string> to_run;
    for (const auto& verdict : verdicts) {
        // RFC 6.1's invariant, enforced again right here at selection time (not just inside
        // ImpactEngine): AFFECTED and UNKNOWN both run by default; only a real UNAFFECTED
        // classification skips. --full overrides this and runs everything, per section 11's
        // "full: Execute every probe and recalibrate graph evidence."
        bool run_it = options.full || verdict.classification != fissure::Classification::Unaffected;
        if (run_it) {
            to_run.push_back(verdict.probe_id);
            events.emit(fissure::EventType::ProbeSelected, {
                {"probe", verdict.probe_id}, {"classification", to_string(verdict.classification)}});
        } else {
            events.emit(fissure::EventType::ProbeSkipped, {{"probe", verdict.probe_id}});
        }
    }

    fissure::Scheduler scheduler(events);
    std::vector<fissure::ProbeResult> results = scheduler.run(vm.probes(), to_run);

    fissure::GraphStore store = fissure::GraphStore::open((state_dir / "fissure.db").string());
    store.save(graph);
    int passed = 0, failed = 0;
    for (const auto& result : results) {
        store.record_result(result);
        if (result.passed) ++passed; else ++failed;
    }

    events.emit(fissure::EventType::RunCompleted, {
        {"passed", std::to_string(passed)},
        {"failed", std::to_string(failed)},
        {"skipped", std::to_string(unaffected)},
    });

    return failed > 0 ? 1 : 0;
}

int explain_command(const std::string& probe_id) {
    fs::path project_root = fs::current_path();
    fissure::EventBus events; // no renderer subscribed -- explain prints its own focused format
    fissure::Graph graph;
    fissure::VM vm(graph, events, project_root.string());
    load_adapters(vm, find_adapters_root(), project_root);
    vm.call_if_defined("DiscoverFiles");
    vm.call_if_defined("DetectChanges");

    std::vector<std::string> disturbance = vm.reported_changes();
    fissure::ImpactEngine impact_engine;
    std::vector<fissure::ProbeVerdict> verdicts = impact_engine.classify(graph, disturbance);

    for (const auto& verdict : verdicts) {
        if (verdict.probe_id != probe_id) continue;
        std::cout << "probe: " << verdict.probe_id << "\n"
                  << "classification: " << to_string(verdict.classification) << "\n"
                  << "disturbance: " << disturbance.size() << " changed file(s)\n";
        for (const auto& path : disturbance) std::cout << "  Δ " << path << "\n";
        std::cout << "reasons:\n";
        for (const auto& reason : verdict.reasons) std::cout << "  " << reason << "\n";
        return 0;
    }
    std::cerr << "fissure explain: no probe registered with id '" << probe_id << "'\n";
    return 2;
}

int status_command() {
    fs::path project_root = fs::current_path();
    fs::path db_path = project_root / ".fissure" / "fissure.db";
    if (!fs::exists(db_path)) {
        std::cout << "[fissure] no state at " << db_path << " yet -- run `fissure run` first\n";
        return 0;
    }
    fissure::GraphStore store = fissure::GraphStore::open(db_path.string());
    fissure::Graph graph = store.load();
    std::size_t probe_count = 0;
    for (const auto& [id, node] : graph.nodes()) {
        if (node.kind == fissure::NodeKind::Probe) ++probe_count;
    }
    std::cout << "[fissure] " << db_path << "\n"
              << "  nodes: " << graph.node_count() << " (" << probe_count << " probe(s))\n"
              << "  edges: " << graph.edge_count() << "\n";
    return 0;
}

void print_usage() {
    std::cerr <<
        "usage: fissure <command> [options]\n"
        "\n"
        "  run [--full] [--since <ref>]   detect changes, classify probes, run what's needed\n"
        "  explain <probe>                explain why a probe would run or be skipped\n"
        "  status                         show persisted graph/probe counts\n"
        "\n"
        "not yet implemented (see fissure/AGENT_PROGRESS.md):\n"
        "  impact, trace, graph, adapters, calibrate, history\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    std::string command = argv[1];
    try {
        if (command == "run") {
            Options options;
            for (int i = 2; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "--full") options.full = true;
                else if (arg == "--since" && i + 1 < argc) options.since_ref = argv[++i];
                else {
                    std::cerr << "fissure run: unrecognized option '" << arg << "'\n";
                    return 2;
                }
            }
            return run_command(options);
        }
        if (command == "explain") {
            if (argc < 3) {
                std::cerr << "usage: fissure explain <probe>\n";
                return 2;
            }
            return explain_command(argv[2]);
        }
        if (command == "status") {
            return status_command();
        }
        print_usage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "fissure: " << error.what() << "\n";
        return 1;
    }
}
