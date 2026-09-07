#include "rivet/store.hpp"

#include <sqlite3.h>

#include <chrono>
#include <stdexcept>

namespace rivet {

namespace {

constexpr char kUnitSep = '\x1f';

std::string join(const std::vector<std::string>& parts) {
    std::string result;
    for (const auto& part : parts) {
        if (!result.empty()) result.push_back(kUnitSep);
        result += part;
    }
    return result;
}

void exec_or_throw(sqlite3* db, const std::string& sql) {
    char* error_message = nullptr;
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message) != SQLITE_OK) {
        std::string message = error_message ? error_message : "unknown sqlite error";
        sqlite3_free(error_message);
        throw std::runtime_error("rivet: sqlite error: " + message);
    }
}

struct Statement {
    sqlite3_stmt* stmt = nullptr;
    Statement(sqlite3* db, const std::string& sql) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error(std::string("rivet: sqlite prepare failed: ") + sqlite3_errmsg(db));
        }
    }
    ~Statement() { sqlite3_finalize(stmt); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
};

void bind_text(sqlite3_stmt* stmt, int index, const std::string& value) {
    sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

std::string column_text(sqlite3_stmt* stmt, int index) {
    const unsigned char* text = sqlite3_column_text(stmt, index);
    return text ? reinterpret_cast<const char*>(text) : "";
}

} // namespace

StateStore::StateStore(sqlite3* handle) : handle_(handle) {}
StateStore::StateStore(StateStore&& other) noexcept : handle_(other.handle_) {
    // mutex_ is not movable (and doesn't need to be -- a moved-from StateStore is never used
    // again by this codebase's own conventions); a fresh, default-constructed mutex is correct.
    other.handle_ = nullptr;
}
StateStore& StateStore::operator=(StateStore&& other) noexcept {
    if (this != &other) {
        if (handle_) sqlite3_close(handle_);
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}
StateStore::~StateStore() { if (handle_) sqlite3_close(handle_); }

StateStore StateStore::open(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::string message = db ? sqlite3_errmsg(db) : "could not allocate sqlite handle";
        if (db) sqlite3_close(db);
        throw std::runtime_error("rivet: could not open " + path + ": " + message);
    }

    exec_or_throw(db,
        "CREATE TABLE IF NOT EXISTS schema_meta(version INTEGER NOT NULL);"
        "CREATE TABLE IF NOT EXISTS actions("
        "  identity TEXT PRIMARY KEY, type TEXT, target TEXT, display_name TEXT,"
        "  tool TEXT, tool_version TEXT, arguments TEXT,"
        "  origin_script TEXT, origin_function TEXT,"
        "  last_fingerprint TEXT, last_status TEXT,"
        "  last_duration_seconds REAL, last_run_at INTEGER, last_reason TEXT"
        ");"
        "CREATE TABLE IF NOT EXISTS action_inputs("
        "  action_identity TEXT, input_path TEXT, input_hash TEXT,"
        "  PRIMARY KEY(action_identity, input_path)"
        ");"
        "CREATE TABLE IF NOT EXISTS action_outputs("
        "  action_identity TEXT, output_path TEXT, output_hash TEXT,"
        "  PRIMARY KEY(action_identity, output_path)"
        ");"
        "CREATE TABLE IF NOT EXISTS action_dependencies("
        "  action_identity TEXT, depends_on_identity TEXT,"
        "  PRIMARY KEY(action_identity, depends_on_identity)"
        ");"
        "CREATE INDEX IF NOT EXISTS action_inputs_path_idx ON action_inputs(input_path);"
        "CREATE INDEX IF NOT EXISTS action_dependencies_target_idx ON action_dependencies(depends_on_identity);"
    );

    {
        Statement select(db, "SELECT version FROM schema_meta LIMIT 1;");
        int step = sqlite3_step(select.stmt);
        if (step == SQLITE_ROW) {
            int stored_version = sqlite3_column_int(select.stmt, 0);
            if (stored_version != kSchemaVersion) {
                sqlite3_close(db);
                throw std::runtime_error("rivet: state database was written by schema version " +
                                          std::to_string(stored_version) + ", this build only understands " +
                                          std::to_string(kSchemaVersion) + " -- no migration path exists yet; "
                                          "delete the .rivet state directory to rebuild it from scratch");
            }
        } else {
            Statement insert(db, "INSERT INTO schema_meta(version) VALUES (?1);");
            sqlite3_bind_int(insert.stmt, 1, kSchemaVersion);
            sqlite3_step(insert.stmt);
        }
    }

    return StateStore(db);
}

