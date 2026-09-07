#pragma once

// Real native concurrent execution (RFC section 14) -- forced by a hard platform fact: no
// non-blocking process-spawn primitive is exposed to ArcoBASIC anywhere in this codebase.
// ArcoBASIC-side code only ever *describes* the graph; this is where actual OS-level concurrency
// happens, matching the RFC's own section-46 architecture diagram (Scheduler/Execution Engine/
// Process Runner are explicitly native subsystems below the ArcoBASIC object-model layer).

#include "rivet/action.hpp"
#include "rivet/cache.hpp"
#include "rivet/graph.hpp"

#include <functional>
#include <string>
#include <vector>

namespace rivet {

struct ActionResult {
    std::string identity;
    ActionStatus status = ActionStatus::Ran;
    double duration_seconds = 0.0;
    std::string output;  // captured combined stdout+stderr -- meaningful for Ran/Failed
    int exit_code = 0;
    std::string reason;   // cache decision reason, or the failure/skip explanation
};

// Fired once as an action starts (`kind` == "start") and once as it finishes ("cache"/"ran"/
// "failed"/"skipped") -- called from whichever worker thread finishes that action; the CLI's own
// callback must do its own synchronization if it isn't already thread-safe (e.g. std::cout under
// a mutex).
using SchedulerEvent = std::function<void(const std::string& kind, const ActionResult& result)>;

class Scheduler {
public:
    Scheduler(CacheManager& cache, unsigned jobs);

    // Runs every action in `graph`, respecting dependency order, using up to `jobs` concurrent
    // workers for independent actions. Throws GraphCycleError if the graph itself is cyclic
    // (checked up front via topo_order() before any worker starts).
    std::vector<ActionResult> run(const BuildGraph& graph, const SchedulerEvent& on_event = {});

private:
    CacheManager& cache_;
    unsigned jobs_;
};

} // namespace rivet
