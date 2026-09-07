#pragma once

// Persistent build state (RFC section 50). Reuses the exact ARCO_SQLITE3_FOUND/PkgConfig::SQLITE3
// wiring already in cmake/Dependencies.cmake (used by Fissure) -- no new dependency detection.
// Schema-versioned with the same disclosed "refuse a mismatch, no migration ladder yet" policy
// fissure/include/fissure/store.hpp already established. This is the single source of truth
// `rivet why` and `rivet clean` read/write directly and natively -- no VM involved in either.

#include "rivet/action.hpp"
#include "rivet/fingerprint.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace rivet {

constexpr int kSchemaVersion = 1;

struct RecordedAction {
    std::string identity;
    Digest fingerprint;
    ActionStatus status = ActionStatus::Ran;
    double duration_seconds = 0.0;
    std::int64_t recorded_at = 0;
    // Why the LAST recorded run made the decision it did ("input 'X' content changed", "fingerprint
    // and outputs match the last recorded run", etc.) -- persisted at record time, not re-derived
    // later by comparing against current disk state. That distinction matters: `rivet why rebuild`
    // is asked AFTER a build already ran and already updated this same record, so a live
    // re-comparison would always report "unchanged" for the very build being asked about. This is
    // the RFC's own "why is 100% native/DB-driven" design decision made concrete.
    std::string reason;
};

struct PathDigest {
    std::string path;
    Digest digest;
};

// A single SQLite connection is not safe to use from multiple threads concurrently without
// serialization (found the hard way: the Scheduler's own worker-thread unit test reliably hit
// "cannot start a transaction within a transaction" the first time two workers called
// CacheManager::record() at once, both sharing one StateStore). Every public method below takes
// an internal mutex, so StateStore itself is safe to share across the Scheduler's worker threads
// without every caller needing to remember to lock externally.
class StateStore {
public:
    ~StateStore();
    StateStore(const StateStore&) = delete;
    StateStore& operator=(const StateStore&) = delete;
    StateStore(StateStore&&) noexcept;
    StateStore& operator=(StateStore&&) noexcept;

    static StateStore open(const std::string& path);

    void record_action(const BuildAction& action, const Digest& fingerprint, ActionStatus status,
                        double duration_seconds, const std::string& reason,
                        const std::vector<PathDigest>& input_hashes,
                        const std::vector<PathDigest>& output_hashes);

    std::optional<RecordedAction> last_record(const std::string& identity) const;
    std::vector<PathDigest> recorded_inputs(const std::string& identity) const;
    std::vector<PathDigest> recorded_outputs(const std::string& identity) const;

    // Every action identity whose recorded inputs reference `path` -- drives `rivet why rebuild`.
    std::vector<std::string> actions_referencing_input(const std::string& path) const;
    // Every action identity that directly depends on `identity` -- drives the cascade explanation.
    std::vector<std::string> dependents_of(const std::string& identity) const;

    // Every output path ever recorded for any action -- drives `rivet clean`.
    std::vector<std::string> all_recorded_outputs() const;

private:
    explicit StateStore(sqlite3* handle);
    sqlite3* handle_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace rivet
