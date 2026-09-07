#include "fissure/graph.hpp"

#include <deque>

namespace fissure {

void Graph::add_node(Node node) {
    nodes_[node.id] = std::move(node);
}

bool Graph::has_node(const std::string& id) const {
    return nodes_.find(id) != nodes_.end();
}

const Node* Graph::find_node(const std::string& id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}

void Graph::add_edge(Edge edge) {
    edges_.push_back(std::move(edge));
    index_dirty_ = true;
}

void Graph::rebuild_index() const {
    out_index_.clear();
    in_index_.clear();
    for (std::size_t i = 0; i < edges_.size(); ++i) {
        out_index_[edges_[i].from].push_back(i);
        in_index_[edges_[i].to].push_back(i);
    }
    index_dirty_ = false;
}

std::vector<const Edge*> Graph::outgoing(const std::string& node_id) const {
    if (index_dirty_) rebuild_index();
    std::vector<const Edge*> result;
    auto it = out_index_.find(node_id);
    if (it == out_index_.end()) return result;
    result.reserve(it->second.size());
    for (std::size_t i : it->second) result.push_back(&edges_[i]);
    return result;
}

std::vector<const Edge*> Graph::incoming(const std::string& node_id) const {
    if (index_dirty_) rebuild_index();
    std::vector<const Edge*> result;
    auto it = in_index_.find(node_id);
    if (it == in_index_.end()) return result;
    result.reserve(it->second.size());
    for (std::size_t i : it->second) result.push_back(&edges_[i]);
    return result;
}

std::vector<Graph::Reached> Graph::propagate(const std::vector<std::string>& seeds, int max_depth,
                                              Direction direction) const {
    if (index_dirty_) rebuild_index();
    std::vector<Reached> result;
    std::unordered_set<std::string> visited;
    std::deque<std::pair<std::string, int>> queue;
    for (const auto& seed : seeds) {
        if (visited.insert(seed).second) {
            queue.emplace_back(seed, 0);
            result.push_back({seed, 0, nullptr});
        }
    }
    while (!queue.empty()) {
        auto [node_id, depth] = queue.front();
        queue.pop_front();
        if (depth >= max_depth) continue;
        const auto& next_edges = direction == Direction::Outgoing ? outgoing(node_id) : incoming(node_id);
        for (const Edge* edge : next_edges) {
            const std::string& next_node = direction == Direction::Outgoing ? edge->to : edge->from;
            if (visited.insert(next_node).second) {
                queue.emplace_back(next_node, depth + 1);
                result.push_back({next_node, depth + 1, edge});
            }
        }
    }
    return result;
}

} // namespace fissure
