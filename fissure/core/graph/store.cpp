#include "fissure/store.hpp"

#include <sqlite3.h>

#include <chrono>
#include <sstream>
#include <stdexcept>

namespace fissure {

namespace {

// Attributes are a small, flat string->string map -- encoding them with ASCII unit/record
// separators avoids pulling in a JSON library for something this simple. \x1F separates key from
// value, \x1E separates pairs; neither character is plausible in a file path or attribute value
// this project actually stores.
constexpr char kUnitSep = '\x1F';
constexpr char kRecordSep = '\x1E';

std::string encode_attributes(const std::map<std::string, std::string>& attributes) {
    std::string result;
    for (const auto& [key, value] : attributes) {
        if (!result.empty()) result.push_back(kRecordSep);
        result += key;
        result.push_back(kUnitSep);
        result += value;
    }
    return result;
}

std::map<std::string, std::string> decode_attributes(const std::string& encoded) {
    std::map<std::string, std::string> result;
    std::size_t start = 0;
    while (start <= encoded.size()) {
        std::size_t end = encoded.find(kRecordSep, start);
        if (end == std::string::npos) end = encoded.size();
        std::string pair = encoded.substr(start, end - start);
        if (!pair.empty()) {
            std::size_t sep = pair.find(kUnitSep);
            if (sep != std::string::npos) {
                result[pair.substr(0, sep)] = pair.substr(sep + 1);
            }
        }
        start = end + 1;
    }
    return result;
}

void exec_or_throw(sqlite3* db, const std::string& sql) {
    char* error_message = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message) != SQLITE_OK) {
        std::string message = error_message ? error_message : "unknown sqlite error";
        sqlite3_free(error_message);
        throw std::runtime_error("fissure: sqlite error: " + message);
    }
}

struct Statement {
    sqlite3_stmt* stmt = nullptr;
    Statement(sqlite3* db, const std::string& sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("fissure: sqlite prepare failed: ") + sqlite3_errmsg(db));
        }
    }
    ~Statement() { sqlite3_finalize(stmt); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

} // namespace

GraphStore::GraphStore(sqlite3* handle) : handle_(handle) {}

GraphStore::GraphStore(GraphStore&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

GraphStore& GraphStore::operator=(GraphStore&& other) noexcept {
    if (this != &other) {
        if (handle_) sqlite3_close(handle_);
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

GraphStore::~GraphStore() {
    if (handle_) sqlite3_close(handle_);
}

GraphStore GraphStore::open(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::string message = db ? sqlite3_errmsg(db) : "could not allocate sqlite handle";
        if (db) sqlite3_close(db);
        throw std::runtime_error("fissure: could not open " + path + ": " + message);
    }

    exec_or_throw(db, "PRAGMA foreign_keys = ON;");
    exec_or_throw(db,
        "CREATE TABLE IF NOT EXISTS schema_meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS nodes (id TEXT PRIMARY KEY, kind TEXT NOT NULL, attributes TEXT NOT NULL);"
        "CREATE TABLE IF NOT EXISTS edges (from_id TEXT NOT NULL, to_id TEXT NOT NULL, kind TEXT NOT NULL, "
        "evidence TEXT NOT NULL, confidence REAL NOT NULL);"
        "CREATE INDEX IF NOT EXISTS edges_from_idx ON edges(from_id);"
        "CREATE INDEX IF NOT EXISTS edges_to_idx ON edges(to_id);"
        "CREATE TABLE IF NOT EXISTS probe_results (probe_id TEXT NOT NULL, passed INTEGER NOT NULL, "
        "duration_seconds REAL NOT NULL, output TEXT NOT NULL, recorded_at INTEGER NOT NULL);"
        "CREATE INDEX IF NOT EXISTS probe_results_probe_idx ON probe_results(probe_id, recorded_at DESC);"
    );

    // Schema versioning (RFC 16 / AGENT_PROGRESS.md design decision #4): a fresh database has no
    // schema_meta row yet, so seed it with the current version rather than treating "no row" as
    // an error. An existing database whose stored version doesn't match this build's
    // kSchemaVersion is refused outright -- there is no migration ladder yet (nothing has shipped
    // a schema before this one), so a mismatch can only mean a newer or incompatible build wrote
    // it, and silently reading rows in an unexpected shape is exactly the failure mode this
    // versioning exists to prevent.
    {
        Statement select(db, "SELECT value FROM schema_meta WHERE key = 'schema_version';");
        int step = sqlite3_step(select.stmt);
        if (step == SQLITE_ROW) {
            std::string stored = reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 0));
            int stored_version = std::stoi(stored);
            if (stored_version != kSchemaVersion) {
                sqlite3_close(db);
                throw std::runtime_error("fissure: " + path + " was written by schema version " +
                                          stored + ", this build only understands version " +
                                          std::to_string(kSchemaVersion) +
                                          " -- no migration path exists yet; delete the .fissure "
                                          "state directory to rebuild it from scratch");
            }
        } else {
            Statement insert(db, "INSERT INTO schema_meta(key, value) VALUES ('schema_version', ?1);");
            bind_text(insert.stmt, 1, std::to_string(kSchemaVersion));
            sqlite3_step(insert.stmt);
        }
    }

    return GraphStore(db);
}

