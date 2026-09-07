#pragma once

// RFC section 6: Impact Analysis, and section 6.1's tri-state classification. This is the single
// place the safety invariant ("UNKNOWN must never be silently converted to UNAFFECTED") is
// enforced -- see impact.cpp's own comment for exactly how.

#include "fissure/graph.hpp"
#include "fissure/types.hpp"

namespace fissure {

class ImpactEngine {
public:
    // Classifies every Probe-kind node in `graph`, given `disturbance` (the changed node ids --
    // typically File nodes, one per changed path). See impact.cpp for the classification rule.
    std::vector<ProbeVerdict> classify(const Graph& graph, const std::vector<std::string>& disturbance) const;
};

} // namespace fissure
