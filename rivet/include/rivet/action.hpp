#pragma once

// The RFC's own central normalization (rivet-rfc.md section 11): every unit of build work becomes
// a BuildAction. Everything downstream (dependency ordering, incremental checks, caching,
// concurrency, explanation) operates on this generic shape, never on source-language-specific
// concepts -- RFC section 54 rule #11: "Do not hard-code C++ assumptions into the core graph
// architecture." ActionType is deliberately just Compile/Link for this M1 slice (RFC's own
// CompileAction/LinkAction); GenerateAction/CopyAction/PackageAction/ProcessAction etc. are real,
// disclosed M6 follow-up work -- see rivet/RIVET_PROGRESS.md.

#include <string>
#include <vector>

namespace rivet {

enum class ActionType { Compile, Archive, Link };

std::string to_string(ActionType type);

enum class ActionStatus { CacheHit, Ran, Failed, SkippedDueToFailure };

std::string to_string(ActionStatus status);

// RFC section 26 (Build Script Provenance). Script:Function only for this slice, not Script:Line
// -- `arco::Runtime` exposes no accessor a host function could use to read the currently-
// executing script's line number mid-call. Adding one is real, small, separate follow-up work,
// not attempted here (see RIVET_PROGRESS.md's own disclosed-gaps list).
struct Origin {
    std::string script;
    std::string function;
};

struct BuildAction {
    std::string identity;      // "<Type>:<primary output>" -- stable logical identity (RFC section 49)
    std::string display_name;   // source path (Compile) or output path (Link) -- what logs/why print
    ActionType type = ActionType::Compile;
    std::string target;          // owning target name, e.g. "tinyapp"
    std::vector<std::string> inputs;    // outputs[0] is the "primary" output used for identity
    std::vector<std::string> outputs;
    std::string tool;             // resolved absolute compiler path
    std::string tool_version;      // `<tool> --version`'s first line, captured once at detection time
    std::vector<std::string> arguments;   // real argv -- never a shell string, see platform.hpp
    std::vector<std::string> dependencies; // upstream action identities
    Origin origin;
};

} // namespace rivet
