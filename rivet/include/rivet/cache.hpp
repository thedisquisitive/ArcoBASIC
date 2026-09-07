#pragma once

// Native-only cache evaluation (RFC section 13) -- deliberately NOT exposed as an ArcoBASIC host
// contract this slice (see RIVET_PROGRESS.md design decision 5): only the native scheduler ever
// needs to evaluate/record a cache decision.

#include "rivet/action.hpp"
#include "rivet/fingerprint.hpp"
#include "rivet/store.hpp"

#include <string>

namespace rivet {

struct CacheDecision {
    bool hit = false;
    Digest fingerprint;
    std::string reason; // human-readable, used directly in [CACHE]/[COMPILE] log lines
    std::vector<PathDigest> input_hashes; // computed as a side effect, reused by record() so inputs aren't re-hashed
};

class CacheManager {
public:
    explicit CacheManager(StateStore& store) : store_(store) {}

    // Hashes every current input, computes the composite fingerprint (action type + input hashes
    // + tool identity + arguments + target + fixed profile/adapter-version strings for this slice
    // + dependency identities -- RFC section 12), and compares against the store's last recorded
    // fingerprint for this identity. A hit ALSO requires every declared output to still exist on
    // disk with a hash matching what was recorded last time -- guards a stale cache record
    // surviving a manual `rm build/whatever`.
    CacheDecision evaluate(const BuildAction& action) const;

    // Hashes every declared output (which must exist by the time this is called -- i.e. after a
    // successful run) and persists the action + its input/output hashes + dependency edges.
    void record(const BuildAction& action, const CacheDecision& decision, ActionStatus status,
                double duration_seconds);

private:
    StateStore& store_;
};

} // namespace rivet
