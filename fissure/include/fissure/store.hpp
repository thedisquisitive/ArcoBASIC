#pragma once

// RFC section 16: "Initial storage recommendation: SQLite, hidden within a project-local Fissure
// state directory." AGENT_PROGRESS.md's Session 1 design decision #4 records the concrete path
// and schema-versioning shape this implements.

#include "fissure/graph.hpp"

#include <memory>
#include <string>

struct sqlite3;

namespace fissure {

// Current on-disk schema version this build writes and can read. GraphStore::open() compares this
// against the `schema_meta` table's stored value (RFC 16: "Schema should preserve provenance and
// versioning so graph evidence can be invalidated when adapters, compiler versions, or analysis
// methods change") -- a mismatch throws rather than silently misreading rows in an unexpected
// shape. There is no migration path yet (nothing has ever shipped a schema before this one); a
// real migration ladder is a natural M2+ addition once the schema has actually changed once.
constexpr int kSchemaVersion = 1;

class GraphStore {
public:
    ~GraphStore();
    GraphStore(const GraphStore&) = delete;
    GraphStore& operator=(const GraphStore&) = delete;
    GraphStore(GraphStore&&) noexcept;
    GraphStore& operator=(GraphStore&&) noexcept;

    // Opens (creating if absent) the SQLite database at `path`. Throws std::runtime_error on any
    // SQLite failure or on a schema-version mismatch this build doesn't know how to read.
    static GraphStore open(const std::string& path);

    // Replaces the entire persisted graph with `graph`'s current contents, inside one transaction
    // (all-or-nothing -- a crash mid-write leaves the previous persisted graph intact, never a
    // half-written one). Simple and correct over incremental for M1's scale; revisit if a real
    // project's graph size ever makes a full rewrite per run measurably slow.
    void save(const Graph& graph);

    // Loads the full persisted graph back into memory.
    Graph load() const;

    // Probe run history -- RFC section 9's "last successful run" / section 22.6's "persist
    // results" requirement. `history_for` returns the most recent results first.
    void record_result(const ProbeResult& result);
    std::vector<ProbeResult> history_for(const std::string& probe_id, int limit = 20) const;

private:
    explicit GraphStore(sqlite3* handle);
    sqlite3* handle_ = nullptr;
};

} // namespace fissure
