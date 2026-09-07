#include "rivet/scheduler.hpp"

#include "rivet/platform.hpp"

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace rivet {

Scheduler::Scheduler(CacheManager& cache, unsigned jobs) : cache_(cache), jobs_(jobs == 0 ? 1 : jobs) {}

std::vector<ActionResult> Scheduler::run(const BuildGraph& graph, const SchedulerEvent& on_event) {
    // Validate the whole graph up front (throws GraphCycleError, citing provenance) before any
    // worker starts -- failing fast and clearly beats a worker pool deadlocking on a cycle nothing
    // ever resolves.
    (void)graph.topo_order();

    struct Node {
        const BuildAction* action;
        std::size_t remaining_dependencies;
        std::vector<std::string> dependents;
        bool blocked_by_failure = false;
    };

    std::unordered_map<std::string, Node> nodes;
    for (const auto& action : graph.actions()) {
        nodes[action.identity] = Node{&action, action.dependencies.size(), {}, false};
    }
    for (const auto& action : graph.actions()) {
        for (const auto& dependency : action.dependencies) {
            nodes[dependency].dependents.push_back(action.identity);
        }
    }

    std::mutex mutex;
    std::condition_variable ready_cv;
    std::deque<std::string> ready_queue;
    std::size_t remaining = nodes.size();
    std::vector<ActionResult> results;
    results.reserve(nodes.size());

    for (const auto& [identity, node] : nodes) {
        if (node.remaining_dependencies == 0) ready_queue.push_back(identity);
    }

    auto worker = [&]() {
        for (;;) {
            std::string identity;
            {
                std::unique_lock<std::mutex> lock(mutex);
                ready_cv.wait(lock, [&] { return !ready_queue.empty() || remaining == 0; });
                if (remaining == 0 && ready_queue.empty()) return;
                identity = std::move(ready_queue.front());
                ready_queue.pop_front();
            }

            Node& node = nodes[identity];
            const BuildAction& action = *node.action;
            ActionResult result;
            result.identity = identity;

            if (on_event) on_event("start", ActionResult{identity, ActionStatus::Ran, 0.0, "", 0, ""});

            if (node.blocked_by_failure) {
                result.status = ActionStatus::SkippedDueToFailure;
                result.reason = "a dependency failed";
                if (on_event) on_event("skipped", result);
            } else {
                CacheDecision decision = cache_.evaluate(action);
                if (decision.hit) {
                    result.status = ActionStatus::CacheHit;
                    result.duration_seconds = 0.0;
                    result.reason = decision.reason;
                    if (on_event) on_event("cache", result);
                    cache_.record(action, decision, ActionStatus::CacheHit, 0.0);
                } else {
                    for (const auto& output : action.outputs) {
                        std::filesystem::path parent = std::filesystem::path(output).parent_path();
                        if (!parent.empty()) {
                            std::error_code error;
                            std::filesystem::create_directories(parent, error);
                        }
                    }
                    std::vector<std::string> argv;
                    argv.push_back(action.tool);
                    for (const auto& arg : action.arguments) argv.push_back(arg);
                    platform::ProcessResult process_result = platform::run_process(argv);

                    result.duration_seconds = process_result.duration.count();
                    result.output = process_result.output;
                    result.exit_code = process_result.exit_code;
                    result.reason = decision.reason;

                    if (process_result.ok) {
                        result.status = ActionStatus::Ran;
                        if (on_event) on_event("ran", result);
                        cache_.record(action, decision, ActionStatus::Ran, result.duration_seconds);
                    } else {
                        result.status = ActionStatus::Failed;
                        if (on_event) on_event("failed", result);
                        // Do not record a failed run's fingerprint as if it succeeded -- record()
                        // would make a subsequent build think this action is cached at a state
                        // that never actually produced valid outputs.
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex);
                results.push_back(result);
                --remaining;
                bool this_failed_or_skipped =
                    result.status == ActionStatus::Failed || result.status == ActionStatus::SkippedDueToFailure;
                for (const auto& dependent_identity : node.dependents) {
                    Node& dependent = nodes[dependent_identity];
                    if (this_failed_or_skipped) dependent.blocked_by_failure = true;
                    if (--dependent.remaining_dependencies == 0) ready_queue.push_back(dependent_identity);
                }
            }
            ready_cv.notify_all();
        }
    };

    unsigned worker_count = std::min<unsigned>(jobs_, static_cast<unsigned>(std::max<std::size_t>(1, nodes.size())));
    std::vector<std::thread> workers;
    workers.reserve(worker_count);
    for (unsigned i = 0; i < worker_count; ++i) workers.emplace_back(worker);
    for (auto& thread : workers) thread.join();

    return results;
}

} // namespace rivet
