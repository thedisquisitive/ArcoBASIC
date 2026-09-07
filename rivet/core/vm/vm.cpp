#include "rivet/vm.hpp"

#include "rivet/toolchain.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#if defined(__linux__)
#include <sys/utsname.h>
#endif

namespace rivet {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("rivet: could not open " + path);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string require_string(const std::vector<arco::Value>& args, std::size_t index, const char* fn) {
    if (args.size() <= index) throw std::runtime_error(std::string(fn) + " expects an argument at position " + std::to_string(index));
    return args[index].to_string();
}

std::vector<std::string> string_array_field(const arco::Value::Object& object, const std::string& key) {
    std::vector<std::string> result;
    auto found = object.find(key);
    if (found == object.end() || !found->second.is_array()) return result;
    for (const auto& entry : found->second.as_array()) result.push_back(entry.to_string());
    return result;
}

std::string string_field(const arco::Value::Object& object, const std::string& key, const std::string& fallback = "") {
    auto found = object.find(key);
    if (found == object.end()) return fallback;
    return found->second.to_string();
}

} // namespace

VM::VM(std::string project_root) : project_root_(std::move(project_root)) {
    register_host_contracts();
}

void VM::load_stdlib(const std::string& rivet_root) {
    std::filesystem::path root(rivet_root);
    std::string stdlib_source = read_file((root / "stdlib" / "rivet.abas").string());
    arco::RunResult stdlib_result = runtime_.run_string(stdlib_source);
    if (!stdlib_result.ok) throw std::runtime_error("rivet: internal error loading stdlib/rivet.abas: " + stdlib_result.error);

    std::string adapter_source = read_file((root / "adapters" / "toolchain" / "cxx.ab").string());
    arco::RunResult adapter_result = runtime_.run_string(adapter_source);
    if (!adapter_result.ok) throw std::runtime_error("rivet: internal error loading adapters/toolchain/cxx.ab: " + adapter_result.error);
}

void VM::load_build_script(const std::string& path) {
    std::string source = read_file(path);
    arco::RunResult result = runtime_.run_string(source);
    if (!result.ok) throw std::runtime_error("rivet: " + path + " failed: " + result.error);
}

