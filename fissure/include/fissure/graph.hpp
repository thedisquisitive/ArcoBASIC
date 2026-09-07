#pragma once

#include "fissure/types.hpp"

#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace fissure {

// The in-memory Fault Graph (RFC section 5). Node/Edge storage plus the traversal primitive
// impact propagation (core/impact) is built on. Persistence (SQLite) is a separate concern --
// see store.hpp -- this class has no I/O of its own so it stays trivially unit-testable (RFC
// 22.7: "Unit tests for graph traversal").
class Graph {
public:
    // Upserts by id -- calling this again for an id already present replaces its kind/attributes,
    // matching how repeated adapter scans of the same file should behave (last-write-wins, not
    // duplicate nodes).
    void add_node(Node node);
    bool has_node(const std::string& id) const;
    const Node* find_node(const std::string& id) const;
    const std::unordered_map<std::string, Node>& nodes() const { return nodes_; }

    // Edges are not deduplicated by (from, to, kind) alone -- the same pair can carry edges from
    // multiple evidence sources (e.g. a declared association AND a later observed one), and
    // `fissure explain` wants to show all of them, not collapse to one.
    void add_edge(Edge edge);
    const std::vector<Edge>& edges() const { return edges_; }
    std::vector<const Edge*> outgoing(const std::string& node_id) const;
    std::vector<const Edge*> incoming(const std::string& node_id) const;

    // Breadth-first traversal outward from `seeds`, up to `max_depth` hops (0 = seeds only).
    // Returns every reached node id together with the hop distance and the edge that reached it
    // (nullptr for a seed itself) -- impact.cpp uses the edge to build a human-readable
    // propagation path for `fissure explain`.
    struct Reached {
        std::string node_id;
        int depth = 0;
        const Edge* via = nullptr;
    };

    // Direction matters and is easy to get backwards: this codebase's edges point "subject ->
    // object it touches/depends on" (an importer -> what it imports, a probe -> what it observed
    // touching -- matching the RFC's own section 4.4 and section 10 examples, both drawn as
    // cause -> effect chains ending at a probe). Finding what a CHANGE affects therefore means
    // walking BACKWARD from the changed node -- via incoming edges, asking "what points at me" --
    // not forward via outgoing edges (that would instead answer "what does the changed node
    // itself depend on", the opposite question). impact.cpp always calls this with
    // Direction::Incoming; Direction::Outgoing is kept as a real, usable general-purpose
    // primitive for the RFC's own section 23 future queries ("what needs recompiling" from a
    // build target does want forward/outgoing traversal), not for impact analysis.
    enum class Direction { Outgoing, Incoming };
    std::vector<Reached> propagate(const std::vector<std::string>& seeds, int max_depth = 64,
                                    Direction direction = Direction::Incoming) const;

    std::size_t node_count() const { return nodes_.size(); }
    std::size_t edge_count() const { return edges_.size(); }

private:
    std::unordered_map<std::string, Node> nodes_;
    std::vector<Edge> edges_;
    // node id -> indices into edges_ starting/ending at that node. Rebuilt lazily on first
    // traversal after a mutation rather than kept incrementally in sync on every add_edge -- graph
    // construction (many add_edge calls in a row during an adapter scan) is the hot path, not
    // traversal-after-every-edge.
    mutable std::unordered_map<std::string, std::vector<std::size_t>> out_index_;
    mutable std::unordered_map<std::string, std::vector<std::size_t>> in_index_;
    mutable bool index_dirty_ = true;
    void rebuild_index() const;
};

} // namespace fissure
