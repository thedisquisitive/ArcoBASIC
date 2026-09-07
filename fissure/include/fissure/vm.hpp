#pragma once

// The embedded ArcoBASIC VM boundary (RFC section 4.2, 7.5). Wraps arco::Runtime (this repo's own
// proven embedding API, see fissure/AGENT_PROGRESS.md's Session 1 "Task 1" findings), registers
// every FISSURE.* host contract, and loads extension scripts (adapters, project config) through
// it.

#include "fissure/events.hpp"
#include "fissure/graph.hpp"
#include "fissure/manifest.hpp"
#include "fissure/types.hpp"

#include "arco/runtime.hpp"

#include <set>
#include <string>
#include <vector>

namespace fissure {

class VM {
public:
    // `project_root` scopes FISSURE.Filesystem.* and FISSURE.Process.Exec's working directory --
    // extensions operate on the target project being analyzed, not Fissure's own source tree.
    VM(Graph& graph, EventBus& events, std::string project_root);

    // Reads `path`, parses its manifest (capability requirements accumulate into this VM
    // instance -- see the capability-model comment below), and executes it. Throws
    // std::runtime_error (propagated from arco::Runtime::run_string's own RunResult on failure,
    // or from a failed capability check inside a host contract) on any error.
    void load_script(const std::string& path);

    // Calls a top-level SUB/FUNCTION already defined by a previously loaded script (RFC section
    // 7.7's `ProjectImpact` convention) -- e.g. the CLI calls a well-known entry point after
    // loading every adapter for the current phase (discovery, then impact, then execution).
    // Returns true if `name` exists and was called; false (not an error) if no such callable was
    // ever defined, since not every extension implements every optional hook.
    bool call_if_defined(const std::string& name, const std::vector<arco::Value>& args = {});

    arco::Runtime& runtime() { return runtime_; }

    const std::vector<Probe>& probes() const { return probes_; }
    const std::vector<std::string>& reported_changes() const { return reported_changes_; }

    // Backs FISSURE.Change.SinceRef() -- what `git.ab` (or a future VCS adapter) compares the
    // working tree against. Empty means "adapter picks its own default" (git.ab defaults to
    // `HEAD`, i.e. working tree vs. last commit). Set from the CLI's own `--since` flag (RFC
    // section 15).
    void set_since_ref(std::string ref) { since_ref_ = std::move(ref); }

    // Capability model (RFC section 8), scoped honestly for M1: every script's declared
    // `#REQUIRES` capabilities are unioned into this ONE VM instance's granted set as it loads --
    // there is no per-script call-stack isolation (arco::Runtime shares one global namespace
    // across every loaded script, matching how every other embedder in this repo already uses
    // it; true least-privilege isolation between two adapters loaded into the same VM would need
    // either separate Runtime instances per adapter or call-stack-aware capability checks, neither
    // of which exists yet). What this DOES genuinely enforce, and is the real point: an adapter
    // that never declares a capability at all cannot invoke the FISSURE.* contract that capability
    // gates, from anywhere, in a VM where nothing else declared it either -- "deny undeclared
    // privileged operations" (section 8) is real, not cosmetic, it just is not yet inter-extension
    // isolation. Documented as a disclosed scope decision in AGENT_PROGRESS.md, not a silent gap.
    bool has_capability(const std::string& capability) const;

private:
    void register_host_contracts();

    arco::Runtime runtime_;
    Graph& graph_;
    EventBus& events_;
    std::string project_root_;
    std::set<std::string> granted_capabilities_;
    std::vector<Probe> probes_;
    std::vector<std::string> reported_changes_;
    std::string since_ref_;
};

} // namespace fissure