void VM::register_host_contracts() {
    runtime_.register_function("RIVET.Log.Info", [](const std::vector<arco::Value>&) -> arco::Value { return arco::Value(); });
    runtime_.register_function("RIVET.Log.Warn", [](const std::vector<arco::Value>& args) -> arco::Value {
        std::fprintf(stderr, "rivet: warning: %s\n", require_string(args, 0, "RIVET.Log.Warn").c_str());
        return arco::Value();
    });
    runtime_.register_function("RIVET.Log.Error", [](const std::vector<arco::Value>& args) -> arco::Value {
        std::fprintf(stderr, "rivet: error: %s\n", require_string(args, 0, "RIVET.Log.Error").c_str());
        return arco::Value();
    });

    runtime_.register_function("RIVET.Filesystem.ProjectRoot", [this](const std::vector<arco::Value>&) -> arco::Value {
        return arco::Value(project_root_);
    });

    // Recursive listing rooted at `root` (project-root-relative or absolute), returning
    // {Name,Path,Directory,Extension,IsFile,IsDirectory,Size} objects -- the richer shape
    // DIRECTORY() (rivet/stdlib/rivet.abas) needs, matching RFC section 9's own Path abstraction.
    // Skips .git/.rivet/build while walking, the same discipline Fissure's own Filesystem.Walk
    // established.
    runtime_.register_function("RIVET.Filesystem.WalkFiles", [this](const std::vector<arco::Value>& args) -> arco::Value {
        std::string root_arg = require_string(args, 0, "RIVET.Filesystem.WalkFiles");
        std::filesystem::path root = std::filesystem::path(root_arg).is_absolute()
            ? std::filesystem::path(root_arg)
            : std::filesystem::path(project_root_) / root_arg;

        static const std::set<std::string> skip_dirs = {".git", ".rivet", "build"};
        arco::Value::Array entries;
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error);
             it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
            if (error) break;
            if (it->is_directory(error)) {
                if (skip_dirs.count(it->path().filename().string())) it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(error)) continue;

            // Relative to project_root_, matching every other path this VM's contracts hand back
            // to ArcoBASIC (BuildTarget.Build()'s own outputs -- object files, link output -- are
            // always constructed relative, e.g. ".rivet/obj/..."). Found the hard way: an earlier
            // version returned absolute paths here, which meant a BuildAction's `inputs` (sourced
            // from this function) and `outputs` (constructed relative in rivet.abas) disagreed on
            // path form, and `rivet why rebuild <file>`'s own lookup (which normalizes its
            // argument to project-root-relative) silently never matched anything recorded.
            std::error_code relative_error;
            std::string relative_path = std::filesystem::relative(it->path(), project_root_, relative_error).generic_string();
            std::string display_path = relative_error ? it->path().generic_string() : relative_path;

            arco::Value::Object entry;
            entry["Name"] = arco::Value(it->path().filename().string());
            entry["Path"] = arco::Value(display_path);
            entry["Directory"] = arco::Value(std::filesystem::path(display_path).parent_path().generic_string());
            entry["Extension"] = arco::Value(it->path().extension().string());
            entry["IsFile"] = arco::Value(true);
            entry["IsDirectory"] = arco::Value(false);
            std::uintmax_t size = std::filesystem::file_size(it->path(), error);
            entry["Size"] = arco::Value(error ? 0.0 : static_cast<double>(size));
            entries.emplace_back(std::move(entry));
        }
        return arco::Value(std::move(entries));
    });

    runtime_.register_function("RIVET.Toolchain.DetectCxx", [](const std::vector<arco::Value>&) -> arco::Value {
        CxxToolchain toolchain = detect_cxx();
        return arco::Value(arco::Value::Object{
            {"Found", toolchain.found},
            {"Path", toolchain.path},
            {"Family", toolchain.family},
            {"Version", toolchain.version},
        });
    });

    runtime_.register_function("RIVET.Host.OS", [](const std::vector<arco::Value>&) -> arco::Value {
#if defined(__linux__)
        return arco::Value(std::string("linux"));
#elif defined(_WIN32)
        return arco::Value(std::string("windows"));
#elif defined(__APPLE__)
        return arco::Value(std::string("macos"));
#else
        return arco::Value(std::string("unknown"));
#endif
    });
    runtime_.register_function("RIVET.Host.Architecture", [](const std::vector<arco::Value>&) -> arco::Value {
#if defined(__linux__)
        struct utsname info{};
        if (uname(&info) == 0) return arco::Value(std::string(info.machine));
        return arco::Value(std::string("unknown"));
#else
        return arco::Value(std::string("unknown"));
#endif
    });

    // The only host contract that mutates the graph -- everything else in this VM is a read-only
    // primitive. `spec` is one ArcoBASIC object literal built by BuildTarget.Build()
    // (rivet/stdlib/rivet.abas) -- see that file's own comment for the exact field shape.
    runtime_.register_function("RIVET.Graph.Action", [this](const std::vector<arco::Value>& args) -> arco::Value {
        if (args.empty() || !args[0].is_object()) throw std::runtime_error("RIVET.Graph.Action expects one object argument");
        const arco::Value::Object& spec = args[0].as_object();

        BuildAction action;
        std::string type_text = string_field(spec, "Type", "Compile");
        action.type = (type_text == "Link") ? ActionType::Link : ActionType::Compile;
        action.target = string_field(spec, "Target");
        action.display_name = string_field(spec, "DisplayName");
        action.inputs = string_array_field(spec, "Inputs");
        action.outputs = string_array_field(spec, "Outputs");
        action.tool = string_field(spec, "Tool");
        action.tool_version = string_field(spec, "ToolVersion");
        action.arguments = string_array_field(spec, "Arguments");
        action.dependencies = string_array_field(spec, "Dependencies");
        action.origin.script = "build.abas";
        action.origin.function = string_field(spec, "OriginFunction", "?");

        if (action.outputs.empty()) throw std::runtime_error("RIVET.Graph.Action: action has no declared outputs");
        action.identity = to_string(action.type) + ":" + action.outputs[0];

        graph_.add_action(action);
        return arco::Value(action.identity);
    });
}

} // namespace rivet
