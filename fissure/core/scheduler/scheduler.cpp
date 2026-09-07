#include "fissure/scheduler.hpp"

#include "fissure/platform.hpp"

#include <unordered_set>

namespace fissure {

std::vector<ProbeResult> Scheduler::run(const std::vector<Probe>& probes, const std::vector<std::string>& to_run) const {
    std::unordered_set<std::string> selected(to_run.begin(), to_run.end());
    std::vector<ProbeResult> results;
    for (const auto& probe : probes) {
        if (!selected.count(probe.id)) continue;

        events_.emit(EventType::ProbeStarted, {{"probe", probe.id}});
        platform::ProcessResult process_result = platform::run_process(probe.command);

        ProbeResult result;
        result.probe_id = probe.id;
        result.passed = process_result.ok;
        result.duration_seconds = process_result.duration.count();
        result.output = process_result.output;

        events_.emit(result.passed ? EventType::ProbePassed : EventType::ProbeFailed,
                      {{"probe", probe.id},
                       {"duration_seconds", std::to_string(result.duration_seconds)},
                       {"exit_code", std::to_string(process_result.exit_code)},
                       // RFC section 21 acceptance criterion 11: "A failed probe produces a
                       // Fracture result with useful diagnostics." The renderer only prints this
                       // on failure (a passing probe's full captured output would be pure noise
                       // at scale, matching the RFC's own terse example CLI output, section
                       // 13.2) -- it is carried on every result regardless, since a future
                       // frontend (or `fissure explain`-style tooling) may want it either way.
                       {"output", result.output}});

        results.push_back(std::move(result));
    }
    return results;
}

} // namespace fissure
