#include "fissure/impact.hpp"

#include <unordered_map>
#include <unordered_set>

namespace fissure {

namespace {

// Evidence sources strong enough, on their own, to prove UNAFFECTED (RFC 6.1: "Evidence is
// sufficiently strong to prove the probe cannot meaningfully observe the disturbance") for a
// probe that propagation did not reach. M1 has exactly one such source: an explicit, human-
// authored Declared association (AGENT_PROGRESS.md Session 1 design decision #1) -- a project
// author who wrote `FISSURE.Probe.DependsOn` enumerated that probe's full dependency set on
// purpose. Observed (Tier 4, RFC 7.3) is a real future candidate for this same set once a runtime
// observation backend exists (M4) -- deliberately not included yet, since nothing produces that
// evidence today and treating an absent capability as proof would violate the safety invariant
// this function exists to uphold.
bool is_sufficient_for_unaffected(Evidence evidence) {
    return evidence == Evidence::Declared;
}

} // namespace

std::vector<ProbeVerdict> ImpactEngine::classify(const Graph& graph, const std::vector<std::string>& disturbance) const {
    std::vector<ProbeVerdict> verdicts;

    // Backward propagation from every changed node (Graph::propagate's own doc comment explains
    // why Incoming, not Outgoing, is the correct direction here) finds every node that has some
    // path of "touches/depends on" edges leading to a changed node -- i.e. everything a change
    // could plausibly ripple into, transitively.
    auto reached = graph.propagate(disturbance, /*max_depth=*/64, Graph::Direction::Incoming);
    std::unordered_map<std::string, const Graph::Reached*> reached_by_id;
    for (const auto& entry : reached) reached_by_id[entry.node_id] = &entry;

    std::unordered_set<std::string> disturbance_set(disturbance.begin(), disturbance.end());

    for (const auto& [id, node] : graph.nodes()) {
        if (node.kind != NodeKind::Probe) continue;

        ProbeVerdict verdict;
        verdict.probe_id = id;

        auto it = reached_by_id.find(id);
        if (it != reached_by_id.end() && it->second->depth > 0) {
            // AFFECTED: reconstruct the concrete path back to a changed node for `fissure
            // explain`, one hop at a time, by following `via` edges back through reached_by_id.
            verdict.classification = Classification::Affected;
            std::vector<std::string> path_lines;
            const Graph::Reached* cursor = it->second;
            while (cursor && cursor->via) {
                const Edge* edge = cursor->via;
                path_lines.push_back(edge->from + " --[" + fissure::to_string(edge->kind) + ", " +
                                      fissure::to_string(edge->evidence) + "]--> " + edge->to);
                auto next = reached_by_id.find(edge->from);
                cursor = (next != reached_by_id.end() && next->second->depth < cursor->depth) ? next->second : nullptr;
            }
            for (auto line = path_lines.rbegin(); line != path_lines.rend(); ++line) {
                verdict.reasons.push_back(*line);
            }
            if (verdict.reasons.empty()) {
                verdict.reasons.push_back(id + " is directly one of the changed nodes");
            }
            verdicts.push_back(std::move(verdict));
            continue;
        }

        // Not reached by propagation. Determine whether that absence is itself strong evidence
        // (a declared dependency set that plainly excludes every changed node) or just an
        // absence of information (nothing declared at all) -- these must NOT be treated the same
        // way. This is the literal enforcement point of RFC 6.1's invariant.
        bool has_sufficient_declaration = false;
        std::vector<std::string> declared_targets;
        for (const Edge* edge : graph.outgoing(id)) {
            if (is_sufficient_for_unaffected(edge->evidence)) {
                has_sufficient_declaration = true;
                declared_targets.push_back(edge->to);
            }
        }

        if (has_sufficient_declaration) {
            verdict.classification = Classification::Unaffected;
            verdict.reasons.push_back(
                "declared dependencies (" + std::to_string(declared_targets.size()) +
                " node(s)) do not intersect the disturbance, and no other evidence connects this "
                "probe to it");
            for (const auto& target : declared_targets) {
                verdict.reasons.push_back("  declared dependency: " + target +
                    (disturbance_set.count(target) ? " (CHANGED -- should not reach here; classifier bug)" : ""));
            }
        } else {
            verdict.classification = Classification::Unknown;
            verdict.reasons.push_back(
                "no declared, static, build, observed, or historical evidence connects this probe "
                "to anything -- absence of evidence is not proof of independence (RFC 6.1), so this "
                "probe runs");
        }
        verdicts.push_back(std::move(verdict));
    }

    return verdicts;
}

} // namespace fissure
