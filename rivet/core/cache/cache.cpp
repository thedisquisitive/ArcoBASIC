#include "rivet/cache.hpp"
#include "rivet/toolchain.hpp"

#include <filesystem>
#include <unordered_set>

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

    // A Compile action's declared `inputs` is just its one primary source file -- the adapter that
    // builds BuildAction (rivet/adapters/toolchain/cxx.ab) never lists transitively `#include`d
    // headers, since it can't know them ahead of a real compile. record() below folds every header
    // the compiler's own `-MMD -MF` depfile named for THIS action's last successful run into its
    // recorded inputs; pull those back in here so a header-only edit still changes the fingerprint
    // instead of silently staying a cache hit (the depfile-based half of RFC section 23's own
    // "use the compiler's own dependency output" design -- previously recorded but never actually
    // consulted on the read side, a real bug found while packaging Fission substrate bytecode into
    // a native capsule: editing include/arco/fission.hpp did not trigger a rebuild of anything that
    // includes it). Inert on an action's first-ever build, same as parse_depfile()'s own doc comment
    // already disclosed -- no prior record means no extra paths to fold in yet.
    std::unordered_set<std::string> declared_inputs(action.inputs.begin(), action.inputs.end());
    for (const auto& recorded_input : store_.recorded_inputs(action.identity)) {
        if (declared_inputs.count(recorded_input.path)) continue; // already hashed above
        std::error_code error;
        if (!std::filesystem::exists(recorded_input.path, error)) {
            decision.hit = false;
            decision.reason = "input '" + recorded_input.path + "' does not exist";
            return decision;
        }
        decision.input_hashes.push_back({recorded_input.path, hash_file(recorded_input.path)});
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

    // Fold in every header this Compile action's own `-MMD -MF <object>.d` just named (see
    // rivet/adapters/toolchain/cxx.ab's comment) so the NEXT evaluate() call notices a header-only
    // edit. A full replace, not a merge, on the read side too (record_action() below deletes this
    // identity's prior recorded inputs first) -- a header dropped from an #include disappears from
    // here on the very next successful compile that no longer names it.
    std::vector<PathDigest> input_hashes = decision.input_hashes;
    if (action.type == ActionType::Compile && !action.outputs.empty()) {
        const std::string depfile_path = action.outputs.front() + ".d";
        std::error_code error;
        if (std::filesystem::exists(depfile_path, error)) {
            std::unordered_set<std::string> already;
            for (const auto& entry : input_hashes) already.insert(entry.path);
            for (const auto& header : parse_depfile(depfile_path)) {
                if (!already.insert(header).second) continue; // dedupe (the depfile also lists the source itself)
                std::error_code header_error;
                if (!std::filesystem::exists(header, header_error)) continue; // e.g. a generated file already consumed and gone
                input_hashes.push_back({header, hash_file(header)});
            }
        }
    }

    store_.record_action(action, decision.fingerprint, status, duration_seconds, decision.reason,
                          input_hashes, output_hashes);
}

} // namespace rivet