void StateStore::record_action(const BuildAction& action, const Digest& fingerprint, ActionStatus status,
                                double duration_seconds, const std::string& reason,
                                const std::vector<PathDigest>& input_hashes,
                                const std::vector<PathDigest>& output_hashes) {
    std::lock_guard<std::mutex> lock(mutex_);
    exec_or_throw(handle_, "BEGIN TRANSACTION;");
    try {
        {
            Statement upsert(handle_,
                "INSERT INTO actions(identity, type, target, display_name, tool, tool_version, arguments, "
                "origin_script, origin_function, last_fingerprint, last_status, last_duration_seconds, last_run_at, last_reason) "
                "VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14) "
                "ON CONFLICT(identity) DO UPDATE SET type=excluded.type, target=excluded.target, "
                "display_name=excluded.display_name, tool=excluded.tool, tool_version=excluded.tool_version, "
                "arguments=excluded.arguments, origin_script=excluded.origin_script, "
                "origin_function=excluded.origin_function, last_fingerprint=excluded.last_fingerprint, "
                "last_status=excluded.last_status, last_duration_seconds=excluded.last_duration_seconds, "
                "last_run_at=excluded.last_run_at, last_reason=excluded.last_reason;");
            bind_text(upsert.stmt, 1, action.identity);
            bind_text(upsert.stmt, 2, to_string(action.type));
            bind_text(upsert.stmt, 3, action.target);
            bind_text(upsert.stmt, 4, action.display_name);
            bind_text(upsert.stmt, 5, action.tool);
            bind_text(upsert.stmt, 6, action.tool_version);
            bind_text(upsert.stmt, 7, join(action.arguments));
            bind_text(upsert.stmt, 8, action.origin.script);
            bind_text(upsert.stmt, 9, action.origin.function);
            bind_text(upsert.stmt, 10, fingerprint.hex());
            bind_text(upsert.stmt, 11, to_string(status));
            sqlite3_bind_double(upsert.stmt, 12, duration_seconds);
            auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            sqlite3_bind_int64(upsert.stmt, 13, static_cast<sqlite3_int64>(now));
            bind_text(upsert.stmt, 14, reason);
            if (sqlite3_step(upsert.stmt) != SQLITE_DONE) {
                throw std::runtime_error(std::string("rivet: failed to record action: ") + sqlite3_errmsg(handle_));
            }
        }

        exec_or_throw(handle_, "DELETE FROM action_inputs WHERE action_identity = '" + action.identity + "';");
        exec_or_throw(handle_, "DELETE FROM action_outputs WHERE action_identity = '" + action.identity + "';");
        exec_or_throw(handle_, "DELETE FROM action_dependencies WHERE action_identity = '" + action.identity + "';");

        {
            Statement insert(handle_, "INSERT INTO action_inputs(action_identity, input_path, input_hash) VALUES (?1,?2,?3);");
            for (const auto& entry : input_hashes) {
                sqlite3_reset(insert.stmt);
                bind_text(insert.stmt, 1, action.identity);
                bind_text(insert.stmt, 2, entry.path);
                bind_text(insert.stmt, 3, entry.digest.hex());
                sqlite3_step(insert.stmt);
            }
        }
        {
            Statement insert(handle_, "INSERT INTO action_outputs(action_identity, output_path, output_hash) VALUES (?1,?2,?3);");
            for (const auto& entry : output_hashes) {
                sqlite3_reset(insert.stmt);
                bind_text(insert.stmt, 1, action.identity);
                bind_text(insert.stmt, 2, entry.path);
                bind_text(insert.stmt, 3, entry.digest.hex());
                sqlite3_step(insert.stmt);
            }
        }
        {
            Statement insert(handle_, "INSERT INTO action_dependencies(action_identity, depends_on_identity) VALUES (?1,?2);");
            for (const auto& dependency : action.dependencies) {
                sqlite3_reset(insert.stmt);
                bind_text(insert.stmt, 1, action.identity);
                bind_text(insert.stmt, 2, dependency);
                sqlite3_step(insert.stmt);
            }
        }

        exec_or_throw(handle_, "COMMIT;");
    } catch (...) {
        exec_or_throw(handle_, "ROLLBACK;");
        throw;
    }
}