void GraphStore::save(const Graph& graph) {
    exec_or_throw(handle_, "BEGIN TRANSACTION;");
    try {
        exec_or_throw(handle_, "DELETE FROM nodes; DELETE FROM edges;");
        {
            Statement insert(handle_, "INSERT INTO nodes(id, kind, attributes) VALUES (?1, ?2, ?3);");
            for (const auto& [id, node] : graph.nodes()) {
                sqlite3_reset(insert.stmt);
                bind_text(insert.stmt, 1, node.id);
                bind_text(insert.stmt, 2, to_string(node.kind));
                bind_text(insert.stmt, 3, encode_attributes(node.attributes));
                if (sqlite3_step(insert.stmt) != SQLITE_DONE) {
                    throw std::runtime_error(std::string("fissure: failed to write node: ") + sqlite3_errmsg(handle_));
                }
            }
        }
        {
            Statement insert(handle_,
                "INSERT INTO edges(from_id, to_id, kind, evidence, confidence) VALUES (?1, ?2, ?3, ?4, ?5);");
            for (const auto& edge : graph.edges()) {
                sqlite3_reset(insert.stmt);
                bind_text(insert.stmt, 1, edge.from);
                bind_text(insert.stmt, 2, edge.to);
                bind_text(insert.stmt, 3, to_string(edge.kind));
                bind_text(insert.stmt, 4, to_string(edge.evidence));
                sqlite3_bind_double(insert.stmt, 5, edge.confidence);
                if (sqlite3_step(insert.stmt) != SQLITE_DONE) {
                    throw std::runtime_error(std::string("fissure: failed to write edge: ") + sqlite3_errmsg(handle_));
                }
            }
        }
        exec_or_throw(handle_, "COMMIT;");
    } catch (...) {
        exec_or_throw(handle_, "ROLLBACK;");
        throw;
    }
}

Graph GraphStore::load() const {
    Graph graph;
    {
        Statement select(handle_, "SELECT id, kind, attributes FROM nodes;");
        while (sqlite3_step(select.stmt) == SQLITE_ROW) {
            Node node;
            node.id = reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 0));
            auto kind = node_kind_from_string(reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 1)));
            node.kind = kind.value_or(NodeKind::File);
            node.attributes = decode_attributes(reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 2)));
            graph.add_node(std::move(node));
        }
    }
    {
        Statement select(handle_, "SELECT from_id, to_id, kind, evidence, confidence FROM edges;");
        while (sqlite3_step(select.stmt) == SQLITE_ROW) {
            Edge edge;
            edge.from = reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 0));
            edge.to = reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 1));
            auto kind = edge_kind_from_string(reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 2)));
            edge.kind = kind.value_or(EdgeKind::Reads);
            auto evidence = evidence_from_string(reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 3)));
            edge.evidence = evidence.value_or(Evidence::Declared);
            edge.confidence = sqlite3_column_double(select.stmt, 4);
            graph.add_edge(std::move(edge));
        }
    }
    return graph;
}

void GraphStore::record_result(const ProbeResult& result) {
    Statement insert(handle_,
        "INSERT INTO probe_results(probe_id, passed, duration_seconds, output, recorded_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5);");
    bind_text(insert.stmt, 1, result.probe_id);
    sqlite3_bind_int(insert.stmt, 2, result.passed ? 1 : 0);
    sqlite3_bind_double(insert.stmt, 3, result.duration_seconds);
    bind_text(insert.stmt, 4, result.output);
    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sqlite3_bind_int64(insert.stmt, 5, static_cast<sqlite3_int64>(now));
    if (sqlite3_step(insert.stmt) != SQLITE_DONE) {
        throw std::runtime_error(std::string("fissure: failed to record probe result: ") + sqlite3_errmsg(handle_));
    }
}

std::vector<ProbeResult> GraphStore::history_for(const std::string& probe_id, int limit) const {
    Statement select(handle_,
        "SELECT probe_id, passed, duration_seconds, output FROM probe_results "
        "WHERE probe_id = ?1 ORDER BY recorded_at DESC LIMIT ?2;");
    bind_text(select.stmt, 1, probe_id);
    sqlite3_bind_int(select.stmt, 2, limit);
    std::vector<ProbeResult> results;
    while (sqlite3_step(select.stmt) == SQLITE_ROW) {
        ProbeResult result;
        result.probe_id = reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 0));
        result.passed = sqlite3_column_int(select.stmt, 1) != 0;
        result.duration_seconds = sqlite3_column_double(select.stmt, 2);
        result.output = reinterpret_cast<const char*>(sqlite3_column_text(select.stmt, 3));
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace fissure
