#pragma once

// RFC section 12: "First implementation may use simple parallel execution; scheduling
// intelligence can evolve later without changing the probe model." M1 doesn't even require
// parallel (section 20 lists real scheduling under M2) -- this runs probes sequentially, but
// through its own seam so a concurrent version later doesn't change ImpactEngine, VM, or the CLI.

#include "fissure/events.hpp"
#include "fissure/types.hpp"

#include <vector>

namespace fissure {

class Scheduler {
public:
    explicit Scheduler(EventBus& events) : events_(events) {}

    // Runs every probe in `probes` whose id appears in `to_run`, in order, emitting
    // ProbeStarted/ProbePassed/ProbeFailed for each. Returns one ProbeResult per executed probe.
    std::vector<ProbeResult> run(const std::vector<Probe>& probes, const std::vector<std::string>& to_run) const;

private:
    EventBus& events_;
};

} // namespace fissure