std::optional<RecordedAction> StateStore::last_record(const std::string& identity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(handle_,
        "SELECT identity, last_fingerprint, last_status, last_duration_seconds, last_run_at, last_reason "
        "FROM actions WHERE identity = ?1;");
    bind_text(select.stmt, 1, identity);
    if (sqlite3_step(select.stmt) != SQLITE_ROW) return std::nullopt;

    RecordedAction record;
    record.identity = column_text(select.stmt, 0);
    std::string fingerprint_hex = column_text(select.stmt, 1);
    // hex() emits hi then lo, 16 chars each -- parse back the same way.
    if (fingerprint_hex.size() == 32) {
        record.fingerprint.hi = std::stoull(fingerprint_hex.substr(0, 16), nullptr, 16);
        record.fingerprint.lo = std::stoull(fingerprint_hex.substr(16, 16), nullptr, 16);
    }
    std::string status_text = column_text(select.stmt, 2);
    if (status_text == "CacheHit") record.status = ActionStatus::CacheHit;
    else if (status_text == "Failed") record.status = ActionStatus::Failed;
    else if (status_text == "SkippedDueToFailure") record.status = ActionStatus::SkippedDueToFailure;
    else record.status = ActionStatus::Ran;
    record.duration_seconds = sqlite3_column_double(select.stmt, 3);
    record.recorded_at = sqlite3_column_int64(select.stmt, 4);
    record.reason = column_text(select.stmt, 5);
    return record;
}

std::vector<PathDigest> StateStore::recorded_inputs(const std::string& identity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(handle_, "SELECT input_path, input_hash FROM action_inputs WHERE action_identity = ?1;");
    bind_text(select.stmt, 1, identity);
    std::vector<PathDigest> result;
    while (sqlite3_step(select.stmt) == SQLITE_ROW) {
        PathDigest entry;
        entry.path = column_text(select.stmt, 0);
        std::string hex = column_text(select.stmt, 1);
        if (hex.size() == 32) {
            entry.digest.hi = std::stoull(hex.substr(0, 16), nullptr, 16);
            entry.digest.lo = std::stoull(hex.substr(16, 16), nullptr, 16);
        }
        result.push_back(entry);
    }
    return result;
}

std::vector<PathDigest> StateStore::recorded_outputs(const std::string& identity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(handle_, "SELECT output_path, output_hash FROM action_outputs WHERE action_identity = ?1;");
    bind_text(select.stmt, 1, identity);
    std::vector<PathDigest> result;
    while (sqlite3_step(select.stmt) == SQLITE_ROW) {
        PathDigest entry;
        entry.path = column_text(select.stmt, 0);
        std::string hex = column_text(select.stmt, 1);
        if (hex.size() == 32) {
            entry.digest.hi = std::stoull(hex.substr(0, 16), nullptr, 16);
            entry.digest.lo = std::stoull(hex.substr(16, 16), nullptr, 16);
        }
        result.push_back(entry);
    }
    return result;
}

std::vector<std::string> StateStore::actions_referencing_input(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(handle_, "SELECT DISTINCT action_identity FROM action_inputs WHERE input_path = ?1;");
    bind_text(select.stmt, 1, path);
    std::vector<std::string> result;
    while (sqlite3_step(select.stmt) == SQLITE_ROW) result.push_back(column_text(select.stmt, 0));
    return result;
}

std::vector<std::string> StateStore::dependents_of(const std::string& identity) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(handle_, "SELECT action_identity FROM action_dependencies WHERE depends_on_identity = ?1;");
    bind_text(select.stmt, 1, identity);
    std::vector<std::string> result;
    while (sqlite3_step(select.stmt) == SQLITE_ROW) result.push_back(column_text(select.stmt, 0));
    return result;
}

std::vector<std::string> StateStore::all_recorded_outputs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Statement select(handle_, "SELECT DISTINCT output_path FROM action_outputs;");
    std::vector<std::string> result;
    while (sqlite3_step(select.stmt) == SQLITE_ROW) result.push_back(column_text(select.stmt, 0));
    return result;
}

} // namespace rivet
