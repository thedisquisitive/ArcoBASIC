#include "rivet/graph.hpp"

#include <unordered_set>

namespace rivet {

void BuildGraph::add_action(BuildAction action) {
    auto existing = index_by_identity_.find(action.identity);
    if (existing != index_by_identity_.end()) {
        throw std::runtime_error("rivet: duplicate action identity '" + action.identity +
                                  "' (registered by " + actions_[existing->second].origin.function +
                                  ", again by " + action.origin.function + ")");
    }
    for (const auto& output : action.outputs) {
        auto owner = owner_of_output_.find(output);
        if (owner != owner_of_output_.end() && owner->second != action.identity) {
            throw std::runtime_error("rivet: output '" + output + "' is claimed by both '" +
                                      owner->second + "' and '" + action.identity +
                                      "' -- no two actions may declare the same output");
        }
    }
    for (const auto& output : action.outputs) owner_of_output_[output] = action.identity;
    index_by_identity_[action.identity] = actions_.size();
    actions_.push_back(std::move(action));
}

const BuildAction* BuildGraph::find(const std::string& identity) const {
    auto it = index_by_identity_.find(identity);
    if (it == index_by_identity_.end()) return nullptr;
    return &actions_[it->second];
}

std::vector<std::string> BuildGraph::topo_order() const {
    // Standard iterative DFS-based topological sort with an explicit recursion stack for cycle
    // detection -- recursive DFS would risk a real stack overflow on a large enough graph, and an
    // iterative approach also makes it straightforward to capture the exact cycle path for
    // GraphCycleError rather than just detecting that one exists.
    enum class Mark { Unvisited, InProgress, Done };
    std::unordered_map<std::string, Mark> mark;
    for (const auto& action : actions_) mark[action.identity] = Mark::Unvisited;

    std::vector<std::string> order;
    order.reserve(actions_.size());

    for (const auto& start : actions_) {
        if (mark[start.identity] != Mark::Unvisited) continue;

        struct Frame { const BuildAction* action; std::size_t next_dependency; };
        std::vector<Frame> stack;
        stack.push_back({&start, 0});
        mark[start.identity] = Mark::InProgress;

        while (!stack.empty()) {
            Frame& frame = stack.back();
            if (frame.next_dependency < frame.action->dependencies.size()) {
                const std::string& dep_identity = frame.action->dependencies[frame.next_dependency];
                ++frame.next_dependency;
                const BuildAction* dep = find(dep_identity);
                if (!dep) {
                    throw std::runtime_error("rivet: action '" + frame.action->identity +
                                              "' depends on unknown action '" + dep_identity + "'");
                }
                Mark dep_mark = mark[dep_identity];
                if (dep_mark == Mark::InProgress) {
                    // Reconstruct the cycle from the current stack, starting at dep_identity.
                    std::vector<std::string> cycle;
                    bool collecting = false;
                    for (const auto& entry : stack) {
                        if (entry.action->identity == dep_identity) collecting = true;
                        if (collecting) cycle.push_back(entry.action->identity);
                    }
                    cycle.push_back(dep_identity);
                    throw GraphCycleError(std::move(cycle));
                }
                if (dep_mark == Mark::Unvisited) {
                    mark[dep_identity] = Mark::InProgress;
                    stack.push_back({dep, 0});
                }
                continue;
            }
            mark[frame.action->identity] = Mark::Done;
            order.push_back(frame.action->identity);
            stack.pop_back();
        }
    }

    return order;
}

} // namespace rivet
