#include "rivet/cache.hpp"

#include <filesystem>

namespace rivet {

namespace {

Digest composite_fingerprint(const BuildAction& action, const std::vector<PathDigest>& input_hashes) {
    std::vector<Digest> parts;
    parts.push_back(hash_string(to_string(action.type)));
    for (const auto& entry : input_hashes) parts.push_back(entry.digest);
    parts.push_back(hash_string(action.tool + "\x1f" + action.tool_version));
    std::string joined_args;
    for (const auto& arg : action.arguments) { joined_args += arg; joined_args.push_back('\x1f'); }
    parts.push_back(hash_string(joined_args));
    parts.push_back(hash_string(action.target));
    parts.push_back(hash_string("profile:default"));       // BuildProfile composition is M7 -- fixed for this slice
    parts.push_back(hash_string("adapter:cxx-adapter/1"));  // AdapterVersion -- bump if cxx.ab's own codegen changes shape
    for (const auto& dependency : action.dependencies) parts.push_back(hash_string(dependency));
    return combine(parts);
}

} // namespace

CacheDecision CacheManager::evaluate(const BuildAction& action) const {
    CacheDecision decision;

    // Every input is hashed fresh, every time -- no mtime/size shortcut (RIVET_PROGRESS.md design
    // decision 8). A missing input is not this function's problem to diagnose gracefully -- the
    // scheduler will fail the action with a clear "input does not exist" message when it tries to
    // actually run the tool; here it simply can't produce a matching fingerprint, so it's always
    // a miss.
    for (const auto& input : action.inputs) {
        std::error_code error;
        if (!std::filesystem::exists(input, error)) {
            decision.hit = false;
            decision.reason = "input '" + input + "' does not exist";
            return decision;
        }
        decision.input_hashes.push_back({input, hash_file(input)});
    }

    decision.fingerprint = composite_fingerprint(action, decision.input_hashes);

    auto recorded = store_.last_record(action.identity);
    if (!recorded) {
        decision.hit = false;
        decision.reason = "no prior record";
        return decision;
    }
    if (recorded->status != ActionStatus::Ran && recorded->status != ActionStatus::CacheHit) {
        decision.hit = false;
        decision.reason = "prior run did not succeed";
        return decision;
    }
    if (recorded->fingerprint != decision.fingerprint) {
        decision.hit = false;
        // Name the specific input that changed when possible -- much more useful for `rivet why
        // rebuild` than a bare "fingerprint changed" (which doesn't say WHICH of possibly several
        // inputs, or whether it was a flag/tool change instead). Falls back to the generic message
        // when nothing at the per-input level explains it (e.g. an argument or tool-version change).
        std::vector<PathDigest> previous_inputs = store_.recorded_inputs(action.identity);
        std::string changed_input;
        for (const auto& current : decision.input_hashes) {
            bool found_previous = false;
            for (const auto& previous : previous_inputs) {
                if (previous.path != current.path) continue;
                found_previous = true;
                if (previous.digest != current.digest) changed_input = current.path;
                break;
            }
            if (!found_previous) changed_input = current.path; // a genuinely new input
            if (!changed_input.empty()) break;
        }
        decision.reason = changed_input.empty()
            ? "fingerprint changed (tool, arguments, or a dependency changed)"
            : "input '" + changed_input + "' content changed";
        return decision;
    }

    // Fingerprint matches -- but also verify every declared output is still really there with the
    // hash that was recorded, guarding against someone deleting/editing an output by hand between
    // builds (e.g. `rm build/tinyapp`) without touching any input.
    for (const auto& recorded_output : store_.recorded_outputs(action.identity)) {
        std::error_code error;
        if (!std::filesystem::exists(recorded_output.path, error)) {
            decision.hit = false;
            decision.reason = "recorded output '" + recorded_output.path + "' is missing";
            return decision;
        }
        Digest current = hash_file(recorded_output.path);
        if (current != recorded_output.digest) {
            decision.hit = false;
            decision.reason = "output '" + recorded_output.path + "' was modified outside Rivet";
            return decision;
        }
    }

    decision.hit = true;
    decision.reason = "fingerprint and outputs match the last recorded run";
    return decision;
}

void CacheManager::record(const BuildAction& action, const CacheDecision& decision, ActionStatus status,
                           double duration_seconds) {
    std::vector<PathDigest> output_hashes;
    for (const auto& output : action.outputs) {
        std::error_code error;
        if (std::filesystem::exists(output, error)) {
            output_hashes.push_back({output, hash_file(output)});
        }
    }
    store_.record_action(action, decision.fingerprint, status, duration_seconds, decision.reason,
                          decision.input_hashes, output_hashes);
}

} // namespace rivet
