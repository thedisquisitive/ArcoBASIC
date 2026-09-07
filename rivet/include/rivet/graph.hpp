#pragma once

// The build action DAG (RFC section 46/51). Node = BuildAction (identified by its own `identity`
// field, RFC section 49), edges = the `dependencies` list on each action. This is deliberately
// generic over ActionType -- RFC section 54 rule #11.

#include "rivet/action.hpp"

#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace rivet {

// RFC section 51: cycles must be detected AND explained with provenance, not just "ERROR: cycle
// detected". `cycle_identities` lists every action identity in the cycle, in order; callers print
// each one's own Origin alongside it.
class GraphCycleError : public std::runtime_error {
public:
    explicit GraphCycleError(std::vector<std::string> cycle)
        : std::runtime_error("rivet: build dependency cycle detected"), cycle_identities(std::move(cycle)) {}
    std::vector<std::string> cycle_identities;
};

class BuildGraph {
public:
    // Throws std::runtime_error if `action.identity` is already registered with a DIFFERENT spec,
    // or if any of its declared outputs is already claimed by a different action -- enforced at
    // REGISTRATION time (RFC's "no concurrent writes to the same declared artifact", section 14),
    // not deferred to schedule time where the conflict would be much harder to explain clearly.
    void add_action(BuildAction action);

    const BuildAction* find(const std::string& identity) const;
    const std::vector<BuildAction>& actions() const { return actions_; }
    std::size_t size() const { return actions_.size(); }

    // Dependency-respecting order (each action appears after everything it depends on). Throws
    // GraphCycleError on a real cycle.
    std::vector<std::string> topo_order() const;

private:
    std::vector<BuildAction> actions_;
    std::unordered_map<std::string, std::size_t> index_by_identity_;
    std::unordered_map<std::string, std::string> owner_of_output_; // output path -> action identity
};

} // namespace rivet
