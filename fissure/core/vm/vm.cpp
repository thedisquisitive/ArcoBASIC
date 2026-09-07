#include "fissure/vm.hpp"

#include "fissure/platform.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fissure {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("fissure: could not open " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string require_string_arg(const std::vector<arco::Value>& args, std::size_t index, const char* fn) {
    if (args.size() <= index) throw std::runtime_error(std::string(fn) + " expects an argument at position " + std::to_string(index));
    return args[index].to_string();
}

} // namespace

VM::VM(Graph& graph, EventBus& events, std::string project_root)
    : graph_(graph), events_(events), project_root_(std::move(project_root)) {
    register_host_contracts();
}

bool VM::has_capability(const std::string& capability) const {
    return granted_capabilities_.count(capability) > 0;
}

void VM::load_script(const std::string& path) {
    std::string source = read_file(path);
    Manifest manifest = parse_manifest(source);
    for (const auto& capability : manifest.capabilities) granted_capabilities_.insert(capability);

    arco::RunResult result = runtime_.run_string(source);
    if (!result.ok) {
        throw std::runtime_error("fissure: " + path + " failed: " + result.error);
    }
}

bool VM::call_if_defined(const std::string& name, const std::vector<arco::Value>& args) {
    if (!runtime_.has_function(name)) return false;
    arco::Value callable = runtime_.make_callable(name, std::nullopt, true);
    runtime_.call_callable(callable, args);
    return true;
}

void VM::register_host_contracts() {
    // FISSURE.Log.* -- no capability required. Logging what an extension is doing is never a
    // privileged operation, and requiring #REQUIRES for it would only encourage authors to
    // over-declare capabilities just to get diagnostics working.
    runtime_.register_function("FISSURE.Log.Info", [this](const std::vector<arco::Value>& args) -> arco::Value {
        events_.emit(EventType::GraphUpdated, {{"level", "info"}, {"message", require_string_arg(args, 0, "FISSURE.Log.Info")}});
        return arco::Value();
    });
    runtime_.register_function("FISSURE.Log.Warn", [this](const std::vector<arco::Value>& args) -> arco::Value {
        events_.emit(EventType::GraphUpdated, {{"level", "warn"}, {"message", require_string_arg(args, 0, "FISSURE.Log.Warn")}});
        return arco::Value();
    });
    runtime_.register_function("FISSURE.Log.Error", [this](const std::vector<arco::Value>& args) -> arco::Value {
        events_.emit(EventType::GraphUpdated, {{"level", "error"}, {"message", require_string_arg(args, 0, "FISSURE.Log.Error")}});
        return arco::Value();
    });

    // FISSURE.Graph.* -- GRAPH.WRITE. Task 6's own required first contracts: "logging, graph node
    // creation, graph edge creation, and project filesystem read."
    runtime_.register_function("FISSURE.Graph.Node", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (!has_capability("GRAPH.WRITE")) throw std::runtime_error("FISSURE.Graph.Node requires undeclared capability GRAPH.WRITE");
        std::string id = require_string_arg(args, 0, "FISSURE.Graph.Node");
        std::string kind_text = args.size() > 1 ? args[1].to_string() : "file";
        auto kind = node_kind_from_string(kind_text);
        if (!kind) throw std::runtime_error("FISSURE.Graph.Node: unknown node kind '" + kind_text + "'");
        Node node;
        node.id = id;
        node.kind = *kind;
        if (args.size() > 2 && args[2].is_object()) {
            for (const auto& [key, value] : args[2].as_object()) node.attributes[key] = value.to_string();
        }
        graph_.add_node(std::move(node));
        return arco::Value();
    });
    runtime_.register_function("FISSURE.Graph.Edge", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (!has_capability("GRAPH.WRITE")) throw std::runtime_error("FISSURE.Graph.Edge requires undeclared capability GRAPH.WRITE");
        Edge edge;
        edge.from = require_string_arg(args, 0, "FISSURE.Graph.Edge");
        edge.to = require_string_arg(args, 1, "FISSURE.Graph.Edge");
        std::string kind_text = args.size() > 2 ? args[2].to_string() : "reads";
        auto kind = edge_kind_from_string(kind_text);
        if (!kind) throw std::runtime_error("FISSURE.Graph.Edge: unknown edge kind '" + kind_text + "'");
        edge.kind = *kind;
        std::string evidence_text = args.size() > 3 ? args[3].to_string() : "declared";
        auto evidence = evidence_from_string(evidence_text);
        if (!evidence) throw std::runtime_error("FISSURE.Graph.Edge: unknown evidence '" + evidence_text + "'");
        edge.evidence = *evidence;
        edge.confidence = args.size() > 4 ? args[4].as_number() : 1.0;
        graph_.add_edge(std::move(edge));
        return arco::Value();
    });

    // FISSURE.Filesystem.* -- FILE.READ. Deliberately routed through this contract rather than
    // the underlying language's own built-in File.*/Directory.* functions, which remain fully
    // available on this same embedded runtime with no capability gate at all (that is how every
    // other embedder in this repo uses them, by design, full trust). Giving adapter scripts that
    // same raw access would make section 8's capability model cosmetic -- an adapter could just
    // bypass FISSURE.Filesystem.* and call File.ReadText directly. See vm.hpp's own capability-
    // model comment for the disclosed scope this enforcement actually covers.
    runtime_.register_function("FISSURE.Filesystem.Read", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (!has_capability("FILE.READ")) throw std::runtime_error("FISSURE.Filesystem.Read requires undeclared capability FILE.READ");
        std::string path = require_string_arg(args, 0, "FISSURE.Filesystem.Read");
        return arco::Value(read_file(path));
    });
    runtime_.register_function("FISSURE.Filesystem.Exists", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (!has_capability("FILE.READ")) throw std::runtime_error("FISSURE.Filesystem.Exists requires undeclared capability FILE.READ");
        std::string path = require_string_arg(args, 0, "FISSURE.Filesystem.Exists");
        std::ifstream probe(path);
        return arco::Value(probe.good());
    });
    runtime_.register_function("FISSURE.Filesystem.ProjectRoot", [this](const std::vector<arco::Value>&) -> arco::Value {
        return arco::Value(project_root_);
    });
    // Recursive listing rooted at the project directory, returning paths relative to it with
    // forward slashes (stable, platform-independent node ids -- RFC 18's own cross-platform
    // concern). Skips version-control and Fissure's own state directories, plus common generated-
    // output directory names already used as `.gitignore` conventions elsewhere in this
    // repository (`build`, `node_modules`) -- a real ignore-file-aware walk (respecting the
    // target project's own `.gitignore`) is real future work, not attempted here; scanning
    // `.git`'s own object store as "source files" would make the generic adapter's own Tier-0
    // graph actively misleading, so this minimal skip list is a correctness floor, not polish.
    runtime_.register_function("FISSURE.Filesystem.Walk", [this](const std::vector<arco::Value>&) -> arco::Value {
        if (!has_capability("FILE.READ")) throw std::runtime_error("FISSURE.Filesystem.Walk requires undeclared capability FILE.READ");
        static const std::set<std::string> skip_dirs = {".git", ".fissure", "build", "node_modules"};
        arco::Value::Array paths;
        std::error_code error;
        std::filesystem::path root(project_root_);
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error);
             it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            if (error) break;
            if (it->is_directory(error)) {
                if (skip_dirs.count(it->path().filename().string())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(error)) continue;
            std::string relative = std::filesystem::relative(it->path(), root, error).generic_string();
            if (!error) paths.emplace_back(relative);
        }
        return arco::Value(std::move(paths));
    });

    // FISSURE.Process.Exec -- PROCESS.EXEC. Routes through the platform boundary (RFC section
    // 18), never the shell directly. This is also the ONLY execution primitive git.ab (and any
    // future VCS/build adapter) uses -- there is no separate native FISSURE.VCS.* contract for
    // M1; git-specific behavior (section 15: "must not depend on Git conceptually") lives
    // entirely in adapters/vcs/git.ab, calling this generic contract, exactly like section 4.3
    // describes for a C++ adapter shelling out to Clang.
    runtime_.register_function("FISSURE.Process.Exec", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (!has_capability("PROCESS.EXEC")) throw std::runtime_error("FISSURE.Process.Exec requires undeclared capability PROCESS.EXEC");
        std::string command = require_string_arg(args, 0, "FISSURE.Process.Exec");
        platform::ProcessResult result = platform::run_process(command);
        return arco::Value(arco::Value::Object{
            {"Ok", result.ok},
            {"ExitCode", static_cast<double>(result.exit_code)},
            {"Output", result.output},
            {"DurationSeconds", result.duration.count()},
        });
    });

    // FISSURE.Change.Report -- an adapter's own disturbance-detection hook (task 10) calls this
    // once per changed path it discovers (e.g. git.ab, one call per `git diff --name-only` line).
    // No capability gate: reporting a path string is not itself privileged (discovering it, via
    // Process.Exec or Filesystem.Read, already was).
    runtime_.register_function("FISSURE.Change.Report", [this](const std::vector<arco::Value>& args) -> arco::Value {
        reported_changes_.push_back(require_string_arg(args, 0, "FISSURE.Change.Report"));
        return arco::Value();
    });
    runtime_.register_function("FISSURE.Change.SinceRef", [this](const std::vector<arco::Value>&) -> arco::Value {
        return arco::Value(since_ref_);
    });

    // FISSURE.Probe.* -- GRAPH.WRITE, matching FISSURE.Graph.* (a probe is a graph Node, RFC
    // section 5.1/9). Register creates the Probe node and its bookkeeping entry; DependsOn adds a
    // Declared-evidence edge from the probe to a path -- the M1 evidence source design decision 1
    // in AGENT_PROGRESS.md's Session 1 entry depends on exactly this edge existing.
    runtime_.register_function("FISSURE.Probe.Register", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (!has_capability("GRAPH.WRITE")) throw std::runtime_error("FISSURE.Probe.Register requires undeclared capability GRAPH.WRITE");
        Probe probe;
        probe.id = require_string_arg(args, 0, "FISSURE.Probe.Register");
        probe.command = args.size() > 1 ? args[1].to_string() : "";
        probe.display_name = probe.id;
        if (args.size() > 2 && args[2].is_array()) {
            for (const auto& tag : args[2].as_array()) probe.tags.push_back(tag.to_string());
        }
        Node node;
        node.id = probe.id;
        node.kind = NodeKind::Probe;
        node.attributes["command"] = probe.command;
        graph_.add_node(std::move(node));
        probes_.push_back(std::move(probe));
        return arco::Value();
    });
    runtime_.register_function("FISSURE.Probe.DependsOn", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (!has_capability("GRAPH.WRITE")) throw std::runtime_error("FISSURE.Probe.DependsOn requires undeclared capability GRAPH.WRITE");
        Edge edge;
        edge.from = require_string_arg(args, 0, "FISSURE.Probe.DependsOn");
        edge.to = require_string_arg(args, 1, "FISSURE.Probe.DependsOn");
        edge.kind = EdgeKind::DeclaredTestAssociation;
        edge.evidence = Evidence::Declared;
        edge.confidence = 1.0;
        graph_.add_edge(std::move(edge));
        return arco::Value();
    });
}

} // namespace fissure
